/*
 * main.c for xawtv -- a TV application
 *
 *   (c) 1997-2001 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
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
#ifdef HAVE_LIBXINERAMA
# include <X11/extensions/Xinerama.h>
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

#include "grab-ng.h"
#include "writefile.h"

#include "grab.h"
#include "sound.h"
#include "channel.h"
#include "commands.h"
#include "frequencies.h"
#include "xv.h"
#include "capture.h"
#include "xt.h"
#include "x11.h"
#include "toolbox.h"
#include "complete.h"
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
#define BOOL_WIDTH          "24"
#define VIDMODE_DELAY        100   /* 0.1 sec */

/*--- public variables ----------------------------------------------------*/

Widget            opt_shell, opt_paned, chan_shell, conf_shell, str_shell;
Widget            on_shell, on_label;
Widget            vtx_shell, vtx_label;
Widget            launch_shell, launch_paned;
Widget            c_norm,c_input,c_freq,c_audio,c_cap;
Widget            s_bright,s_color,s_hue,s_contrast,s_volume;
Widget            chan_viewport, chan_box;
XtWorkProcId      idle_id;
Pixmap            tv_pix;
int               stay_on_top = 0;

int               str_pid = -1;

int               have_config = 0;
XtIntervalId      title_timer, audio_timer, zap_timer, scan_timer, on_timer;
int               debug = 0;
int               fs = 0;
int               zap_start,zap_fast;

char              modename[64];
char              *progname;
int               lirc;

int               rec_writer;
int               rec_wsync;
XtWorkProcId      rec_work_id;

/* movie params / setup */
Widget            w_movie_status;
Widget            w_movie_driver;

Widget            w_movie_fvideo;
Widget            w_movie_video;
Widget            w_movie_fps;
Widget            w_movie_size;

Widget            w_movie_flabel;
Widget            w_movie_faudio;
Widget            w_movie_audio;
Widget            w_movie_rate;

struct STRTAB     *m_movie_driver;
struct STRTAB     *m_movie_audio;
struct STRTAB     *m_movie_video;

int               movie_driver = 0;
int               movie_audio  = 0;
int               movie_video  = 0;
int               movie_fps    = 12;
int               movie_rate   = 44100;
void              *movie_state;

static struct STRTAB m_movie_fps[] = {
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
static struct STRTAB m_movie_rate[] = {
    {   8000, " 8000" },
    {  11025, "11025" },
    {  22050, "22050" },
    {  44100, "44100" },
    {  48000, "48000" },
    { -1, NULL },
};

struct xaw_attribute {
    struct ng_attribute   *attr;
    Widget                cmd,scroll;
    struct xaw_attribute  *next;
};
static struct xaw_attribute *xaw_attrs;

#define MOVIE_DRIVER  "movie driver"
#define MOVIE_AUDIO   "audio format"
#define MOVIE_VIDEO   "video format"
#define MOVIE_FPS     "frames/sec"
#define MOVIE_RATE    "sample rate"
#define MOVIE_SIZE    "video size"

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

struct DO_AC {
    int  argc;
    char *name;
    char *argv[8];
};

/*--- actions -------------------------------------------------------------*/

/* conf.c */
extern void create_confwin(void);
extern void conf_station_switched(void);
extern void conf_list_update(void);

void CloseMainAction(Widget, XEvent*, String*, Cardinal*);
void SetBgAction(Widget, XEvent*, String*, Cardinal*);
void SetShadowAction(Widget, XEvent*, String*, Cardinal*);
void ScanAction(Widget, XEvent*, String*, Cardinal*);
void ChannelAction(Widget, XEvent*, String*, Cardinal*);
void RemoteAction(Widget, XEvent*, String*, Cardinal*);
void ZapAction(Widget, XEvent*, String*, Cardinal*);
void StayOnTop(Widget, XEvent*, String*, Cardinal*);
void LaunchAction(Widget, XEvent*, String*, Cardinal*);
void PopupAction(Widget, XEvent*, String*, Cardinal*);

static XtActionsRec actionTable[] = {
    { "CloseMain",   CloseMainAction  },
    { "SetBg",       SetBgAction },
    { "SetShadow",   SetShadowAction },
    { "Scan",        ScanAction },
    { "Channel",     ChannelAction },
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
    video_overlay(0);
    video_close();
    if (have_mixer)
	mixer_close();
    if (fs)
	do_va_cmd(1,"fullscreen");
    XSync(dpy,False);
    drv->close(h_drv);
    XtAppAddWorkProc (app_context,ExitWP, NULL);
    XtDestroyWidget(app_shell);
}

static void
do_exit(void)
{
    ExitCB(NULL,NULL,NULL);    
}

void
CloseMainAction(Widget widget, XEvent *event,
		String *params, Cardinal *num_params)
{
    if (NULL != event && event->type == ClientMessage) {
	if (debug)
	    fprintf(stderr,"CloseMainAction: received %s message\n",
		    XGetAtomName(dpy,event->xclient.data.l[0]));
	if (event->xclient.data.l[0] == wm_delete_window) {
	    /* fall throuth -- popdown window */
	} else {
	    /* whats this ?? */
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
	if (event->xclient.data.l[0] == wm_delete_window) {
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
	if (wm_stay_on_top && stay_on_top > 0)
	    wm_stay_on_top(dpy,XtWindow(*(my_toplevels[i].shell)),1);
	my_toplevels[i].mapped = 1;
	if (!my_toplevels[i].first) {
	    XSetWMProtocols(XtDisplay(*(my_toplevels[i].shell)),
			    XtWindow(*(my_toplevels[i].shell)),
			    &wm_delete_window, 1);
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

void action_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct DO_AC *ca = clientdata;
    XtCallActionProc(widget,ca->name,NULL,ca->argv,ca->argc);
}

/*--- onscreen display (fullscreen) --------------------------------------*/

void create_onscreen(void)
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
    if (debug)
	fprintf(stderr,"osd: hide\n");
    XtPopdown(on_shell);
    on_timer = 0;
}

void
display_onscreen(char *title)
{
    static int first = 1;
    Dimension x,y;

    if (!fs)
	return;
    if (!use_osd)
	return;

    if (debug)
	fprintf(stderr,"osd: show (%s)\n",title);
    XtVaGetValues(app_shell,XtNx,&x,XtNy,&y,NULL);
    XtVaSetValues(on_shell,XtNx,x+30,XtNy,y+20,NULL);
    XtVaSetValues(on_label,XtNlabel,title,NULL);
    XtPopup(on_shell, XtGrabNone);
    if (wm_stay_on_top && stay_on_top > 0)
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

/*--- videotext ----------------------------------------------------------*/

struct TEXTELEM {
    char  *str;
    int   len;
    int   nl;
    int   x,y;
    Pixel fg,bg;
};

void create_vtx(void)
{
    vtx_shell = XtVaCreateWidget("vtx",transientShellWidgetClass,
				 app_shell,
				 XtNoverrideRedirect,True,
				 XtNvisual,vinfo.visual,
				 XtNcolormap,colormap,
				 XtNdepth,vinfo.depth,
				 NULL);
    vtx_label = XtVaCreateManagedWidget("label", labelWidgetClass, vtx_shell,
					NULL);
}

void
display_vtx(int lines, char **text)
{
    static char *names[8] = { "black", "red", "green", "yellow",
			      "blue", "magenta", "cyan", "white" };
    static XColor colors[8], dummy;
    static XFontStruct *font;
    static Pixmap pix;
    static GC gc;
    static int first = 1;
    XGCValues  values;
    Dimension x,y,w,h,sw,sh;
    int maxwidth,width,height,direction,ascent,descent,i,n,t;
    struct TEXTELEM tt[32];
    XCharStruct cs;

    if (0 == lines) {
	XtPopdown(vtx_shell);
	return;
    }

    if (NULL == font) {
	XtVaGetValues(vtx_label,XtNfont,&font,NULL);
	for (i = 0; i < 8; i++)
	    XAllocNamedColor(dpy,colormap,names[i],
			     &colors[i],&dummy);
	values.font = font->fid;
	gc = XCreateGC(dpy, XtWindow(vtx_label), GCFont, &values);
    }

    /* parse */
    t = 0;
    memset(tt,0,sizeof(tt));
    for (i = 0; i < lines; i++) {
	tt[t].fg  = colors[7].pixel;
	tt[t].bg  = colors[0].pixel;
	tt[t].str = text[i];
	tt[t].nl  = 1;
	for (n = 0; text[i][n] != 0;) {
	    if (text[i][n] == '\033') {
		if (tt[t].len) {
		    t++;
		    if (32 == t)
			goto calc;
		}
		n++;
		if (text[i][n] >= '0' && text[i][n] < '8') {
		    tt[t].fg  = colors[text[i][n]-'0'].pixel;
		    n++;
		}
		if (text[i][n] >= '0' && text[i][n] < '8') {
		    tt[t].bg  = colors[text[i][n]-'0'].pixel;
		    n++;
		}
		tt[t].str = text[i]+n;
	    } else {
		tt[t].len++;
		n++;
	    }
	}
	t++;
	if (32 == t)
	    break;
    }

 calc:
    /* calc size + positions */
    width = 0; height = 0; maxwidth = 0;
    for (i = 0; i < t; i++) {
	XTextExtents(font,tt[i].str,tt[i].len,
		     &direction,&ascent,&descent,&cs);
	if (tt[i].nl) {
	    if (maxwidth < width)
		maxwidth = width;
	    width = 0;
	    height += ascent+descent;
	}
	tt[i].x = width;
	tt[i].y = height - descent;
	width += cs.width;
    }
    if (maxwidth < width)
	maxwidth = width;

    /* alloc pixmap + draw text */
    if (pix)
	XFreePixmap(dpy,pix);
    pix = XCreatePixmap(dpy, RootWindowOfScreen(XtScreen(vtx_label)),
			maxwidth, height,
			DefaultDepthOfScreen(XtScreen(vtx_label)));
    for (i = 0; i < t; i++) {
	values.foreground = tt[i].fg;
	values.background = tt[i].bg;
	XChangeGC(dpy, gc, GCForeground | GCBackground, &values);
	XDrawImageString(dpy,pix,gc,tt[i].x,tt[i].y,tt[i].str,tt[i].len);
    }
    XtVaSetValues(vtx_label,XtNbitmap,pix,XtNlabel,NULL,NULL);

    XtVaGetValues(app_shell,XtNx,&x,XtNy,&y,XtNwidth,&w,XtNheight,&h,NULL);
    XtVaGetValues(vtx_shell,XtNwidth,&sw,XtNheight,&sh,NULL);
    XtVaSetValues(vtx_shell,XtNx,x+(w-sw)/2,XtNy,y+h-10-sh,NULL);
    XtPopup(vtx_shell, XtGrabNone);
    if (wm_stay_on_top && stay_on_top > 0)
	wm_stay_on_top(dpy,XtWindow(vtx_shell),1);

    if (first) {
	first = 0;
	XDefineCursor(dpy, XtWindow(vtx_shell), left_ptr);
	XDefineCursor(dpy, XtWindow(vtx_label), left_ptr);
    }
}

/*--- tv -----------------------------------------------------------------*/

static XImage     *grab_ximage;
static void       *grab_ximage_shm;
static GC          grab_gc;
static int         win_width, win_height;
static int         grabdisplay_suspended;
static int         use_hw_scale;

#ifdef HAVE_LIBXV
static XvImage     *xv_image = NULL;
static void        *xv_shm = NULL;
static struct ng_video_fmt xv_fmt;
#endif

void
freeze_image(void)
{
    struct ng_video_buf buf;
    
    if (!(f_drv & CAN_CAPTURE))
	return;
    if (NULL == grab_ximage)
	return;
    if (cur_capture != CAPTURE_GRABDISPLAY) {
	buf.fmt  = x11_fmt;
	buf.data = grab_ximage->data;
	if (NULL == ng_grabber_capture(&buf,1))
	    return;
    }
    
    if (!tv_pix) {
	tv_pix = XCreatePixmap(dpy, RootWindowOfScreen(XtScreen(tv)),
			       win_width, win_height,
			       DefaultDepthOfScreen(XtScreen(tv)));
    }
    XPUTIMAGE(dpy, tv_pix, grab_gc, grab_ximage, 0,0,
	      (win_width  - x11_fmt.width)  >> 1,
	      (win_height - x11_fmt.height) >> 1,
	      x11_fmt.width, x11_fmt.height);
    XtVaSetValues(tv,XtNbackgroundPixmap,tv_pix,NULL);
}

static Boolean
grabdisplay_idle(XtPointer data)
{
    static long count,lastsec,errors;
    struct timeval  t;
    struct timezone tz;
    struct ng_video_buf buf;
    
    if (!(f_drv & CAN_CAPTURE))
	goto oops;

#ifdef HAVE_LIBXV
    if (use_hw_scale) {
	if (NULL == xv_image)
	    goto oops;
	buf.fmt  = xv_fmt;
	buf.data = xv_image->data;
	if (NULL == ng_grabber_capture(&buf,0)) {
	    if (errors++ > 10)
		goto oops;
	} else {
	    errors = 0;
	    XVPUTIMAGE(dpy, im_port, XtWindow(tv), grab_gc, xv_image,
		       0, 0,  xv_fmt.width, xv_fmt.height,
		       0, 0,  win_width, win_height);
	}
    }
#endif
    if (!use_hw_scale) {
	if (!grab_ximage)
	    goto oops;
	buf.fmt  = x11_fmt;
	buf.data = grab_ximage->data;
	if (NULL == ng_grabber_capture(&buf,0)) {
	    if (errors++ > 10)
		goto oops;
	} else {
	    errors = 0;
	    XPUTIMAGE(dpy, XtWindow(tv), grab_gc, grab_ximage, 0,0,
		      (win_width  - x11_fmt.width)  >> 1,
		      (win_height - x11_fmt.height) >> 1,
		      x11_fmt.width, x11_fmt.height);
	}
    }

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
    if (f_drv & CAN_CAPTURE)
	drv->stopvideo(h_drv);
    return TRUE;
}

static void
grabdisplay_suspend(void)
{
    if (cur_capture != CAPTURE_GRABDISPLAY)
	return;
    grabdisplay_suspended= 1;
    do_va_cmd(2, "capture", "off");
}

static void
grabdisplay_restart(void)
{
#ifdef HAVE_LIBXV
    if (use_hw_scale)
	ng_grabber_setparams(&xv_fmt,0);
#endif
    if (!use_hw_scale)
	ng_grabber_setparams(&x11_fmt,0);
    
    if (cur_capture != CAPTURE_OFF)
	return;
    if (!grabdisplay_suspended)
	return;
    do_va_cmd(2, "capture", "grab");
    grabdisplay_suspended = 0;
}

static void
grabdisplay_setsize(int width, int height)
{
    if (!XtWindow(tv))
	return;

    win_width       = width;
    win_height      = height;
    x11_fmt.width   = width & ~3; /* alignment */
    x11_fmt.height  = height;
    x11_fmt.bytesperline = 0;

    /* check what the driver can do ... */
    if (!(f_drv & CAN_CAPTURE))
	return;

    if (!grab_gc)
	grab_gc = XCreateGC(dpy,XtWindow(tv),0,NULL);

    if (cur_capture == CAPTURE_GRABDISPLAY)
	drv->stopvideo(h_drv);

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

    use_hw_scale = 0;
#ifdef HAVE_LIBXV
    if (xv_image) {
	xv_destroy_ximage(dpy,xv_image,xv_shm);
	xv_image = NULL;
    }
    if (have_xv_scale) {
	xv_fmt.width  = width;
	xv_fmt.height = height;
	xv_fmt.fmtid = VIDEO_YUV422;
	xv_fmt.bytesperline = 0;
	if (0 == ng_grabber_setparams(&xv_fmt,0)) {
	    xv_image = xv_create_ximage(dpy, xv_fmt.width, xv_fmt.height,
					&xv_shm);
	    use_hw_scale = 1;
	}
    }
#endif
    if (0 == use_hw_scale) {
	ng_grabber_setparams(&x11_fmt,1);
	grab_ximage = x11_create_ximage(dpy,&vinfo,
					x11_fmt.width,x11_fmt.height,
					&grab_ximage_shm);
	if (NULL == grab_ximage) {
	    fprintf(stderr,"oops: out of memory\n");
	    exit(1);
	}
    }
    if (cur_capture == CAPTURE_GRABDISPLAY)
	drv->startvideo(h_drv,-1,2);
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
	    sprintf(label,"%-" LABEL_WIDTH "s: %dx%d",MOVIE_SIZE,width,height);
	    if (w_movie_size)
		XtVaSetValues(w_movie_size,XtNlabel,label,NULL);
	}
	break;
    }
}

#if 0
static void
tv_expose_event(Widget widget, XtPointer client_data,
		XEvent *event, Boolean *d)
{
    static GC  gc;
    XGCValues  values;

    switch(event->type) {
    case Expose:
	if (debug)
	    fprintf(stderr,"chromakey expose %d\n",
		    event->xexpose.count);
	if (0 == event->xexpose.count && CAPTURE_OVERLAY == cur_capture) {
	    if (0 == gc) {
		XColor color;
		color.red   = (grabber->colorkey & 0x00ff0000) >> 8;
		color.green = (grabber->colorkey & 0x0000ff00);
		color.blue  = (grabber->colorkey & 0x000000ff) << 8;
		XAllocColor(dpy,colormap,&color);
		values.foreground = color.pixel;
		gc = XCreateGC(dpy, XtWindow(widget), GCForeground, &values);
	    }
	    /* TODO: draw background for chroma keying */
	    XFillRectangle(dpy,XtWindow(widget),gc,
			   (win_width  - x11_fmt.width)  >> 1,
			   (win_height - x11_fmt.height) >> 1,
			   x11_fmt.width, x11_fmt.height);
	}
	break;
    }
}
#endif

/*------------------------------------------------------------------------*/

/* the RightWay[tm] to set float resources (copyed from Xaw specs) */
void set_float(Widget widget, char *name, float value)
{
    Arg   args[1];

    if (sizeof(float) > sizeof(XtArgVal)) {
	/*
	 * If a float is larger than an XtArgVal then pass this 
	 * resource value by reference.
	 */
	XtSetArg(args[0], name, &value);
    } else {
        /*
	 * Convince C not to perform an automatic conversion, which
	 * would truncate 0.5 to 0.
	 *
	 * switched from pointer tricks to the union to fix alignment
	 * problems on ia64 (Stephane Eranian <eranian@cello.hpl.hp.com>)
	 */
	union {
	    XtArgVal xt;
	    float   fp;
	} foo;
	foo.fp = value;
	XtSetArg(args[0], name, foo.xt);
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
new_freqtab(void)
{
    char label[64];

    if (c_freq) {
	sprintf(label,"%-" LABEL_WIDTH "s: %s","Frequency table",
		chanlists[chantab].name);
	XtVaSetValues(c_freq,XtNlabel,label,NULL);
    }
}

static void
new_volume(void)
{
    if (s_volume)
	set_float(s_volume,XtNtopOfThumb,
		  (float)cur_attrs[ATTR_ID_VOLUME]/65536);
}

static void
new_attr(struct ng_attribute *attr, int val)
{
    struct xaw_attribute *a;
    char label[64],*olabel;
    const char *valstr;

    for (a = xaw_attrs; NULL != a; a = a->next) {
	if (a->attr->id == attr->id)
	    break;
    }
    if (NULL != a) {
	switch (attr->type) {
	case ATTR_TYPE_CHOICE:
	    XtVaGetValues(a->cmd,XtNlabel,&olabel,NULL);
	    valstr = ng_attr_getstr(attr,val);
	    sprintf(label,"%-" LABEL_WIDTH "." LABEL_WIDTH "s: %s",
		    olabel,valstr ? valstr : "unknown");
	    XtVaSetValues(a->cmd,XtNlabel,label,NULL);
	    break;
	case ATTR_TYPE_BOOL:
	    XtVaGetValues(a->cmd,XtNlabel,&olabel,NULL);
	    sprintf(label,"%-" BOOL_WIDTH "." BOOL_WIDTH "s  %s",
		    olabel,val ? "on" : "off");
	    XtVaSetValues(a->cmd,XtNlabel,label,NULL);
	    break;
	case ATTR_TYPE_INTEGER:
	    set_float(a->scroll,XtNtopOfThumb,(float)val/65536);
	    break;
	}
	if (ATTR_ID_NORM == attr->id  &&  win_width > 0)
	    grabdisplay_setsize(win_width,win_height);
	return;
    }
}

static void
new_channel(void)
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
    audio_timer = XtAppAddTimeOut(app_context, 2000, watch_audio, NULL);
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
    struct ng_attribute *attr;
    char label[64];
    char mname[10];

    attr = ng_attr_byid(a_drv,ATTR_ID_AUDIO_MODE);
    if (NULL == attr)
	return;

    if (-1 != mode) {
	/* set */
	drv->write_attr(h_drv,attr,mode);
    }
    if (-1 == mode || 0 == mode) {
	/* read back */
	mode = drv->read_attr(h_drv,attr);
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
}

void
watch_audio(XtPointer data, XtIntervalId *id)
{
    if (-1 != cur_sender)
	change_audio(channels[cur_sender]->audio);
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
	    if (f_drv & CAN_CAPTURE)
		drv->stopvideo(h_drv);
	}
	idle_id = 0;
	XClearArea(XtDisplay(tv), XtWindow(tv), 0,0,0,0, True);
	break;
    case CAPTURE_OVERLAY:
	video_overlay(0);
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
	if (f_drv & CAN_CAPTURE)
	    drv->startvideo(h_drv,-1,2);
	break;
    case CAPTURE_OVERLAY:
	sprintf(label,"%-" LABEL_WIDTH "s: %s","Capture","overlay");
	video_overlay(1);
	break;
    }
    if (c_cap)
	XtVaSetValues(c_cap,XtNlabel,label,NULL);
}

/* gets called before switching away from a channel */
void
pixit(void)
{
    Pixmap pix;
    struct ng_video_fmt fmt;
    struct ng_video_buf *buf;

    if (cur_sender == -1)
	return;

    /* save picture settings */
    channels[cur_sender]->color    = cur_attrs[ATTR_ID_COLOR];
    channels[cur_sender]->bright   = cur_attrs[ATTR_ID_BRIGHT];
    channels[cur_sender]->hue      = cur_attrs[ATTR_ID_HUE];
    channels[cur_sender]->contrast = cur_attrs[ATTR_ID_CONTRAST];

    if (0 == pix_width || 0 == pix_height)
	return;

    /* capture mini picture */
    if (!(f_drv & CAN_CAPTURE))
	return;

    grabdisplay_suspend();
    fmt = x11_fmt;
    fmt.width  = pix_width;
    fmt.height = pix_height;
    if (0 == ng_grabber_setparams(&fmt,0) &&
	NULL != (buf = ng_grabber_capture(NULL,1)) &&
	0 != (pix = x11_create_pixmap(dpy,&vinfo,colormap,buf->data,
				      fmt.width,fmt.height,
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
	ng_release_video_buf(buf);
    }
    grabdisplay_restart();
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
	    for (i = 0, argc = 0; i <= nitems; i += strlen(args + i) + 1) {
		if (i == nitems || args[i] == '\0') {
		    argv[argc] = NULL;
		    do_command(argc,argv);
		    argc = 0;
	        } else {
		    argv[argc++] = args+i;
	        }
	    }
	    XFree(args);
	}
    }
}

void
scan_timeout(XtPointer client_data, XtIntervalId *id)
{
    scan_timer = 0;
    
    /* check */
    if (f_drv & CAN_TUNE)
	return;
    if (drv->is_tuned(h_drv))
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
do_fullscreen(void)
{
    static Dimension x,y,w,h;
    static int timeout,interval,prefer_blanking,allow_exposures,rpx,rpy;
    static int warp_pointer;
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

	if (warp_pointer)
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
	    fprintf(stderr,"%d x %d\n",vp_width,vp_height);
	    if (vp_width < sheight || vp_width < swidth) {
		/* move viewpoint, make sure the pointer is in there */
		warp_pointer = 1;
		XWarpPointer(dpy, None, RootWindowOfScreen(XtScreen(tv)),
			     0, 0, 0, 0, vp_width/2, vp_height/2);
		XF86VidModeSetViewPort(dpy,XDefaultScreen(dpy),0,0);
	    }
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

#ifdef HAVE_LIBXINERAMA
	if (nxinerama) {
	    /* check which physical screen we are visible on */
	    int i;
	    for (i = 0; i < nxinerama; i++) {
		if (x >= xinerama[i].x_org &&
		    y >= xinerama[i].y_org && 
		    x <  xinerama[i].x_org + xinerama[i].width &&
		    y <  xinerama[i].y_org + xinerama[i].height) {
		    vp_x      = xinerama[i].x_org;
		    vp_y      = xinerama[i].y_org;
		    vp_width  = xinerama[i].width;
		    vp_height = xinerama[i].height;
		    break;
		}
	    }
	}
#endif

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

	if (warp_pointer)
	    XWarpPointer(dpy, None, XtWindow(tv), 0, 0, 0, 0, 30, 15);
	fs = 1;
    }
    XtAppAddWorkProc (app_context, MyResize, NULL);
}

void button_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct CHANNEL *channel = clientdata;
    do_va_cmd(2,"setstation",channel->name);
}

void create_chanwin(void)
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
channel_menu(void)
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

    if (zap_fast && !cur_attrs[ATTR_ID_MUTE]) {
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

    stay_on_top++;
    if (stay_on_top == 2)
	stay_on_top = -1;
    fprintf(stderr,"layer: %d\n",stay_on_top);
	
    wm_stay_on_top(dpy,XtWindow(app_shell),stay_on_top);
    wm_stay_on_top(dpy,XtWindow(on_shell),stay_on_top);
    for (i = 0; i < TOPLEVELS; i++)
	wm_stay_on_top(dpy,XtWindow(*(my_toplevels[i].shell)),
		       (stay_on_top == -1) ? 0 : stay_on_top);
}

/*--- option window ------------------------------------------------------*/

void
build_menus(void)
{
    Boolean sensitive;
    int i;

    /* drivers */
    if (NULL == m_movie_driver) {
	for (i = 0; NULL != ng_writers[i]; i++)
	    ;
	m_movie_driver = malloc(sizeof(struct STRTAB)*(i+1));
	memset(m_movie_driver,0,sizeof(struct STRTAB)*(i+1));
	for (i = 0; NULL != ng_writers[i]; i++) {
	    m_movie_driver[i].nr  = i;
	    m_movie_driver[i].str = ng_writers[i]->desc;
	}
    }

    /* audio formats */
    for (i = 0; NULL != ng_writers[movie_driver]->audio[i].name; i++)
	;
    if (m_movie_audio)
	free(m_movie_audio);
    m_movie_audio = malloc(sizeof(struct STRTAB)*(i+2));
    memset(m_movie_audio,0,sizeof(struct STRTAB)*(i+2));
    for (i = 0; NULL != ng_writers[movie_driver]->audio[i].name; i++) {
	m_movie_audio[i].nr  = i;
	m_movie_audio[i].str = ng_writers[movie_driver]->audio[i].desc ?
	    ng_writers[movie_driver]->audio[i].desc : 
	    ng_afmt_to_desc[ng_writers[movie_driver]->audio[i].fmtid];
    }
    m_movie_audio[i].nr  = i;
    m_movie_audio[i].str = "no sound";
    movie_audio = 0;

    /* video formats */
    for (i = 0; NULL != ng_writers[movie_driver]->video[i].name; i++)
	;
    if (m_movie_video)
	free(m_movie_video);
    m_movie_video = malloc(sizeof(struct STRTAB)*(i+1));
    memset(m_movie_video,0,sizeof(struct STRTAB)*(i+1));
    for (i = 0; NULL != ng_writers[movie_driver]->video[i].name; i++) {
	m_movie_video[i].nr  = i;
	m_movie_video[i].str = ng_writers[movie_driver]->video[i].desc ?
	    ng_writers[movie_driver]->video[i].desc : 
	    ng_vfmt_to_desc[ng_writers[movie_driver]->video[i].fmtid];
    }
    movie_video = 0;

    /* need audio filename? */
    sensitive = ng_writers[movie_driver]->combined ? False: True;
    XtVaSetValues(w_movie_flabel,
		  XtNsensitive,sensitive,
		  NULL);
    XtVaSetValues(w_movie_faudio,
		  XtNsensitive,sensitive,
		  NULL);
}

#define PANED_FIX               \
        XtNallowResize, False,  \
        XtNshowGrip,    False,  \
        XtNskipAdjust,  True

struct DO_CMD cmd_fs   = { 1, { "fullscreen",        NULL }};
struct DO_CMD cmd_mute = { 2, { "volume",  "mute",   NULL }};
struct DO_CMD cmd_cap  = { 2, { "capture", "toggle", NULL }};
struct DO_CMD cmd_jpeg = { 2, { "snap",    "jpeg",   NULL }};
struct DO_CMD cmd_ppm  = { 2, { "snap",    "ppm",    NULL }};

struct DO_AC  ac_fs    = { 0, "FullScreen", { NULL }};
struct DO_AC  ac_top   = { 0, "StayOnTop",  { NULL }};

struct DO_AC  ac_avi   = { 1, "Popup",      { "streamer", NULL }};
struct DO_AC  ac_chan  = { 1, "Popup",      { "channels", NULL }};
struct DO_AC  ac_conf  = { 1, "Popup",      { "config",   NULL }};
struct DO_AC  ac_launch = { 1, "Popup",      { "launcher",  NULL }};
struct DO_AC  ac_zap   = { 0, "Zap",        { NULL }};

void menu_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct ng_attribute *attr;
    long  cd = (long)clientdata;
    int   j;

    switch (cd) {
#if 0
    case 10:
	attr = ng_attr_byid(a_drv,ATTR_ID_NORM);
	if (-1 != (j=popup_menu(widget,"TV Norm",attr->choices)))
	    do_va_cmd(2,"setnorm",ng_attr_getstr(attr,j));
	break;
    case 11:
	attr = ng_attr_byid(a_drv,ATTR_ID_INPUT);
	if (-1 != (j=popup_menu(widget,"Video Source",attr->choices)))
	    do_va_cmd(2,"setinput",ng_attr_getstr(attr,j));
	break;
#endif
    case 12:
	if (-1 != (j=popup_menu(widget,"Freq table",chanlist_names)))
	    do_va_cmd(2,"setfreqtab",chanlist_names[j].str);
	break;
    case 13:
	attr = ng_attr_byid(a_drv,ATTR_ID_AUDIO_MODE);
	if (NULL != attr) {
	    int i,mode = drv->read_attr(h_drv,attr);
	    for (i = 1; attr->choices[i].str != NULL; i++) {
		attr->choices[i].nr =
		    (1 << (i-1)) & mode ? (1 << (i-1)) : -1;
	    }
	    if (-1 != (j=popup_menu(widget,"Audio",attr->choices)))
		change_audio(attr->choices[j].nr);
	}
	break;
    case 14:
	if (-1 != (j=popup_menu(widget,"Capture",cap_list)))
	    do_va_cmd(2,"capture",cap_list[j].str);
	break;

    case 20:
	if (-1 != (j=popup_menu(widget,MOVIE_DRIVER,m_movie_driver)) &&
	    movie_driver != j) {
	    movie_driver = m_movie_driver[j].nr;
	    set_menu_val(w_movie_driver,MOVIE_DRIVER,
			 m_movie_driver,movie_driver);
	    build_menus();
	    movie_audio = 0;
	    movie_video = 0;
	    set_menu_val(w_movie_audio,MOVIE_AUDIO,
			 m_movie_audio,movie_audio);
	    set_menu_val(w_movie_video,MOVIE_VIDEO,
			 m_movie_video,movie_video);
	}
	break;
    case 21:
	if (-1 != (j=popup_menu(widget,MOVIE_AUDIO,m_movie_audio))) {
	    movie_audio = m_movie_audio[j].nr;
	    set_menu_val(w_movie_audio,MOVIE_AUDIO,
			 m_movie_audio,movie_audio);
	}
	break;
    case 22:
	if (-1 != (j=popup_menu(widget,MOVIE_RATE,m_movie_rate))) {
	    movie_rate = m_movie_rate[j].nr;
	    set_menu_val(w_movie_rate,MOVIE_RATE,
			 m_movie_rate,movie_rate);
	}
	break;
    case 23:
	if (-1 != (j=popup_menu(widget,MOVIE_VIDEO,m_movie_video))) {
	    movie_video = m_movie_video[j].nr;
	    set_menu_val(w_movie_video,MOVIE_VIDEO,
			 m_movie_video,movie_video);
	}
	break;
    case 24:
	if (-1 != (j=popup_menu(widget,MOVIE_FPS,m_movie_fps))) {
	    movie_fps = m_movie_fps[j].nr;
	    set_menu_val(w_movie_fps,MOVIE_FPS,
			 m_movie_fps,movie_fps);
	}
	break;
    default:
    }
}

void jump_scb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct xaw_attribute *a = clientdata;
    const char *name;
    char val[16];
    int  value;

    value = (int)(*(float*)call_data * 65535);
    if (value > 65535) value = 65535;
    if (value < 0)     value = 0;
    sprintf(val,"%d",value);

    if (a) {
	name = a->attr->name;
	do_va_cmd(3,"setattr",name,val);
    } else {
	name  = XtName(XtParent(widget));
	do_va_cmd(2,name,val);
    }
}

void scroll_scb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    long      move = (long)call_data;
    Dimension length;
    float    shown,top1,top2;

    XtVaGetValues(widget,
		  XtNlength,     &length,
		  XtNshown,      &shown,
		  XtNtopOfThumb, &top1,
		  NULL);

    top2 = top1 + (float)move/length/5;
    if (top2 < 0) top2 = 0;
    if (top2 > 1) top2 = 1;
#if 0
    fprintf(stderr,"scroll by %d\tlength %d\tshown %f\ttop %f => %f\n",
	    move,length,shown,top1,top2);
#endif
    jump_scb(widget,clientdata,&top2);
}

static void
attr_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct xaw_attribute *a = clientdata;
    int j;

    switch (a->attr->type) {
    case ATTR_TYPE_CHOICE:
	j=popup_menu(widget,a->attr->name,a->attr->choices);
	if (-1 != j)
	    do_va_cmd(3,"setattr",a->attr->name,ng_attr_getstr(a->attr,j));
	break;
    case ATTR_TYPE_BOOL:
	do_va_cmd(3,"setattr",a->attr->name,"toggle");
	break;
    }
}

static void
add_attr_option(Widget paned, struct ng_attribute *attr)
{
    struct xaw_attribute *a;
    Widget p,l;

    a = malloc(sizeof(*a));
    memset(a,0,sizeof(*a));
    a->attr = attr;
    
    switch (attr->type) {
    case ATTR_TYPE_BOOL:
    case ATTR_TYPE_CHOICE:
	a->cmd = XtVaCreateManagedWidget(attr->name,
					 commandWidgetClass, paned,
					 PANED_FIX,
					 NULL);
	XtAddCallback(a->cmd,XtNcallback,attr_cb,a);
	break;
    case ATTR_TYPE_INTEGER:
	p = XtVaCreateManagedWidget(attr->name,
				    panedWidgetClass, paned,
				    XtNorientation, XtEvertical,
				    PANED_FIX,
				    NULL);
	l = XtVaCreateManagedWidget("l",labelWidgetClass, p,
				    XtNshowGrip, False,
				    NULL);
	a->scroll = XtVaCreateManagedWidget("s",scrollbarWidgetClass,p,
					    PANED_FIX,
					    NULL);
	XtAddCallback(a->scroll, XtNjumpProc,   jump_scb,   a);
	XtAddCallback(a->scroll, XtNscrollProc, scroll_scb, a);
	if (attr->id >= ATTR_ID_COUNT)
	    XtVaSetValues(l,XtNlabel,attr->name,NULL);
	break;
    }
    a->next = xaw_attrs;
    xaw_attrs = a;
}

void create_optwin(void)
{
    struct ng_attribute *attr;
    Widget c,p,l;

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
    
    c = XtVaCreateManagedWidget("fs", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,command_cb,(XtPointer)&cmd_fs);

    c = XtVaCreateManagedWidget("grabppm", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,command_cb,(XtPointer)&cmd_ppm);
    c = XtVaCreateManagedWidget("grabjpeg", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,command_cb,(XtPointer)&cmd_jpeg);
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

    /* menus / multiple choice options */
    attr = ng_attr_byid(a_drv,ATTR_ID_NORM);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(a_drv,ATTR_ID_INPUT);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);

    if (f_drv & CAN_TUNE) {
	c_freq = XtVaCreateManagedWidget("freq", commandWidgetClass, opt_paned,
					 PANED_FIX, NULL);
	XtAddCallback(c_freq,XtNcallback,menu_cb,(XtPointer)12);
    }

    if (NULL != ng_attr_byid(a_drv,ATTR_ID_AUDIO_MODE)) {
	c_audio = XtVaCreateManagedWidget("audio",commandWidgetClass,opt_paned,
					  PANED_FIX, NULL);
	XtAddCallback(c_audio,XtNcallback,menu_cb,(XtPointer)13);
    }
    c_cap = XtVaCreateManagedWidget("cap", commandWidgetClass, opt_paned,
				    PANED_FIX, NULL);
    XtAddCallback(c_cap,XtNcallback,menu_cb,(XtPointer)14);

    for (attr = a_drv; attr->name != NULL; attr++) {
	if (attr->id < ATTR_ID_COUNT)
	    continue;
	if (attr->type != ATTR_TYPE_CHOICE)
	    continue;
	add_attr_option(opt_paned,attr);
    }
    
    /* integer options */
    attr = ng_attr_byid(a_drv,ATTR_ID_BRIGHT);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(a_drv,ATTR_ID_HUE);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(a_drv,ATTR_ID_CONTRAST);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(a_drv,ATTR_ID_COLOR);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    
    if (have_mixer ||
	NULL != ng_attr_byid(a_drv,ATTR_ID_VOLUME)) {
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

    for (attr = a_drv; attr->name != NULL; attr++) {
	if (attr->id < ATTR_ID_COUNT)
	    continue;
	if (attr->type != ATTR_TYPE_INTEGER)
	    continue;
	add_attr_option(opt_paned,attr);
    }

    /* boolean options */
    for (attr = a_drv; attr->name != NULL; attr++) {
	if (attr->id < ATTR_ID_COUNT)
	    continue;
	if (attr->type != ATTR_TYPE_BOOL)
	    continue;
	add_attr_option(opt_paned,attr);
    }

    /* quit */
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
    if (debug) {
	if (-1 == pid) {
	    perror("waitpid");
	} else if (0 == pid) {
	    fprintf(stderr,"oops: got sigchild and waitpid returns 0 ???\n");
	} else if (WIFEXITED(stat)){
	    fprintf(stderr,"[%d]: normal exit (%d)\n",pid,WEXITSTATUS(stat));
	} else if (WIFSIGNALED(stat)){
	    fprintf(stderr,"[%d]: %s\n",pid,strsignal(WTERMSIG(stat)));
	} else if (WIFSTOPPED(stat)){
	    fprintf(stderr,"[%d]: %s\n",pid,strsignal(WSTOPSIG(stat)));
	}
    }
    if (pid == str_pid && !WIFSTOPPED(stat))
	str_pid = -1;
}

static void
exec_output(XtPointer data, int *fd, XtInputId * iproc)
{
    char buffer[81];
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
	fprintf(stderr,"%s",buffer);
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

static Boolean
rec_work(XtPointer client_data)
{
    movie_grab_put_video(movie_state);
    return False;
}

void
exec_record(Widget widget, XtPointer client_data, XtPointer calldata)
{
    if (!(f_drv & CAN_CAPTURE)) {
	fprintf(stderr,"grabbing: not supported\n");
	return;
    }

    if (rec_work_id) {
	do_va_cmd(2,"movie","stop");
    } else {
	do_va_cmd(2,"movie","start");
    }
    return;
}

void
do_movie_record(int argc, char **argv)
{
    char *fvideo,*faudio;
    struct ng_video_fmt video;
    struct ng_audio_fmt audio;
    const struct ng_writer *wr;
    int i;

    /* set parameters */
    if (argc > 1 && 0 == strcasecmp(argv[0],"driver")) {
	for (i = 0; m_movie_driver[i].str != NULL; i++)
	    if (0 == strcasecmp(argv[1],m_movie_driver[i].str))
		movie_driver = m_movie_driver[i].nr;
	set_menu_val(w_movie_driver,MOVIE_DRIVER,
		     m_movie_driver,movie_driver);
	build_menus();
	movie_audio = 0;
	movie_video = 0;
	set_menu_val(w_movie_audio,MOVIE_AUDIO,
		     m_movie_audio,movie_audio);
	set_menu_val(w_movie_video,MOVIE_VIDEO,
		     m_movie_video,movie_video);
	return;
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"fvideo")) {
	XtVaSetValues(w_movie_fvideo,XtNstring,argv[1],NULL);
	return;
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"faudio")) {
	XtVaSetValues(w_movie_faudio,XtNstring,argv[1],NULL);
	return;
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"audio")) {
	for (i = 0; m_movie_audio[i].str != NULL; i++)
	    if (0 == strcasecmp(argv[1],m_movie_audio[i].str))
		movie_audio = m_movie_audio[i].nr;
	set_menu_val(w_movie_audio,MOVIE_AUDIO,
		     m_movie_audio,movie_audio);
	return;
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"rate")) {
	for (i = 0; m_movie_rate[i].str != NULL; i++)
	    if (atoi(argv[1]) == m_movie_rate[i].nr)
		movie_rate = m_movie_rate[i].nr;
	set_menu_val(w_movie_rate,MOVIE_RATE,
		     m_movie_rate,movie_rate);
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"video")) {
	for (i = 0; m_movie_video[i].str != NULL; i++)
	    if (0 == strcasecmp(argv[1],m_movie_video[i].str))
		movie_video = m_movie_video[i].nr;
	set_menu_val(w_movie_video,MOVIE_VIDEO,
		     m_movie_video,movie_video);
	return;
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"fps")) {
	for (i = 0; m_movie_fps[i].str != NULL; i++)
	    if (atoi(argv[1]) == m_movie_fps[i].nr)
		movie_fps = m_movie_fps[i].nr;
	set_menu_val(w_movie_fps,MOVIE_FPS,
		     m_movie_fps,movie_fps);
    }

    /* start */
    if (argc > 0 && 0 == strcasecmp(argv[0],"start")) {
	if (0 != cur_movie)
	    return; /* records already */
	cur_movie = 1;
	grabdisplay_suspend();

	XtVaGetValues(w_movie_fvideo,XtNstring,&fvideo,NULL);
	XtVaGetValues(w_movie_faudio,XtNstring,&faudio,NULL);
	fvideo = tilde_expand(fvideo);
	faudio = tilde_expand(faudio);

	memset(&video,0,sizeof(video));
	memset(&audio,0,sizeof(audio));

	wr = ng_writers[movie_driver];
	video.fmtid  = wr->video[movie_video].fmtid;
	video.width  = win_width;
	video.height = win_height;
	if (NULL != wr->audio[movie_audio].name) {
	    audio.fmtid  = wr->audio[movie_audio].fmtid;
	    audio.rate   = movie_rate;
	} else {
	    audio.fmtid  = AUDIO_NONE;
	}

	movie_state = movie_writer_init
	    (fvideo, faudio, wr,
	     &video, wr->video[movie_video].priv, movie_fps,
	     &audio, wr->audio[movie_audio].priv,16);
	if (NULL == movie_state) {
	    /* init failed */
	    grabdisplay_restart();
	    cur_movie = 0;
	    /* hmm, not the most elegant way to flag an error ... */
	    XtVaSetValues(w_movie_status,XtNlabel,"error",NULL);
	    return;
	}
	movie_writer_start(movie_state);
	rec_work_id  = XtAppAddWorkProc(app_context,rec_work,NULL);
	XtVaSetValues(w_movie_status,XtNlabel,"recording",NULL);
	return;
    }
    
    /* stop */
    if (argc > 0 && 0 == strcasecmp(argv[0],"stop")) {
	if (0 == cur_movie)
	    return; /* nothing to stop here */

	movie_writer_stop(movie_state);
	XtRemoveWorkProc(rec_work_id);
	rec_work_id = 0;
	XtVaSetValues(w_movie_status,XtNlabel,"",NULL);
	grabdisplay_restart();
	cur_movie = 0;
	return;
    }
}

void
exec_xanim_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    static char *command = "xanim +f +Sr +Ze -Zr";
    char *filename,*cmd;
    char **argv;
    int  argc;
    
    /* filename */
    XtVaGetValues(w_movie_fvideo,XtNstring,&filename,NULL);
    filename = tilde_expand(filename);

    /* go! */
    cmd = malloc(strlen(command)+strlen(filename)+5);
    sprintf(cmd,"%s %s",command,filename);
    argv = split_cmdline(cmd,&argc);
    exec_x11(argv);
}

#define FIX_LEFT_TOP        \
    XtNleft,XawChainLeft,   \
    XtNright,XawChainRight, \
    XtNtop,XawChainTop,     \
    XtNbottom,XawChainTop

void
create_strwin(void)
{
    Widget form,label,button,text;

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

    /* driver */
    button = XtVaCreateManagedWidget("driver", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     NULL);
    w_movie_driver = button;

    /* movie filename */
    label = XtVaCreateManagedWidget("vlabel", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert, button,
				    NULL);
    text = XtVaCreateManagedWidget("vname", asciiTextWidgetClass, form,
				   FIX_LEFT_TOP,
				   XtNfromVert, label,
				   NULL);
    w_movie_fvideo = text;
    
    /* audio filename */
    label = XtVaCreateManagedWidget("alabel", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert, text,
				    NULL);
    w_movie_flabel = label;
    text= XtVaCreateManagedWidget("aname", asciiTextWidgetClass, form,
				  FIX_LEFT_TOP,
				  XtNfromVert, label,
				  NULL);
    w_movie_faudio = text;

    /* audio format */
    button = XtVaCreateManagedWidget("audio", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, text,
				     NULL);
    w_movie_audio = button;
    button = XtVaCreateManagedWidget("rate", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, button,
				     NULL);
    w_movie_rate = button;

    /* video format */
    button = XtVaCreateManagedWidget("video", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, button,
				     NULL);
    w_movie_video = button;
    button = XtVaCreateManagedWidget("fps", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, button,
				     NULL);
    w_movie_fps = button;
    label = XtVaCreateManagedWidget("size", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert, button,
				    NULL);
    w_movie_size = label;

    /* status line */
    label = XtVaCreateManagedWidget("status", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert, label,
				    XtNlabel,    "",
				    NULL);
    w_movie_status = label;

    /* cmd buttons */
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

    build_menus();
    XtAddCallback(w_movie_driver,XtNcallback,menu_cb,(XtPointer)20);
    set_menu_val(w_movie_driver,MOVIE_DRIVER,m_movie_driver,movie_driver);

    XtAddCallback(w_movie_audio,XtNcallback,menu_cb,(XtPointer)21);
    set_menu_val(w_movie_audio,MOVIE_AUDIO,m_movie_audio,movie_audio);
    XtAddCallback(w_movie_rate,XtNcallback,menu_cb,(XtPointer)22);
    set_menu_val(w_movie_rate,MOVIE_RATE,m_movie_rate,movie_rate);

    XtAddCallback(w_movie_video,XtNcallback,menu_cb,(XtPointer)23);
    set_menu_val(w_movie_video,MOVIE_VIDEO,m_movie_video,movie_video);
    XtAddCallback(w_movie_fps,XtNcallback,menu_cb,(XtPointer)24);
    set_menu_val(w_movie_fps,MOVIE_FPS,m_movie_fps,movie_fps);
}

/*--- launcher window -----------------------------------------------------*/

void
create_launchwin(void)
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
segfault(int signal)
{
    fprintf(stderr,"[pid=%d] segfault catched\n",getpid());
    exit(1);
}


static void
siginit(void)
{
    struct sigaction act,old;

    memset(&act,0,sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_handler  = exec_done;
    sigaction(SIGCHLD,&act,&old);
    if (debug) {
	act.sa_handler  = segfault;
	sigaction(SIGSEGV,&act,&old);
	fprintf(stderr,"main thread [pid=%d]\n",getpid());
    }
}

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
hello_world(void)
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
	    "  -v, -debug n        debug level n, n = [0..2]\n"
	    "      -remote         assume remote display\n"
	    "  -n  -noconf         don't read the config file\n"
	    "  -m  -nomouse        startup with mouse pointer disabled\n"
	    "  -f  -fullscreen     startup in fullscreen mode\n"
	    "      -dga/-nodga     enable/disable DGA extention\n"
	    "      -vm/-novm       enable/disable VidMode extention\n"
	    "      -xv/-noxv       enable/disable Xvideo extention (for video overlay)\n"
	    "      -scale/-noscale enable/disable Xvideo extention (for image scaling)\n"
	    "                      you might need that if your hardware does support\n"
	    "                      neither hardware overlay nor yuv capture\n"
	    "  -b  -bpp n          color depth of the display is n (n=24,32)\n"
	    "  -o  -outfile file   filename base for snapshots\n"
	    "  -c  -device file    use <file> as video4linux device\n"
	    "      -shift x        shift display by x bytes\n"
	    "      -fb             let fb (not X) set up v4l device\n"
	    "  -h  -help           print this text\n"
	    "station:\n"
	    "  this is one of the stations listed in $HOME/.xawtv\n"
	    "\n"
	    "--\n"
	    "Gerd Knorr <kraxel@goldbach.in-berlin.de>\n");
}

int
main(int argc, char *argv[])
{
    Dimension      w;
    int            i;
    unsigned long  freq;

    if (0 == geteuid() && 0 != getuid()) {
	fprintf(stderr,"xawtv /must not/ be installed suid root\n");
	exit(1);
    }
    
    hello_world();
    progname = strdup(argv[0]);

    /* toplevel */
    /* XInitThreads(); */
    app_shell = XtVaAppInitialize(&app_context,
				  "Xawtv",
				  opt_desc, opt_count,
				  &argc, argv,
				  NULL /* fallback_res */,
				  NULL);
    dpy = XtDisplay(app_shell);

    /* command line args */
    XtGetApplicationResources(app_shell,&args,
			      args_desc,args_count,
			      NULL,0);
    if (args.help) {
	usage();
	exit(0);
    }
    snapbase = args.basename;
    debug    = args.debug;
    ng_debug = args.debug;
    ng_init();
    
    /* look for a useful visual */
    visual_init("xawtv","Xawtv");

    /* remote display? */
    do_overlay = !args.remote;
    if (do_overlay)
	x11_check_remote();
    v4lconf_init();

    /* x11 stuff */
    XtAppAddActions(app_context,actionTable,
		    sizeof(actionTable)/sizeof(XtActionsRec));
    x11_misc_init();
    if (debug)
	fprintf(stderr,"main: dga extention...\n");
    xfree_dga_init();
    if (debug)
	fprintf(stderr,"main: xinerama extention...\n");
    xfree_xinerama_init();
    if (debug)
	fprintf(stderr,"main: xvideo extention...\n");
    xv_init(args.xv_video,args.xv_scale,args.xv_port);

    /* set hooks (command.c) */
    update_title        = new_title;
    display_message     = new_message;
    vtx_message         = display_vtx;
    attr_notify         = new_attr;
    volume_notify       = new_volume;
    freqtab_notify      = new_freqtab;
    setfreqtab_notify   = new_freqtab;
    setstation_notify   = new_channel;
    set_capture_hook    = do_capture;
    fullscreen_hook     = do_fullscreen;
    movie_hook          = do_movie_record;
    exit_hook           = do_exit;
    capture_get_hook    = grabdisplay_suspend;
    capture_rel_hook    = grabdisplay_restart;
    channel_switch_hook = pixit;
    
    if (debug)
	fprintf(stderr,"main: init main window...\n");
    tv = video_init(app_shell,&vinfo,simpleWidgetClass);
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
    if (debug)
	fprintf(stderr,"main: install signal handlers...\n");
    siginit();
    if (NULL == drv) {
	if (debug)
	    fprintf(stderr,"main: open grabber device...\n");
	grabber_init();
    }

    XSetIOErrorHandler(x11_ctrl_alt_backspace);
    if (debug)
	fprintf(stderr,"main: checking wm...\n");
    wm_detect(dpy);
    if (debug)
	fprintf(stderr,"main: creating windows ...\n");
    create_optwin();
    create_onscreen();
    create_vtx();
    create_chanwin();
    create_confwin();
    create_strwin();
    create_launchwin();

    /* read config file */
    if (debug)
	fprintf(stderr,"main: read config file ...\n");
    if (args.readconfig)
	read_config();
    channel_menu();
    if (fs_width && fs_height && !args.vidmode) {
	if (debug)
	    fprintf(stderr,"fullscreen mode configured (%dx%d), "
		    "VidMode extention enabled\n",fs_width,fs_height);
	args.vidmode = 1;
    }
    if (debug)
	fprintf(stderr,"main: checking for vidmode extention ...\n");
    xfree_vm_init();

    /* lirc support */
    if (debug)
	fprintf(stderr,"main: checking for lirc ...\n");
    if (-1 != (lirc = lirc_tv_init()))
	XtAppAddInput(app_context,lirc,(XtPointer)XtInputReadMask,lirc_input,NULL);

    if (debug)
	fprintf(stderr,"main: mapping main window ...\n");
    XtRealizeWidget(app_shell);
    create_pointers(app_shell);
    create_bitmaps(app_shell);
    XDefineCursor(dpy, XtWindow(app_shell), left_ptr);
    XSetWMProtocols(XtDisplay(app_shell), XtWindow(app_shell),
		    &wm_delete_window, 1);

    XtVaSetValues(app_shell,
		  XtNwidthInc,  WIDTH_INC,
		  XtNheightInc, HEIGHT_INC,
		  XtNminWidth,  WIDTH_INC,
		  XtNminHeight, HEIGHT_INC,
		  NULL);
    XtVaSetValues(chan_shell,
		  XtNwidth,pix_width*pix_cols+30,
		  NULL);

    /* mouse pointer magic */
    XtAddEventHandler(tv, PointerMotionMask, True, mouse_event, NULL);
    mouse_event(tv,NULL,NULL,NULL);

    /* init hardware */
    if (debug)
	fprintf(stderr,"main: initialize hardware ...\n");
    attr_init();
    audio_on();
    audio_init();
    do_va_cmd(2,"setfreqtab",chanlist_names[chantab].str);

    cur_capture = 0;
    do_va_cmd(2,"capture","overlay");
    set_property(0,NULL,NULL);
    if (optind+1 == argc) {
	do_va_cmd(2,"setstation",argv[optind]);
    } else {
	if ((f_drv & CAN_TUNE) && 0 != (freq = drv->getfreq(h_drv))) {
	    for (i = 0; i < chancount; i++)
		if (chanlist[i].freq == freq*1000/16) {
		    do_va_cmd(2,"setchannel",chanlist[i].name);
		    break;
		}
	}
	if (-1 == cur_channel) {
	    if (count > 0) {
		if (debug)
		    fprintf(stderr,"main: tuning first station\n");
		do_va_cmd(2,"setstation","0");
	    } else {
		if (debug)
		    fprintf(stderr,"main: setting defaults\n");
		set_defaults();
	    }
	} else {
	    if (debug)
		fprintf(stderr,"main: known station tuned, not changing\n");
	}
    }

#if 0
    if (0 != grabber->colorkey)
	XtAddEventHandler(tv,ExposureMask, True, tv_expose_event, NULL);
#endif

    if (args.fullscreen) {
	do_fullscreen();
    } else {
	XtAppAddWorkProc(app_context,MyResize,NULL);
    }

    sprintf(modename,"%dx%d, ",
	    XtScreen(app_shell)->width,XtScreen(app_shell)->height);
    strcat(modename,ng_vfmt_to_desc[x11_native_format]);
    new_message(modename);
    if (!have_config)
	XtCallActionProc(tv,"Help",NULL,NULL,0);

    signal(SIGHUP,SIG_IGN); /* don't really need a tty ... */
    XtAppMainLoop(app_context);

    /* keep compiler happy */
    return 0;
}
