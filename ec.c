/*
 * Copyright (c) 2026 Devin Teske <dteske@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Chrome EC ACPI command port + NPCX memmap accel0 orientation.
 */

#include <sys/types.h>

#include <machine/cpufunc.h>

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "framework_autorotate.h"

/*
 * Standard PC Embedded Controller (EC) I/O ports (LPC), same addresses
 * ACPI has used for decades to talk to the EC:
 *
 *   ACPI_CMD  (0x66) - write a cmd here; read status from here (see CMDR_*)
 *   ACPI_DATA (0x62) - transfer byte (write addr/cmd params; read results)
 *   ACPI_READ (0x80) - the EC "read EC RAM byte" command
 *                      (Chrome EC EC_CMD_ACPI_READ)
 *
 * Sequence in acpi_read(): write ACPI_READ to CMD, write the offset to DATA,
 * then read the value back from DATA once ready. Separate from the NPCX memmap
 * window below (direct inb of accel samples).
 */
#define	ACPI_CMD	0x66
#define	ACPI_DATA	0x62
#define	ACPI_READ	0x80

/*
 * Bits in the ACPI command status byte (read from ACPI_CMD / EC port 0x66).
 * CMDR = command register. Matches Chrome EC EC_LPC_CMDR_*:
 *
 *   BUSY    - EC is still processing a command
 *   DATA    - EC has a byte ready in ACPI_DATA for the host to read
 *   PENDING - host wrote a command/data byte; EC has not finished taking it
 *
 * acpi_read() polls these around the EC_CMD_ACPI_READ (0x80) handshake.
 */
#define	CMDR_BUSY	0x04
#define	CMDR_DATA	0x01
#define	CMDR_PENDING	0x02

/*
 * Chrome EC LPC memory-map base for Framework boards using a Nuvoton NPCX
 * EC (e.g. npcx9m3f). Stock ChromeOS NPCX platforms use 0x900; Framework
 * remaps the same memmap window to 0xe00 (see Linux cros_ec_lpc Framework
 * quirk). Older Framework Intel machines used a Microchip MEC instead.
 */
#define	MEMMAP_NPCX	0xe00

/*
 * Orientation decision threshold for accel0 memmap samples (ax/ay/az).
 * g is gravity (~9.8 m/s^2); at rest one axis is typically ~+/-1g.
 * The EC reports ~16000 counts per g (~16k/g), so 8000 is about 0.5g:
 * ignore weaker axes so mid-tilt noise does not flip RandR.
 */
#define	THR		8000	/* ~0.5g at ~16k counts/g */

static int
wait_mask(uint8_t mask, int set, int usec)
{
	int i;
	uint8_t v;

	for (i = 0; i < usec; i++) {
		v = inb(ACPI_CMD);
		if (set) {
			if ((v & mask) != 0)
				return (0);
		} else if ((v & mask) == 0)
			return (0);
		usleep(1);
	}
	return (-1);
}

int
acpi_read(uint8_t addr, uint8_t *val)
{
	if (wait_mask(CMDR_BUSY, 0, 100000) != 0)
		return (-1);
	outb(ACPI_CMD, ACPI_READ);
	if (wait_mask(CMDR_PENDING, 0, 100000) != 0)
		return (-2);
	outb(ACPI_DATA, addr);
	if (wait_mask(CMDR_DATA, 1, 100000) != 0)
		return (-3);
	*val = inb(ACPI_DATA);
	return (0);
}

static uint16_t
mem_inw(u_int off)
{
	uint8_t lo, hi;

	lo = inb(MEMMAP_NPCX + off);
	hi = inb(MEMMAP_NPCX + off + 1);
	return ((uint16_t)lo | ((uint16_t)hi << 8));
}

int16_t
mem_inw_s(u_int off)
{
	return ((int16_t)mem_inw(off));
}

/*
 * Map gravity (accel0) to xrandr rotation name while in tablet mode.
 * Axis convention tuned from FW12 memmap samples (laptop rest ~ +Y).
 * cur is unused (kept for call-site compatibility).
 */
const char *
orient_from_accel(int16_t ax, int16_t ay, int16_t az, const char *cur)
{
	int axa = abs(ax), aya = abs(ay), aza = abs(az);

	(void)cur;
	if (axa > aya && axa > aza && axa > THR)
		return (ax > 0 ? "right" : "left");
	if (aya > aza && aya > THR)
		return (ay > 0 ? "normal" : "inverted");
	if (aza > THR)
		return (az > 0 ? "normal" : "inverted");
	return (NULL);	/* unstable / flat-ambiguous; keep previous */
}
