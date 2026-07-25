/*
 * Copyright (c) 2026 Devin Teske <dteske@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Apply orientation: RandR on the built-in panel, touch CTM remap, then
 * chrome hook and xlogin recenter.
 */

#include <sys/types.h>

#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "framework_autorotate.h"
#include "x11_priv.h"

static int
run_xrandr(const char *rot)
{
	char cmd[384];
	int n, status;

	if (!rot_is_safe(rot)) {
		warnx("refusing unsafe rotation '%s'",
		    rot != NULL ? rot : "(null)");
		return (-1);
	}

	/*
	 * Rotate only the built-in panel. External heads in an extended
	 * layout stay put. The greeter and tablet orientation belong on
	 * eDP/LVDS/DSI, never on HDMI/DP clones of the desk.
	 */
	n = snprintf(cmd, sizeof(cmd),
	    "out=$( timeout 3 xrandr --query | awk '"
	    "	/ connected/ {"
	    "	  if ($1 ~ /^(eDP|LVDS|DSI)/) { print $1; found=1; exit }"
	    "	  if (first == \"\") first = $1"
	    "	}"
	    "	END { if (!found && first != \"\") print first }"
	    "' ) && [ -n \"$out\" ] && "
	    "	timeout 3 xrandr --output \"$out\" --rotate %s",
	    rot);
	if (n < 0 || (size_t)n >= sizeof(cmd)) {
		warnx("xrandr command too long for %s", rot);
		return (-1);
	}
	if (verbose)
		fprintf(stderr, "+ %s\n", cmd);
	status = system(cmd);
	if (status != 0)
		warnx("xrandr status=%d for %s", status, rot);
	return (status);
}

/*
 * Remap absolute touch/stylus to the built-in panel after RandR.
 *
 * CTM / map-to-output only. Do not float the Tablet node or warm gesture
 * helpers as root on the greeter (races into an X cursor and lost relative
 * pointers).
 */
static int
run_touch_ctm(const char *rot)
{
	const char *map;
	char mappath[PATH_MAX];
	char *argv[12];
	int status, i;

	(void)rot;
	map = getenv("FRAMEWORK_TOUCH_MAP");
	if (map == NULL || map[0] == '\0')
		map = LIBEXEC_DIR "map-touchscreen";
	if (!path_in_libexec(map)) {
		if (verbose)
			warnx("refusing touch map outside libexec: %s", map);
		return (0);
	}
	if (access(map, R_OK) != 0)
		return (0);
	if (strlcpy(mappath, map, sizeof(mappath)) >= sizeof(mappath))
		return (-1);

	/* -W: no stylus watcher; it would hold timeout(1) open */
	i = 0;
	argv[i++] = "timeout";
	argv[i++] = "5";
	argv[i++] = "/bin/sh";
	argv[i++] = mappath;
	argv[i++] = "-W";
	argv[i] = NULL;

	if (verbose)
		fprintf(stderr, "+ touch remap via %s\n", mappath);
	status = run_argv(argv);
	if (status != 0 && verbose)
		warnx("touch remap status=%d", status);
	return (status);
}

static int
run_xlogin_recenter(void)
{
	const char *display;
	char path[PATH_MAX];
	char *argv[4];
	int status;

	if (!path_in_libexec(xlogin_recenter))
		return (0);
	if (strlcpy(path, xlogin_recenter, sizeof(path)) >= sizeof(path))
		return (-1);
	if (access(path, X_OK) != 0)
		return (0);
	display = safe_display();
	/* Session up: leave greeter chrome alone */
	if (session_present(display))
		return (0);

	argv[0] = "timeout";
	argv[1] = "2";
	argv[2] = path;
	argv[3] = NULL;

	if (verbose)
		fprintf(stderr, "+ timeout 2 %s\n", path);
	status = run_argv(argv);
	if (status != 0 && verbose)
		warnx("xlogin_recenter status=%d", status);

	return (status);
}

int
apply_orientation(const char *rot)
{

	if (run_xrandr(rot) != 0)
		return (-1);
	(void)run_touch_ctm(rot);
	(void)run_user_chrome_hook(rot);
	/*
	 * XDM greeter ignores RandR; best-effort re-center xlogin.
	 * Florence (x11/florence) in --greeter mode warps its own
	 * glyph/keyboard on RandR.
	 */
	(void)run_xlogin_recenter();
	return (0);
}
