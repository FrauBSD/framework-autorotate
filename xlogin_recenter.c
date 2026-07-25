/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Best-effort: place the XDM xlogin window on the internal laptop panel
 * after RandR.  Classic xdm maps the greeter once and does not listen for
 * screen size changes.  When the desktop is extended, the root window is
 * the full virtual screen — centering on root would park the greeter on
 * an external monitor.  Always use the eDP/LVDS/DSI CRTC rectangle.
 */

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int
name_is_internal(const char *name)
{

	if (name == NULL)
		return (0);
	if (strncmp(name, "eDP", 3) == 0)
		return (1);
	if (strncmp(name, "LVDS", 4) == 0)
		return (1);
	if (strncmp(name, "DSI", 3) == 0)
		return (1);
	return (0);
}

/*
 * Return the active CRTC rectangle for the built-in panel.
 * Falls back to primary, then first connected output.
 */
static int
panel_rect(Display *dpy, Window root, int *px, int *py, int *pw, int *ph)
{
	XRRScreenResources *res;
	XRROutputInfo *oi;
	XRRCrtcInfo *ci;
	int i;
	int fx = 0, fy = 0, fw = 0, fh = 0;
	int pox = 0, poy = 0, pow = 0, poh = 0;
	int have_first = 0, have_pri = 0;

	res = XRRGetScreenResourcesCurrent(dpy, root);
	if (res == NULL)
		return (-1);

	for (i = 0; i < res->noutput; i++) {
		oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
		if (oi == NULL)
			continue;
		if (oi->connection != RR_Connected || oi->crtc == None) {
			XRRFreeOutputInfo(oi);
			continue;
		}
		ci = XRRGetCrtcInfo(dpy, res, oi->crtc);
		if (ci == NULL || ci->mode == None) {
			if (ci != NULL)
				XRRFreeCrtcInfo(ci);
			XRRFreeOutputInfo(oi);
			continue;
		}
		if (name_is_internal(oi->name)) {
			*px = ci->x;
			*py = ci->y;
			*pw = (int)ci->width;
			*ph = (int)ci->height;
			XRRFreeCrtcInfo(ci);
			XRRFreeOutputInfo(oi);
			XRRFreeScreenResources(res);
			return (0);
		}
		if (!have_first) {
			fx = ci->x;
			fy = ci->y;
			fw = (int)ci->width;
			fh = (int)ci->height;
			have_first = 1;
		}
		/* RandR primary is often flagged via RR_GetOutputPrimary. */
		if (!have_pri &&
		    XRRGetOutputPrimary(dpy, root) == res->outputs[i]) {
			pox = ci->x;
			poy = ci->y;
			pow = (int)ci->width;
			poh = (int)ci->height;
			have_pri = 1;
		}
		XRRFreeCrtcInfo(ci);
		XRRFreeOutputInfo(oi);
	}
	XRRFreeScreenResources(res);

	if (have_pri) {
		*px = pox;
		*py = poy;
		*pw = pow;
		*ph = poh;
		return (0);
	}
	if (have_first) {
		*px = fx;
		*py = fy;
		*pw = fw;
		*ph = fh;
		return (0);
	}
	return (-1);
}

static int
class_is_xlogin(Display *dpy, Window w)
{
	XClassHint ch;
	int ok = 0;

	memset(&ch, 0, sizeof(ch));
	if (XGetClassHint(dpy, w, &ch) == 0)
		return (0);
	if ((ch.res_name != NULL && strcasecmp(ch.res_name, "xlogin") == 0) ||
	    (ch.res_class != NULL && strcasecmp(ch.res_class, "XLogin") == 0) ||
	    (ch.res_class != NULL && strcasecmp(ch.res_class, "xlogin") == 0))
		ok = 1;
	if (ch.res_name != NULL)
		XFree(ch.res_name);
	if (ch.res_class != NULL)
		XFree(ch.res_class);
	return (ok);
}

static int
find_xlogin(Display *dpy, Window root, Window w, Window *out)
{
	Window parent, *kids = NULL;
	unsigned n = 0, i;

	if (class_is_xlogin(dpy, w)) {
		*out = w;
		return (1);
	}
	if (XQueryTree(dpy, w, &root, &parent, &kids, &n) == 0)
		return (0);
	for (i = 0; i < n; i++) {
		if (find_xlogin(dpy, root, kids[i], out)) {
			XFree(kids);
			return (1);
		}
	}
	if (kids != NULL)
		XFree(kids);
	return (0);
}

int
main(void)
{
	Display *dpy;
	Window root, login = None;
	XWindowAttributes wa, ra;
	int x, y;
	int px = 0, py = 0, pw = 0, ph = 0;
	const char *panel_src;

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL)
		return (1);
	root = DefaultRootWindow(dpy);
	if (!find_xlogin(dpy, root, root, &login)) {
		XCloseDisplay(dpy);
		return (0);	/* greeter not up yet — not an error */
	}
	if (XGetWindowAttributes(dpy, login, &wa) == 0 ||
	    XGetWindowAttributes(dpy, root, &ra) == 0) {
		XCloseDisplay(dpy);
		return (1);
	}

	/*
	 * Place on the internal panel (extend = wallpaper on externals;
	 * greeter stays on the laptop).  Match xdm greeter/Login.c Realize
	 * when XtNx/XtNy are unset (-1):
	 *   x = panel_x + (panel_w - w) / 2
	 *   y = panel_y + (panel_h - h) / 3
	 */
	if (panel_rect(dpy, root, &px, &py, &pw, &ph) == 0) {
		panel_src = "panel";
	} else {
		px = 0;
		py = 0;
		pw = ra.width;
		ph = ra.height;
		panel_src = "root";
	}
	x = px + (pw - wa.width) / 2;
	y = py + (ph - wa.height) / 3;
	if (x < px)
		x = px;
	if (y < py)
		y = py;
	if (x != wa.x || y != wa.y) {
		XMoveWindow(dpy, login, x, y);
		XRaiseWindow(dpy, login);
		XFlush(dpy);
		fprintf(stderr, "xlogin_recenter: %dx%d -> +%d+%d "
		    "(%s %dx%d+%d+%d, xdm /2 /3; root %dx%d)\n",
		    wa.width, wa.height, x, y,
		    panel_src, pw, ph, px, py, ra.width, ra.height);
	}
	XCloseDisplay(dpy);
	return (0);
}
