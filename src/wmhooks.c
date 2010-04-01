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

static Atom gnome;
static Atom gnome_layer;

#define WIN_LAYER_NORMAL                 4
#define WIN_LAYER_ONTOP                  6

/* tested with icewm + WindowMaker */
void
gnome_stay_on_top(Display *dpy, Window win, int state)
{
    XClientMessageEvent  xev;

    if (0 == win)
	return;

    xev.type = ClientMessage;
    xev.window = win;
    xev.message_type = gnome_layer;
    xev.format = 32;
    xev.data.l[0] = state ? WIN_LAYER_ONTOP : WIN_LAYER_NORMAL;
    XSendEvent(dpy,DefaultRootWindow(dpy),False,
	       SubstructureNotifyMask,(XEvent*)&xev);
    if (state)
	XRaiseWindow(dpy,win);
}

/* ------------------------------------------------------------------------ */

static Atom kwm;

void
kde_stay_on_top(Display *dpy, Window win, int state)
{
    /* TODO */
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
    gnome       = XInternAtom(dpy, "_WIN_SUPPORTING_WM_CHECK", False);
    gnome_layer = XInternAtom(dpy, "_WIN_LAYER", False);

    kwm = XInternAtom(dpy, "KWM_RUNNING", False);

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
    
    /* kwm (KDE) */
    if (Success == XGetWindowProperty
	(dpy, root, kwm, 0, (65536 / sizeof(long)), False,
	 AnyPropertyType, &type, &format, &nitems, &bytesafter, &args) &&
	nitems > 0) {
	fprintf(stderr,"wmhooks: kde\n");
	XFree(args);
	return 0;
    }
    
    /* nothing found... */
    return -1;
}
