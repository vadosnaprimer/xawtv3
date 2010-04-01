/*
 * main.c  --  (c) 1997 Gerd Knorr <kraxel@cs.tu-berlin.de>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <sys/time.h>

#include "config.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xmd.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xlib.h>
#include <X11/Shell.h>
#include <X11/Xaw/XawInit.h>
#include <X11/Xaw/Paned.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/Box.h>
#ifdef HAVE_LIBXXF86DGA
# include <X11/extensions/xf86dga.h>
# include <X11/extensions/xf86dgastr.h>
#endif
#ifdef HAVE_LIBXXF86VM
# include <X11/extensions/xf86vmode.h>
# include <X11/extensions/xf86vmstr.h>
#endif

#ifdef HAVE_LIBJPEG
# include "jpeglib.h"
#endif

#include "mixer.h"
#include "channel.h"
#include "channels.h"
#include "grab.h"
#include "x11.h"
#include "toolbox.h"

#define TITLE_TIME          6000
#define ZAP_TIME            8000
#define CAP_TIME              20
#define WIDTH_INC             64
#define HEIGHT_INC            48
#define LABEL_WIDTH         "16"
#define VIDMODE_DELAY        100   /* 0.1 sec */

/*--- public variables ----------------------------------------------------*/

XtAppContext      app_context;
Widget            app_shell, tv;
Widget            opt_shell, opt_paned, chan_shell;
Widget            c_norm,c_input,c_freq,c_audio,c_cap;
Widget            s_bright,s_color,s_hue,s_contrast,s_volume;
Display           *dpy;
XtWorkProcId      idle_id;

Atom              wm_protocols[2];
XtIntervalId      title_timer, audio_timer, zap_timer;
int               pointer_on = 1;
int               debug = 0;
int               fs = 0;
int               fs_width,fs_height,fs_xoff,fs_yoff;
int               pix_width=128,pix_height=96;
int               bpp = 0;
char              *ppmfile;
char              *jpegfile;
int               zap_start,zap_fast;

char              modename[64];
char              *progname;
int               xfree_ext = 1;
int               have_dga = 0;
int               have_vm = 0;
#ifdef HAVE_LIBXXF86VM
int               vm_count;
XF86VidModeModeInfo **vm_modelines;
#endif

/*                            PAL  NTSC SECAM */
static int    maxwidth[]  = { 768, 640, 768 };
static int    maxheight[] = { 576, 480, 576 };


/*--- drivers -------------------------------------------------------------*/

extern struct GRABBER grab_v4l;
struct GRABBER *grabbers[] = {
    &grab_v4l,
};

int grabber;

/*--- channels ------------------------------------------------------------*/

struct STRTAB    *cmenu    = NULL;
char  title[256];

int cur_color;
int cur_bright;
int cur_hue;
int cur_contrast;
int cur_capture;

int cur_mute   = 0;
int cur_volume = 65535;

/*--- actions -------------------------------------------------------------*/

void CloseMainAction(Widget, XEvent*, String*, Cardinal*);
void SetResAction(Widget, XEvent*, String*, Cardinal*);
void SetChannelAction(Widget, XEvent*, String*, Cardinal*);
void TuneAction(Widget, XEvent*, String*, Cardinal*);
void ChannelAction(Widget, XEvent*, String*, Cardinal*);
void VolumeAction(Widget, XEvent*, String*, Cardinal*);
void PointerAction(Widget, XEvent*, String*, Cardinal*);
void FullScreenAction(Widget, XEvent*, String*, Cardinal*);
void OptionsAction(Widget, XEvent*, String*, Cardinal*);
void ChannelsAction(Widget, XEvent*, String*, Cardinal*);
void ZapAction(Widget, XEvent*, String*, Cardinal*);
void SnapAction(Widget, XEvent*, String*, Cardinal*);

static XtActionsRec actionTable[] = {
    { "CloseMain",   CloseMainAction  },
    { "SetRes",      SetResAction },
    { "SetChannel",  SetChannelAction },
    { "Tune",        TuneAction },
    { "Channel",     ChannelAction },
    { "Volume",      VolumeAction },
    { "Pointer",     PointerAction },
    { "FullScreen",  FullScreenAction },
    { "Options",     OptionsAction },
    { "Channels",    ChannelsAction },
    { "Zap",         ZapAction },
    { "Snap",        SnapAction },
};

static struct STRTAB stereo[] = {
    {  0, "auto"    },
    {  1, "mono"    },
    {  2, "stereo"  },
    {  3, "lang1"   },
    {  4, "lang2"   },
    { -1, NULL,     },
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
    video_overlay(NULL);
    video_close();
    if (have_mixer)
	mixer_close();
    if (fs)
	XtCallActionProc(widget,"FullScreen",NULL,NULL,0);
    XtAppAddWorkProc (app_context,ExitWP, NULL);
    XtDestroyWidget(app_shell);
}

void
CloseMainAction(Widget widget, XEvent *event,
		String *params, Cardinal *num_params)
{
    static Dimension x,y,w,h;
    char *argv[32];
    int   argc = 0;

    if (event->type == ClientMessage) {
	if (event->xclient.data.l[0] == wm_protocols[1]) {
	    if (debug)
		fprintf(stderr,"Main: wm_save_yourself\n");

	    argv[argc++] = progname;

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
	    
	    /* channel */
	    argv[argc++] = strdup("-c");
	    sprintf(argv[argc++] = malloc(8),"%d",cur_sender);

	    /* grab filename */
	    if (ppmfile) {
		argv[argc++] = strdup("-o");
		argv[argc++] = malloc(256);
		argv[argc-1][0] = '\0';
		if (ppmfile[0] != '/') {
		    getcwd(argv[argc-1],128);
		    strcat(argv[argc-1],"/");
		}
		strcat(argv[argc-1],ppmfile);
	    }
	    if (jpegfile) {
		argv[argc++] = strdup("-j");
		argv[argc++] = malloc(256);
		argv[argc-1][0] = '\0';
		if (jpegfile[0] != '/') {
		    getcwd(argv[argc-1],128);
		    strcat(argv[argc-1],"/");
		}
		strcat(argv[argc-1],jpegfile);
	    }
	    
	    /* options */
	    if (!xfree_ext)
		argv[argc++] = strdup("-x");
	    if (!pointer_on)
		argv[argc++] = strdup("-m");
	    if (bpp) {
		argv[argc++] = strdup("-b");
		sprintf(argv[argc++] = malloc(8),"%d",bpp);
	    }

	    XSetCommand(XtDisplay(app_shell), XtWindow(app_shell), argv, argc);
	    return;
	}
    }
    ExitCB(widget,NULL,NULL);
}

/*--- tv -----------------------------------------------------------------*/

static Boolean
idle_grabdisplay(XtPointer data)
{
    static long count,lastsec;
    struct timeval  tv;
    struct timezone tz;

    if (debug) {
	gettimeofday(&tv,&tz);
	if (tv.tv_sec != lastsec) {
	    if (lastsec == tv.tv_sec-1)
		fprintf(stderr,"%5ld fps \r", count);
	    lastsec = tv.tv_sec;
	    count = 0;
	}
	count++;
    }

    if (-1 == video_displayframe(grabbers[grabber]->grab_scr)) {
	idle_id = 0;
	return TRUE;
    }
    return FALSE;
}

void
set_title()
{
    if (-1 != cur_sender) {
	sprintf(title, "%s", channels[cur_sender]->name);
    } else {
	sprintf(title,"channel %s",tvtuner[cur_channel].name);
	if (cur_fine != 0)
	    sprintf(title+strlen(title)," (%d)",cur_fine);
	sprintf(title+strlen(title)," (%s/%s)",
		grabbers[grabber]->norms[cur_norm].str,
		chan_names[chan_tab].str);
    }
    XtVaSetValues(app_shell,XtNtitle,title,NULL);

    if (title_timer) {
	XtRemoveTimeOut(title_timer);
	title_timer = 0;
    }
}

void
set_title_timeout(XtPointer client_data, XtIntervalId *id)
{
    set_title();
}

void
set_timer_title()
{
    XtVaSetValues(app_shell,XtNtitle,title,NULL);
    if (title_timer)
	XtRemoveTimeOut(title_timer);
    title_timer = XtAppAddTimeOut
	(app_context, TITLE_TIME, set_title_timeout,NULL);
}

void
change_audio(int mode)
{
    if (grabbers[grabber]->grab_audio)
	grabbers[grabber]->grab_audio(-1,-1,&mode);
    sprintf(title,"%-" LABEL_WIDTH "s: %s","Audio",stereo[mode].str);
    if (c_audio)
	XtVaSetValues(c_audio,XtNlabel,title,NULL);
    set_title();
    sprintf(title+strlen(title)," (%s)",stereo[mode].str);
    XtVaSetValues(app_shell,XtNtitle,title,NULL);
}

void
watch_audio(XtPointer data, XtIntervalId *id)
{
    change_audio(0);
    audio_timer = 0;
}

void set_norm(int j)
{
    sprintf(title,"%-" LABEL_WIDTH "s: %s","TV Norm",
	    grabbers[grabber]->norms[j].str);
    if (c_norm)
	XtVaSetValues(c_norm,XtNlabel,title,NULL);
    cur_norm = j;
    grabbers[grabber]->grab_input(-1,cur_norm);
    video_setmax(maxwidth[cur_norm],maxheight[cur_norm]);
}

void set_source(int j)
{
    sprintf(title,"%-" LABEL_WIDTH "s: %s","Video Source",
	    grabbers[grabber]->inputs[j].str);
    if (c_input)
	XtVaSetValues(c_input,XtNlabel,title,NULL);
    cur_input = j;
    grabbers[grabber]->grab_input(cur_input,-1);
}

void set_freqtab(int j)
{
    sprintf(title,"%-" LABEL_WIDTH "s: %s","Frequency table",
	    chan_names[j].str);
    if (c_freq)
	XtVaSetValues(c_freq,XtNlabel,title,NULL);
    chan_tab = j;
}

void
set_capture(int capture)
{
    static int niced = 0;

    if (cur_capture == capture) {
	if (cur_capture == CAPTURE_GRABDISPLAY && !idle_id)
	    idle_id = XtAppAddWorkProc(app_context, idle_grabdisplay, NULL);
	return;
    }
    
    /* off */
    switch (cur_capture) {
    case CAPTURE_GRABDISPLAY:
	if (idle_id)
	    XtRemoveWorkProc(idle_id);
	idle_id = 0;
	XClearArea(XtDisplay(tv), XtWindow(tv), 0,0,0,0, True);
	break;
    case CAPTURE_OVERLAY:
	video_overlay(NULL);
	break;
    }

    cur_capture = capture;

    /* on */
    switch (cur_capture) {
    case CAPTURE_OFF:
	sprintf(title,"%-" LABEL_WIDTH "s: %s","Capture","off");
	break;
    case CAPTURE_GRABDISPLAY:
	sprintf(title,"%-" LABEL_WIDTH "s: %s","Capture","grabdisplay");
	if (!niced)
	    nice(niced = 10);
	idle_id = XtAppAddWorkProc(app_context, idle_grabdisplay, NULL);
	break;
    case CAPTURE_OVERLAY:
	sprintf(title,"%-" LABEL_WIDTH "s: %s","Capture","overlay");
	video_overlay(grabbers[grabber]->grab_overlay);
	break;
    }
    if (c_cap)
	XtVaSetValues(c_cap,XtNlabel,title,NULL);
}

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
	 */
	XtArgVal * l_top = (XtArgVal *) &value;
	XtSetArg(args[0], name, *l_top);
    }
    XtSetValues(widget,args,1);
}

void
set_picparams(int color, int bright, int hue, int contrast)
{
    if (color != -1) {
	cur_color = color;
	if (s_color)
	    set_float(s_color,XtNtopOfThumb,(float)cur_color/65536);
    }
    if (bright != -1) {
	cur_bright = bright;
	if (s_bright)
	    set_float(s_bright,XtNtopOfThumb,(float)cur_bright/65536);
    }
    if (hue != -1) {
	cur_hue = hue;
	if (s_hue)
	    set_float(s_hue,XtNtopOfThumb,(float)cur_hue/65536);
    }
    if (contrast != -1) {
	cur_contrast = contrast;
	if (s_contrast)
	    set_float(s_contrast,XtNtopOfThumb,(float)cur_contrast/65536);
    }
    grabbers[grabber]->grab_picture(cur_color,cur_bright,cur_hue,cur_contrast);
}

void
pixit()
{
    Pixmap pix;
    char *data;

    if (0 == pix_width || 0 == pix_height)
	return;

    if (cur_sender != -1 && grabbers[grabber]->grab_scr) {
	strcpy(title,channels[cur_sender]->name);
	data = malloc(pix_width*pix_height*4);
	if (NULL != grabbers[grabber]->grab_scr(data,pix_width,pix_height,1) &&
	    0 != (pix = x11_create_pixmap(channels[cur_sender]->button,data,
					  pix_width,pix_height,title))) {
	    if (channels[cur_sender]->pixmap)
		XFreePixmap(dpy,channels[cur_sender]->pixmap);
	    channels[cur_sender]->pixmap = pix;
	    XtVaSetValues(channels[cur_sender]->button,
			  XtNbackgroundPixmap,pix,
			  XtNlabel,"",
			  XtNwidth,pix_width,
			  XtNheight,pix_height,
			  NULL);
	}
	free(data);
    }
}
    
void
set_channel(struct CHANNEL *channel)
{
    /* image parameters */
    set_picparams(channel->color, channel->bright,
		  channel->hue, channel->contrast);
    set_capture(channel->capture);

    /* input source */
    if (cur_input   != channel->source)
	set_source(channel->source);
    if (cur_norm    != channel->norm)
	set_norm(channel->norm);

    /* station */
    cur_channel  = channel->channel;
    cur_fine     = channel->fine;
    grabbers[grabber]->grab_tune(channel->freq);
    set_title();

    if (zap_timer) {
	XtRemoveTimeOut(zap_timer);
	zap_timer = 0;
    }
    if (audio_timer) {
	XtRemoveTimeOut(audio_timer);
	audio_timer = 0;
    }
    audio_timer = XtAppAddTimeOut(app_context, 10000, watch_audio, NULL);
}

void
change_int(char *name, char *par, int *val)
{
    switch (par[0]) {
    case '+':
    case '-':
	*val += atoi(par);
	break;
    default:
	*val = atoi(par);
	break;
    }
    if (*val < 0)     *val = 0;
    if (*val > 65535) *val = 65535;
    sprintf(title,"%s: %d%%",name,*val * 100 / 65535);
}

void
SetResAction(Widget widget, XEvent *event,
	     String *params, Cardinal *num_params)
{
    if (*num_params != 2)
	fprintf(stderr,"SetRes: usage: SetRes(resource,value)\n");

    if (0 == strcmp("color",params[0])) {
	change_int(params[0],params[1],&cur_color);
	set_picparams(cur_color,-1,-1,-1);
    } else if (0 == strcmp("contrast",params[0])) {
	change_int(params[0],params[1],&cur_contrast);
	set_picparams(-1,-1,-1,cur_contrast);
    } else if (0 == strcmp("bright",params[0])) {
	change_int(params[0],params[1],&cur_bright);
	set_picparams(-1,cur_bright,-1,-1);
    } else if (0 == strcmp("hue",params[0])) {
	change_int(params[0],params[1],&cur_hue);
	set_picparams(-1,-1,cur_hue,-1);
    } else if (0 == strcmp(params[0],"capture")) {
	set_capture((cur_capture+1)%3);
    } else if (0 == strcmp("audio",params[0])) {
	change_audio(atoi(params[1]));
    } else
	return;

    set_timer_title();
    return;
}

void
set_volume()
{
    int vol;
    
    if (have_mixer) {
	/* sound card */
	vol = cur_volume * 100 / 65536;
	mixer_set_volume(vol);
	cur_mute ? mixer_mute() : mixer_unmute();
    } else {
	/* v4l */
	grabbers[grabber]->grab_audio(cur_mute,cur_volume,NULL);
    }

    if (s_volume)
	set_float(s_volume,XtNtopOfThumb,(float)cur_volume/65536);
}

void
VolumeAction(Widget widget, XEvent *event,
	     String *params, Cardinal *num_params)
{
    if (*num_params < 1)
	return;

    if (0 == strcasecmp(params[0],"mute"))
	cur_mute = !cur_mute;
    else if (0 == strcasecmp(params[0],"inc"))
	cur_volume += 512;
    else if (0 == strcasecmp(params[0],"dec"))
	cur_volume -= 512;
    else
	cur_volume = atoi(params[0]);

    if (cur_volume < 0)     cur_volume = 0;
    if (cur_volume > 65535) cur_volume = 65535;

    set_volume();

    if (cur_mute)
	sprintf(title,"Volume (%s): muted",have_mixer ? "mixer" : "v4l");
    else
	sprintf(title,"Volume (%s): %d%%",have_mixer ? "mixer" : "v4l",
		cur_volume*100/65535);
    set_timer_title();
}

void
SetChannelAction(Widget widget, XEvent *event,
		 String *params, Cardinal *num_params)
{
    int i;
    
    if (*num_params > 1)
	return;

    if (count && 0 == strcmp(params[0],"next")) {
	i = (cur_sender+1) % count;
    } else if (count && 0 == strcmp(params[0],"prev")) {
	i = (cur_sender+count-1) % count;
    } else {
	i = atoi(params[0]);
    }
    if (i >= 0 && i < count) {
	pixit();
	cur_sender = i;
	set_channel(channels[i]);
    }
}

void
TuneAction(Widget widget, XEvent *event,
		 String *params, Cardinal *num_params)
{
    int freq;
    if (*num_params != 1)
	return;

    if (0 == strcasecmp(params[0],"next")) {
	do {
	    cur_channel = (cur_channel+1) % CHAN_ENTRIES;
	} while (!tvtuner[cur_channel].freq[chan_tab]);
	cur_fine = 0;
    } else if (0 == strcasecmp(params[0],"prev")) {
	do {
	    cur_channel = (cur_channel+CHAN_ENTRIES-1) % CHAN_ENTRIES;
	} while (!tvtuner[cur_channel].freq[chan_tab]);
	cur_fine = 0;
    } else if (0 == strcasecmp(params[0],"fine_up")) {
	cur_fine++;
    } else if (0 == strcasecmp(params[0],"fine_down")) {
	cur_fine--;
    }

    pixit();
    cur_sender  = -1;
    set_capture(defaults.capture /* CAPTURE_OVERLAY */ );

    freq = get_freq(cur_channel)+cur_fine;
    grabbers[grabber]->grab_tune(freq);
    set_title();

    if (audio_timer) {
	XtRemoveTimeOut(audio_timer);
	audio_timer = 0;
    }
}

void
ChannelAction(Widget widget, XEvent *event,
	      String *params, Cardinal *num_params)
{
    int i;

    if (0 == count)
	return;
    i = popup_menu(widget,"Stations",cmenu);

    if (i != -1) {
	pixit();
	cur_sender = i-1;
	set_channel(channels[cur_sender]);
    }
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
    video_new_size();
    return TRUE;
}

#ifdef HAVE_LIBXXF86VM
static void
vidmode_timer(XtPointer clientData, XtIntervalId *id)
{
    set_capture(CAPTURE_OVERLAY);
}

static void
set_vidmode(int nr)
{
    if (CAPTURE_OVERLAY == cur_capture)
	video_overlay(NULL);
    usleep(VIDMODE_DELAY*1000);
    if (debug)
	fprintf(stderr,"switching video mode to %dx%d now\n",
		vm_modelines[nr]->hdisplay,
		vm_modelines[nr]->vdisplay);
    XF86VidModeSwitchToMode(dpy,XDefaultScreen(dpy),vm_modelines[nr]);

    if (CAPTURE_OVERLAY == cur_capture) {
	cur_capture = 0;
	XtAppAddTimeOut(app_context,VIDMODE_DELAY,vidmode_timer,NULL);
    }
}
#endif

void
FullScreenAction(Widget widget, XEvent *event,
		 String *params, Cardinal *num_params)
{
    static Dimension x,y,w,h;
    static int timeout,interval,prefer_blanking,allow_exposures;
#ifdef HAVE_LIBXXF86VM
    static int vm_switched;
#endif

    if (fs) {
#ifdef HAVE_LIBXXF86VM
	if (have_vm && vm_switched) {
	    set_vidmode(0);
	    vm_switched = 0;
	}
#endif
	    
	XtVaSetValues(app_shell,
		      XtNwidthInc, WIDTH_INC,
		      XtNheightInc,HEIGHT_INC,
		      XtNx,        x + fs_xoff,
		      XtNy,        y + fs_yoff,
		      XtNwidth,    w,
		      XtNheight,   h,
		      NULL);

	XSetScreenSaver(dpy,timeout,interval,prefer_blanking,allow_exposures);
	fs = 0;
    } else {
	int vp_x, vp_y, vp_width, vp_height;

	vp_x = 0;
	vp_y = 0;
	vp_width  = swidth;
	vp_height = sheight;
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
	    XF86VidModeGetViewPort(dpy,XDefaultScreen(dpy),&vp_x,&vp_y);
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
	
	XGetScreenSaver(dpy,&timeout,&interval,
			&prefer_blanking,&allow_exposures);
	XSetScreenSaver(dpy,0,0,DefaultBlanking,DefaultExposures);
	XWarpPointer(dpy, None, XtWindow(tv), 0, 0, 0, 0, 30, 15);
	fs = 1;
    }
    XtAppAddWorkProc (app_context,MyResize, NULL);
}

void
ChannelsAction(Widget widget, XEvent *event,
	       String *params, Cardinal *num_params)
{
    static int mapped = 0, first = 1;
    Dimension height;

    if (!count)
	return;

    if (event && event->type == ClientMessage) {
	if (event->xclient.data.l[0] == wm_protocols[1]) {
	    if (debug)
		fprintf(stderr,"Options: wm_save_yourself\n");
	    XSetCommand(XtDisplay(chan_shell), XtWindow(chan_shell), NULL, 0);
	    return;
	}
    }

    if (mapped) {
	XtPopdown(chan_shell);
	mapped = 0;
    } else {
	XtPopup(chan_shell, XtGrabNone);
	mapped = 1;
	if (first) {
	    first = 0;
	    XSetWMProtocols(XtDisplay(chan_shell), XtWindow(chan_shell),
			    wm_protocols, 2);
	    XtVaGetValues(chan_shell,XtNheight,&height,NULL);
	    if (height > sheight-100)
		XtVaSetValues(chan_shell,XtNheight,sheight-100,NULL);
	}
    }
}

void button_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    int i = (int)clientdata;

    pixit();
    cur_sender = i;
    set_channel(channels[i]);
}

void
channel_menu()
{
    int  i,max,len;
    char str[100],key[32],ctrl[16];
    Widget box,viewport;

    chan_shell = XtVaAppCreateShell("Channels", "Xawtv",
				    applicationShellWidgetClass,
				    dpy,
				    XtNtransientFor,app_shell,
				    NULL);
    XtOverrideTranslations(chan_shell, XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: Channels()"));
    viewport = XtVaCreateManagedWidget("viewport",
				       viewportWidgetClass, chan_shell,
				       XtNallowHoriz, False,
				       XtNallowVert, True,
				       NULL);
    box = XtVaCreateManagedWidget("channelbox",
				  boxWidgetClass, viewport,
				  XtNsensitive, True,
				  NULL);
	
    cmenu = malloc((count+1)*sizeof(struct STRTAB));
    memset(cmenu,0,(count+1)*sizeof(struct STRTAB));
    for (i = 0, max = 0; i < count; i++) {
	len = strlen(channels[i]->name);
	if (max < len)
	    max = len;
    }
    for (i = 0; i < count; i++) {
	cmenu[i].nr      = i+1;
	cmenu[i].str     = channels[i]->name;
	if (channels[i]->key) {
	    if (2 == sscanf(channels[i]->key,"%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
			    ctrl,key))
		sprintf(str,"%s<Key>%s: SetChannel(%d)",ctrl,key,i);
	    else
		sprintf(str,"<Key>%s: SetChannel(%d)",channels[i]->key,i);
	    XtOverrideTranslations(tv,XtParseTranslationTable(str));
	    XtOverrideTranslations(opt_paned,XtParseTranslationTable(str));
	    XtOverrideTranslations(viewport,XtParseTranslationTable(str));
	    sprintf(str,"%-*s %s",max+2,channels[i]->name,channels[i]->key);
	    cmenu[i].str=strdup(str);
	}
	channels[i]->button =
	    XtVaCreateManagedWidget(channels[i]->name,
				    commandWidgetClass, box,
				    XtNwidth,pix_width,
				    XtNheight,pix_height,
				    NULL);
	XtAddCallback(channels[i]->button,XtNcallback,button_cb,(XtPointer)i);
    }
}

void
zap_timeout(XtPointer client_data, XtIntervalId *id)
{
    pixit();
    cur_sender = (cur_sender+1)%count;
    set_channel(channels[cur_sender]);
    if (cur_sender != zap_start) {
	sprintf(title, "Hop: %s", channels[cur_sender]->name);
	XtVaSetValues(app_shell,XtNtitle,title,NULL);
	zap_timer = XtAppAddTimeOut
	    (app_context, zap_fast ? CAP_TIME : ZAP_TIME, zap_timeout,NULL);
    }
}

void
ZapAction(Widget widget, XEvent *event,
	  String *params, Cardinal *num_params)
{
    if (zap_timer) {
	XtRemoveTimeOut(zap_timer);
	zap_timer = 0;
	strcpy(title,"channel hopping off");
	set_timer_title();
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

/*--- option window ------------------------------------------------------*/

#define PANED_FIX               \
        XtNallowResize, False,  \
        XtNshowGrip,    False,  \
        XtNskipAdjust,  True

struct CALL_ACTION {
    int  argc;
    char *name;
    char *argv[4];
};

struct CALL_ACTION call_ptr   = { 0, "Pointer",    { NULL }};
struct CALL_ACTION call_cap   = { 2, "SetRes",     { "capture", "toggle", NULL }};
struct CALL_ACTION call_mute  = { 1, "Volume",     { "mute", NULL }};
struct CALL_ACTION call_fs    = { 1, "FullScreen", { "mute", NULL }};

struct CALL_ACTION call_gjpeg = { 1, "Snap",       { "jpeg", NULL }};
struct CALL_ACTION call_gppm  = { 1, "Snap",       { "ppm",  NULL }};

struct CALL_ACTION call_chan  = { 0, "Channels",   { NULL }};
struct CALL_ACTION call_zap   = { 0, "Zap",        { NULL }};

void action_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct CALL_ACTION *ca = clientdata;

    XtCallActionProc(widget,ca->name,NULL,ca->argv,ca->argc);
}

void menu_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    int   cd = (int)clientdata;
    int   j;

    switch (cd) {
    case 10:
	if (-1 != (j=popup_menu(widget,"TV Norm",grabbers[grabber]->norms)))
	    set_norm(j);
	break;
    case 11:
	if (-1 != (j=popup_menu(widget,"Video Source",
				grabbers[grabber]->inputs)))
	    set_source(j);
	break;
    case 12:
	if (-1 != (j=popup_menu(widget,"Freq table",chan_names)))
	    set_freqtab(j);
	break;
    case 13:
	if (-1 != (j=popup_menu(widget,"Audio",stereo))) {
	    change_audio(j);
	}
	break;
    case 14:
	if (-1 != (j=popup_menu(widget,"Capture",cap_list))) {
	    set_capture(j);
	}
	break;
	
    default:
    }

    if (title[0] != 0)
	set_timer_title();
}

void jump_scb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char *name;
    int  value;

    name  = XtName(XtParent(widget));
    value = (int)(*(float*)call_data * 65535);
#if 0
    fprintf(stderr,"jump to %f (%s/%d)\n",*(float*)call_data,name,value);
#endif

    if (0 == strcmp("color",name))
	set_picparams(value,-1,-1,-1);

    else if (0 == strcmp("contrast",name))
	set_picparams(-1,-1,-1,value);

    else if (0 == strcmp("bright",name))
	set_picparams(-1,value,-1,-1);

    else if (0 == strcmp("hue",name))
	set_picparams(-1,-1,value,-1);

    else if (0 == strcmp("volume",name)) {
	cur_mute   = 0;
	cur_volume = value;
	set_volume();
    }
}

void scroll_scb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    int       move = (int)call_data;
    Dimension length;
    float     shown,top1,top2;

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

void
OptionsAction(Widget widget, XEvent *event,
	      String *params, Cardinal *num_params)
{
    static int mapped = 0, first = 1;

    if (event->type == ClientMessage) {
	if (event->xclient.data.l[0] == wm_protocols[1]) {
	    if (debug)
		fprintf(stderr,"Options: wm_save_yourself\n");
	    XSetCommand(XtDisplay(opt_shell), XtWindow(opt_shell), NULL, 0);
	    return;
	}
    }

    if (mapped) {
	XtPopdown(opt_shell);
	mapped = 0;
    } else {
	XtPopup(opt_shell, XtGrabNone);
	mapped = 1;
	if (first) {
	    XSetWMProtocols(XtDisplay(opt_shell), XtWindow(opt_shell),
			    wm_protocols, 2);
	    first = 0;
	}
    }
}

void create_optwin()
{
    Widget c, p,l;

    opt_shell = XtVaAppCreateShell("Options", "Xawtv",
				   applicationShellWidgetClass,
				   dpy,
				   XtNtransientFor,app_shell,
				   NULL);
    opt_paned = XtVaCreateManagedWidget("paned", panedWidgetClass, opt_shell,
				    NULL);
    
    
    c = XtVaCreateManagedWidget("mute", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&call_mute);
    
    c = XtVaCreateManagedWidget("ptr", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&call_ptr);
    
    c = XtVaCreateManagedWidget("fs", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&call_fs);

    c = XtVaCreateManagedWidget("grabppm", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&call_gppm);
#ifdef HAVE_LIBJPEG
    c = XtVaCreateManagedWidget("grabjpeg", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&call_gjpeg);
#endif
    c = XtVaCreateManagedWidget("chanwin", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&call_chan);
    c = XtVaCreateManagedWidget("zap", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&call_zap);
    
    
    c_norm = XtVaCreateManagedWidget("norm", commandWidgetClass, opt_paned,
				     PANED_FIX, NULL);
    XtAddCallback(c_norm,XtNcallback,menu_cb,(XtPointer)10);
    
    c_input = XtVaCreateManagedWidget("input", commandWidgetClass, opt_paned,
				      PANED_FIX, NULL);
    XtAddCallback(c_input,XtNcallback,menu_cb,(XtPointer)11);
    
    c_freq = XtVaCreateManagedWidget("freq", commandWidgetClass, opt_paned,
				     PANED_FIX, NULL);
    XtAddCallback(c_freq,XtNcallback,menu_cb,(XtPointer)12);
    
    c_audio = XtVaCreateManagedWidget("audio", commandWidgetClass, opt_paned,
				      PANED_FIX, NULL);
    XtAddCallback(c_audio,XtNcallback,menu_cb,(XtPointer)13);
    c_cap = XtVaCreateManagedWidget("cap", commandWidgetClass, opt_paned,
				    PANED_FIX, NULL);
    XtAddCallback(c_cap,XtNcallback,menu_cb,(XtPointer)14);
    
    
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
    
    
    c = XtVaCreateManagedWidget("quit", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,ExitCB,NULL);
}

/*--- grab (single frames) to file ---------------------------------------*/

void
patch_up(char *name)
{
    char *ptr;
    
    for (ptr = name+strlen(name); ptr >= name; ptr--)
	if (isdigit(*ptr))
	    break;
    if (ptr < name)
	return;
    while (*ptr == '9' && ptr >= name)
	*(ptr--) = '0';
    if (ptr < name)
	return;
    if (isdigit(*ptr))
	(*ptr)++;
}

#ifdef HAVE_LIBJPEG
static int write_jpeg(char *filename, char *data, int width, int height)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *fp;
    int i;
    unsigned char *line;

    if (NULL == (fp = fopen(filename,"w"))) {
	fprintf(stderr,"grab: can't open %s: %s\n",filename,strerror(errno));
	return -1;
    }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 75, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    for (i = 0, line = data; i < height; i++, line += width*3)
	jpeg_write_scanlines(&cinfo, &line, 1);
    
    jpeg_finish_compress(&(cinfo));
    jpeg_destroy_compress(&(cinfo));
    fclose(fp);

    return 0;
}
#endif

static int write_ppm(char *filename, char *data, int width, int height)
{
    FILE *fp;
    
    if (NULL == (fp = fopen(filename,"w"))) {
	fprintf(stderr,"grab: can't open %s: %s\n",filename,strerror(errno));
	return -1;
    }
    fprintf(fp,"P6\n%d %d\n255\n",width,height);
    fwrite(data,height,3*width,fp);
    fclose(fp);

    return 0;
}

void
SnapAction(Widget widget, XEvent *event,
	   String *params, Cardinal *num_params)
{
    void *buffer;
    int   jpeg = 0;
    Dimension width  = maxwidth[cur_norm];
    Dimension height = maxheight[cur_norm];

    if (*num_params > 0) {
	if (0 == strcasecmp(params[0],"jpeg"))
	    jpeg = 1;
	if (0 == strcasecmp(params[0],"ppm"))
	    jpeg = 0;
    }

    if (*num_params > 1) {
	if (0 == strcasecmp(params[1],"full")) {
	    width  = maxwidth[cur_norm];
	    height = maxheight[cur_norm];
	}
	if (0 == strcasecmp(params[1],"win"))
	    XtVaGetValues(tv,XtNwidth,&width,XtNheight,&height,NULL);
    }
    
    if (!grabbers[grabber]->grab_one) {
	fprintf(stderr,"grabbing: not supported\n");
	return;
    }

    strcpy(title,"grabbing...");
    set_timer_title();

    if (NULL == (buffer = grabbers[grabber]->grab_one(width,height))) {
	strcpy(title,"grabbing failed");
	set_timer_title();
	return;
    }

    if (jpeg) {
#ifdef HAVE_LIBJPEG
	if (NULL == jpegfile) {
	    jpegfile = malloc(32);
	    strcpy(jpegfile, "snap0000.jpeg");
	}
	write_jpeg(jpegfile,buffer,width,height);
	sprintf(title,"saved jpeg: %s",jpegfile);
	patch_up(jpegfile);
#else
	sprintf(title,"no jpeg support, sorry");
#endif
    } else {
	if (NULL == ppmfile) {
	    ppmfile = malloc(32);
	    strcpy(ppmfile, "snap0000.ppm");
	}
	write_ppm(ppmfile,buffer,width,height);
	sprintf(title,"saved ppm: %s",ppmfile);
	patch_up(ppmfile);
    }

    set_timer_title();
}

/*--- main ---------------------------------------------------------------*/

static void
xfree_init()
{
    int  flags,foo,bar,i,ma,mi;

#ifdef HAVE_LIBXXF86DGA
    if (XF86DGAQueryExtension(dpy,&foo,&bar)) {
	XF86DGAQueryDirectVideo(dpy,XDefaultScreen(dpy),&flags);
	if (flags & XF86DGADirectPresent) {
	    XF86DGAQueryVersion(dpy,&ma,&mi);
	    fprintf(stderr,"DGA: server=%d.%d, include=%d.%d\n",
		    ma,mi,XF86DGA_MAJOR_VERSION,XF86DGA_MINOR_VERSION);
	    if ((ma != XF86DGA_MAJOR_VERSION) || (mi != XF86DGA_MINOR_VERSION))
		fprintf(stderr,"DGA: version mismatch -- disabled\n");
	    else
		have_dga = 1;
	}
    }
#endif
#ifdef HAVE_LIBXXF86VM
    if (XF86VidModeQueryExtension(dpy,&foo,&bar)) {
	XF86VidModeQueryVersion(dpy,&ma,&mi);
	fprintf(stderr,"VidMode: server=%d.%d, include=%d.%d\n",
		ma,mi,XF86VIDMODE_MAJOR_VERSION,XF86VIDMODE_MINOR_VERSION);
	if ((ma != XF86VIDMODE_MAJOR_VERSION) || (mi != XF86VIDMODE_MINOR_VERSION))
	    fprintf(stderr,"VidMode: version mismatch -- disabled\n");
	else {
	    have_vm = 1;
	    fprintf(stderr,"  available video mode(s):");
	    XF86VidModeGetAllModeLines(dpy,XDefaultScreen(dpy),
				       &vm_count,&vm_modelines);
	    for (i = 0; i < vm_count; i++) {
		fprintf(stderr," %dx%d",
			vm_modelines[i]->hdisplay,
			vm_modelines[i]->vdisplay);
	    }	    
	    fprintf(stderr,"\n");
	}
    }
#endif
}

static void
grabber_init()
{
    int sw,sh;
    void *base = NULL;
    int  width = 0;
#ifdef HAVE_LIBXXF86DGA
    int bar,fred;

    if (have_dga) {
	XF86DGAGetVideoLL(dpy,XDefaultScreen(dpy),(int*)&base,
			  &width,&bar,&fred);
	fprintf(stderr,"dga: base=%p, width=%d\n",base,width);
    }
#endif
    sw = XtScreen(app_shell)->width;
    sh = XtScreen(app_shell)->height;
    for (grabber = 0; grabber < sizeof(grabbers)/sizeof(struct GRABBERS*);
	 grabber++) {
	if (-1 != grabbers[grabber]->grab_open
	    (NULL,sw,sh,x11_native_format,x11_pixmap_format,base,
	     width ? width : sw))
	    break;
    }
    if (grabber == sizeof(grabbers)/sizeof(struct GRABBERS*)) {
	fprintf(stderr,"no video grabber device available\n");
	exit(1);
    }
}


int main(int argc, char *argv[])
{
    int  c,noconf,fullscreen,nomouse;
    char v4l_conf[32] = "v4l-conf -q";

    progname = strdup(argv[0]);
    app_shell = XtAppInitialize(&app_context,
				"Xawtv",
				NULL, 0, /* opt_desc, 7, */
				&argc, argv,
				NULL /* fallback_res */,
				NULL, 0);

    /* parse options */
    debug = noconf = fullscreen = nomouse = 0;
    xfree_ext = 1;
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hxnmfv:c:o:j:b:")))
	    break;
	switch (c) {
	case 'v':
	    debug = atoi(optarg);
	    break;
	case 'c':
	    cur_sender = atoi(optarg);
	    break;
	case 'n':
	    noconf = 1;
	    break;
	case 'm':
	    nomouse = 1;
	    break;
	case 'f':
	    fullscreen = 1;
	    break;
	case 'x':
	    xfree_ext = 0;
	    break;
	case 'b':
	    bpp = atoi(optarg);
	    /* v4l-conf needs this too */
	    strcat(v4l_conf," -b ");
	    strcat(v4l_conf,optarg);
	    break;
	case 'o':
	    ppmfile = strdup(optarg);
	    break;
	case 'j':
	    jpegfile = strdup(optarg);
	    break;
	case 'h':
	default:
	    fprintf(stderr,"usage: %s [ -v debuglevel ]\n",argv[0]);
	    exit(1);
	}
    }

    switch (system(v4l_conf)) {
    case -1: /* can't run */
	fprintf(stderr,"could'nt start v4l-conf\n");
	break;
    case 0: /* ok */
	break;
    default: /* non-zero return */
	fprintf(stderr,"v4l-conf had some trouble, "
		"trying to continue anyway\n");
	break;
    }

    XtAppAddActions(app_context,actionTable,
		    sizeof(actionTable)/sizeof(XtActionsRec));
    wm_protocols[0] =
	XInternAtom(XtDisplay(app_shell), "WM_DELETE_WINDOW", False);
    wm_protocols[1] =
	XInternAtom(XtDisplay(app_shell), "WM_SAVE_YOURSELF", False);
    dpy = XtDisplay(app_shell);
    if (xfree_ext)
	xfree_init();
    create_optwin();
    
    tv = video_init(app_shell);
    switch(bpp) {
    case  8: x11_native_format = VIDEO_RGB08; break;
    case 15: x11_native_format = VIDEO_RGB15; break;
    case 16: x11_native_format = VIDEO_RGB16; break;
    case 24: x11_native_format = VIDEO_RGB24; break;
    case 32: x11_native_format = VIDEO_RGB32; break;
    default:
	bpp = 0;
    }
    grabber_init();
    if (!noconf)
	read_config();
    set_freqtab(chan_tab);
    channel_menu();
    if (have_mixer)
	cur_volume = mixer_get_volume() * 65535/100;
    set_volume();
    cur_capture = 0;

    sprintf(modename,"%d x %d",
	    XtScreen(app_shell)->width,XtScreen(app_shell)->height);
    switch (x11_native_format) {
    case VIDEO_RGB08: strcat(modename," x 8 bit"); break;
    case VIDEO_RGB15: strcat(modename," x 15 bit"); break;
    case VIDEO_RGB16: strcat(modename," x 16 bit"); break;
    case VIDEO_RGB24: strcat(modename," x 24 bit"); break;
    case VIDEO_RGB32: strcat(modename," x 32 bit"); break;
    }
    strcat(modename," - using ");
    strcat(modename,grabbers[grabber]->name);
    fprintf(stderr,"x11: %s\n",modename);

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

    if (count) {
	if ((cur_sender < 0) || (cur_sender >= count))
	    cur_sender = 0;
	set_channel(channels[cur_sender]);
    } else {
	set_channel(&defaults);
    }
    
    if (fullscreen)
	FullScreenAction(NULL,NULL,NULL,NULL);
    else
	XtAppAddWorkProc(app_context,MyResize,NULL);
    if (nomouse)
	PointerAction(NULL,NULL,NULL,NULL);

    set_timer_title();
    XtAppMainLoop(app_context);

    /* keep compiler happy */
    return 0;
}
