#include "config.h"
#ifndef HAVE_LIBXV
#include "stdio.h"
int main(){puts("Compiled without Xvideo extention support, sorry.");exit(0);}
#else
/*
 * put a TV image to the root window - requires Xvideo
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>

#include <X11/Xlib.h>
#include <X11/StringDefs.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>

#include "parseconfig.h"

int     port=-1,bye=0;
GC      gc;
Atom    mute;

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

static void
wm_menu(void)
{
    char filename[100];
    char **list;
    
    sprintf(filename,"%s/%s",getenv("HOME"),".xawtv");
    cfg_parse_file(CONFIGFILE);
    cfg_parse_file(filename);

    printf("\"TV stations\" MENU\n");
    for (list = cfg_list_sections(); *list != NULL; list++) {
	if (0 == strcmp(*list,"defaults")) continue;
	if (0 == strcmp(*list,"global"))   continue;
	if (0 == strcmp(*list,"launch"))   continue;
	printf("\t\"%s\" EXEC v4lctl setstation \"%s\"\n",*list,*list);    
    }
    printf("\"TV stations\" END\n");    
}

int
main(int argc, char *argv[])
{
    Display *dpy;
    Screen  *scr;
    Window win;
    XWindowAttributes wts;

    int ver, rel, req, ev, err;
    int adaptors,attributes;
    int i,stop;

    stop = 0;
    if (argc > 1) {
	/* windowmaker menu */
	if (0 == strcmp(argv[1],"-wm")) {
	    wm_menu();
	    exit(0);
	}
	/* stop video */
	if (0 == strcmp(argv[1],"-stop")) {
	    stop = 1;
	}
    }

    /* init X11 */
    dpy = XOpenDisplay(NULL);
    scr = DefaultScreenOfDisplay(dpy);
    win = RootWindowOfScreen(scr);
    
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
	printf("  name:  %s\n",ai[i].name);
	
	/* video adaptor ? */
	if ((ai[i].type & XvInputMask) &&
	    (ai[i].type & XvVideoMask) &&
	    (port == -1)) {
	    port = ai[i].base_id;
	}
    }
    if (adaptors > 0)
	XvFreeAdaptorInfo(ai);
    if (-1 == port)
	exit(0);

    at = XvQueryPortAttributes(dpy,port,&attributes);
    for (i = 0; i < attributes; i++) {
	if (0 == strcmp("XV_MUTE",at[i].name))
	    mute = XInternAtom(dpy, "XV_MUTE", False);
    }

    if (stop) {
	/* stop video */
	XvStopVideo(dpy,port,win);
	if (mute)
	    XvSetPortAttribute(dpy,port,mute,1);
	XCloseDisplay(dpy);
	exit(0);
    }

    /* fork into background, but keep tty */
    if (fork())
	exit(0);
    
    /* put video to the root window */
    gc = XCreateGC(dpy,win,0,NULL);
    XGetWindowAttributes(dpy, win, &wts);
    XvPutVideo(dpy,port,win,gc,
	       0,0,wts.width,wts.height,
	       0,0,wts.width,wts.height);
    if (mute)
	XvSetPortAttribute(dpy,port,mute,0);

    /* receive events */
    XvSelectPortNotify(dpy, port, 1);
    XvSelectVideoNotify(dpy, win, 1);
    
    /* main loop */
    for (;!bye;) {
	XEvent event;
	XNextEvent(dpy,&event);
	switch (event.type-ev) {
	case XvVideoNotify:
	{
	    XvVideoNotifyEvent *xve = (XvVideoNotifyEvent*)&event;
	    printf("XvVideoNotify, reason=%s, exiting\n",
		   reasons[xve->reason]);
	    bye=1;
	    break;
	}
	case XvPortNotify:
	{
	    XvPortNotifyEvent *xpe = (XvPortNotifyEvent*)&event;
	    printf("XvPortNotify: %s=%ld\n",
		   XGetAtomName(dpy,xpe->attribute),xpe->value);
	    break;
	}
	}
    }
    XvStopVideo(dpy,port,win);
    XClearWindow(dpy,win);
    XCloseDisplay(dpy);

    /* keep compiler happy */
    exit(0);
}

#endif
