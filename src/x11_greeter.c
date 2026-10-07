/*
 * Copyright (c) 2026 Devin Teske <dteske@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * XDM greeter helpers: XAUTHORITY refresh, display readiness, unblank,
 * and double-flip recovery after logout.
 */

#include <sys/types.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "framework_autorotate.h"

/*
 * Refresh XAUTHORITY to the newest XDM cookie for :0 (path can change when
 * the server is restarted after a wedged logout).
 */
void
refresh_xauthority(void)
{
	FILE *fp;
	char path[PATH_MAX];
	size_t n;

	fp = popen(
	    "ls -t /var/db/xdm/authdir/authfiles/A:0-* 2> /dev/null | head -1",
	    "r");
	if (fp == NULL)
		return;
	if (fgets(path, sizeof(path), fp) != NULL) {
		n = strlen(path);
		while (n > 0 && (path[n - 1] == '\n' || path[n - 1] == '\r'))
			path[--n] = '\0';
		if (n > 0 && access(path, R_OK) == 0) {
			setenv("XAUTHORITY", path, 1);
			if (verbose > 1)
				fprintf(stderr, "XAUTHORITY=%s\n", path);
		}
	}
	pclose(fp);
}

/* True if we can open DISPLAY (false during XDM reset / server restart) */
int
x_display_ready(void)
{
	/* timeout(1): never block forever on a wedged X socket */
	return (system("timeout 2 xdpyinfo > /dev/null 2>&1") == 0);
}

/* Wake a blanked greeter after RandR (unstick taught us this) */
void
run_unblank(void)
{
	(void)system("timeout 2 xset s reset > /dev/null 2>&1");
	(void)system("timeout 2 xset dpms force on > /dev/null 2>&1");
}

/*
 * After logout XDM often keeps the old RandR transform; a black/invisible
 * greeter is usually "still inverted". Recover with normal+unblank, then
 * immediately re-apply accel so tent/tablet posture is correct without
 * restarting the daemon.
 */
int
greeter_double_flip(const char **appliedp, int tbmd,
    int16_t ax, int16_t ay, int16_t az)
{
	const char *want;

	fprintf(stderr, "greeter recover: force normal + unblank\n");
	if (apply_orientation("normal") != 0)
		return (-1);
	run_unblank();
	*appliedp = "normal";
	usleep(300000);

	if (rotate_mode == MODE_TABLET && !tbmd)
		want = "normal";
	else {
		want = orient_from_accel(ax, ay, az, *appliedp);
		if (want == NULL)
			want = "normal";
	}
	if (strcmp(want, "normal") == 0) {
		fprintf(stderr,
		    "greeter recover: desired orientation is normal\n");
		return (0);
	}
	fprintf(stderr, "greeter recover: re-apply %s from accel\n", want);
	if (apply_orientation(want) != 0)
		return (-1);
	run_unblank();
	*appliedp = want;
	return (0);
}
