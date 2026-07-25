/*
 * Copyright (c) 2026 Devin Teske <dteske@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Optional per-user chrome hook after RandR (spirit of .xinitrc).
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "framework_autorotate.h"
#include "x11_priv.h"

static int
line_is_internal_output(const char *line)
{
	char name[64];

	if (sscanf(line, "%63s", name) != 1)
		return (0);
	if (strncmp(name, "eDP", 3) == 0)
		return (1);
	if (strncmp(name, "LVDS", 4) == 0)
		return (1);
	if (strncmp(name, "DSI", 3) == 0)
		return (1);
	return (0);
}

/* Parse WxH from a connected output line; swap for left/right rotate */
static int
parse_output_wh(const char *line, unsigned *wp, unsigned *hp)
{
	unsigned w = 0, h = 0;
	char rot[32];
	int n;
	char *tok, *save = NULL;
	char buf[256];

	n = sscanf(line, "%*s %*s %*s %ux%u+%*d+%*d %31s", &w, &h, rot);
	if (n < 2) {
		/* not "primary" form; try without primary token */
		n = sscanf(line, "%*s %*s %ux%u+%*d+%*d %31s", &w, &h, rot);
	}
	if (n >= 2 && w > 0 && h > 0) {
		if (n >= 3 && (strcmp(rot, "left") == 0 ||
		    strcmp(rot, "right") == 0) && w > h) {
			unsigned t = w;

			w = h;
			h = t;
		}
		*wp = w;
		*hp = h;
		return (0);
	}

	strlcpy(buf, line, sizeof(buf));
	for (tok = strtok_r(buf, " \t\n", &save); tok != NULL;
	    tok = strtok_r(NULL, " \t\n", &save)) {
		if (sscanf(tok, "%ux%u+%*d+%*d", &w, &h) == 2 &&
		    w > 0 && h > 0) {
			char *next = strtok_r(NULL, " \t\n", &save);

			if (next != NULL &&
			    (strcmp(next, "left") == 0 ||
			    strcmp(next, "right") == 0) &&
			    w > h) {
				unsigned t = w;

				w = h;
				h = t;
			}
			*wp = w;
			*hp = h;
			return (0);
		}
	}

	return (-1);
}

static int
chrome_geometry(int *wp, int *hp)
{
	FILE *fp;
	char line[256];
	unsigned w = 0, h = 0;
	unsigned fw = 0, fh = 0;
	int have_fallback = 0;

	fp = popen("timeout 3 xrandr --query 2> /dev/null", "r");
	if (fp == NULL)
		return (-1);
	while (fgets(line, sizeof(line), fp) != NULL) {
		if (strstr(line, " connected") == NULL)
			continue;
		if (parse_output_wh(line, &w, &h) != 0)
			continue;

		/* Prefer the built-in panel over any external head */
		if (line_is_internal_output(line)) {
			*wp = (int)w;
			*hp = (int)h;
			pclose(fp);
			return (0);
		}
		if (!have_fallback) {
			fw = w;
			fh = h;
			have_fallback = 1;
		}
	}
	pclose(fp);

	if (have_fallback) {
		*wp = (int)fw;
		*hp = (int)fh;
		return (0);
	}

	return (-1);
}

int
run_user_chrome_hook(const char *rot)
{
	char user[MAXLOGNAME], hook[PATH_MAX], homebuf[PATH_MAX];
	char widthbuf[32], heightbuf[32];
	const char *display;
	struct passwd *pw;
	struct stat st;
	uid_t uid;
	gid_t gid;
	pid_t pid;
	int status, w = 0, h = 0;
	int executable, n;

	/*
	 * Opt-in only: most DEs (KDE/GNOME/XFCE/...) follow RandR without
	 * help. Absent ~/.framework_autorotate -> silent no-op (FVWM-style
	 * chrome fixups live behind this .xinitrc-like hook).
	 */
	if (!rot_is_safe(rot))
		return (-1);
	if (session_username(user, sizeof(user)) != 0) {
		if (verbose)
			fprintf(stderr,
			    "chrome hook skipped (no session user on %s)\n",
			    safe_display());
		return (0);
	}

	pw = getpwnam(user);
	if (pw == NULL || pw->pw_dir == NULL) {
		warnx("session user %s: no passwd entry", user);
		return (-1);
	}
	uid = pw->pw_uid;
	gid = pw->pw_gid;
	if (strlcpy(homebuf, pw->pw_dir, sizeof(homebuf)) >= sizeof(homebuf)) {
		warnx("home path too long for %s", user);
		return (-1);
	}

	if (abs_hook_path != NULL) {
		if (strlcpy(hook, abs_hook_path, sizeof(hook)) >=
		    sizeof(hook)) {
			warnx("absolute hook path too long");
			return (-1);
		}
	} else if (!hook_basename_ok(hook_basename)) {
		warnx("unsafe hook basename");
		return (-1);
	} else {
		n = snprintf(hook, sizeof(hook), "%s/%s", homebuf,
		    hook_basename);
		if (n < 0 || (size_t)n >= sizeof(hook)) {
			warnx("hook path too long");
			return (-1);
		}
	}

	if (!hook_is_trusted(hook, uid)) {
		if (lstat(hook, &st) != 0) {
			if (verbose > 1)
				fprintf(stderr, "chrome hook absent: %s\n",
				    hook);
			return (0);
		}
		warnx("refusing untrusted chrome hook %s", hook);
		return (-1);
	}
	if (lstat(hook, &st) != 0)
		return (0);
	executable = (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;

	display = safe_display();

	/*
	 * Geometry as root (XDM auth). Hook runs as the session user and
	 * need not open :0 itself (e.g. [bvwm-]FvwmCommand/MFL).
	 */
	if (chrome_geometry(&w, &h) != 0) {
		warnx("could not parse xrandr geometry for chrome hook");
		return (-1);
	}
	if (verbose)
		fprintf(stderr, "chrome geometry %dx%d user=%s hook=%s\n",
		    w, h, user, hook);

	n = snprintf(widthbuf, sizeof(widthbuf), "%d", w);
	if (n < 0 || (size_t)n >= sizeof(widthbuf)) {
		warnx("CHROME_WIDTH overflow");
		return (-1);
	}
	n = snprintf(heightbuf, sizeof(heightbuf), "%d", h);
	if (n < 0 || (size_t)n >= sizeof(heightbuf)) {
		warnx("CHROME_HEIGHT overflow");
		return (-1);
	}

	pid = fork();
	if (pid < 0) {
		warn("fork");
		return (-1);
	}
	if (pid == 0) {
		/* Drop to session user; exec hook without a shell. */
		if (initgroups(user, gid) != 0 ||
		    setgid(gid) != 0 || setuid(uid) != 0)
			_exit(126);
		if (setenv("HOME", homebuf, 1) != 0 ||
		    setenv("DISPLAY", display, 1) != 0 ||
		    setenv("CHROME_WIDTH", widthbuf, 1) != 0 ||
		    setenv("CHROME_HEIGHT", heightbuf, 1) != 0 ||
		    setenv("FRAMEWORK_AUTOROTATE_ROTATION", rot, 1) != 0)
			_exit(126);
		if (executable)
			execl(hook, hook, (char *)NULL);
		else
			execl("/bin/sh", "sh", hook, (char *)NULL);
		_exit(127);
	}
	for (;;) {
		if (waitpid(pid, &status, 0) < 0) {
			if (errno == EINTR)
				continue;
			warn("waitpid");
			return (-1);
		}
		break;
	}

	if (verbose)
		fprintf(stderr, "+ chrome hook %s as %s\n", hook, user);
	if (WIFEXITED(status)) {
		status = WEXITSTATUS(status);
		if (status != 0 && verbose)
			warnx("chrome hook status=%d", status);
		return (status);
	}

	return (-1);
}
