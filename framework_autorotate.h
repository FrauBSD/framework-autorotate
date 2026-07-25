/*
 * Copyright (c) 2026 Devin Teske <dteske@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Shared types and API for framework_autorotate(8).
 */

#ifndef FRAMEWORK_AUTOROTATE_H
#define	FRAMEWORK_AUTOROTATE_H

#include <sys/types.h>

#include <signal.h>
#include <stdint.h>

#define	LIBEXEC_DIR	"/usr/local/libexec/framework_autorotate/"

#define	ACC_DATA_OFF	0x92
#define	ORI_ADDR	0x09

enum rotate_mode {
	MODE_ALWAYS = 0,	/* autorotate regardless of TBMD */
	MODE_TABLET = 1,	/* only while EC TBMD=1; else force normal */
};

extern volatile sig_atomic_t stop_flag;
extern int verbose;
extern enum rotate_mode rotate_mode;

/* util.c: path / DISPLAY / hook policy and argv exec */
extern const char *hook_basename;
extern const char *abs_hook_path;
extern const char *xlogin_recenter;

int	path_in_libexec(const char *path);
int	path_has_dotdot(const char *p);
const char *safe_display(void);
int	rot_is_safe(const char *rot);
int	hook_basename_ok(const char *name);
int	hook_is_trusted(const char *path, uid_t uid);
int	run_argv(char *const argv[]);
void	resolve_paths(void);

/* session.c: who owns the graphical session */
int	session_username(char *out, size_t outsz);
int	session_present(const char *display);

/* ec.c: Chrome EC memmap / ACPI ori + accel -> rotation name */
int	acpi_read(uint8_t addr, uint8_t *val);
int16_t	mem_inw_s(u_int off);
const char *orient_from_accel(int16_t ax, int16_t ay, int16_t az,
	    const char *cur);

/* x11_*.c: RandR / touch / chrome hook / greeter helpers */
int	apply_orientation(const char *rot);
void	refresh_xauthority(void);
int	x_display_ready(void);
void	run_unblank(void);
int	greeter_double_flip(const char **appliedp, int tbmd,
	    int16_t ax, int16_t ay, int16_t az);

#endif /* FRAMEWORK_AUTOROTATE_H */
