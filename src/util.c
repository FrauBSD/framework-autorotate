/*
 * Copyright (c) 2026 Devin Teske <dteske@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Path / DISPLAY / hook trust helpers and argv-based process spawn.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "framework_autorotate.h"

static char hook_basename_buf[NAME_MAX + 1];
static char abs_hook_buf[PATH_MAX];

const char *hook_basename = ".framework_autorotate";
const char *abs_hook_path;	/* optional absolute hook override */
const char *xlogin_recenter =
    "/usr/local/libexec/framework_autorotate/xlogin_recenter";

int
path_has_dotdot(const char *p)
{
	const char *s;

	for (s = p; *s != '\0';) {
		if (s[0] == '.' && s[1] == '.' &&
		    (s[2] == '/' || s[2] == '\0') &&
		    (s == p || s[-1] == '/'))
			return (1);
		while (*s != '\0' && *s != '/')
			s++;
		while (*s == '/')
			s++;
	}
	return (0);
}

/* Absolute path under our libexec dir; no .. components */
int
path_in_libexec(const char *path)
{
	size_t n;

	if (path == NULL || path[0] != '/')
		return (0);
	if (path_has_dotdot(path))
		return (0);
	n = sizeof(LIBEXEC_DIR) - 1;
	return (strncmp(path, LIBEXEC_DIR, n) == 0 && path[n] != '\0');
}

/* DISPLAY for setenv / MFL path: host:N[.S] with safe host chars */
static int
display_is_safe(const char *d)
{
	size_t i;

	if (d == NULL || d[0] == '\0')
		return (0);
	i = 0;
	while (d[i] != '\0' && d[i] != ':') {
		char c = d[i];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '.' || c == '-' ||
		    c == '_'))
			return (0);
		i++;
	}
	if (d[i] != ':')
		return (0);
	i++;
	if (d[i] < '0' || d[i] > '9')
		return (0);
	while (d[i] >= '0' && d[i] <= '9')
		i++;
	if (d[i] == '.') {
		i++;
		if (d[i] < '0' || d[i] > '9')
			return (0);
		while (d[i] >= '0' && d[i] <= '9')
			i++;
	}
	return (d[i] == '\0');
}

const char *
safe_display(void)
{
	const char *d;

	d = getenv("DISPLAY");
	if (d != NULL && display_is_safe(d))
		return (d);
	return (":0");
}

int
rot_is_safe(const char *rot)
{

	return (rot != NULL && (strcmp(rot, "normal") == 0 ||
	    strcmp(rot, "left") == 0 || strcmp(rot, "right") == 0 ||
	    strcmp(rot, "inverted") == 0));
}

/* Basename only: no slash, not . or .. */
int
hook_basename_ok(const char *name)
{

	if (name == NULL || name[0] == '\0')
		return (0);
	if (strchr(name, '/') != NULL)
		return (0);
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return (0);
	return (1);
}

/*
 * Hook must be a non-symlink regular file, not group/other-writable,
 * owned by the session user or root (admin-installed absolute hook).
 */
int
hook_is_trusted(const char *path, uid_t uid)
{
	struct stat st;

	if (path == NULL || path[0] != '/' || path_has_dotdot(path))
		return (0);
	if (lstat(path, &st) != 0)
		return (0);
	if (!S_ISREG(st.st_mode))
		return (0);
	if (st.st_uid != uid && st.st_uid != 0)
		return (0);
	if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0)
		return (0);
	return (1);
}

int
run_argv(char *const argv[])
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0) {
		warn("fork");
		return (-1);
	}
	if (pid == 0) {
		execvp(argv[0], argv);
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
	if (WIFEXITED(status))
		return (WEXITSTATUS(status));
	return (-1);
}

void
resolve_paths(void)
{
	const char *e;

	e = getenv("FRAMEWORK_AUTOROTATE_HOOK_NAME");
	if (e != NULL && e[0] != '\0') {
		if (!hook_basename_ok(e))
			warnx("ignoring unsafe FRAMEWORK_AUTOROTATE_HOOK_NAME");
		else {
			(void)strlcpy(hook_basename_buf, e,
			    sizeof(hook_basename_buf));
			hook_basename = hook_basename_buf;
		}
	}
	e = getenv("FRAMEWORK_AUTOROTATE_HOOK");
	if (e != NULL && e[0] == '/' && !path_has_dotdot(e)) {
		(void)strlcpy(abs_hook_buf, e, sizeof(abs_hook_buf));
		abs_hook_path = abs_hook_buf;
	} else if (e != NULL && e[0] != '\0')
		warnx("ignoring unsafe FRAMEWORK_AUTOROTATE_HOOK");
	e = getenv("FRAMEWORK_AUTOROTATE_XLOGIN_RECENTER");
	if (e != NULL && e[0] != '\0') {
		if (!path_in_libexec(e))
			warnx("ignoring unsafe "
			    "FRAMEWORK_AUTOROTATE_XLOGIN_RECENTER");
		else
			xlogin_recenter = e;
	}
}
