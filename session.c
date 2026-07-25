/*
 * Copyright (c) 2026 Devin Teske <dteske@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Detect the interactive graphical session user on DISPLAY.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>

#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "framework_autorotate.h"

/*
 * Real interactive session user; not root, not sockstat's "??", and
 * must resolve in passwd with uid != 0. Transient "??" rows flip
 * session_present and spam greeter with double-flips.
 */
static int
valid_session_user(const char *name)
{
	struct passwd *pw;

	if (name == NULL || name[0] == '\0')
		return (0);
	if (strcmp(name, "root") == 0 || strcmp(name, "??") == 0)
		return (0);
	pw = getpwnam(name);
	return (pw != NULL && pw->pw_uid != 0);
}

/*
 * Who owns the graphical session on DISPLAY?
 *   1) first non-root sockstat client on the X11 unix socket (any DE/WM)
 *   2) owner of FvwmMFL socket if present (FVWM3/BVWM)
 *   3) SUDO_USER (foreground sudo test only)
 *
 * Returns 0 and copies username into out[], or -1 if unknown (greeter)
 */
int
session_username(char *out, size_t outsz)
{
	const char *e, *display;
	struct passwd *pw;
	struct stat st;
	char path[PATH_MAX], cmd[256], line[MAXLOGNAME];
	FILE *fp;
	unsigned dnum = 0;
	int n;
	size_t L;

	display = safe_display();
	if (display[0] == ':')
		dnum = (unsigned)atoi(display + 1);

	/* Typical case: KDE/GNOME/XFCE/etc. (non-root X clients) */
	n = snprintf(cmd, sizeof(cmd),
	    "sockstat -u 2>/dev/null | awk -v s='X11-unix/X%u' "
	    "'index($0,s) && $1!=\"root\" && $1!=\"??\" { print $1; exit }'",
	    dnum);
	if (n > 0 && (size_t)n < sizeof(cmd)) {
		fp = popen(cmd, "r");
		if (fp != NULL) {
			if (fgets(line, sizeof(line), fp) != NULL) {
				L = strlen(line);
				while (L > 0 && (line[L - 1] == '\n' ||
				    line[L - 1] == '\r'))
					line[--L] = '\0';
				pclose(fp);
				if (valid_session_user(line)) {
					(void)strlcpy(out, line, outsz);
					return (0);
				}
			} else
				pclose(fp);
		}
	}

	/* Optional: FVWM3/BVWM MFL socket owner */
	n = snprintf(path, sizeof(path), "/tmp/fvwmmfl/fvwm_mfl_%s.sock",
	    display);
	if (n > 0 && (size_t)n < sizeof(path) &&
	    stat(path, &st) == 0 && S_ISSOCK(st.st_mode)) {
		pw = getpwuid(st.st_uid);
		if (pw != NULL && pw->pw_name != NULL &&
		    valid_session_user(pw->pw_name)) {
			(void)strlcpy(out, pw->pw_name, outsz);
			return (0);
		}
	}

	e = getenv("SUDO_USER");
	if (e != NULL && valid_session_user(e)) {
		(void)strlcpy(out, e, outsz);
		return (0);
	}
	return (-1);
}

/* True if a non-root session is attached to DISPLAY (any DE/WM) */
int
session_present(const char *display)
{
	char user[MAXLOGNAME];

	(void)display;
	return (session_username(user, sizeof(user)) == 0);
}
