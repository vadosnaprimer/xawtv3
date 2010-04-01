/*
 * Some WindowManager specific stuff
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "wmhooks.h"
#include "atoms.h"

/* ------------------------------------------------------------------------ */

void (*wm_stay_on_top)(Display *dpy, Window win, int state) = NULL;

/* ------------------------------------------------------------------------ */

extern int debug;

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */

static void
net_wm_stay_on_top(Display *dpy, Window win, int state)
{
    XEvent e;

    e.xclient.type = ClientMessage;
    e.xclient.message_type = _NET_WM_STATE;
    e.xclient.display = dpy;
    e.xclient.window = win;
    e.xclient.format = 32;
    e.xclient.data.l[0] = (state == 1)
	? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
    e.xclient.data.l[1] = _NET_WM_STATE_STAYS_ON_TOP;
    e.xclient.data.l[2] = 0l;
    e.xclient.data.l[3] = 0l;
    e.xclient.data.l[4] = 0l;

    XSendEvent(dpy, DefaultRootWindow(dpy), False,
	       SubstructureRedirectMask, &e);
}

/* ------------------------------------------------------------------------ */

#define WIN_LAYER_ONBOTTOM               2
#define WIN_LAYER_NORMAL                 4
#define WIN_LAYER_ONTOP                  6

/* tested with icewm + WindowMaker */
static void
gnome_stay_on_top(Display *dpy, Window win, int state)
{
    XClientMessageEvent  xev;

    if (0 == win)
	return;

    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.window = win;
    xev.message_type = _WIN_LAYER;
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

    /* netwm compliant */
    if (Success == XGetWindowProperty
        (dpy, root, _NET_SUPPORTED, 0, (65536 / sizeof(long)), False,
         AnyPropertyType, &type, &format, &nitems, &bytesafter, &args) &&
	nitems > 0) {
	if (debug)
	    fprintf(stderr,"wmhooks: netwm\n");
	wm_stay_on_top = net_wm_stay_on_top;
        XFree(args);
        return 0;
    }

    /* gnome-compilant */
    if (Success == XGetWindowProperty
	(dpy, root, _WIN_SUPPORTING_WM_CHECK, 0, (65536 / sizeof(long)), False,
	 AnyPropertyType, &type, &format, &nitems, &bytesafter, &args) &&
	nitems > 0) {
	if (debug)
	    fprintf(stderr,"wmhooks: gnome\n");
	/* FIXME: check capabilities */
	wm_stay_on_top = gnome_stay_on_top;
	XFree(args);
	return 0;
    }

    /* nothing found... */
    return -1;
}
