############################################################ LICENSE
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Devin Teske <dteske@FreeBSD.org>
#
############################################################ IDENT(1)
#
# $Title: framework-autorotate - Framework Laptop 12 autorotate $
# $Copyright: 2026 Devin Teske. All rights reserved. $
# $FrauBSD: framework-autorotate/Makefile 2026-10-07 14:01:26 -0700 Devin Teske $
#
############################################################ PROGRAMS

PROGRAM=	framework_autorotate

############################################################ PATHS

# Does not RUN_DEPENDS bhotkeys and does not ship a plugin.
# Super+R is framework-autorotate-hotkey.

PREFIX?=	/usr/local
SBINDIR?=	${PREFIX}/sbin
LIBEXECDIR?=	${PREFIX}/libexec/${PROGRAM}
RCDIR?=		${PREFIX}/etc/rc.d
SHAREDIR?=	${PREFIX}/share
MANDIR?=	${SHAREDIR}/man/man8
EXAMPLESDIR?=	${SHAREDIR}/examples/${PROGRAM}

############################################################ COMPILER

CC?=		cc
CFLAGS?=	-O2 -Wall -Wextra
CPPFLAGS+=	-I/usr/local/include -Isrc
LDFLAGS+=	-L/usr/local/lib

.c.o:
	${CC} ${CFLAGS} ${CPPFLAGS} -c -o ${.TARGET} ${.IMPSRC}

############################################################ FILES

PROGS=		${PROGRAM} xlogin_recenter
MAN=		man/${PROGRAM}.8

FA_SRCS=	src/framework_autorotate.c src/util.c src/session.c \
		src/ec.c src/x11_orient.c src/x11_chrome.c \
		src/x11_greeter.c
FA_OBJS=	${FA_SRCS:.c=.o}

EXAMPLES=	examples/dot.${PROGRAM}.fvwm \
		examples/dot.${PROGRAM}.bvwm

############################################################ TARGETS

.PHONY: all

all: ${PROGS}

${PROGRAM}: ${FA_OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${.ALLSRC}

${FA_OBJS}: src/framework_autorotate.h

src/x11_orient.o src/x11_chrome.o: src/x11_priv.h

xlogin_recenter: src/xlogin_recenter.c
	${CC} ${CFLAGS} ${CPPFLAGS} -o ${.TARGET} src/xlogin_recenter.c \
		${LDFLAGS} -lX11 -lXrandr

.PHONY: install

install: ${PROGS} ${MAN} libexec/map-touchscreen rc.d/${PROGRAM} \
	${EXAMPLES}
	mkdir -p ${DESTDIR}${SBINDIR} \
		${DESTDIR}${LIBEXECDIR} \
		${DESTDIR}${RCDIR} \
		${DESTDIR}${MANDIR} \
		${DESTDIR}${EXAMPLESDIR}
	install -m 755 ${PROGRAM} ${DESTDIR}${SBINDIR}
	install -m 755 xlogin_recenter ${DESTDIR}${LIBEXECDIR}
	install -m 755 libexec/map-touchscreen ${DESTDIR}${LIBEXECDIR}
	install -m 755 rc.d/${PROGRAM} ${DESTDIR}${RCDIR}
	install -m 644 ${MAN} ${DESTDIR}${MANDIR}
	install -m 644 ${EXAMPLES} ${DESTDIR}${EXAMPLESDIR}

.PHONY: clean

clean:
	rm -f ${PROGS} ${FA_OBJS}

################################################################################
# END
################################################################################
