/*
 * Copyright (c) 2026 Devin Teske <dteske@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Framework Laptop 12 autorotate: orient via Chrome EC memmap accel0, apply
 * xrandr(1) + touch CTM. Gate with -m tablet (TBMD only) or -m always
 * (default). After rotate, optionally run ~/.framework_autorotate (opt-in
 * chrome fixup for WMs that do not follow RandR alone; KDE/GNOME/XFCE
 * typically need no hook).
 *
 * Out of scope: Framework Laptop 13 Pro (even with the touchscreen display
 * kit) is a clamshell, not a convertible (no 360 hinge / tablet mode).
 * Touchscreen alone is not a reason to port this daemon.
 */

#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "framework_autorotate.h"

#define	SESSION_GONE_SECS	3 /* debounce before greeter double-flip */

volatile sig_atomic_t stop_flag;
int verbose;
enum rotate_mode rotate_mode = MODE_ALWAYS;

static int interval_ms = 500;

static void
on_sig(int sig)
{
	(void)sig;
	stop_flag = 1;
}

static void
usage(const char *argv0)
{
	const char optfmt[] = "\t%-12s %s\n";

	fprintf(stderr,
	    "Usage: %s [-hv] [-i msec] [-m always|tablet]\n"
	    "       %s [-hv] -hold | [-hv] -release\n", argv0, argv0);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, optfmt, "-h", "Print usage statement and exit.");
	fprintf(stderr, optfmt, "-i msec",
	    "Poll interval in msec (minimum 50; default 500).");
	fprintf(stderr, optfmt, "-m always",
	    "Autorotate from accel at all times (default).");
	fprintf(stderr, optfmt, "-m tablet",
	    "Autorotate only while EC TBMD=1; else normal.");
	fprintf(stderr, optfmt, "-v",
	    "Verbose. Can be specified multiple times (up to 2).");
	fprintf(stderr, optfmt, "-hold",
	    "Lock orientation (create ~/.framework_hold_autorotate).");
	fprintf(stderr, optfmt, "-release",
	    "Unlock orientation (remove ~/.framework_hold_autorotate).");
	exit(1);
}

/*
 * -hold / -release are whole argv words, not getopt(3) letters.  Pull them
 * out before getopt so "-v -hold" works and "-hold" not eaten as -h -o -l -d.
 *
 * Returns 1 if a hold/release verb was found (*createp set), 0 if none,
 * -1 if both or duplicate.
 */
static int
extract_hold_release(int *argcp, char **argv, int *createp)
{
	int i, j, found = -1;

	for (i = 1; i < *argcp; i++) {
		if (strcmp(argv[i], "-hold") == 0) {
			if (found != -1)
				return (-1);
			found = 1;
		} else if (strcmp(argv[i], "-release") == 0) {
			if (found != -1)
				return (-1);
			found = 0;
		} else
			continue;
		for (j = i; j < *argcp - 1; j++)
			argv[j] = argv[j + 1];
		argv[--(*argcp)] = NULL;
		i--;
	}
	if (found == -1)
		return (0);
	*createp = found;
	return (1);
}

/*
 * Touch or unlink ~/.framework_hold_autorotate for the real uid so the root
 * daemon skips apply_orientation without stopping.
 *
 * Prefer passwd home for getuid() over $HOME: greeter helpers may set
 * HOME to the prospective login user while still running as root, and that
 * must not place the hold under that user's home by mistake.
 *
 * Basename is intentionally not a prefix of the chrome hook
 * (.framework_autorotate); tab-completing ~/.frame must not land on the
 * hook.
 */
static int
cmd_hold(int create)
{
	const char *home;
	struct passwd *pw;
	char path[PATH_MAX];
	int fd, n;

	pw = getpwuid(getuid());
	if (pw != NULL && pw->pw_dir != NULL && pw->pw_dir[0] != '\0')
		home = pw->pw_dir;
	else {
		home = getenv("HOME");
		if (home == NULL || home[0] == '\0')
			errx(1, "cannot determine home directory");
	}
	n = snprintf(path, sizeof(path), "%s/%s", home, HOLD_BASENAME);
	if (n < 0 || (size_t)n >= sizeof(path))
		errx(1, "hold path too long");

	if (create) {
		fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd < 0)
			err(1, "%s", path);
		close(fd);
		if (verbose)
			fprintf(stderr, "orientation locked (%s)\n", path);
	} else {
		if (unlink(path) != 0 && errno != ENOENT)
			err(1, "%s", path);
		if (verbose)
			fprintf(stderr, "orientation unlocked (%s)\n", path);
	}
	return (0);
}

int
main(int argc, char **argv)
{
	int fd, ch, rc, create;
	uint8_t ori;
	int tbmd, prev_tbmd = -1;
	int16_t ax, ay, az;
	const char *want, *prev = "";
	const char *pending = "";
	int pending_hits = 0;
	const int confirm_hits = 2; /* ~1s at 500ms, avoid bounce rebuilds */
	/*
	 * Empty until first successful apply; forces xrandr+CTM once at
	 * startup. Starting as "normal" skipped CTM when already landscape,
	 * leaving a stale scaled matrix that makes the taskbar untouchable.
	 */
	const char *applied = "";
	int x_ok = 1, x_ready, had_session = 0, sess;
	int need_double_flip = 0;
	int greeter_recovered = 0;
	time_t quiet_until = 0;
	time_t session_gone_since = 0;
	const char *display, *prog;
	char holdpath[PATH_MAX];
	char last_hold_log[PATH_MAX];
	int hold_logged = 0;

	last_hold_log[0] = '\0';

	prog = strrchr(argv[0], '/');
	if (prog != NULL)
		prog++;
	else
		prog = argv[0];

	/*
	 * Userland hold/release — no /dev/io, works for the session user.
	 * Whole-word -hold/-release may mix with -h/-v only.
	 */
	rc = extract_hold_release(&argc, argv, &create);
	if (rc < 0)
		usage(prog);
	if (rc == 1) {
		optind = 1;
		while ((ch = getopt(argc, argv, "hv")) != -1) {
			switch (ch) {
			case 'h':
				usage(prog);
				/* NOTREACHED */
			case 'v':
				verbose++;
				break;
			default:
				usage(prog);
			}
		}
		argc -= optind;
		argv += optind;
		if (argc != 0)
			usage(prog);
		return (cmd_hold(create));
	}

	while ((ch = getopt(argc, argv, "hi:m:v")) != -1) {
		switch (ch) {
		case 'h':
			usage(prog);
			/* NOTREACHED */
		case 'i':
			interval_ms = atoi(optarg);
			if (interval_ms < 50)
				interval_ms = 50;
			break;
		case 'm':
			if (strcmp(optarg, "always") == 0)
				rotate_mode = MODE_ALWAYS;
			else if (strcmp(optarg, "tablet") == 0)
				rotate_mode = MODE_TABLET;
			else
				usage(prog);
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage(prog);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 0)
		usage(prog);

	fd = open("/dev/io", O_RDWR);
	if (fd < 0)
		err(1, "open /dev/io");

	resolve_paths();

	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);
	setlinebuf(stderr);

	if (verbose)
		fprintf(stderr,
		    "framework_autorotate: interval=%dms mode=%s\n",
		    interval_ms,
		    rotate_mode == MODE_TABLET ? "tablet" : "always");

	display = safe_display();

	while (!stop_flag) {
		/*
		 * Logout: non-root X clients vanish. Debounce a few seconds
		 * so sockstat "??" blips / TakeConsole races do not spam
		 * greeter double-flips. Recover at most once per logout.
		 */
		sess = session_present(display);
		if (sess) {
			had_session = 1;
			session_gone_since = 0;
			greeter_recovered = 0;
			if (need_double_flip) {
				/* Logged-in before recover finished */
				need_double_flip = 0;
				applied = "";
			}
		} else if (had_session && !greeter_recovered) {
			if (session_gone_since == 0) {
				session_gone_since = time(NULL);
				if (verbose)
					fprintf(stderr,
					    "session gone; debounce %ds then "
					    "greeter double-flip\n",
					    SESSION_GONE_SECS);
			} else if (time(NULL) - session_gone_since >=
			    SESSION_GONE_SECS) {
				fprintf(stderr,
				    "session gone confirmed; greeter "
				    "double-flip\n");
				need_double_flip = 1;
				had_session = 0;
				session_gone_since = 0;
			}
		} else {
			session_gone_since = 0;
		}

		rc = acpi_read(ORI_ADDR, &ori);
		if (rc != 0) {
			warnx("acpi_read ori rc=%d", rc);
			usleep((useconds_t)interval_ms * 1000);
			continue;
		}
		tbmd = ori & 1;
		ax = mem_inw_s(ACC_DATA_OFF + 2);
		ay = mem_inw_s(ACC_DATA_OFF + 4);
		az = mem_inw_s(ACC_DATA_OFF + 6);

		if (tbmd != prev_tbmd) {
			fprintf(stderr, "TBMD %d -> %d (ori=0x%02x) "
			    "accel=(%d,%d,%d)\n",
			    prev_tbmd, tbmd, ori, ax, ay, az);
			prev_tbmd = tbmd;
		}

		if (rotate_mode == MODE_TABLET && !tbmd) {
			want = "normal";
		} else {
			want = orient_from_accel(ax, ay, az, applied);
			if (want == NULL)
				want = applied[0] != '\0' ? applied : "normal";
		}

		if (quiet_until != 0 && time(NULL) < quiet_until) {
			usleep((useconds_t)interval_ms * 1000);
			continue;
		}
		quiet_until = 0;

		if (need_double_flip) {
			x_ready = x_display_ready();
			if (!x_ready) {
				if (x_ok) {
					fprintf(stderr,
					    "DISPLAY not ready; pausing X "
					    "ops (XDM reset?)\n");
					x_ok = 0;
				}
				refresh_xauthority();
				quiet_until = time(NULL) + 2;
				usleep((useconds_t)interval_ms * 1000);
				continue;
			}
			x_ok = 1;
			refresh_xauthority();
			if (greeter_double_flip(&applied, tbmd, ax, ay, az)
			    != 0) {
				warnx("greeter double-flip failed; retry");
				quiet_until = time(NULL) + 2;
			} else {
				need_double_flip = 0;
				greeter_recovered = 1;
			}
			usleep((useconds_t)interval_ms * 1000);
			continue;
		}

		/*
		 * Only probe DISPLAY when we would apply (or are recovering;
		 * a steady hold does not open X at all).
		 */
		if (strcmp(want, applied) != 0 || !x_ok) {
			x_ready = x_display_ready();
			if (!x_ready) {
				if (x_ok) {
					fprintf(stderr,
					    "DISPLAY not ready; pausing X "
					    "ops (XDM reset?)\n");
					x_ok = 0;
				}
				refresh_xauthority();
				quiet_until = time(NULL) + 2;
				usleep((useconds_t)interval_ms * 1000);
				continue;
			}
			if (!x_ok) {
				fprintf(stderr,
				    "DISPLAY ready again; "
				    "re-applying orientation\n");
				refresh_xauthority();
				applied = "";
				x_ok = 1;
			}
		}

		if (strcmp(want, applied) != 0) {
			/*
			 * Lock orientation via ~/.framework_hold_autorotate
			 * (session user) or ~root/.framework_hold_autorotate
			 * (greeter) without stopping the root daemon.
			 */
			if (session_orientation_held(holdpath,
			    sizeof(holdpath))) {
				if (verbose &&
				    (!hold_logged ||
				    strcmp(holdpath, last_hold_log) != 0)) {
					fprintf(stderr,
					    "orientation locked by %s\n",
					    holdpath);
					(void)strlcpy(last_hold_log, holdpath,
					    sizeof(last_hold_log));
					hold_logged = 1;
				}
				pending = "";
				pending_hits = 0;
				prev = want;
				usleep((useconds_t)interval_ms * 1000);
				continue;
			}
			if (hold_logged) {
				hold_logged = 0;
				last_hold_log[0] = '\0';
			}
			/*
			 * Require confirm_hits consecutive samples before
			 * applying; cuts mid-tilt bounce and chrome churn.
			 */
			if (strcmp(want, pending) != 0) {
				pending = want;
				pending_hits = 1;
				if (verbose)
					fprintf(stderr,
					    "pending %s (1/%d) "
					    "accel=(%d,%d,%d)\n",
					    want, confirm_hits, ax, ay, az);
				usleep((useconds_t)interval_ms * 1000);
				continue;
			}
			pending_hits++;
			if (pending_hits < confirm_hits) {
				usleep((useconds_t)interval_ms * 1000);
				continue;
			}
			pending = "";
			pending_hits = 0;
			fprintf(stderr, "rotate %s -> %s accel=(%d,%d,%d) "
			    "tbmd=%d\n",
			    applied[0] != '\0' ? applied : "(none)",
			    want, ax, ay, az, tbmd);
			if (apply_orientation(want) == 0) {
				applied = want;
			} else {
				warnx("orientation apply failed for %s", want);
				quiet_until = time(NULL) + 2;
				refresh_xauthority();
			}
		} else {
			pending = "";
			pending_hits = 0;
			if (verbose > 1 && strcmp(want, prev) != 0) {
				fprintf(stderr,
				    "hold %s accel=(%d,%d,%d) tbmd=%d\n",
				    want, ax, ay, az, tbmd);
			}
		}
		prev = want;
		usleep((useconds_t)interval_ms * 1000);
	}

	/* Leave display + touch normal on exit, only if X is still up */
	if (x_display_ready())
		(void)apply_orientation("normal");
	close(fd);
	return (0);
}
