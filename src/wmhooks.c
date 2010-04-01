/*
 * Some WindowManager specific stuff
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "grab.h"

/* ------------------------------------------------------------------------ */

void (*wm_stay_on_top)(Display *dpy, Window win, int state) = NULL;

/* ------------------------------------------------------------------------ */

static Atom net_wm;
static Atom net_wm_state;
static Atom net_wm_top;

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */

void
net_wm_stay_on_top(Display *dpy, Window win, int state)
{
    XEvent e;

    e.xclient.type = ClientMessage;
    e.xclient.message_type = net_wm_state;
    e.xclient.display = dpy;
    e.xclient.window = win;
    e.xclient.format = 32;
    e.xclient.data.l[0] = (state == 1)
	? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
    e.xclient.data.l[1] = net_wm_top;
    e.xclient.data.l[2] = 0l;
    e.xclient.data.l[3] = 0l;
    e.xclient.data.l[4] = 0l;

    XSendEvent(dpy, DefaultRootWindow(dpy), False,
	       SubstructureRedirectMask, &e);
}

/* ------------------------------------------------------------------------ */

static Atom gnome;
static Atom gnome_layer;

#define WIN_LAYER_ONBOTTOM               2
#define WIN_LAYER_NORMAL                 4
#define WIN_LAYER_ONTOP                  6

/* tested with icewm + WindowMaker */
void
gnome_stay_on_top(Display *dpy, Window win, int state)
{
    XClientMessageEvent  xev;

    if (0 == win)
	return;

    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.window = win;
    xev.message_type = gnome_layer;
    xev.format = 32;
    switch (state) {
    case -1: xev.data.l[0] = WIN_LAYER_ONBOTTOM; break;
    case  0: xev.data.l[0] = WIN_LAYER_NORMAL;   break;
    case  1: xev.data.l[0] = WIN_LAYER_ONTOP;    break;
    }
    XSendEvent(dpy,DefaultRootWindow(dpy),False,
	       SubstructureNotifyMask,(XEvent*)&xev);
    if (state)
	XRaiseWindow(dpy,win);
}

/* ------------------------------------------------------------------------ */

int
wm_detect(Display *dpy)
{
    Atom            type;
    int             format;
    unsigned long   nitems, bytesafter;
    unsigned char  *args = NULL;
    Window root = DefaultRootWindow(dpy);

    /* build atoms */
    net_wm       = XInternAtom(dpy, "_NET_SUPPORTED", False);
    net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    net_wm_top   = XInternAtom(dpy, "_NET_WM_STATE_STAYS_ON_TOP", False);
    gnome        = XInternAtom(dpy, "_WIN_SUPPORTING_WM_CHECK", False);
    gnome_layer  = XInternAtom(dpy, "_WIN_LAYER", False);

    /* gnome-compilant */
    if (Success == XGetWindowProperty
	(dpy, root, gnome, 0, (65536 / sizeof(long)), False,
	 AnyPropertyType, &type, &format, &nitems, &bytesafter, &args) &&
	nitems > 0) {
	fprintf(stderr,"wmhooks: gnome\n");
	/* FIXME: check capabilities */
	wm_stay_on_top = gnome_stay_on_top;
	XFree(args);
	return 0;
    }

    /* netwm compliant */
    if (Success == XGetWindowProperty
        (dpy, root, net_wm, 0, (65536 / sizeof(long)), False,
         AnyPropertyType, &type, &format, &nitems, &bytesafter, &args) &&
	nitems > 0) {
	fprintf(stderr,"wmhooks: netwm\n");
	wm_stay_on_top = net_wm_stay_on_top;
        XFree(args);
        return 0;
    }

    /* nothing found... */
    return -1;
}
