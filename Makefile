# framework-autorotate - Framework Laptop 12 FreeBSD X11 autorotate
#
PREFIX?=	/usr/local
SBINDIR?=	${PREFIX}/sbin
LIBEXECDIR?=	${PREFIX}/libexec/framework_autorotate
RCDIR?=		${PREFIX}/etc/rc.d
MANDIR?=	${PREFIX}/share/man/man8
EXAMPLESDIR?=	${PREFIX}/share/examples/framework_autorotate

CC?=		cc
CFLAGS?=	-O2 -Wall -Wextra
CPPFLAGS+=	-I/usr/local/include -I.
LDFLAGS+=	-L/usr/local/lib

PROGS=		framework_autorotate xlogin_recenter
MAN=		framework_autorotate.8

FA_SRCS=	framework_autorotate.c util.c session.c ec.c \
		x11_orient.c x11_chrome.c x11_greeter.c
FA_OBJS=	${FA_SRCS:.c=.o}

all: ${PROGS}

framework_autorotate: ${FA_OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ ${FA_OBJS}

.SUFFIXES: .c .o
.c.o:
	${CC} ${CFLAGS} ${CPPFLAGS} -c $< -o $@

${FA_OBJS}: framework_autorotate.h
x11_orient.o x11_chrome.o: x11_priv.h

xlogin_recenter: xlogin_recenter.c
	${CC} ${CFLAGS} ${CPPFLAGS} -o $@ xlogin_recenter.c \
		${LDFLAGS} -lX11 -lXrandr

install: ${PROGS} ${MAN} libexec/map-touchscreen rc.d/framework_autorotate
	mkdir -p ${DESTDIR}${SBINDIR} ${DESTDIR}${LIBEXECDIR} \
		${DESTDIR}${RCDIR} ${DESTDIR}${MANDIR} \
		${DESTDIR}${EXAMPLESDIR}
	install -m 755 framework_autorotate ${DESTDIR}${SBINDIR}/framework_autorotate
	install -m 755 xlogin_recenter ${DESTDIR}${LIBEXECDIR}/xlogin_recenter
	install -m 755 libexec/map-touchscreen \
		${DESTDIR}${LIBEXECDIR}/map-touchscreen
	install -m 755 rc.d/framework_autorotate \
		${DESTDIR}${RCDIR}/framework_autorotate
	install -m 644 ${MAN} ${DESTDIR}${MANDIR}/${MAN}
	install -m 644 examples/dot.framework_autorotate.fvwm \
		${DESTDIR}${EXAMPLESDIR}/dot.framework_autorotate.fvwm
	install -m 644 examples/dot.framework_autorotate.bvwm \
		${DESTDIR}${EXAMPLESDIR}/dot.framework_autorotate.bvwm

clean:
	rm -f ${PROGS} ${FA_OBJS}

.PHONY: all install clean
