/*
 * main.c for xawtv -- a TV application
 *
 *   (c) 1997-99 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "config.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/Xmd.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xatom.h>
#include <X11/Shell.h>
#include <X11/Xaw/XawInit.h>
#include <X11/Xaw/Paned.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/Box.h>
#include <X11/Xaw/Dialog.h>
#include <X11/Xaw/AsciiText.h>
#ifdef HAVE_LIBXXF86DGA
# include <X11/extensions/xf86dga.h>
# include <X11/extensions/xf86dgastr.h>
#endif
#ifdef HAVE_LIBXXF86VM
# include <X11/extensions/xf86vmode.h>
# include <X11/extensions/xf86vmstr.h>
#endif
#ifdef HAVE_MITSHM
# include <X11/extensions/XShm.h>
#endif
#ifdef HAVE_LIBXDPMS
# include <X11/extensions/dpms.h>
/* XFree 3.3.x has'nt prototypes for this ... */
Bool   DPMSQueryExtension(Display*, int*, int*);
Bool   DPMSCapable(Display*);
Status DPMSInfo(Display*, CARD16*, BOOL*);
Status DPMSEnable(Display*);
Status DPMSDisable(Display*);
#endif
#ifdef HAVE_LIBXV
# include <X11/extensions/Xv.h>
# include <X11/extensions/Xvlib.h>
#endif

#include "writefile.h"
#include "sound.h"
#include "channel.h"
#include "commands.h"
#include "frequencies.h"
#include "grab.h"
#include "xv.h"
#include "capture.h"
#include "x11.h"
#include "toolbox.h"
#include "complete.h"
#include "colorspace.h"
#include "wmhooks.h"
#include "lirc.h"

#define ONSCREEN_TIME       5000
#define TITLE_TIME          6000
#define ZAP_TIME            8000
#define CAP_TIME             100
#define SCAN_TIME            100

#define WIDTH_INC             64
#define HEIGHT_INC            48
#define LABEL_WIDTH         "16"
#define VIDMODE_DELAY        100   /* 0.1 sec */

/* what we are using for floating point X resources */
typedef float fp_res;

/*--- public variables ----------------------------------------------------*/

XtAppContext      app_context;
Widget            app_shell, tv;
Widget            opt_shell, opt_paned, chan_shell, conf_shell, str_shell;
Widget            on_shell, on_label;
Widget            launch_shell, launch_paned;
Widget            c_norm,c_input,c_freq,c_audio,c_cap;
Widget            s_bright,s_color,s_hue,s_contrast,s_volume;
Widget            chan_viewport, chan_box;
Display           *dpy;
XVisualInfo       vinfo;
Colormap          colormap;
XtWorkProcId      idle_id;
Pixmap            tv_pix;
int               stay_on_top = 0;

int               str_pid = -1;
Widget            str_fbutton,str_text;
char              *str_filename;

int               have_config = 0;
Atom              wm_protocols[2],xawtv_remote,xawtv_station;
XtIntervalId      title_timer, audio_timer, zap_timer, scan_timer, on_timer;
int               pointer_on = 1, on_skip = 0;
int               debug = 0;
int               fs = 0;
int               zap_start,zap_fast;

char              modename[64];
char              *progname;
int               have_dga = 0;
int               have_vm = 0;
int               have_shmem = 0;
#ifdef HAVE_LIBXXF86VM
int               vm_count;
XF86VidModeModeInfo **vm_modelines;
#endif
int               lirc;

int               rec_writer;
int               rec_wsync;
XtWorkProcId      rec_id;
int               cur_avi_audio  = 0;
int               cur_avi_format = VIDEO_RGB15_LE;
int               cur_avi_fps    = 15;
Widget            avi_size,avi_status;

char v4l_conf[128] = "v4l-conf";

/* fwd decl */
void change_audio(int mode);
void watch_audio(XtPointer data, XtIntervalId *id);

/*-------------------------------------------------------------------------*/

static struct MY_TOPLEVELS {
    char        *name;
    Widget      *shell;
    int         *check;
    int          first;
    int          mapped;
} my_toplevels [] = {
    { "options",  &opt_shell             },
    { "channels", &chan_shell,  &count   },
    { "config",   &conf_shell,           },
    { "streamer", &str_shell             },
    { "launcher",  &launch_shell, &nlaunch }
};
#define TOPLEVELS (sizeof(my_toplevels)/sizeof(struct MY_TOPLEVELS))

struct STRTAB *cmenu = NULL;
static char default_title[256] = "???";

struct DO_CMD {
    int  argc;
    char *argv[8];
};

struct DO_AC {
    int  argc;
    char *name;
    char *argv[8];
};

/*--- args ----------------------------------------------------------------*/

static struct ARGS {
    /* char */
    char *device;
    char *basename;
    /* int */
    int  debug;
    int  bpp;
    int  shift;
    /* boolean */
    int  remote;
    int  readconfig;
    int  showpointer;
    int  fullscreen;
    int  fbdev;
    int  xv_video;
    int  xv_scale;
    int  vidmode;
    int  dga;
    int  help;
} args;

static XtResource args_desc[] = {
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
	/* Boolean */
	"remote",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,remote),
	XtRString, "0"
    },{
	"readconfig",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,readconfig),
	XtRString, "1"
    },{
	"showpointer",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,showpointer),
	XtRString, "1"
    },{
	"fullscreen",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,fullscreen),
	XtRString, "0"
    },{
	"fbdev",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,fbdev),
	XtRString, "0"
    },{
	"xvideo",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,xv_video),
	XtRString, "1"
    },{
	"hwscale",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,xv_scale),
	XtRString, "1"
    },{
	"vidmode",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,vidmode),
	XtRString, "1"
    },{
	"dga",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,dga),
	XtRString, "1"
    },{
	"help",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,help),
	XtRString, "0"
    }
};

static XrmOptionDescRec opt_desc[] = {
  { "-c",          "device",      XrmoptionSepArg, NULL },
  { "-device",     "device",      XrmoptionSepArg, NULL },
  { "-o",          "basename",    XrmoptionSepArg, NULL },
  { "-outfile",    "basename",    XrmoptionSepArg, NULL },

  { "-v",          "debug",       XrmoptionSepArg, NULL },
  { "-debug",      "debug",       XrmoptionSepArg, NULL },
  { "-b",          "bpp",         XrmoptionSepArg, NULL },
  { "-bpp",        "bpp",         XrmoptionSepArg, NULL },
  { "-shift",      "shift",       XrmoptionSepArg, NULL },

  { "-remote",     "remote",      XrmoptionNoArg,  "1" },
  { "-n",          "readconfig",  XrmoptionNoArg,  "0" },
  { "-noconf",     "readconfig",  XrmoptionNoArg,  "0" },
  { "-m",          "showpointer", XrmoptionNoArg,  "0" },
  { "-nomouse",    "showpointer", XrmoptionNoArg,  "0" },
  { "-f",          "fullscreen",  XrmoptionNoArg,  "1" },
  { "-fullscreen", "fullscreen",  XrmoptionNoArg,  "1" },

  { "-fb",         "fbdev",       XrmoptionNoArg,  "1" },
  { "-noxv",       "xvideo",      XrmoptionNoArg,  "0" },
  { "-noscale",    "hwscale",     XrmoptionNoArg,  "0" },
  { "-novm",       "vidmode",     XrmoptionNoArg,  "0" },
  { "-nodga",      "dga",         XrmoptionNoArg,  "0" },

  { "-h",          "help",        XrmoptionNoArg,  "1" },
  { "-help",       "help",        XrmoptionNoArg,  "1" },
  { "--help",      "help",        XrmoptionNoArg,  "1" },
};

#define OPT_COUNT (sizeof(opt_desc)/sizeof(XrmOptionDescRec))

/*--- actions -------------------------------------------------------------*/

/* conf.c */
extern void create_confwin();
extern void conf_station_switched();
extern void conf_list_update();

void CloseMainAction(Widget, XEvent*, String*, Cardinal*);
void SetBgAction(Widget, XEvent*, String*, Cardinal*);
void SetShadowAction(Widget, XEvent*, String*, Cardinal*);
void ScanAction(Widget, XEvent*, String*, Cardinal*);
void ChannelAction(Widget, XEvent*, String*, Cardinal*);
void PointerAction(Widget, XEvent*, String*, Cardinal*);
void RemoteAction(Widget, XEvent*, String*, Cardinal*);
void ZapAction(Widget, XEvent*, String*, Cardinal*);
void StayOnTop(Widget, XEvent*, String*, Cardinal*);
void LaunchAction(Widget, XEvent*, String*, Cardinal*);
void PopupAction(Widget, XEvent*, String*, Cardinal*);
void CommandAction(Widget, XEvent*, String*, Cardinal*);

static XtActionsRec actionTable[] = {
    { "CloseMain",   CloseMainAction  },
    { "SetBg",       SetBgAction },
    { "SetShadow",   SetShadowAction },
    { "Scan",        ScanAction },
    { "Channel",     ChannelAction },
    { "Pointer",     PointerAction },
    { "Remote",      RemoteAction },
    { "Zap",         ZapAction },
    { "Complete",    CompleteAction },
    { "Help",        help_AC },
    { "StayOnTop",   StayOnTop },
    { "Launch",       LaunchAction },
    { "Popup",       PopupAction },
    { "Command",     CommandAction },
    { "Autoscroll",  offscreen_scroll_AC }
};

static struct STRTAB cap_list[] = {
    {  CAPTURE_OFF,         "off"         },
    {  CAPTURE_OVERLAY,     "overlay"     },
    {  CAPTURE_GRABDISPLAY, "grabdisplay" },
    {  -1, NULL,     },
};

static struct STRTAB avi_audio[] = {
    {  0, "no sound" },
    {  1, "mono"     },
    {  2, "stereo"   },
    { -1, NULL },
};

static struct STRTAB avi_format[] = {
    {  VIDEO_RGB15_LE, "15 bpp (rgb)" },
    {  VIDEO_BGR24,    "24 bpp (rgb)" },
    {  VIDEO_MJPEG,    "mjpeg"        },
    { -1, NULL },
};

static struct STRTAB avi_fps[] = {
    {  5, " 5 fps" },
    {  8, " 8 fps" },
    { 10, "10 fps" },
    { 12, "12 fps" },
    { 15, "15 fps" },
    { 18, "18 fps" },
    { 20, "20 fps" },
    { 24, "24 fps" },
    { 25, "25 fps" },
    { 30, "30 fps" },
    { -1, NULL },
};

/*--- exit ----------------------------------------------------------------*/

Boolean
ExitWP(XtPointer client_data)
{
    /* exit if the application is idle,
     * i.e. all the DestroyCallback's are called.
     */
    exit(0);
}

void
ExitCB(Widget widget, XtPointer client_data, XtPointer calldata)
{
    audio_off();
    video_overlay(NULL);
    video_close();
    if (have_mixer)
	mixer_close();
    if (fs)
	do_va_cmd(1,"fullscreen");
    XSync(dpy,False);
    if (grabber->grab_close)
	grabber->grab_close();
    XtAppAddWorkProc (app_context,ExitWP, NULL);
    XtDestroyWidget(app_shell);
}

static void
do_exit()
{
    ExitCB(NULL,NULL,NULL);    
}

void
CloseMainAction(Widget widget, XEvent *event,
		String *params, Cardinal *num_params)
{
#if 0
    static Dimension x,y,w,h;
#endif
    char *argv[32];
    int   argc = 0;

    if (event->type == ClientMessage) {
	if (event->xclient.data.l[0] == wm_protocols[1]) {
	    if (debug)
		fprintf(stderr,"Main: wm_save_yourself\n");

	    argv[argc++] = progname;
#if 0

	    /* position */
	    if (fs) {
		argv[argc++] = strdup("-f");
	    } else {
		XtVaGetValues(app_shell,
			      XtNx,&x,XtNy,&y,XtNwidth,&w,XtNheight,&h,
			      NULL);
		argv[argc++] = strdup("-geometry");
		sprintf(argv[argc++] = malloc(32),"%dx%d+%d+%d",w,h,x,y);
	    }
	    argv[argc++] = strdup("-c");
	    argv[argc++] = strdup(device);

	    /* grab filename */
	    if (snapbase) {
		argv[argc++] = strdup("-o");
		argv[argc++] = malloc(256);
		argv[argc-1][0] = '\0';
		if (snapbase[0] != '/') {
		    getcwd(argv[argc-1],128);
		    strcat(argv[argc-1],"/");
		}
		strcat(argv[argc-1],snapbase);
	    }
	    
	    /* options */
	    if (!dga_ext)
		argv[argc++] = strdup("--nodga");
	    if (!vm_ext)
		argv[argc++] = strdup("--novm");
	    if (!xvideo_ext)
		argv[argc++] = strdup("--noxv");
	    if (!pointer_on)
		argv[argc++] = strdup("-m");
	    if (bpp) {
		argv[argc++] = strdup("-b");
		sprintf(argv[argc++] = malloc(8),"%d",bpp);
	    }

	    /* channel */
	    argv[argc++] = channels[cur_sender]->name;
#endif

	    XSetCommand(XtDisplay(app_shell), XtWindow(app_shell), argv, argc);
	    return;
	}
    }
    ExitCB(widget,NULL,NULL);
}

void
PopupAction(Widget widget, XEvent *event,
	    String *params, Cardinal *num_params)
{
    Dimension h;
    int i,mh;

    /* which window we are talking about ? */
    if (*num_params > 0) {
	for (i = 0; i < TOPLEVELS; i++) {
	    if (0 == strcasecmp(my_toplevels[i].name,params[0]))
		break;
	}
    } else {
	for (i = 0; i < TOPLEVELS; i++) {
	    if (*(my_toplevels[i].shell) == widget)
		break;
	}
    }
    if (i == TOPLEVELS) {
	fprintf(stderr,"PopupAction: oops: shell widget not found (%s)\n",
		(*num_params > 0) ? params[0] : "-");
	return;
    }

    /* Message from WM ??? */
    if (NULL != event && event->type == ClientMessage) {
	if (debug)
	    fprintf(stderr,"%s: received %s message\n",
		    my_toplevels[i].name,
		    XGetAtomName(dpy,event->xclient.data.l[0]));
	if (event->xclient.data.l[0] == wm_protocols[1]) {
	    XSetCommand(XtDisplay(*(my_toplevels[i].shell)),
			XtWindow(*(my_toplevels[i].shell)), NULL, 0);
	    return;
	} else if (event->xclient.data.l[0] == wm_protocols[0]) {
	    /* fall throuth -- popdown window */
	} else {
	    /* whats this ?? */
	    return;
	}
    }

    /* check if window should be displayed */
    if (NULL != my_toplevels[i].check)
	if (0 == *(my_toplevels[i].check))
	    return;

    /* popup/down window */
    if (my_toplevels[i].mapped) {
	XtPopdown(*(my_toplevels[i].shell));
	my_toplevels[i].mapped = 0;
    } else {
	XtPopup(*(my_toplevels[i].shell), XtGrabNone);
	if (wm_stay_on_top && stay_on_top)
	    wm_stay_on_top(dpy,XtWindow(*(my_toplevels[i].shell)),1);
	my_toplevels[i].mapped = 1;
	if (!my_toplevels[i].first) {
	    XSetWMProtocols(XtDisplay(*(my_toplevels[i].shell)),
			    XtWindow(*(my_toplevels[i].shell)),
			    wm_protocols, 2);
	    mh = h = 0;
	    XtVaGetValues(*(my_toplevels[i].shell),
			  XtNmaxHeight,&mh,
			  XtNheight,&h,
			  NULL);
	    if (mh > 0 && h > mh) {
		if (debug)
		    fprintf(stderr,"height fixup: %d => %d\n",h,mh);
		XtVaSetValues(*(my_toplevels[i].shell),XtNheight,mh,NULL);
	    }
	    my_toplevels[i].first = 1;
	}
    }
}

void
CommandAction(Widget widget, XEvent *event,
	      String *params, Cardinal *num_params)
{
    do_command(*num_params,params);
}

void command_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct DO_CMD *cmd = clientdata;
    do_command(cmd->argc,cmd->argv);
}

void action_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct DO_AC *ca = clientdata;
    XtCallActionProc(widget,ca->name,NULL,ca->argv,ca->argc);
}

/*--- onscreen display (fullscreen) --------------------------------------*/

void create_onscreen()
{
    on_shell = XtVaCreateWidget("onscreen",transientShellWidgetClass,
				app_shell,
				XtNoverrideRedirect,True,
				XtNvisual,vinfo.visual,
				XtNcolormap,colormap,
				XtNdepth,vinfo.depth,
				NULL);    
    on_label = XtVaCreateManagedWidget("label", labelWidgetClass, on_shell,
					NULL);
}

void
popdown_onscreen(XtPointer client_data, XtIntervalId *id)
{
    XtPopdown(on_shell);
    on_timer = 0;
}

void
display_onscreen(char *title)
{
    static int first = 1;
    Dimension x,y;

    if (on_skip) {
	on_skip = 0;
	return;
    }
    if (!fs)
	return;
    if (!use_osd)
	return;

    XtVaGetValues(app_shell,XtNx,&x,XtNy,&y,NULL);
    XtVaSetValues(on_shell,XtNx,x+30,XtNy,y+20,NULL);
    XtVaSetValues(on_label,XtNlabel,title,NULL);
    XtPopup(on_shell, XtGrabNone);
    if (wm_stay_on_top && stay_on_top)
	wm_stay_on_top(dpy,XtWindow(on_shell),1);
    if (on_timer)
	XtRemoveTimeOut(on_timer);
    on_timer = XtAppAddTimeOut
	(app_context, ONSCREEN_TIME, popdown_onscreen,NULL);

    if (first) {
	first = 0;
	XDefineCursor(dpy, XtWindow(on_shell), no_ptr);
	XDefineCursor(dpy, XtWindow(on_label), no_ptr);
    }
}

/*--- tv -----------------------------------------------------------------*/

static XImage     *grab_ximage;
static void       *grab_ximage_shm;
static GC          grab_gc;
static int         grab_linelength;
static int         win_width, win_height;

#ifdef HAVE_LIBXV
static XvImage     *xv_image = NULL;
static void        *xv_shm = NULL;
static int         xv_width = 320, xv_height = 240;
#endif

void
freeze_image()
{
    if (NULL == grabber->grab_capture)
	return;
    if (NULL == grab_ximage)
	return;
    if (cur_capture != CAPTURE_GRABDISPLAY)
	if (NULL == grabber_capture(grab_ximage->data,0,1,NULL))
	    return;
    
    if (!tv_pix) {
	tv_pix = XCreatePixmap(dpy, RootWindowOfScreen(XtScreen(tv)),
			       win_width, win_height,
			       DefaultDepthOfScreen(XtScreen(tv)));
    }
    XPUTIMAGE(dpy, tv_pix, grab_gc, grab_ximage, 0,0,
	      (win_width-grab_width) >> 1, (win_height-grab_height) >> 1,
	      grab_width, grab_height);
    XtVaSetValues(tv,XtNbackgroundPixmap,tv_pix,NULL);
}

static Boolean
grabdisplay_idle(XtPointer data)
{
    static long count,lastsec;
    struct timeval  t;
    struct timezone tz;

    if (NULL == grabber->grab_capture)
	goto oops;

#ifdef HAVE_LIBXV
    if (have_xv_scale) {
	if (NULL == xv_image)
	    goto oops;
	if (NULL == grabber_capture(xv_image->data,0,0,NULL))
	    goto oops;
	XvShmPutImage(dpy, im_port, XtWindow(tv), grab_gc, xv_image,
		      0, 0,  xv_width, xv_height,
		      0, 0,  win_width, win_height,
		      False);
	
    } else {
#endif
	if (!grab_ximage)
	    goto oops;
	if (NULL == grabber_capture(grab_ximage->data,0,0,NULL))
	    goto oops;
	XPUTIMAGE(dpy, XtWindow(tv), grab_gc, grab_ximage, 0,0,
		  (win_width-grab_width) >> 1, (win_height-grab_height) >> 1,
		  grab_width, grab_height);
#ifdef HAVE_LIBXV
    }
#endif

    if (debug) {
	gettimeofday(&t,&tz);
	if (t.tv_sec != lastsec) {
	    if (lastsec == t.tv_sec-1)
		fprintf(stderr,"%5ld fps \r", count);
	    lastsec = t.tv_sec;
	    count = 0;
	}
	count++;
    }
    return FALSE;

 oops:
    idle_id = 0;
    return TRUE;
}

static void
grabdisplay_reconfigure()
{
#ifdef HAVE_LIBXV
    if (have_xv_scale)
	grabber_setparams(VIDEO_YUV422,&xv_width,&xv_height,
			  &grab_linelength,1);
    else
#endif
	grabber_setparams(x11_pixmap_format,&grab_width,&grab_height,
			  &grab_linelength,1);
}

static void
grabdisplay_setsize(int width, int height)
{
    if (!XtWindow(tv))
	return;

    win_width       = width;
    win_height      = height;
    grab_width      = width & ~3; /* alignment */
    grab_height     = height;
    grab_linelength = 0;

    /* check what the driver can do ... */
    if (!grabber->grab_setparams)
	return;

    if (!grab_gc)
	grab_gc = XCreateGC(dpy,XtWindow(tv),0,NULL);

    /* free old stuff */
    if (grab_ximage) {
        x11_destroy_ximage(dpy,grab_ximage,grab_ximage_shm);
        grab_ximage = NULL;
    }
    if (tv_pix) {
	XtVaSetValues(tv,XtNbackgroundPixmap,XtUnspecifiedPixmap,NULL);
	XFreePixmap(dpy,tv_pix);
	tv_pix = 0;
    }

#ifdef HAVE_LIBXV
    if (xv_image) {
	xv_destroy_ximage(dpy,xv_image,xv_shm);
	xv_image = NULL;
    }
    if (have_xv_scale) {
	/* FIXME: no hard coded max size, better ask the X-Server */
	xv_width  = (width  > 320) ? 320 : width;
	xv_height = (height > 240) ? 240 : height;
	grabber_setparams(VIDEO_YUV422,&xv_width,&xv_height,
			  &grab_linelength,1);
	xv_image = xv_create_ximage(dpy, xv_width, xv_height, &xv_shm);
    } else {
#endif
	grabber_setparams(x11_pixmap_format,&grab_width,&grab_height,
			  &grab_linelength,1);
	grab_ximage = x11_create_ximage(dpy,&vinfo,grab_width,grab_height,
					&grab_ximage_shm);
	if (NULL == grab_ximage) {
	    fprintf(stderr,"oops: out of memory\n");
	    exit(1);
	}
#ifdef HAVE_LIBXV
    }
#endif
}

static void
resize_event(Widget widget, XtPointer client_data, XEvent *event, Boolean *d)
{
    static int width = 0, height = 0;
    char label[64];
    
    switch(event->type) {
    case ConfigureNotify:
	if (width  != event->xconfigure.width ||
	    height != event->xconfigure.height) {
	    width  = event->xconfigure.width;
	    height = event->xconfigure.height;
	    grabdisplay_setsize(width, height);
	    XClearWindow(XtDisplay(tv),XtWindow(tv));
	    sprintf(label,"%-" LABEL_WIDTH "s: %dx%d","AVI Size",width,height);
	    if (avi_size)
		XtVaSetValues(avi_size,XtNlabel,label,NULL);
	}
	break;
    }
}

/*------------------------------------------------------------------------*/

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

/* the RightWay[tm] to set float resources (copyed from Xaw specs) */
void set_float(Widget widget, char *name, fp_res value)
{
    Arg   args[1];

    if (sizeof(fp_res) > sizeof(XtArgVal)) {
	/*
	 * If a float is larger than an XtArgVal then pass this 
	 * resource value by reference.
	 */
	XtSetArg(args[0], name, &value);
    } else {
        /*
	 * Convince C not to perform an automatic conversion, which
	 * would truncate 0.5 to 0. 
	 */
	XtArgVal * l_top = (XtArgVal *) &value;
	XtSetArg(args[0], name, *l_top);
    }
    XtSetValues(widget,args,1);
}

void
title_timeout(XtPointer client_data, XtIntervalId *id)
{
    keypad_timeout();
    XtVaSetValues(app_shell,XtNtitle,default_title,NULL);
    title_timer = 0;
}

void
new_title(char *txt)
{
    strcpy(default_title,txt);
    XtVaSetValues(app_shell,XtNtitle,default_title,NULL);
    display_onscreen(default_title);

    if (title_timer) {
	XtRemoveTimeOut(title_timer);
	title_timer = 0;
    }
}

static void
new_message(char *txt)
{
    XtVaSetValues(app_shell,XtNtitle,txt,NULL);
    display_onscreen(txt);
    if (title_timer)
	XtRemoveTimeOut(title_timer);
    title_timer = XtAppAddTimeOut
	(app_context, TITLE_TIME, title_timeout,NULL);
}

static void
new_norm()
{
    char label[64];

    if (c_norm) {
	sprintf(label,"%-" LABEL_WIDTH "s: %s","TV Norm",
		grabber->norms[cur_norm].str);
	XtVaSetValues(c_norm,XtNlabel,label,NULL);
    }
    if (win_width > 0)
	grabdisplay_setsize(win_width,win_height);
    video_new_size();
}

static void
new_input()
{
    char label[64];

    if (c_input) {
	sprintf(label,"%-" LABEL_WIDTH "s: %s","Video Source",
		grabber->inputs[cur_input].str);
	XtVaSetValues(c_input,XtNlabel,label,NULL);
    }
}

static void
new_freqtab()
{
    char label[64];

    if (c_freq) {
	sprintf(label,"%-" LABEL_WIDTH "s: %s","Frequency table",
		chanlists[chantab].name);
	XtVaSetValues(c_freq,XtNlabel,label,NULL);
    }
}

static void
new_attr(int id)
{
    switch (id) {
    case GRAB_ATTR_VOLUME:
	if (s_volume)
	    set_float(s_volume,XtNtopOfThumb,(fp_res)cur_volume/65536);
	break;
    case GRAB_ATTR_COLOR:
	if (s_color)
	    set_float(s_color,XtNtopOfThumb,(fp_res)cur_color/65536);
	break;
    case GRAB_ATTR_BRIGHT:
	if (s_bright)
	    set_float(s_bright,XtNtopOfThumb,(fp_res)cur_bright/65536);
	break;
    case GRAB_ATTR_HUE:
	if (s_hue)
	    set_float(s_hue,XtNtopOfThumb,(fp_res)cur_hue/65536);
	break;
    case GRAB_ATTR_CONTRAST:
	if (s_contrast)
	    set_float(s_contrast,XtNtopOfThumb,(fp_res)cur_contrast/65536);
	break;
    default:
	/* nothing */
	break;
    }
}

static void
new_channel()
{
    set_property(cur_freq,
		 (cur_channel == -1) ? NULL : chanlist[cur_channel].name,
		 (cur_sender == -1)  ? NULL : channels[cur_sender]->name);
    conf_station_switched();
    
    if (zap_timer) {
	XtRemoveTimeOut(zap_timer);
	zap_timer = 0;
    }
    if (scan_timer) {
	XtRemoveTimeOut(scan_timer);
	scan_timer = 0;
    }
    if (audio_timer) {
	XtRemoveTimeOut(audio_timer);
	audio_timer = 0;
    }
    audio_timer = XtAppAddTimeOut(app_context, 10000, watch_audio, NULL);
}

/*------------------------------------------------------------------------*/

/*
 * mode = -1: check mode (just update the title)
 * mode =  0: set autodetect (and read back result)
 * mode >  0: set some mode
 */
void
change_audio(int mode)
{
    char label[64];

    char mname[10];
    if (!grabber->grab_hasattr(GRAB_ATTR_MODE))
	return;

    if (-1 != mode) {
	/* set */
	grabber->grab_setattr(GRAB_ATTR_MODE,mode);
    }
    if (-1 == mode || 0 == mode) {
	/* read */
	mode = grabber->grab_getattr(GRAB_ATTR_MODE);
    }

    if (mode & 2)
	strcpy(mname,"stereo");
    else if (mode & 4)
	strcpy(mname,"lang1");
    else if (mode & 8)
	strcpy(mname,"lang2");
    else if (mode & 1)
	strcpy(mname,"mono");
    else
	strcpy(mname,"???");

    sprintf(label,"%-" LABEL_WIDTH "s: %s","Audio", mname);
    if (c_audio)
	XtVaSetValues(c_audio,XtNlabel,label,NULL);

    sprintf(label,"%s (%s)",default_title,mname);
    XtVaSetValues(app_shell,XtNtitle,label,NULL);
    /* new_title(label); */
}

void
watch_audio(XtPointer data, XtIntervalId *id)
{
    on_skip=1;
    change_audio(-1);
    audio_timer = 0;
}

void
do_capture(int from, int to)
{
    static int niced = 0;
    char label[64];

    /* off */
    switch (from) {
    case CAPTURE_OFF:
	XtVaSetValues(tv,XtNbackgroundPixmap,XtUnspecifiedPixmap,NULL);
	if (tv_pix)
	    XFreePixmap(dpy,tv_pix);
	tv_pix = 0;
	break;
    case CAPTURE_GRABDISPLAY:
	if (idle_id) {
	    XtRemoveWorkProc(idle_id);
	    if (grabber->grab_cleanup)
		grabber->grab_cleanup();
	}
	idle_id = 0;
	XClearArea(XtDisplay(tv), XtWindow(tv), 0,0,0,0, True);
	break;
    case CAPTURE_OVERLAY:
	video_overlay(NULL);
	break;
    }

    /* on */
    switch (to) {
    case CAPTURE_OFF:
	sprintf(label,"%-" LABEL_WIDTH "s: %s","Capture","off");
	freeze_image();
	break;
    case CAPTURE_GRABDISPLAY:
	sprintf(label,"%-" LABEL_WIDTH "s: %s","Capture","grabdisplay");
	if (!niced)
	    nice(niced = 10);
	idle_id = XtAppAddWorkProc(app_context, grabdisplay_idle, NULL);
	break;
    case CAPTURE_OVERLAY:
	sprintf(label,"%-" LABEL_WIDTH "s: %s","Capture","overlay");
	video_overlay(grabber->grab_overlay);
	break;
    }
    if (c_cap)
	XtVaSetValues(c_cap,XtNlabel,label,NULL);
}

/* gets called before switching away from a channel */
void
pixit()
{
    Pixmap pix;
    char *data;
    int linelength = 0;

    if (cur_sender == -1)
	return;

    /* save picture settings */
    channels[cur_sender]->color    = cur_color;
    channels[cur_sender]->bright   = cur_bright;
    channels[cur_sender]->hue      = cur_hue;
    channels[cur_sender]->contrast = cur_contrast;

    if (0 == pix_width || 0 == pix_height)
	return;

    /* capture mini picture */
    if (!grabber->grab_capture)
	return;
    if (!grabber->grab_setparams)
	return;

    if (0 == grabber_setparams(x11_pixmap_format,&pix_width,&pix_height,
			       &linelength,1) &&
	NULL != (data = grabber_capture(NULL,0,1,NULL)) &&
	0 != (pix = x11_create_pixmap(dpy,&vinfo,colormap,data,
				      pix_width,pix_height,
				      channels[cur_sender]->name))) {
	XtVaSetValues(channels[cur_sender]->button,
		      XtNbackgroundPixmap,pix,
		      XtNlabel,"",
		      XtNwidth,pix_width,
		      XtNheight,pix_height,
		      NULL);
	if (channels[cur_sender]->pixmap)
	    XFreePixmap(dpy,channels[cur_sender]->pixmap);
	channels[cur_sender]->pixmap = pix;
    }

    /* set parameters to main window size */
    grabdisplay_reconfigure();
}

void
SetBgAction(Widget widget, XEvent *event,
	    String *params, Cardinal *num_params)
{
    XColor have,exact;

    if (*num_params != 1)
	fprintf(stderr,"SetBg: usage: SetRes(color)\n");
    XAllocNamedColor(dpy,colormap,
		     params[0],&have,&exact);
    XtVaSetValues(widget,XtNbackground,have.pixel,NULL);
}

void
SetShadowAction(Widget widget, XEvent *event,
		String *params, Cardinal *num_params)
{
    Dimension n;

    if (*num_params != 1)
	fprintf(stderr,"SetShadow: usage: SetShadow(int)\n");
    n = atoi(params[0]);
    XtVaSetValues(widget,"shadowWidth",n,NULL);
}

static void
set_menu_val(Widget widget, char *name, struct STRTAB *tab, int val)
{
    char label[64];
    int i;

    for (i = 0; tab[i].str != NULL; i++) {
	if (tab[i].nr == val)
	    break;
    }
    sprintf(label,"%-15s : %s",name,
	    (tab[i].str != NULL) ? tab[i].str : "invalid");
    XtVaSetValues(widget,XtNlabel,label,NULL);
}

void
RemoteAction(Widget widget, XEvent * event,
	     String * params, Cardinal * num_params)
{
    Atom            type;
    int             format, argc, i;
    char            *argv[32];
    unsigned long   nitems, bytesafter;
    unsigned char   *args = NULL;

    if (event->type == PropertyNotify) {
	if (debug > 1)
	    fprintf(stderr,"PropertyNotify %s\n",
		    XGetAtomName(dpy,event->xproperty.atom));
	if (event->xproperty.atom == xawtv_remote &&
	    Success == XGetWindowProperty(dpy,
					  event->xproperty.window,
					  event->xproperty.atom,
					  0, (65536 / sizeof(long)),
					  True, XA_STRING,
					  &type, &format, &nitems, &bytesafter,
					  &args) &&
	    nitems != 0) {
	    if (debug)
		fprintf(stderr, "remote control: ");
	    for (i = 0, argc = 0; i < nitems; i += strlen(args + i) + 1) {
		if(debug)
		    fprintf(stderr, "%s ", args+i);
		argv[argc++] = args+i;
	    }
	    if (debug)
		fprintf(stderr, "\n");
	    argv[argc] = NULL;

	    do_command(argc,argv);
	    XFree(args);
	}
    }
}

void
scan_timeout(XtPointer client_data, XtIntervalId *id)
{
    scan_timer = 0;
    
    /* check */
    if (!grabber->grab_tuned)
	return;
    if (grabber->grab_tuned())
	return;

    do_va_cmd(2,"setchannel","next");
    scan_timer = XtAppAddTimeOut
	(app_context, SCAN_TIME, scan_timeout, NULL);
}

void
ScanAction(Widget widget, XEvent *event,
	   String *params, Cardinal *num_params)
{
    pixit();
    do_va_cmd(2,"setchannel","next");
    scan_timer = XtAppAddTimeOut
	(app_context, SCAN_TIME, scan_timeout,NULL);
}

void
ChannelAction(Widget widget, XEvent *event,
	      String *params, Cardinal *num_params)
{
    int i;

    if (0 == count)
	return;
    i = popup_menu(widget,"Stations",cmenu);

    if (i != -1)
	do_va_cmd(2,"setstation",channels[i]->name);
}

void
PointerAction(Widget widget, XEvent *event,
	      String *params, Cardinal *num_params)
{
    pointer_on = !pointer_on;
    XDefineCursor(dpy, XtWindow(tv), pointer_on ? left_ptr : no_ptr);
}

Boolean
MyResize(XtPointer client_data)
{
    /* needed program-triggered resizes (fullscreen mode) */
    video_new_size();
    return TRUE;
}

#ifdef HAVE_LIBXXF86VM
static void
vidmode_timer(XtPointer clientData, XtIntervalId *id)
{
    do_va_cmd(2,"capture", "on");
}

static void
set_vidmode(int nr)
{
    if (CAPTURE_OVERLAY == cur_capture) {
	do_va_cmd(2,"capture", "off");
	XtAppAddTimeOut(app_context,VIDMODE_DELAY,vidmode_timer,NULL);
    }
    /* usleep(VIDMODE_DELAY*1000); */
    if (debug)
	fprintf(stderr,"switching mode: %d  %d %d %d %d  %d %d %d %d  %d\n",
		vm_modelines[nr]->dotclock,
		vm_modelines[nr]->hdisplay,
		vm_modelines[nr]->hsyncstart,
		vm_modelines[nr]->hsyncend,
		vm_modelines[nr]->htotal,
		vm_modelines[nr]->vdisplay,
		vm_modelines[nr]->vsyncstart,
		vm_modelines[nr]->vsyncend,
		vm_modelines[nr]->vtotal,
		vm_modelines[nr]->flags);
    XF86VidModeSwitchToMode(dpy,XDefaultScreen(dpy),vm_modelines[nr]);
}
#endif

static void
do_fullscreen()
{
    static Dimension x,y,w,h;
    static int timeout,interval,prefer_blanking,allow_exposures,rpx,rpy,mouse;
#ifdef HAVE_LIBXXF86VM
    static int vm_switched;
#endif
#ifdef HAVE_LIBXDPMS
    static BOOL dpms_on;
    CARD16 dpms_state;
    int dpms_dummy;
#endif

    Window root,child;
    int    wpx,wpy,mask;

    if (fs) {
	fprintf(stderr,"turning fs off (%dx%d+%d+%d)\n",w,h,x,y);
#ifdef HAVE_LIBXXF86VM
	if (have_vm && vm_switched) {
	    set_vidmode(0);
	    vm_switched = 0;
	}
#endif

	if (on_timer) {
	    XtPopdown(on_shell);
	    XtRemoveTimeOut(on_timer);
	    on_timer = 0;
	}
	    
	XtVaSetValues(app_shell,
		      XtNwidthInc, WIDTH_INC,
		      XtNheightInc,HEIGHT_INC,
		      XtNx,        x + fs_xoff,
		      XtNy,        y + fs_yoff,
		      XtNwidth,    w,
		      XtNheight,   h,
		      NULL);

	XSetScreenSaver(dpy,timeout,interval,prefer_blanking,allow_exposures);
#ifdef HAVE_LIBXDPMS
	if ((DPMSQueryExtension(dpy, &dpms_dummy, &dpms_dummy)) && 
	    (DPMSCapable(dpy)) && (dpms_on)) {
		DPMSEnable(dpy);
	}
#endif

	XWarpPointer(dpy, None, RootWindowOfScreen(XtScreen(tv)),
		     0, 0, 0, 0, rpx, rpy);
	fs = 0;
    } else {
	int vp_x, vp_y, vp_width, vp_height;

	fprintf(stderr,"turning fs on\n");
	vp_x = 0;
	vp_y = 0;
	vp_width  = swidth;
	vp_height = sheight;
	XQueryPointer(dpy, RootWindowOfScreen(XtScreen(tv)),
		      &root, &child, &rpx, &rpy, &wpx, &wpy, &mask);

#ifdef HAVE_LIBXXF86VM
	if (have_vm) {
	    int i;
	    XF86VidModeGetAllModeLines(dpy,XDefaultScreen(dpy),
				       &vm_count,&vm_modelines);
	    for (i = 0; i < vm_count; i++)
		if (fs_width  == vm_modelines[i]->hdisplay &&
		    fs_height == vm_modelines[i]->vdisplay)
		    break;
	    if (i != 0 && i != vm_count) {
		set_vidmode(i);
		vm_switched = 1;
		vp_width = vm_modelines[i]->hdisplay;
		vp_height = vm_modelines[i]->vdisplay;
	    } else {
		vm_switched = 0;
		vp_width = vm_modelines[0]->hdisplay;
		vp_height = vm_modelines[0]->vdisplay;
	    }
#if 0
	    XF86VidModeGetViewPort(dpy,XDefaultScreen(dpy),&vp_x,&vp_y);
#else
	    XWarpPointer(dpy, None, RootWindowOfScreen(XtScreen(tv)),
			 0, 0, 0, 0, vp_width/2, vp_height/2);
	    XF86VidModeSetViewPort(dpy,XDefaultScreen(dpy),0,0);
#endif
	    if (debug)
		fprintf(stderr,"viewport: %dx%d+%d+%d\n",
			vp_width,vp_height,vp_x,vp_y);
	}
#endif
	XtVaGetValues(app_shell,
		      XtNx,          &x,
		      XtNy,          &y,
		      XtNwidth,      &w,
		      XtNheight,     &h,
		      NULL);

	XtVaSetValues(app_shell,
		      XtNwidthInc,   1,
		      XtNheightInc,  1,
		      NULL);
	XtVaSetValues(app_shell,
		      XtNx,          (vp_x & 0xfffc) + fs_xoff,
		      XtNy,          vp_y            + fs_yoff,
		      XtNwidth,      vp_width,
		      XtNheight,     vp_height,
		      NULL);

        XRaiseWindow(dpy, XtWindow(app_shell));

	XGetScreenSaver(dpy,&timeout,&interval,
			&prefer_blanking,&allow_exposures);
	XSetScreenSaver(dpy,0,0,DefaultBlanking,DefaultExposures);
#ifdef HAVE_LIBXDPMS
	if ((DPMSQueryExtension(dpy, &dpms_dummy, &dpms_dummy)) && 
	    (DPMSCapable(dpy))) {
	    DPMSInfo(dpy, &dpms_state, &dpms_on);
            DPMSDisable(dpy); 
	}
#endif

	XWarpPointer(dpy, None, XtWindow(tv), 0, 0, 0, 0, 30, 15);
	mouse = pointer_on;
	fs = 1;
    }
    if (mouse)
	XtCallActionProc(tv,"Pointer",NULL,NULL,0);
    XtAppAddWorkProc (app_context,MyResize, NULL);
}

void button_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct CHANNEL *channel = clientdata;
    do_va_cmd(2,"setstation",channel->name);
}

void create_chanwin()
{
    chan_shell = XtVaAppCreateShell("Channels", "Xawtv",
				    topLevelShellWidgetClass,
				    dpy,
				    XtNclientLeader,app_shell,
				    XtNvisual,vinfo.visual,
				    XtNcolormap,colormap,
				    XtNdepth,vinfo.depth,
		      XtNmaxHeight,XtScreen(app_shell)->height-50,
				    NULL);
    XtOverrideTranslations(chan_shell, XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: Popup()"));
    chan_viewport = XtVaCreateManagedWidget("viewport",
					    viewportWidgetClass, chan_shell,
					    XtNallowHoriz, False,
					    XtNallowVert, True,
					    NULL);
    chan_box = XtVaCreateManagedWidget("channelbox",
				       boxWidgetClass, chan_viewport,
				       XtNsensitive, True,
				       NULL);
}

void
channel_menu()
{
    int  i,max,len;
    char str[100];

    if (cmenu)
	free(cmenu);
    cmenu = malloc((count+1)*sizeof(struct STRTAB));
    memset(cmenu,0,(count+1)*sizeof(struct STRTAB));
    for (i = 0, max = 0; i < count; i++) {
	len = strlen(channels[i]->name);
	if (max < len)
	    max = len;
    }
    for (i = 0; i < count; i++) {
	cmenu[i].nr      = i;
	cmenu[i].str     = channels[i]->name;
	if (channels[i]->key) {
	    sprintf(str,"%2d  %-*s  %s",i+1,
		    max+2,channels[i]->name,channels[i]->key);
	} else {
	    sprintf(str,"%2d  %-*s",i+1,max+2,channels[i]->name);
	}
	cmenu[i].str=strdup(str);
    }
    conf_list_update();
    calc_frequencies();
}

void
zap_timeout(XtPointer client_data, XtIntervalId *id)
{
    static int muted = 0;

    if (zap_fast && !cur_mute) {
	/* mute for fast channel scan */
	muted = 1;
	do_va_cmd(2,"volume","mute","on");
    }
    /* pixit(); */
    do_va_cmd(2,"setstation","next");
    if (cur_sender != zap_start) {
	zap_timer = XtAppAddTimeOut
	    (app_context, zap_fast ? CAP_TIME : ZAP_TIME, zap_timeout,NULL);
    } else {
	if(muted) {
	    /* unmute */
	    muted = 0;
	    do_va_cmd(2,"volume","mute","off");
	}
    }
}

void
ZapAction(Widget widget, XEvent *event,
	  String *params, Cardinal *num_params)
{
    if (zap_timer) {
	XtRemoveTimeOut(zap_timer);
	zap_timer = 0;
#if 0
	strcpy(title,"channel hopping off");
	set_timer_title();
#endif
    } else {
	zap_start = (cur_sender == -1) ? 0 : cur_sender;
	zap_fast = 0;
	if (*num_params > 0) {
	    if (0 == strcasecmp(params[0],"fast"))
		zap_fast = 1;
	}
	if (count)
	    zap_timer = XtAppAddTimeOut
		(app_context, CAP_TIME, zap_timeout,NULL);
    }
}

void
StayOnTop(Widget widget, XEvent *event,
	  String *params, Cardinal *num_params)
{
    int i;

    if (!wm_stay_on_top)
	return;

    stay_on_top = 1-stay_on_top;

    wm_stay_on_top(dpy,XtWindow(app_shell),stay_on_top);
    wm_stay_on_top(dpy,XtWindow(on_shell),stay_on_top);
    for (i = 0; i < TOPLEVELS; i++)
	wm_stay_on_top(dpy,XtWindow(*(my_toplevels[i].shell)),stay_on_top);
}

/*--- option window ------------------------------------------------------*/

#define PANED_FIX               \
        XtNallowResize, False,  \
        XtNshowGrip,    False,  \
        XtNskipAdjust,  True

struct DO_CMD cmd_fs   = { 1, { "fullscreen",        NULL }};
struct DO_CMD cmd_mute = { 2, { "volume",  "mute",   NULL }};
struct DO_CMD cmd_cap  = { 2, { "capture", "toggle", NULL }};
struct DO_CMD cmd_jpeg = { 2, { "snap",    "jpeg",   NULL }};
struct DO_CMD cmd_ppm  = { 2, { "snap",    "ppm",    NULL }};

struct DO_AC  ac_ptr   = { 0, "Pointer",    { NULL }};
struct DO_AC  ac_fs    = { 0, "FullScreen", { NULL }};
struct DO_AC  ac_top   = { 0, "StayOnTop",  { NULL }};

struct DO_AC  ac_avi   = { 1, "Popup",      { "streamer", NULL }};
struct DO_AC  ac_chan  = { 1, "Popup",      { "channels", NULL }};
struct DO_AC  ac_conf  = { 1, "Popup",      { "config",   NULL }};
struct DO_AC  ac_launch = { 1, "Popup",      { "launcher",  NULL }};
struct DO_AC  ac_zap   = { 0, "Zap",        { NULL }};

void menu_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    long  cd = (long)clientdata;
    int   j;

    switch (cd) {
    case 10:
	if (-1 != (j=popup_menu(widget,"TV Norm",grabber->norms)))
	    do_va_cmd(2,"setnorm",grabber->norms[j].str);
	break;
    case 11:
	if (-1 != (j=popup_menu(widget,"Video Source",grabber->inputs)))
	    do_va_cmd(2,"setinput",grabber->inputs[j].str);
	break;
    case 12:
	if (-1 != (j=popup_menu(widget,"Freq table",chanlist_names)))
	    do_va_cmd(2,"setfreqtab",chanlist_names[j].str);
	break;
    case 13:
	if (grabber->audio_modes) {
	    int i,mode = grabber->grab_getattr(GRAB_ATTR_MODE);
	    for (i = 1; grabber->audio_modes[i].str != NULL; i++) {
		grabber->audio_modes[i].nr =
		    (1 << (i-1)) & mode ? (1 << (i-1)) : -1;
	    }
	    if (-1 != (j=popup_menu(widget,"Audio",grabber->audio_modes)))
		change_audio(grabber->audio_modes[j].nr);
	}
	break;
    case 14:
	if (-1 != (j=popup_menu(widget,"Capture",cap_list)))
	    do_va_cmd(2,"capture",cap_list[j].str);
	break;
	
    case 20:
	if (-1 != (j=popup_menu(widget,"AVI Audio",avi_audio))) {
	    cur_avi_audio = avi_audio[j].nr;
	    set_menu_val(widget,"AVI Audio",avi_audio,cur_avi_audio);
	}
	break;
    case 21:
	if (-1 != (j=popup_menu(widget,"AVI Format",avi_format))) {
	    cur_avi_format = avi_format[j].nr;
	    set_menu_val(widget,"AVI Format",avi_format,cur_avi_format);
	}
	break;
    case 22:
	if (-1 != (j=popup_menu(widget,"AVI Framerate",avi_fps))) {
	    cur_avi_fps = avi_fps[j].nr;
	    set_menu_val(widget,"AVI Framerate",avi_fps,cur_avi_fps);
	}
	break;
	
    default:
    }
}

void jump_scb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char *name,val[16];
    int  value;

    name  = XtName(XtParent(widget));
    value = (int)(*(fp_res*)call_data * 65535);
    if (value > 65535) value = 65535;
    if (value < 0)     value = 0;
#if 0
    fprintf(stderr,"jump to %f (%s/%d)\n",*(fp_res*)call_data,name,value);
#endif
    sprintf(val,"%d",value);
    do_va_cmd(2,name,val);
}

void scroll_scb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    long      move = (long)call_data;
    Dimension length;
    fp_res    shown,top1,top2;

    XtVaGetValues(widget,
		  XtNlength,     &length,
		  XtNshown,      &shown,
		  XtNtopOfThumb, &top1,
		  NULL);

    top2 = top1 + (fp_res)move/length/5;
    if (top2 < 0) top2 = 0;
    if (top2 > 1) top2 = 1;
#if 0
    fprintf(stderr,"scroll by %d\tlength %d\tshown %f\ttop %f => %f\n",
	    move,length,shown,top1,top2);
#endif
    jump_scb(widget,clientdata,&top2);
}

void create_optwin()
{
    Widget c, p,l;

    opt_shell = XtVaAppCreateShell("Options", "Xawtv",
				   topLevelShellWidgetClass,
				   dpy,
				   XtNclientLeader,app_shell,
				   XtNvisual,vinfo.visual,
				   XtNcolormap,colormap,
				   XtNdepth,vinfo.depth,
				   NULL);
    XtOverrideTranslations(opt_shell, XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: Popup()"));
    opt_paned = XtVaCreateManagedWidget("paned", panedWidgetClass, opt_shell,
					NULL);
    
    c = XtVaCreateManagedWidget("mute", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,command_cb,(XtPointer)&cmd_mute);
    
    c = XtVaCreateManagedWidget("ptr", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_ptr);

    c = XtVaCreateManagedWidget("fs", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,command_cb,(XtPointer)&cmd_fs);

    c = XtVaCreateManagedWidget("grabppm", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,command_cb,(XtPointer)&cmd_ppm);
#ifdef HAVE_LIBJPEG
    c = XtVaCreateManagedWidget("grabjpeg", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,command_cb,(XtPointer)&cmd_jpeg);
#endif
    c = XtVaCreateManagedWidget("recavi", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_avi);
    c = XtVaCreateManagedWidget("chanwin", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_chan);
    c = XtVaCreateManagedWidget("confwin", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_conf);
    c = XtVaCreateManagedWidget("launchwin", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_launch);
    c = XtVaCreateManagedWidget("zap", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_zap);
    if (wm_stay_on_top) {
	c = XtVaCreateManagedWidget("top", commandWidgetClass, opt_paned,
				    PANED_FIX, NULL);
	XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_top);
    }
    
    
    c_norm = XtVaCreateManagedWidget("norm", commandWidgetClass, opt_paned,
				     PANED_FIX, NULL);
    XtAddCallback(c_norm,XtNcallback,menu_cb,(XtPointer)10);
    
    c_input = XtVaCreateManagedWidget("input", commandWidgetClass, opt_paned,
				      PANED_FIX, NULL);
    XtAddCallback(c_input,XtNcallback,menu_cb,(XtPointer)11);

    if (grabber->grab_tune) {
	c_freq = XtVaCreateManagedWidget("freq", commandWidgetClass, opt_paned,
					 PANED_FIX, NULL);
	XtAddCallback(c_freq,XtNcallback,menu_cb,(XtPointer)12);
    }

    if (grabber->grab_hasattr(GRAB_ATTR_MODE)) {
	c_audio = XtVaCreateManagedWidget("audio",commandWidgetClass,opt_paned,
					  PANED_FIX, NULL);
	XtAddCallback(c_audio,XtNcallback,menu_cb,(XtPointer)13);
    }
    c_cap = XtVaCreateManagedWidget("cap", commandWidgetClass, opt_paned,
				    PANED_FIX, NULL);
    XtAddCallback(c_cap,XtNcallback,menu_cb,(XtPointer)14);
    
    if (grabber->grab_hasattr(GRAB_ATTR_BRIGHT)) {
	p = XtVaCreateManagedWidget("bright", panedWidgetClass, opt_paned,
				    XtNorientation, XtEvertical,
				    PANED_FIX, NULL);
	l = XtVaCreateManagedWidget("l", labelWidgetClass, p,
				    XtNshowGrip, False,
				    NULL);
	s_bright = XtVaCreateManagedWidget("s", scrollbarWidgetClass, p,
					   PANED_FIX, NULL);
	XtAddCallback(s_bright,XtNjumpProc,  jump_scb,  NULL);
	XtAddCallback(s_bright,XtNscrollProc,scroll_scb,NULL);
    }
    
    if (grabber->grab_hasattr(GRAB_ATTR_HUE)) {
	p = XtVaCreateManagedWidget("hue", panedWidgetClass, opt_paned,
				    XtNorientation, XtEvertical,
				    PANED_FIX, NULL);
	l = XtVaCreateManagedWidget("l", labelWidgetClass, p,
				    XtNshowGrip, False,
				    NULL);
	s_hue = XtVaCreateManagedWidget("s", scrollbarWidgetClass, p,
					PANED_FIX, NULL);
	XtAddCallback(s_hue,XtNjumpProc,  jump_scb,  NULL);
	XtAddCallback(s_hue,XtNscrollProc,scroll_scb,NULL);
    }
    
    if (grabber->grab_hasattr(GRAB_ATTR_CONTRAST)) {
	p = XtVaCreateManagedWidget("contrast", panedWidgetClass, opt_paned,
				    XtNorientation, XtEvertical,
				    PANED_FIX, NULL);
	l = XtVaCreateManagedWidget("l", labelWidgetClass, p,
				    XtNshowGrip, False,
				    NULL);
	s_contrast = XtVaCreateManagedWidget("s", scrollbarWidgetClass, p,
					     PANED_FIX, NULL);
	XtAddCallback(s_contrast,XtNjumpProc,  jump_scb,  NULL);
	XtAddCallback(s_contrast,XtNscrollProc,scroll_scb,NULL);
    }
    
    if (grabber->grab_hasattr(GRAB_ATTR_COLOR)) {
	p = XtVaCreateManagedWidget("color", panedWidgetClass, opt_paned,
				    XtNorientation, XtEvertical,
				    PANED_FIX, NULL);
	l = XtVaCreateManagedWidget("l", labelWidgetClass, p,
				    XtNshowGrip, False,
				    NULL);
	s_color = XtVaCreateManagedWidget("s", scrollbarWidgetClass, p,
					  PANED_FIX, NULL);
	XtAddCallback(s_color,XtNjumpProc,  jump_scb,  NULL);
	XtAddCallback(s_color,XtNscrollProc,scroll_scb,NULL);
    }
    
    if (have_mixer ||
	grabber->grab_hasattr(GRAB_ATTR_VOLUME)) {
	p = XtVaCreateManagedWidget("volume", panedWidgetClass, opt_paned,
				    XtNorientation, XtEvertical,
				    PANED_FIX, NULL);
	l = XtVaCreateManagedWidget("l", labelWidgetClass, p,
				    XtNshowGrip, False,
				    NULL);
	s_volume = XtVaCreateManagedWidget("s", scrollbarWidgetClass, p,
					   PANED_FIX, NULL);
	XtAddCallback(s_volume,XtNjumpProc,  jump_scb,  NULL);
	XtAddCallback(s_volume,XtNscrollProc,scroll_scb,NULL);
    }

    c = XtVaCreateManagedWidget("quit", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,ExitCB,NULL);
}

/*--- avi recording ------------------------------------------------------*/

static void
exec_done(int signal)
{
    int pid,stat;

    if (debug)
	fprintf(stderr,"got sigchild\n");
    pid = waitpid(-1,&stat,WUNTRACED|WNOHANG);
    if (-1 == pid) {
	perror("waitpid");
    } else if (0 == pid) {
	fprintf(stderr,"oops: got sigchild and waitpid returns 0 ???\n");
    } else if (WIFEXITED(stat)){
	if (debug)
	    fprintf(stderr,"[%d]: normal exit (%d)\n",pid,WEXITSTATUS(stat));
    } else if (WIFSIGNALED(stat)){
	if (debug)
	    fprintf(stderr,"[%d]: %s\n",pid,strsignal(WTERMSIG(stat)));
    } else if (WIFSTOPPED(stat)){
	if (debug)
	    fprintf(stderr,"[%d]: %s\n",pid,strsignal(WSTOPSIG(stat)));
    }
    if (pid == str_pid && !WIFSTOPPED(stat))
	str_pid = -1;
}

static void
exec_output(XtPointer data, int *fd, XtInputId * iproc)
{
    char buffer[81];
#if 0
    XawTextPosition pos;
    XawTextBlock blk = { 0, 0, buffer, FMT8BIT };
#endif
    int len;

    switch (len = read(*fd,buffer,80)) {
    case -1: /* error */
	perror("read pipe");
	/* fall */
    case 0:  /* EOF */
	close(*fd);
	XtRemoveInput(*iproc);
	break;
    default: /* got some bytes */
	buffer[len] = 0;
#if 0
	blk.length = len;
	pos = XawTextGetInsertionPoint(str_text);
	XawTextReplace(str_text,pos,pos,&blk);
	XawTextSetInsertionPoint(str_text,pos+len);
#else
	fprintf(stderr,"%s",buffer);
#endif
	break;
    }
}

static int
exec_x11(char **argv)
{
    int p[2],pid,i;

    if (debug) {
	fprintf(stderr,"exec: \"%s\"",argv[0]);
	for (i = 1; argv[i] != NULL; i++)
	    fprintf(stderr,", \"%s\"",argv[i]);
	fprintf(stderr,"\n");
    }
#if 0
    XtVaSetValues(str_text,XtNstring,"",NULL);
#endif
    pipe(p);
    switch (pid = fork()) {
    case -1:
	perror("fork");
	return -1;
    case 0:
	/* child */
	dup2(p[1],1);
	dup2(p[1],2);
	close(p[0]);
	close(p[1]);
	close(ConnectionNumber(dpy));
	execvp(argv[0],argv);
	perror("execvp");
	exit(1);
    default:
	/* parent */
	close(p[1]);
	XtAppAddInput(app_context, p[0], (XtPointer) XtInputReadMask,
		      exec_output, NULL);
	break;
    }
    return pid;
}

Boolean
rec_work(XtPointer client_data)
{
    grab_putbuffer(0,rec_writer,rec_wsync);
    return False;
}

void
exec_record(Widget widget, XtPointer client_data, XtPointer calldata)
{
    static int pid1,pid2;
    struct MOVIE_PARAMS params;
    char *filename, *buffers;
    int bufsize,rec_width,rec_height,linelength;
    
    if (!grabber->grab_setparams ||
	!grabber->grab_capture) {
	missing_feature(MISSING_CAPTURE);
	fprintf(stderr,"grabbing: not supported\n");
	return;
    }

    /* stop of running */
    if (rec_id) {
	int foo;
	XtRemoveWorkProc(rec_id);
	shutdown(rec_writer,1);
	close(rec_wsync);
	waitpid(pid1,&foo,0);
	waitpid(pid2,&foo,0);
	close(rec_writer);
	rec_id = 0;
	grabdisplay_reconfigure();
	XtVaSetValues(avi_status,XtNlabel,"",NULL);
	return;
    }

    /* filename */
    XtVaGetValues(str_fbutton,XtNstring,&filename,NULL);
    filename = tilde_expand(filename);

    /* image size + buffer init */
    rec_width  = win_width;
    rec_height = win_height;
    linelength = 0;
    grabber_setparams(cur_avi_format, &rec_width, &rec_height,
		      &linelength,0);
    bufsize = rec_width*rec_height*format2depth[cur_avi_format]/8;
    if (0 == bufsize)
	bufsize = rec_width*rec_height*3; /* compressed - should be enouth */
    if(NULL == (buffers = grab_initbuffers(bufsize,6))) {
	grabdisplay_reconfigure();
	return;
    }
    
    /* set params */
    memset(&params,0,sizeof(params));
    params.video_format = cur_avi_format;
    params.width        = rec_width;
    params.height       = rec_height;
    params.fps          = cur_avi_fps;

    if (cur_avi_audio > 0) {
	params.bits         = 16;
	params.channels     = cur_avi_audio;
	params.rate         = 44100;
    }

    /* go! */
    pid1 = grab_writer_avi(filename,&params,
			   buffers,bufsize,0,&rec_writer);
    pid2 = grab_syncer(&rec_wsync,0);
    rec_id = XtAppAddWorkProc(app_context,rec_work,NULL);
    XtVaSetValues(avi_status,XtNlabel,"recording",NULL);
}

void
exec_xanim_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    char *command[] = {
	"xanim",
	"+f",   /* read from file    */
	"+Sr",  /* allow resize      */
	"+Ze",  /* exit when done    */
	"-Zr",  /* no control window */
	NULL,   /* filename */
	NULL
    };
    
    /* filename */
    XtVaGetValues(str_fbutton,XtNstring,command+5,NULL); /* XXX memory ??? */
    command[5] = tilde_expand(command[5]);

    /* go! */
    exec_x11(command);
}

#define FIX_LEFT_TOP        \
    XtNleft,XawChainLeft,   \
    XtNright,XawChainRight, \
    XtNtop,XawChainTop,     \
    XtNbottom,XawChainTop

void
create_strwin()
{
    Widget form,label,button;

    str_shell = XtVaAppCreateShell("Streamer", "Xawtv",
				   topLevelShellWidgetClass,
				   dpy,
				   XtNclientLeader,app_shell,
				   XtNvisual,vinfo.visual,
				   XtNcolormap,colormap,
				   XtNdepth,vinfo.depth,
				   NULL);
    XtOverrideTranslations(str_shell, XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: Popup()"));

    form = XtVaCreateManagedWidget("form", formWidgetClass, str_shell,
                                   NULL);

    label = XtVaCreateManagedWidget("flabel", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    NULL);
    str_fbutton = XtVaCreateManagedWidget("fname", asciiTextWidgetClass, form,
					  FIX_LEFT_TOP,
					  XtNfromVert, label,
					  XtNstring,"movie.avi",
					  NULL);

    button = XtVaCreateManagedWidget("audio", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, str_fbutton,
				     NULL);
    XtAddCallback(button,XtNcallback,menu_cb,(XtPointer)20);
    set_menu_val(button,"AVI Audio",avi_audio,cur_avi_audio);

    button = XtVaCreateManagedWidget("format", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, button,
				     NULL);
    XtAddCallback(button,XtNcallback,menu_cb,(XtPointer)21);
    set_menu_val(button,"AVI Format",avi_format,cur_avi_format);

    button = XtVaCreateManagedWidget("fps", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, button,
				     NULL);
    XtAddCallback(button,XtNcallback,menu_cb,(XtPointer)22);
    set_menu_val(button,"AVI Framerate",avi_fps,cur_avi_fps);

    label = XtVaCreateManagedWidget("size", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert, button,
				    NULL);
    avi_size = label;
    label = XtVaCreateManagedWidget("status", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert, label,
				    XtNlabel,    "",
				    NULL);
    avi_status = label;

    button = XtVaCreateManagedWidget("streamer", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, label,
				     NULL);
    XtAddCallback(button,XtNcallback,exec_record,NULL);
    
    button = XtVaCreateManagedWidget("xanim", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, button,
				     NULL);
    XtAddCallback(button,XtNcallback,exec_xanim_cb,NULL);

#if 0
    label = XtVaCreateManagedWidget("olabel", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert,button,
				    NULL);
    str_text = XtVaCreateManagedWidget("output", asciiTextWidgetClass, form,
				       XtNleft,XawChainLeft,
				       XtNright,XawChainRight,
				       XtNtop,XawChainTop,
				       XtNbottom,XawChainBottom,
				       XtNfromVert,label,
				       NULL);
#endif
}

/*--- launcher window -----------------------------------------------------*/

void
create_launchwin()
{
    launch_shell = XtVaAppCreateShell("Launcher", "Xawtv",
				     topLevelShellWidgetClass,
				     dpy,
				     XtNclientLeader,app_shell,
				     XtNvisual,vinfo.visual,
				     XtNcolormap,colormap,
				     XtNdepth,vinfo.depth,
				     NULL);
    XtOverrideTranslations(launch_shell, XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: Popup()"));
    launch_paned = XtVaCreateManagedWidget("paned", panedWidgetClass,
					  launch_shell, NULL);
}

void
LaunchAction(Widget widget, XEvent *event,
	    String *params, Cardinal *num_params)
{
    char **argv;
    int  i,argc;
    
    if (*num_params != 1)
	return;
    for (i = 0; i < nlaunch; i++) {
	if (0 == strcasecmp(params[0],launch[i].name))
	    break;
    }
    if (i == nlaunch)
	return;

    argv = split_cmdline(launch[i].cmdline,&argc);

    switch (fork()) {
    case -1:
	perror("fork");
	break;
    case 0:
	if (debug) {
	    fprintf(stderr,"[%d]: exec ",getpid());
	    for (i = 0; i < argc; i++) {
		fprintf(stderr,"\"%s\" ",argv[i]);
	    }
	    fprintf(stderr,"\n");
	}
	execvp(argv[0],argv);
	fprintf(stderr,"execvp %s: %s",argv[0],strerror(errno));
	exit(1);
	break;
    default:
	break;
    }
}

/*------------------------------------------------------------------------*/

static void
lirc_input(XtPointer data, int *fd, XtInputId *iproc)
{
    if (debug)
	fprintf(stderr,"lirc_input triggered\n");
    if (-1 == lirc_tv_havedata()) {
	fprintf(stderr,"lirc: connection lost\n");
	XtRemoveInput(*iproc);
	close(*fd);
    }
}

static void
xfree_init()
{
    int  flags,foo,bar,i,ma,mi;

    if (!do_overlay)
	return;
    
#ifdef HAVE_LIBXXF86DGA
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
#ifdef HAVE_LIBXXF86VM
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

static void
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
	depth = format2depth[x11_native_format];
	width *= (depth+7)/8;
	fprintf(stderr,"x11: %dx%d, %d bit/pixel, %d byte/scanline%s%s\n",
		sw,sh,depth,width,
		have_dga ? ", DGA" : "",
		have_vm ? ", VidMode" : "");
    }
    grabber_open(args.device,sw,sh,base,x11_native_format,width);
}

void
x11_check_remote(Display *dpy)
{
    int fd = ConnectionNumber(dpy);
    struct sockaddr_in me,peer;
    int length;

    do_overlay = 1;
    length = sizeof(me);
    if (-1 == getsockname(fd,&me,&length)) {
	perror("getsockname");
	return;
    }
    length = sizeof(peer);
    if (-1 == getpeername(fd,&peer,&length)) {
	perror("getpeername");
	return;
    }
    if (me.sin_family == PF_UNIX) {
	if (debug)
	    fprintf(stderr,"x11 uses unix sockets\n");
	return;
    }
    if (me.sin_family == PF_INET) {
	if (debug) {
	    fprintf(stderr,"x11 uses inet sockets\n");
	    fprintf(stderr,"\tme  : %s\n",inet_ntoa(me.sin_addr));
	    fprintf(stderr,"\tpeer: %s\n",inet_ntoa(peer.sin_addr));
	}
	if (0 == memcmp(&me.sin_addr,&peer.sin_addr,sizeof(me.sin_addr)))
	    return;
	/* ip addr not equal => assume remote display */
	do_overlay = 0;
	return;
    }
    fprintf(stderr,"x11: Oops: unknown socket family: %d\n",me.sin_family);
    return;
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

static void
siginit(void)
{
    struct sigaction act,old;

    memset(&act,0,sizeof(act));
    act.sa_handler  = exec_done;
    sigemptyset(&act.sa_mask);
    sigaction(SIGCHLD,&act,&old);
}

static void
hello_world()
{
    struct utsname uts;

    uname(&uts);
    fprintf(stderr,"This is %s, running on %s/%s (%s)\n",
	    VERSION,uts.sysname,uts.machine,uts.release);
}

/*--- main ---------------------------------------------------------------*/

void
usage(void)
{
    fprintf(stderr,
	    "\n"
	    "usage: xawtv [ options ] [ station ]\n"
	    "options:\n"
	    "  -v, -debug=n      debug level n, n = [0..2]\n"
	    "      -remote       assume remote display\n"
	    "  -n  -noconf       don't read the config file\n"
	    "  -m  -nomouse      startup with mouse pointer disabled\n"
	    "  -f  -fullscreen   startup in fullscreen mode\n"
	    "      -nodga        disable DGA extention\n"
	    "      -novm         disable VidMode extention\n"
	    "      -noxv         disable Xvideo extention\n"
	    "  -b  -bpp n        color depth of the display is n (n=24,32)\n"
	    "  -o  -outfile file filename base for snapshots\n"
	    "  -c  -device file  use <file> as video4linux device\n"
	    "      -shift x      shift display by x bytes\n"
	    "      -fb           let fb (not X) set up v4l device\n"
	    "  -h  -help         print this text\n"
	    "station:\n"
	    "  this is one of the stations listed in $HOME/.xawtv\n"
	    "\n"
	    "--\n"
	    "Gerd Knorr <kraxel@goldbach.in-berlin.de>\n");
}

int
main(int argc, char *argv[])
{
    Dimension    w;
    Visual       *visual;
    XVisualInfo  *vinfo_list;
    int          n;

    if (0 == geteuid() && 0 != getuid()) {
	fprintf(stderr,"xawtv /must not/ be installed suid root\n");
	exit(1);
    }
    
    hello_world();
    progname = strdup(argv[0]);

    /* toplevel */
    app_shell = XtVaAppInitialize(&app_context,
				  "Xawtv",
				  opt_desc, OPT_COUNT,
				  &argc, argv,
				  NULL /* fallback_res */,
				  NULL);
    dpy = XtDisplay(app_shell);
    XtGetApplicationResources(app_shell,&args,
			      args_desc,XtNumber(args_desc),
			      NULL,0);
    if (args.help) {
	usage();
	exit(0);
    }
    debug = args.debug;
    do_overlay = !args.remote;
    snapbase = args.basename;
    
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
	app_shell = XtVaAppCreateShell("xawtv", "Xawtv",
				       applicationShellWidgetClass, dpy,
				       XtNvisual,vinfo.visual,
				       XtNcolormap,colormap,
				       XtNdepth, vinfo.depth,
				       NULL);
    } else {
	colormap = DefaultColormapOfScreen(XtScreen(app_shell));
    }

    /* build v4l-conf args */
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

    if (do_overlay)
	x11_check_remote(XtDisplay(app_shell));

    XtAppAddActions(app_context,actionTable,
		    sizeof(actionTable)/sizeof(XtActionsRec));
    fcntl(ConnectionNumber(dpy),F_SETFD,FD_CLOEXEC);
    wm_protocols[0] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wm_protocols[1] = XInternAtom(dpy, "WM_SAVE_YOURSELF", False);
    xawtv_station   = XInternAtom(dpy, "_XAWTV_STATION", False);
    xawtv_remote    = XInternAtom(dpy, "_XAWTV_REMOTE", False);

    xfree_init();
    xv_init(args.xv_video,args.xv_scale);

    /* set hooks (command.c) */
    update_title        = new_title;
    display_message     = new_message;
    norm_notify         = new_norm;
    input_notify        = new_input;
    attr_notify         = new_attr;
    freqtab_notify      = new_freqtab;
    setfreqtab_notify   = new_freqtab;
    setstation_notify   = new_channel;
    set_capture_hook    = do_capture;
    fullscreen_hook     = do_fullscreen;
    exit_hook           = do_exit;
    reconfigure_hook    = grabdisplay_reconfigure;
    channel_switch_hook = pixit;
    
    tv = video_init(app_shell,&vinfo);
    XtAddEventHandler(XtParent(tv),StructureNotifyMask, True,
		      resize_event, NULL);
    XtVaGetValues(tv,XtNwidth,&w,NULL);
    if (!w) {
	fprintf(stderr,"The app-defaults file is not correctly installed.\n");
	fprintf(stderr,"Your fault (core dumped)\n");
	exit(1);
    }
    if (ImageByteOrder(dpy) == MSBFirst) {
	/* X-Server is BE */
	switch(args.bpp) {
	case  8: x11_native_format = VIDEO_RGB08;    break;
	case 15: x11_native_format = VIDEO_RGB15_BE; break;
	case 16: x11_native_format = VIDEO_RGB16_BE; break;
	case 24: x11_native_format = VIDEO_BGR24;    break;
	case 32: x11_native_format = VIDEO_BGR32;    break;
	default:
	    args.bpp = 0;
	}
    } else {
	/* X-Server is LE */
	switch(args.bpp) {
	case  8: x11_native_format = VIDEO_RGB08;    break;
	case 15: x11_native_format = VIDEO_RGB15_LE; break;
	case 16: x11_native_format = VIDEO_RGB16_LE; break;
	case 24: x11_native_format = VIDEO_BGR24;    break;
	case 32: x11_native_format = VIDEO_BGR32;    break;
	default:
	    args.bpp = 0;
	}
    }
    siginit();
    if (NULL == grabber)
	grabber_init();

    XSetIOErrorHandler(x11_ctrl_alt_backspace);
    wm_detect(dpy);
    create_optwin();
    create_onscreen();
    create_chanwin();
    create_confwin();
    create_strwin();
    create_launchwin();

    if (args.readconfig)
	read_config();
    channel_menu();

    /* lirc support */
    if (-1 != (lirc = lirc_tv_init()))
	XtAppAddInput(app_context,lirc,(XtPointer)XtInputReadMask,lirc_input,NULL);

    XtRealizeWidget(app_shell);
    create_pointers(app_shell);
    create_bitmaps(app_shell);
    XDefineCursor(dpy, XtWindow(app_shell), left_ptr);
    XSetWMProtocols(XtDisplay(app_shell), XtWindow(app_shell),
		    wm_protocols, 2);

    XtVaSetValues(app_shell,
		  XtNwidthInc,  WIDTH_INC,
		  XtNheightInc, HEIGHT_INC,
		  XtNminWidth,  WIDTH_INC,
		  XtNminHeight, HEIGHT_INC,
		  NULL);
    XtVaSetValues(chan_shell,
		  XtNwidth,pix_width*pix_cols+30,
		  NULL);

    /* init hardware */
    attr_init();
    audio_init();
    audio_on();
    do_va_cmd(2,"setfreqtab",chanlist_names[chantab].str);

    cur_capture = 0;
    do_va_cmd(2,"capture","overlay");
    set_property(0,NULL,NULL);
    if (optind+1 == argc) {
	do_va_cmd(2,"setstation",argv[optind]);
    } else {
	if (!grabber->grab_tuned || !grabber->grab_tuned()) {
	    if (count > 0)
		do_va_cmd(2,"setstation","0");
	    else
		set_defaults();
	}
    }

    if (args.fullscreen)
	do_fullscreen();
    else
	XtAppAddWorkProc(app_context,MyResize,NULL);
    if (!args.showpointer)
	PointerAction(NULL,NULL,NULL,NULL);

    sprintf(modename,"%dx%d, ",
	    XtScreen(app_shell)->width,XtScreen(app_shell)->height);
    strcat(modename,format_desc[x11_native_format]);
    new_message(modename);
    if (!have_config)
	XtCallActionProc(tv,"Help",NULL,NULL,0);

    signal(SIGHUP,SIG_IGN); /* don't really need a tty ... */
    XtAppMainLoop(app_context);

    /* keep compiler happy */
    return 0;
}
