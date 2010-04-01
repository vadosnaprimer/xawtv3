#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "config.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/XawInit.h>
#include <X11/Xaw/Simple.h>

#include "grab.h"
#include "x11.h"

#define DISPLAY             XtDisplay
#define SCREEN              XtScreen
#define WINDOW              XtWindow
#define DEL_TIMER(proc)     XtRemoveTimeOut(proc)
#define ADD_TIMER(proc)     XtAppAddTimeOut(app_context,200,proc,NULL)

/* ------------------------------------------------------------------------ */

extern XtAppContext  app_context;

static int             display_bits = 0;
static int             display_bytes = 0;

int
x11_init(Display *dpy)
{
    XVisualInfo         *info, template;
    int                  found;
    int                  format = 0;
    Screen               *scr = DefaultScreenOfDisplay(dpy);
    Window               root;
    XWindowAttributes    wts;

    /* Ask for visual type */
    template.screen = XDefaultScreen(dpy);
    template.visualid =
	XVisualIDFromVisual(DefaultVisualOfScreen(scr));
    info = XGetVisualInfo(dpy, VisualIDMask | VisualScreenMask, &template,
			  &found);

    root=DefaultRootWindow(dpy);
    XGetWindowAttributes(dpy, root, &wts);

    display_bits = wts.depth;
    display_bytes = (display_bits+7)/8;
    if (debug)
	fprintf(stderr,"x11: display: %d bits, %d bytes\n",
		display_bits,display_bytes);
    if (info->class == TrueColor) {
	switch (display_bytes) {
	case 2:
	    format = (display_bits == 15) ? VIDEO_RGB15 : VIDEO_RGB16;
	    break;
	case 3:
	    format = VIDEO_RGB24;
	    break;
	case 4:
	    format = VIDEO_RGB32;
	    break;
	}
    }
    if (0 == format) {
	fprintf(stderr, "sorry, visual not supported\n");
	exit(1);
    }
    XFree(info);
    return format;
}

/* ------------------------------------------------------------------------ */
/* video overlay stuff                                                      */

int        x11_native_format;
int        swidth,sheight;                         /* screen  */

static Widget    video,video_parent;
static int       wx, wy, wwidth,wheight,wmap;      /* window  */
static int       ox, oy, owidth,oheight;           /* overlay */
static int       maxx = 768, maxy = 576;

static set_overlay           overlay_cb;
static XtIntervalId          overlay_refresh;
static int                   did_refresh, oc_count;
static int                   visibility = VisibilityFullyObscured;
static int                   conf = 1, overlay_on = 0;
static struct OVERLAY_CLIP   oc[32];

/* ------------------------------------------------------------------------ */

static char *events[] = {
    "0", "1",
    "KeyPress",
    "KeyRelease",
    "ButtonPress",
    "ButtonRelease",
    "MotionNotify",
    "EnterNotify",
    "LeaveNotify",
    "FocusIn",
    "FocusOut",
    "KeymapNotify",
    "Expose",
    "GraphicsExpose",
    "NoExpose",
    "VisibilityNotify",
    "CreateNotify",
    "DestroyNotify",
    "UnmapNotify",
    "MapNotify",
    "MapRequest",
    "ReparentNotify",
    "ConfigureNotify",
    "ConfigureRequest",
    "GravityNotify",
    "ResizeRequest",
    "CirculateNotify",
    "CirculateRequest",
    "PropertyNotify",
    "SelectionClear",
    "SelectionRequest",
    "SelectionNotify",
    "ColormapNotify",
    "ClientMessage",
    "MappingNotify"
};

/* ------------------------------------------------------------------------ */

int
x11_error_dev_null(Display * dpy, XErrorEvent * event)
{
    if (debug > 1)
	fprintf(stderr," x11-error");
    return 0;
}

static void
add_clip(int x1, int y1, int x2, int y2)
{
    if (oc[oc_count].x1 != x1 || oc[oc_count].y1 != y1 ||
	oc[oc_count].x2 != x2 || oc[oc_count].y2 != y2) {
	conf = 1;
    }
    oc[oc_count].x1 = x1;
    oc[oc_count].y1 = y1;
    oc[oc_count].x2 = x2;
    oc[oc_count].y2 = y2;
    oc_count++;
} 

static void
get_clips()
{
    int x1,y1,x2,y2,lastcount;
    Display *dpy;
    XWindowAttributes wts;
    Window root, me, rroot, parent, *children;
    uint nchildren, i;
    void *old_handler = XSetErrorHandler(x11_error_dev_null);

    if (debug > 1)
	fprintf(stderr," getclips");
    lastcount = oc_count;
    oc_count = 0;
    dpy = DISPLAY(video);

    if (ox<0)
	add_clip(0, 0, (uint)(-ox), oheight);
    if (oy<0)
	add_clip(0, 0, owidth, (uint)(-oy));
    if ((ox+owidth) > swidth)
	add_clip(swidth-ox, 0, owidth, oheight);
    if ((oy+oheight) > sheight)
	add_clip(0, sheight-oy, owidth, oheight);
    
    root=DefaultRootWindow(dpy);
    me = WINDOW(video);
    for (;;) {
	XQueryTree(dpy, me, &rroot, &parent, &children, &nchildren);
	XFree((char *) children);
	/* fprintf(stderr,"me=0x%x, parent=0x%x\n",me,parent); */
	if (root == parent)
	    break;
	me = parent;
    }
    XQueryTree(dpy, root, &rroot, &parent, &children, &nchildren);

    for (i = 0; i < nchildren; i++)
	if (children[i]==me)
	    break;
    
    for (i++; i<nchildren; i++) {
	XGetWindowAttributes(dpy, children[i], &wts);
	if (!(wts.map_state & IsViewable))
	    continue;
	
	x1=wts.x-ox;
	y1=wts.y-oy;
	x2=x1+wts.width+2*wts.border_width;
	y2=y1+wts.height+2*wts.border_width;
	if ((x2 < 0) || (x1 > (int)owidth) || (y2 < 0) || (y1 > (int)oheight))
	    continue;
	
	if (x1<0)      	     x1=0;
	if (y1<0)            y1=0;
	if (x2>(int)owidth)  x2=owidth;
	if (y2>(int)oheight) y2=oheight;
	add_clip(x1, y1, x2, y2);
    }
    XFree((char *) children);

    if (lastcount != oc_count)
	conf = 1;
    XSetErrorHandler(old_handler);
}

static void
refresh_timer(XtPointer clientData, XtIntervalId *id)
{
    Window   win = RootWindowOfScreen(SCREEN(video));
    Display *dpy = DISPLAY(video);
    XSetWindowAttributes xswa;
    unsigned long mask;
    Window   tmp;

    if (debug > 1)
	fprintf(stderr,"video: refresh\n");
    overlay_refresh = 0;
    if (wmap && visibility != VisibilityFullyObscured)
	did_refresh = 1;

    xswa.override_redirect = True;
    xswa.backing_store = NotUseful;
    xswa.save_under = False;
    mask = (CWSaveUnder | CWBackingStore| CWOverrideRedirect );
    tmp = XCreateWindow(dpy,win, 0,0, swidth,sheight, 0,
			CopyFromParent, InputOutput, CopyFromParent,
			mask, &xswa);
    XMapWindow(dpy, tmp);
    XUnmapWindow(dpy, tmp);
    XDestroyWindow(dpy, tmp);
}

static void
configure_overlay()
{
    if (!overlay_cb)
	return;

    if (debug > 1)
	fprintf(stderr,"video: configure");
    if (wmap && visibility != VisibilityFullyObscured) {
	if (visibility == VisibilityPartiallyObscured)
	    get_clips();
	else
	    oc_count = 0;

	if (debug > 1)
	    fprintf(stderr," %s\n",conf ? "yes" : "no");
	if (conf) {
	    overlay_on = 1;
	    overlay_cb(ox,oy,owidth,oheight,
		       x11_native_format,oc,oc_count);
	    if (overlay_refresh)
		DEL_TIMER(overlay_refresh);
	    overlay_refresh = ADD_TIMER(refresh_timer);
	    conf = 0;
	}
    } else {
	if (debug > 1)
	    fprintf(stderr," off\n");
	if (conf && overlay_on) {
	    overlay_on = 0;
	    overlay_cb(0,0,0,0,0,NULL,0);
	    if (overlay_refresh)
		DEL_TIMER(overlay_refresh);
	    overlay_refresh = ADD_TIMER(refresh_timer);
	    conf = 0;
	}
    }
}

void
video_new_size()
{
    Dimension x,y,w,h;

    XtVaGetValues(video_parent, XtNx, &x, XtNy, &y,
		  XtNwidth, &w, XtNheight, &h, NULL);
    wx      = x; if (wx > 32768)      wx      -= 65536;
    wy      = y; if (wy > 32768)      wy      -= 65536;
    wwidth  = w; if (wwidth > 32768)  wwidth  -= 65536;
    wheight = h; if (wheight > 32768) wheight -= 65536;
    if (debug > 1)
	fprintf(stderr,"video: shell: size %dx%d+%d+%d\n",
		wwidth,wheight,wx,wy);
    
    if (wwidth  > maxx) owidth  = maxx; else owidth  = wwidth;
    if (wheight > maxy) oheight = maxy; else oheight = wheight;
    owidth  &= ~3;
    oheight &= ~3;
    ox = wx + (wwidth -owidth)  /2;
    oy = wy + (wheight-oheight) /2;
    ox &= ~3;
    if (ox        < wx)         ox     += 4;
    if (ox+owidth > wx+wwidth)  owidth -= 4;

    conf = 1;
    configure_overlay();
}

/* ------------------------------------------------------------------------ */

static void
video_event(Widget widget, XtPointer client_data, XEvent *event, Boolean *d)
{
    if (widget == video_parent) {
	/* shell widget */
	switch(event->type) {
	case ConfigureNotify:
#if 0
	    wx      = event->xconfigure.x;
	    wy      = event->xconfigure.y;
	    wwidth  = event->xconfigure.width;
	    wheight = event->xconfigure.height;
	    if (debug > 1)
		fprintf(stderr,"video: shell: cfg %dx%d+%d+%d\n",
			wwidth,wheight,wx,wy);
#endif
	    video_new_size();
	    break;
	case MapNotify:
	    if (debug > 1)
		fprintf(stderr,"video: shell: map\n");
	    wmap = 1;
	    conf = 1;
	    configure_overlay();
	    break;
	case UnmapNotify:
	    if (debug > 1)
		fprintf(stderr,"video: shell: unmap\n");
	    wmap = 0;
	    conf = 1;
	    configure_overlay();
	    break;
	default:
	    if (debug > 1)
	    fprintf(stderr,"video: shell: %s\n",
		    events[event->type]);
	}
	return;

    } else {
	/* TV widget (+root window) */
	switch(event->type) {
	case Expose:
	    if (event->xvisibility.window == WINDOW(video)) {
		/* tv */
		if (!event->xexpose.count) {
		    if (did_refresh) {
			did_refresh = 0;
			if (debug > 1)
			    fprintf(stderr,"video: tv: last refresh expose\n");
		    } else {
			if (debug > 1)
			    fprintf(stderr,"video: tv: expose\n");
			conf = 1;
			configure_overlay();
		    }
		}
	    }
	    break;
	case VisibilityNotify:
	    if (event->xvisibility.window == WINDOW(video)) {
		/* tv */
		visibility = event->xvisibility.state;
		if (debug > 1)
		    fprintf(stderr,"video: tv: visibility %d%s\n",
			    event->xvisibility.state,
			    did_refresh?" (ignored)":"");
		if (did_refresh) {
		    if (event->xvisibility.state != VisibilityFullyObscured)
			did_refresh = 0;
		} else {
		    conf = 1;
		    configure_overlay();
		}
	    } else {
		/* root */
		if (debug > 1)
		    fprintf(stderr,"video: root: visibility\n");
	    }
	    break;
	case MapNotify:
	case UnmapNotify:
	case ConfigureNotify:
	    if (event->xvisibility.window != WINDOW(video)) {
		if (debug > 1)
		    fprintf(stderr,"video: root: %s%s\n",
			    events[event->type],did_refresh?" (ignored)":"");
		if (!did_refresh)
		    configure_overlay();
	    }
	    break;
	default:
	    if (debug > 1)
		fprintf(stderr,"video: tv(+root): %s\n",
			events[event->type]);
	    break;
	}	
    }
}

void
video_setmax(int x, int y)
{
    int ow,oh;

    if (debug)
	fprintf(stderr,"video: new maxsize %dx%d\n",x,y);

    ow = owidth;
    oh = oheight;
    maxx = x;
    maxy = y;

    video_new_size();
}

void
video_overlay(set_overlay cb)
{

    if (cb) {
	overlay_cb = cb;
	conf = 1;
	configure_overlay();
    } else {
	if (overlay_cb) {
	    overlay_on = 0;
	    overlay_cb(0,0,0,0,0,NULL,0);
	    overlay_refresh = ADD_TIMER(refresh_timer);
	}
	overlay_cb = NULL;
    }
}

Widget
video_init(Widget parent)
{
    Window root = DefaultRootWindow(DISPLAY(parent));

    swidth  = SCREEN(parent)->width;
    sheight = SCREEN(parent)->height;

    x11_native_format = x11_init(DISPLAY(parent));
    video_parent = parent;
    video = XtVaCreateManagedWidget("tv",simpleWidgetClass,parent,
				    NULL);

    /* TV Widget -- need visibility, expose */
    XtAddEventHandler(video,
		      VisibilityChangeMask |
		      StructureNotifyMask,
		      False, video_event, NULL);

    /* Shell widget -- need map, unmap, configure */
    XtAddEventHandler(parent,
		      StructureNotifyMask,
		      True, video_event, NULL);

    /* root window -- need */
    XSelectInput(DISPLAY(video),root,
		 VisibilityChangeMask |
		 SubstructureNotifyMask |
		 StructureNotifyMask);

    XtRegisterDrawable(DISPLAY(video),root,video);

    return video;
}

void
video_close(void)
{
    Window root = DefaultRootWindow(DISPLAY(video));
    
    XSelectInput(DISPLAY(video),root,0);
    XtUnregisterDrawable(DISPLAY(video),root);
}


