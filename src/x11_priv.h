/*
 * Copyright (c) 2026 Devin Teske <dteske@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Internal API between x11_*.c units (not for the daemon main loop).
 */

#ifndef X11_PRIV_H
#define	X11_PRIV_H

int	run_user_chrome_hook(const char *rot);

#endif /* X11_PRIV_H */
