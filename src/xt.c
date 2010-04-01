/*
 * common X11 stuff (mostly libXt level) moved here from main.c
 *
 *   (c) 1997-2000 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "config.h"

#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <X11/StringDefs.h>
#include <X11/cursorfont.h>
#ifdef HAVE_LIBXXF86DGA
# include <X11/extensions/xf86dga.h>
# include <X11/extensions/xf86dgastr.h>
#endif
#ifdef HAVE_LIBXXF86VM
# include <X11/extensions/xf86vmode.h>
# include <X11/extensions/xf86vmstr.h>
#endif

#include "grab-ng.h"
#include "commands.h"
#include "sound.h"
#include "toolbox.h"
#include "xt.h"
#include "x11.h"
#include "channel.h"

/*----------------------------------------------------------------------*/

XtAppContext      app_context;
Widget            app_shell, tv;
Display           *dpy;
Atom              wm_protocols,wm_delete_window;
Atom              xawtv_remote,xawtv_station;

XVisualInfo       vinfo;
Colormap          colormap;

int               have_dga = 0;
int               have_vm = 0;
int               have_shmem = 0;

#ifdef HAVE_LIBXXF86VM
int               vm_count;
XF86VidModeModeInfo **vm_modelines;
#endif

char v4l_conf[128];

/*--- args ----------------------------------------------------------------*/

struct ARGS args;

XtResource args_desc[] = {
    /* name, class, type, size, offset, default_type, default_addr */
    {
	/* Strings */
	"device",
	XtCString, XtRString, sizeof(char*),
	XtOffset(struct ARGS*,device),
	XtRString, "/dev/video"
    },{
	"basename",
	XtCString, XtRString, sizeof(char*),
	XtOffset(struct ARGS*,basename),
	XtRString, "snap"
    },{
	/* Integer */
	"debug",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,debug),
	XtRString, "0"
    },{
	"bpp",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,bpp),
	XtRString, "0"
    },{
	"shift",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,shift),
	XtRString, "0"
    },{
	"xvport",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,xv_port),
	XtRString, "0"
    },{
	/* Boolean */
	"remote",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,remote),
	XtRString, "0"
    },{
	"readconfig",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,readconfig),
	XtRString, "1"
    },{
	"fullscreen",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,fullscreen),
	XtRString, "0"
    },{
	"fbdev",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,fbdev),
	XtRString, "0"
    },{
	"xvideo",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,xv_video),
	XtRString, "1"
    },{
	"hwscale",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,xv_scale),
	XtRString, "1"
    },{
	"vidmode",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,vidmode),
	XtRString, "0"
    },{
	"dga",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,dga),
	XtRString, "1"
    },{
	"help",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,help),
	XtRString, "0"
    }
};

const int args_count = XtNumber(args_desc);

XrmOptionDescRec opt_desc[] = {
    { "-c",          "device",      XrmoptionSepArg, NULL },
    { "-device",     "device",      XrmoptionSepArg, NULL },
    { "-o",          "basename",    XrmoptionSepArg, NULL },
    { "-outfile",    "basename",    XrmoptionSepArg, NULL },
    
    { "-v",          "debug",       XrmoptionSepArg, NULL },
    { "-debug",      "debug",       XrmoptionSepArg, NULL },
    { "-b",          "bpp",         XrmoptionSepArg, NULL },
    { "-bpp",        "bpp",         XrmoptionSepArg, NULL },
    { "-shift",      "shift",       XrmoptionSepArg, NULL },
    { "-xvport",     "xvport",      XrmoptionSepArg, NULL },
    
    { "-remote",     "remote",      XrmoptionNoArg,  "1" },
    { "-n",          "readconfig",  XrmoptionNoArg,  "0" },
    { "-noconf",     "readconfig",  XrmoptionNoArg,  "0" },
    { "-f",          "fullscreen",  XrmoptionNoArg,  "1" },
    { "-fullscreen", "fullscreen",  XrmoptionNoArg,  "1" },
    
    { "-fb",         "fbdev",       XrmoptionNoArg,  "1" },
    { "-xv",         "xvideo",      XrmoptionNoArg,  "1" },
    { "-scale",      "hwscale",     XrmoptionNoArg,  "1" },
    { "-vm",         "vidmode",     XrmoptionNoArg,  "1" },
    { "-dga",        "dga",         XrmoptionNoArg,  "1" },
    { "-noxv",       "xvideo",      XrmoptionNoArg,  "0" },
    { "-noscale",    "hwscale",     XrmoptionNoArg,  "0" },
    { "-novm",       "vidmode",     XrmoptionNoArg,  "0" },
    { "-nodga",      "dga",         XrmoptionNoArg,  "0" },
    
    { "-h",          "help",        XrmoptionNoArg,  "1" },
    { "-help",       "help",        XrmoptionNoArg,  "1" },
    { "--help",      "help",        XrmoptionNoArg,  "1" },
};

const int opt_count = (sizeof(opt_desc)/sizeof(XrmOptionDescRec));

/*----------------------------------------------------------------------*/

void
CommandAction(Widget widget, XEvent *event,
	      String *params, Cardinal *num_params)
{
    do_command(*num_params,params);
}

void
set_property(int freq, char *channel, char *name)
{
    int  len;
    char line[80];

    len  = sprintf(line,"%.3f",(float)freq/16)+1;
    if (NULL != channel)
	len += sprintf(line+len,"%s",channel)+1;
    if (NULL != name)
	len += sprintf(line+len,"%s",name)+1;
    XChangeProperty(dpy, XtWindow(app_shell),
                    xawtv_station, XA_STRING,
                    8, PropModeReplace,
                    line, len);
}

void command_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct DO_CMD *cmd = clientdata;
    do_command(cmd->argc,cmd->argv);
}

/*----------------------------------------------------------------------*/

void
xfree_dga_init()
{
#ifdef HAVE_LIBXXF86DGA
    int  flags,foo,bar,ma,mi;

    if (!do_overlay)
	return;
    
    if (args.dga) {
	if (XF86DGAQueryExtension(dpy,&foo,&bar)) {
	    XF86DGAQueryDirectVideo(dpy,XDefaultScreen(dpy),&flags);
	    if (flags & XF86DGADirectPresent) {
		XF86DGAQueryVersion(dpy,&ma,&mi);
		if (debug)
		    fprintf(stderr,"DGA version %d.%d\n",ma,mi);
		have_dga = 1;
	    }
	}
    }
#endif
}

void
xfree_vm_init()
{
#ifdef HAVE_LIBXXF86DGA
    int  foo,bar,i,ma,mi;

    if (!do_overlay)
	return;

    if (args.vidmode) {
	if (XF86VidModeQueryExtension(dpy,&foo,&bar)) {
	    XF86VidModeQueryVersion(dpy,&ma,&mi);
	    if (debug)
		fprintf(stderr,"VidMode  version %d.%d\n",ma,mi);
	    have_vm = 1;
	    XF86VidModeGetAllModeLines(dpy,XDefaultScreen(dpy),
				       &vm_count,&vm_modelines);
	    if (debug) {
		fprintf(stderr,"  available video mode(s):");
		for (i = 0; i < vm_count; i++) {
		    fprintf(stderr," %dx%d",
			    vm_modelines[i]->hdisplay,
			    vm_modelines[i]->vdisplay);
		}	    
		fprintf(stderr,"\n");
	    }
	}
    }
#endif
}

void
grabber_init()
{
    int sw,sh;
    void *base = NULL;
    int  width = 0, depth = 0;
#ifdef HAVE_LIBXXF86DGA
    int bar,fred;

    if (have_dga) {
	XF86DGAGetVideoLL(dpy,XDefaultScreen(dpy),(int*)&base,
			  &width,&bar,&fred);
    }
#endif
    if (!do_overlay) {
	sw = sh = depth = 0;
	fprintf(stderr,"x11: remote display (overlay disabled)\n");
    } else {
	sw = XtScreen(app_shell)->width;
	sh = XtScreen(app_shell)->height;
	depth = ng_vfmt_to_depth[x11_native_format];
	width *= (depth+7)/8;
	fprintf(stderr,"x11: %dx%d, %d bit/pixel, %d byte/scanline%s%s\n",
		sw,sh,depth,width,
		have_dga ? ", DGA" : "",
		have_vm ? ", VidMode" : "");
    }
    grabber_open(args.device,sw,sh,base,x11_native_format,width);
}

void
x11_check_remote()
{
#ifdef HAVE_GETNAMEINFO
    int fd = ConnectionNumber(dpy);
#ifdef HAVE_SOCKADDR_STORAGE
    struct sockaddr_storage ss;
#else
    struct sockaddr ss;
#endif
    char me[INET6_ADDRSTRLEN+1];
    char peer[INET6_ADDRSTRLEN+1];
    char port[17];
    int length;

    if (debug)
	fprintf(stderr, "check if the X-Server is local ... ");
    
    /* me */
    length = sizeof(ss);
    if (-1 == getsockname(fd,(struct sockaddr*)&ss,&length)) {
	perror("getsockname");
	return;
    }
    if (debug)
	fprintf(stderr,"*");

    getnameinfo((struct sockaddr*)&ss,length,
		me,INET6_ADDRSTRLEN,port,16,
		NI_NUMERICHOST | NI_NUMERICSERV);
    if (debug)
	fprintf(stderr,"*");

    /* peer */
    length = sizeof(ss);
    if (-1 == getpeername(fd,(struct sockaddr*)&ss,&length)) {
	perror("getsockname");
	return;
    }
    if (debug)
	fprintf(stderr,"*");

    getnameinfo((struct sockaddr*)&ss,length,
		peer,INET6_ADDRSTRLEN,port,16,
		NI_NUMERICHOST | NI_NUMERICSERV);
    if (debug)
	fprintf(stderr,"*");

    if (debug)
	fprintf(stderr," ok\nx11 socket: me=%s, server=%s\n",me,peer);
    if (0 != strcmp(me,peer))
	/* different hosts => assume remote display */
	do_overlay = 0;
#endif
    return;
}

void x11_misc_init()
{
    fcntl(ConnectionNumber(dpy),F_SETFD,FD_CLOEXEC);
    wm_protocols     = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    xawtv_station    = XInternAtom(dpy, "_XAWTV_STATION", False);
    xawtv_remote     = XInternAtom(dpy, "_XAWTV_REMOTE", False);
}

/*----------------------------------------------------------------------*/

void
visual_init(char *n1, char *n2)
{
    Visual         *visual;
    XVisualInfo    *vinfo_list;
    int            n;

    /* look for a useful visual */
    visual = x11_visual(XtDisplay(app_shell));
    vinfo.visualid = XVisualIDFromVisual(visual);
    vinfo_list = XGetVisualInfo(dpy, VisualIDMask, &vinfo, &n);
    vinfo = vinfo_list[0];
    XFree(vinfo_list);
    if (visual != DefaultVisualOfScreen(XtScreen(app_shell))) {
	fprintf(stderr,"switching visual (0x%lx)\n",vinfo.visualid);
	colormap = XCreateColormap(dpy,RootWindowOfScreen(XtScreen(app_shell)),
				   vinfo.visual,AllocNone);
	XtDestroyWidget(app_shell);
	app_shell = XtVaAppCreateShell(n1,n2,
				       applicationShellWidgetClass, dpy,
				       XtNvisual,vinfo.visual,
				       XtNcolormap,colormap,
				       XtNdepth, vinfo.depth,
				       NULL);
    } else {
	colormap = DefaultColormapOfScreen(XtScreen(app_shell));
    }
}

void
v4lconf_init()
{
    if (!do_overlay)
	return;

    strcpy(v4l_conf,"v4l-conf");
    if (!args.debug)
	strcat(v4l_conf," -q");
    if (args.fbdev)
	strcat(v4l_conf," -f");
    if (args.shift)
	sprintf(v4l_conf+strlen(v4l_conf)," -s %d",args.shift);
    if (args.bpp)
	sprintf(v4l_conf+strlen(v4l_conf)," -b %d",args.bpp);
    if (args.device)
	sprintf(v4l_conf+strlen(v4l_conf)," -c %s",args.device);
}

int
x11_ctrl_alt_backspace(Display *dpy)
{
    audio_off();
    video_overlay(NULL);
    video_close();
    if (have_mixer)
	mixer_close();
    if (grabber->grab_close)
	grabber->grab_close();
    fprintf(stderr,"xawtv: game over\n");
    exit(0);
}

/*----------------------------------------------------------------------*/

static int mouse_visible;
static XtIntervalId mouse_timer;

static void
mouse_timeout(XtPointer clientData, XtIntervalId *id)
{
    Widget widget = clientData;
    if (debug)
	fprintf(stderr,"xt: pointer hide\n");
    XDefineCursor(dpy, XtWindow(widget), no_ptr);
    mouse_visible = 0;
    mouse_timer = 0;
}

void
mouse_event(Widget widget, XtPointer client_data, XEvent *event, Boolean *d)
{
    if (!mouse_visible) {
	if (debug)
	    fprintf(stderr,"xt: pointer show\n");
	XDefineCursor(dpy, XtWindow(widget), left_ptr);
	mouse_visible = 1;
    }
    if (mouse_timer)
	XtRemoveTimeOut(mouse_timer);
    mouse_timer = XtAppAddTimeOut(app_context, 1000, mouse_timeout,widget);
}

/* ---------------------------------------------------------------------- */

Cursor left_ptr;
Cursor menu_ptr;
Cursor qu_ptr;
Cursor no_ptr;

Pixmap bm_yes;
Pixmap bm_no;

static unsigned char bm_yes_data[] = {
    /* -------- -------- */  0x00,
    /* -------- -------- */  0x00,
    /* ------xx xx------ */  0x18,			   
    /* ----xxxx xxxx---- */  0x3c,
    /* ----xxxx xxxx---- */  0x3c,
    /* ------xx xx------ */  0x18,
    /* -------- -------- */  0x00,
    /* -------- -------- */  0x00
};

static unsigned char bm_no_data[] = { 0,0,0,0, 0,0,0,0 };

void
create_pointers(Widget app_shell)
{
    XColor white,red,dummy;
    Screen *scr;
    
    left_ptr = XCreateFontCursor(dpy,XC_left_ptr);
    menu_ptr = XCreateFontCursor(dpy,XC_right_ptr);
    qu_ptr   = XCreateFontCursor(dpy,XC_question_arrow);
    scr = DefaultScreenOfDisplay(dpy);
    if (vinfo.depth > 1) {
	if (XAllocNamedColor(dpy,colormap,"white",&white,&dummy) &&
	    XAllocNamedColor(dpy,colormap,"red",&red,&dummy)) {
	    XRecolorCursor(dpy,left_ptr,&red,&white);
	    XRecolorCursor(dpy,menu_ptr,&red,&white);
	    XRecolorCursor(dpy,qu_ptr,&red,&white);
	} 
    }
}

void
create_bitmaps(Widget app_shell)
{
    XColor black, dummy;

    bm_yes = XCreateBitmapFromData(dpy, XtWindow(app_shell),
				   bm_yes_data, 8,8);
    bm_no = XCreateBitmapFromData(dpy, XtWindow(app_shell),
				  bm_no_data, 8,8);

    XAllocNamedColor(dpy,colormap,"black",&black,&dummy);
    no_ptr = XCreatePixmapCursor(dpy, bm_no, bm_no,
				 &black, &black,
				 0, 0);
}
