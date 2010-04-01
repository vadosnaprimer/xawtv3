#include "config.h"
#ifndef HAVE_LIBXV
#include "stdio.h"
int main(void)
{puts("Compiled without Xvideo extention support, sorry.");exit(0);}
#else

/*
 * this is a simple test app for playing around with the xvideo extention
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef HAVE_GETOPT_H
# include <getopt.h>
#endif

#include <X11/Xlib.h>
#include <X11/StringDefs.h>
#include <X11/Xatom.h>
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <X11/Xaw/XawInit.h>
#include <X11/Xaw/Simple.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>

#define WIDTH_INC     64
#define HEIGHT_INC    48

void CloseMainAction(Widget, XEvent*, String*, Cardinal*);

static XtActionsRec actionTable[] = {
    { "CloseMain",   CloseMainAction  },
};

int     port=-1;
GC      gc;
Atom    wm;
Widget  app_shell,video;

/*-------------------------------------------------------------------------*/

void
CloseMainAction(Widget widget, XEvent *event,
		String *params, Cardinal *num_params)
{
    exit(0);
}

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

static void
resize_event(Widget widget, XtPointer client_data, XEvent *event, Boolean *d)
{
    static int width,height;
    int wx,wy,wwidth,wheight;
    Display *dpy = XtDisplay(video);
    Screen  *scr = DefaultScreenOfDisplay(dpy);
    Pixmap   pix;

    switch(event->type) {
    case ConfigureNotify:
	wx      = event->xconfigure.x;
	wy      = event->xconfigure.y;
	wwidth  = event->xconfigure.width;
	wheight = event->xconfigure.height;
#if 0
	fprintf(stderr,"ConfigureNotify: %dx%d+%d+%d\n",
		wwidth,wheight,wx,wy);
#endif
	if (width != wwidth || height != wheight) {
	    width  = wwidth;
	    height = wheight;
	    fprintf(stderr,"resize: %dx%d\n",
		    width,height);
#if 1
	    XvPutVideo(dpy,port,XtWindow(video),gc,
		       0,0,width,height,0,0,width,height);
#else
	    pix = XCreatePixmap(dpy, RootWindowOfScreen(scr),
				width, height,DefaultDepthOfScreen(scr));
	    XvPutStill(dpy,port,pix,gc,0,0,width,height,0,0,width,height);
#endif
	}
	break;
    default:
	fprintf(stderr,"got event: %s (%d)\n",
		events[event->type],event->type);
	break;
    }
}

/*-------------------------------------------------------------------------*/

XvAdaptorInfo        *ai;
XvEncodingInfo       *ei;
XvAttribute          *at;
XvImageFormatValues  *fo;

static char *reasons[] = {
    "XvStarted",
    "XvStopped",
    "XvBusy",
    "XvPreempted",
    "XvHardError",
};

int
main(int argc, char *argv[])
{
#ifdef HAVE_GETOPT_LONG
    static struct option long_opts[] = {
	{"port",        1, 0, 'p'},
	{"help",        0, 0, 'h'},
	{0,             0, 0, 0}
    };
#endif

    XtAppContext app_context;
    Display *dpy;
    Atom attr;

    int ver, rel, req, ev, err, val;
    int adaptors,encodings,attributes,formats;
    int i,j,p,c;

    /* init X11 */
    app_shell = XtAppInitialize(&app_context,
				"xvideo",
				NULL, 0,
				&argc, argv,
				NULL,
				NULL, 0);
    dpy = XtDisplay(app_shell);
    XtAppAddActions(app_context,actionTable,
		    sizeof(actionTable)/sizeof(XtActionsRec));
    XtOverrideTranslations(app_shell,XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: CloseMain()"));
    XtAddEventHandler(app_shell, StructureNotifyMask,
		      True, resize_event, NULL);
    wm = XInternAtom(XtDisplay(app_shell), "WM_DELETE_WINDOW", False);

    /* parse options */
    for (;;) {
#ifdef HAVE_GETOPT_LONG
	if (-1 == (c = getopt_long(argc, argv, "hp:", long_opts,NULL)))
	    break;
#else
	if (-1 == (c = getopt(argc, argv, "hp:")))
	    break;
#endif
	switch (c) {
	case 0:
	    /* long option */
	    break;
	case 'p':
	    port = atoi(optarg);
	    break;
	case 'h':
	default:
	    fprintf(stderr,
		    "This is a xvideo test application.\n"
		    "Options:\n"
		    "  -h | --help    this text"
		    "  -p | --port n  create a window and call XvPutVideo with"
		    "                 port >n<\n");
	    exit(1);
	}
    }

    /* query+print Xvideo properties */
    if (Success != XvQueryExtension(dpy,&ver,&rel,&req,&ev,&err)) {
	puts("Server does'nt support Xvideo");
	exit(1);
    }
    if (Success != XvQueryAdaptors(dpy,DefaultRootWindow(dpy),&adaptors,&ai)) {
	puts("Oops: XvQueryAdaptors failed");
	exit(1);
    }
    printf("%d adaptors available.\n",adaptors);
    for (i = 0; i < adaptors; i++) {
	printf("  name:  %s\n"
	       "  type:  %s%s%s%s%s\n"
	       "  ports: %ld\n"
	       "  first: %ld\n",
	       ai[i].name,
	       (ai[i].type & XvInputMask)  ? "input "  : "",
	       (ai[i].type & XvOutputMask) ? "output " : "",
	       (ai[i].type & XvVideoMask)  ? "video "  : "",
	       (ai[i].type & XvStillMask)  ? "still "  : "",
	       (ai[i].type & XvImageMask)  ? "image "  : "",
	       ai[i].num_ports,
	       ai[i].base_id);
	printf("  format list\n");
	for (j = 0; j < ai[i].num_formats; j++) {
	    printf("    depth=%d, visual=%ld\n",
		   ai[i].formats[j].depth,
		   ai[i].formats[j].visual_id);
	}
	for (p = ai[i].base_id; p < ai[i].base_id+ai[i].num_ports; p++) {

	    printf("  encoding list for port %d\n",p);
	    if (Success != XvQueryEncodings(dpy, p, &encodings, &ei)) {
		puts("Oops: XvQueryEncodings failed");
		continue;
	    }
	    for (j = 0; j < encodings; j++) {
		printf("    id=%ld, name=%s, size=%ldx%ld\n",
		       ei[j].encoding_id, ei[j].name,
		       ei[j].width, ei[j].height);		
	    }
	    XvFreeEncodingInfo(ei);

	    printf("  attribute list for port %d\n",p);
	    at = XvQueryPortAttributes(dpy,p,&attributes);
	    for (j = 0; j < attributes; j++) {
		fprintf(stderr,"    %s%s%s, %i -> %i",
			at[j].name,
			(at[j].flags & XvGettable) ? " get" : "",
			(at[j].flags & XvSettable) ? " set" : "",
			at[j].min_value,at[j].max_value);
		attr = XInternAtom(dpy, at[j].name, False);
		if (at[j].flags & XvGettable) {
		    XvGetPortAttribute(dpy, p, attr, &val);
		    fprintf(stderr,", val=%d",val);
		}
		fprintf(stderr,"\n");
	    }
	    if (at)
		XFree(at);
	    
	    fo = XvListImageFormats(dpy, p, &formats);
	    printf("  image format list for port %d\n",p);
	    for(j = 0; j < formats; j++) {
		fprintf(stderr, "    0x%x (%4.4s) %s\n",
			fo[j].id,
			(char*)&fo[j].id,
			(fo[j].format == XvPacked) ? "packed" : "planar");
	    }
	    if (fo)
		XFree(fo);
	}
	printf("\n");
    }
    if (adaptors > 0)
	XvFreeAdaptorInfo(ai);
    if (-1 == port)
	exit(0);

    /* open test window */
    video = XtVaCreateManagedWidget("video",simpleWidgetClass,app_shell,
				    XtNwidth,  4*WIDTH_INC,
				    XtNheight, 4*HEIGHT_INC,
				    NULL);
    XtRealizeWidget(app_shell);
    XtVaSetValues(app_shell,
		  XtNtitle,     "Xv test application",
		  XtNwidthInc,  WIDTH_INC,
		  XtNheightInc, HEIGHT_INC,
		  XtNminWidth,  WIDTH_INC,
		  XtNminHeight, HEIGHT_INC,
		  NULL);
    XSetWMProtocols(XtDisplay(app_shell), XtWindow(app_shell), &wm, 1);
    gc = XCreateGC(dpy,XtWindow(video),0,NULL);

    /* receive events */
    XvSelectPortNotify(dpy, port, 1);
    XvSelectVideoNotify(dpy, XtWindow(video), 1);
    
    /* main loop */
    for (;;) {
	XEvent event;
	XtAppNextEvent(app_context,&event);
	if (XtDispatchEvent(&event))
	    continue;
	switch (event.type-ev) {
	case XvVideoNotify:
	{
	    XvVideoNotifyEvent *xve = (XvVideoNotifyEvent*)&event;
	    fprintf(stderr,"XvVideoNotify, reason=%s\n",
		    reasons[xve->reason]);
	    break;
	}
	case XvPortNotify:
	{
	    XvPortNotifyEvent *xpe = (XvPortNotifyEvent*)&event;
	    fprintf(stderr,"XvPortNotify: %s=%ld\n",
		    XGetAtomName(dpy,xpe->attribute),xpe->value);
	    break;
	}
	}
    }

    /* keep compiler happy */
    exit(0);
}

#endif
