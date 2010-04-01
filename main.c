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

#include "config.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xlib.h>
#include <X11/Shell.h>
#include <X11/Xaw/XawInit.h>
#include <X11/Xaw/Paned.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Scrollbar.h>
#ifdef HAVE_LIBXXF86DGA
# include <X11/extensions/xf86dga.h>
#endif

#ifdef HAVE_LIBJPEG
# include "jpeglib.h"
#endif

#include "mixer.h"
#include "channel.h"
#include "grab.h"
#include "x11.h"
#include "toolbox.h"

#define TITLE_TIME 6000
#define WIDTH_INC    64
#define HEIGHT_INC   48
#define LABEL_WIDTH  "16"

/*--- public variables ----------------------------------------------------*/

XtAppContext      app_context;
Widget            app_shell, tv;
Widget            opt_shell, opt_paned;
Widget            c_norm,c_input,c_freq,c_audio;
Widget            s_bright,s_color,s_hue,s_contrast,s_volume;
Display           *dpy;

Atom              wm_protocols[2];
XtIntervalId      title_timer;
int               pointer_on = 1;
int               debug = 0;
int               fs = 0;
int               bpp = 0;
char              *ppmfile;
char              *jpegfile;

char              modename[64];
char              *progname;
#ifdef HAVE_LIBXXF86DGA
int               have_dga;
#endif

/*                            PAL  NTSC SECAM */
static int    maxwidth[]  = { 768, 640, 768 };
static int    maxheight[] = { 576, 480, 576 };


/*--- drivers -------------------------------------------------------------*/

extern struct GRABBER grab_v4l;
#ifdef HAVE_BTTV
extern struct GRABBER grab_bttv;
#endif
struct GRABBER *grabbers[] = {
    &grab_v4l,
#ifdef HAVE_BTTV
    &grab_bttv,
#endif
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
void MenuAction(Widget, XEvent*, String*, Cardinal*);
void ChannelAction(Widget, XEvent*, String*, Cardinal*);
void VolumeAction(Widget, XEvent*, String*, Cardinal*);
void PointerAction(Widget, XEvent*, String*, Cardinal*);
void FullScreenAction(Widget, XEvent*, String*, Cardinal*);
void OptionsAction(Widget, XEvent*, String*, Cardinal*);
void SnapAction(Widget, XEvent*, String*, Cardinal*);

static XtActionsRec actionTable[] = {
    { "CloseMain",  CloseMainAction  },
    { "SetRes",     SetResAction },
    { "SetChannel", SetChannelAction },
    { "Tune",       TuneAction },
    { "Menu",       MenuAction },
    { "Channel",    ChannelAction },
    { "Volume",     VolumeAction },
    { "Pointer",    PointerAction },
    { "FullScreen", FullScreenAction },
    { "Options",    OptionsAction },
    { "Snap",       SnapAction },
};

static struct STRTAB try[] = {
    {  1, "cap"     },
    {  2, "mute"    },
    {  3, "ptr"     },
    {  4, "fs"      },
    { 10, "norm"    },
    { 11, "input"   },
    { 12, "freq"    },
    { 13, "audio"   },
    { 99, "quit"    },
    { -1, NULL,     },
};

static struct STRTAB stereo[] = {
    {  0, "auto"    },
    {  1, "mono"    },
    {  2, "stereo"  },
    {  3, "lang1"   },
    {  4, "lang2"   },
    { -1, NULL,     },
};

/*--- exit ----------------------------------------------------------------*/

Boolean
ExitWP(XtPointer client_data)
{
    /* exit if the application is idle,
     * i.e. all the DestroyCallback's are called.
     */
    if (have_mixer)
	mixer_close();
    exit(0);
}

void
ExitCB(Widget widget, XtPointer client_data, XtPointer calldata)
{
    video_overlay(NULL);
    video_close();
    XtAppAddWorkProc (app_context,ExitWP, NULL);
    XtDestroyWidget(app_shell);
}

void
CloseMainAction(Widget widget, XEvent *event,
		String *params, Cardinal *num_params)
{
    static Dimension x,y,w,h;
    char *argv[20];
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

void
set_title()
{
    if (-1 != cur_sender) {
	strcpy(title,channels[cur_sender]->name);
    } else {
	sprintf(title,"channel %d",cur_channel);
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
    int freq;
    
    sprintf(title,"%-" LABEL_WIDTH "s: %s","Frequency table",
	    chan_names[j].str);
    if (c_freq)
	XtVaSetValues(c_freq,XtNlabel,title,NULL);
    chan_tab = j;
    freq = cf2freq(cur_channel,cur_fine);
    grabbers[grabber]->grab_tune(freq);
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
set_channel(struct CHANNEL *channel)
{
    /* image parameters */
    set_picparams(channel->color, channel->bright,
		  channel->hue, channel->contrast);

    /* input source */
    if (cur_capture != channel->capture)
	video_overlay(channel->capture ?
		      grabbers[grabber]->grab_overlay : NULL);
    cur_capture  = channel->capture;

    if (cur_input   != channel->source)
	set_source(channel->source);
    if (cur_norm    != channel->norm)
	set_norm(channel->norm);

    /* station */
    cur_channel  = channel->channel;
    cur_fine     = channel->fine;
    grabbers[grabber]->grab_tune(channel->freq);
    set_title();
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
	cur_capture = !cur_capture;
	sprintf(title,"capture: %s",cur_capture ? "on" : "off");
	video_overlay(cur_capture ? grabbers[grabber]->grab_overlay : NULL);
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
	cur_sender = i;
	set_channel(channels[cur_sender]);
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
	cur_channel++;
	cur_fine = 0;
    } else if (0 == strcasecmp(params[0],"prev")) {
	cur_channel--;
	cur_fine = 0;
    } else if (0 == strcasecmp(params[0],"fine_up")) {
	cur_fine++;
    } else if (0 == strcasecmp(params[0],"fine_down")) {
	cur_fine--;
    }

    cur_sender  = -1;

    if (!cur_capture) {
	cur_capture = 1;
	video_overlay(cur_capture ? grabbers[grabber]->grab_overlay : NULL);
    }
    freq = cf2freq(cur_channel,cur_fine);
    grabbers[grabber]->grab_tune(freq);
    set_title();
}

void
MenuAction(Widget widget, XEvent *event,
	   String *params, Cardinal *num_params)
{
    static String mute[] = { "mute", NULL };
    int   i,j;

    title[0] = 0;
    switch(i=popup_menu(widget,"Settings",try)) {
    case 1:
	cur_capture = !cur_capture;
	sprintf(title,"capture: %s",cur_capture ? "on" : "off");
	video_overlay(cur_capture ? grabbers[grabber]->grab_overlay : NULL);
	break;
    case 2:
	XtCallActionProc(widget,"Volume",event,mute,1);
	break;
    case 3:
	XtCallActionProc(widget,"Pointer",event,NULL,0);
	break;
    case 4:
	XtCallActionProc(widget,"FullScreen",event,NULL,0);
	break;
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
    case 99:
	ExitCB(widget,NULL,NULL);
	break;
    default:
	/* nothing */
    }

    if (title[0] != 0) {
	set_timer_title();
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

void
FullScreenAction(Widget widget, XEvent *event,
		 String *params, Cardinal *num_params)
{
    static Dimension x,y,w,h;

    if (fs) {
	XtVaSetValues(app_shell,
		      XtNwidthInc, WIDTH_INC,
		      XtNheightInc,HEIGHT_INC,
		      XtNx,        x,
		      XtNy,        y,
		      XtNwidth,    w,
		      XtNheight,   h,
		      NULL);
	fs = 0;
    } else {
	int                vp_width  = swidth;
	int                vp_height = sheight;

#ifdef HAVE_LIBXXF86DGA
	if (have_dga) {
	    XF86DGAGetViewPortSize(dpy,XDefaultScreen(dpy),
				   &vp_width, &vp_height);
	    if (debug)
		fprintf(stderr,"dga: viewport size: %dx%d\n",
			vp_width,vp_height);
	    if (vp_width < swidth || vp_height < sheight)
		XF86DGASetViewPort(dpy,XDefaultScreen(dpy),0,0);
	}
#endif
	XtVaGetValues(app_shell,
		      XtNx,      &x,
		      XtNy,      &y,
		      XtNwidth,  &w,
		      XtNheight, &h,
		      NULL);

	XtVaSetValues(app_shell,
		      XtNwidthInc,  1,
		      XtNheightInc, 1,
		      NULL);
	XtVaSetValues(app_shell,
		      XtNx,          0,
		      XtNy,          0,
		      XtNwidth,     vp_width,
		      XtNheight,    vp_height,
		      NULL);
	fs = 1;
    }
    XtAppAddWorkProc (app_context,MyResize, NULL);
}

void
channel_menu()
{
    int  i,max,len;
    char str[100],key[32],ctrl[16];

    cmenu = malloc((count+1)*sizeof(struct STRTAB));
    memset(cmenu,0,(count+1)*sizeof(struct STRTAB));
    for (i = 0, max = 0; i < count; i++) {
	len = strlen(channels[i]->name);
	if (max < len)
	    max = len;
    }
    defaults.freq = cf2freq(defaults.channel,defaults.fine);
    for (i = 0; i < count; i++) {
	channels[i]->freq = cf2freq(channels[i]->channel,channels[i]->fine);
	cmenu[i].nr  = i+1;
	cmenu[i].str = channels[i]->name;
	if (channels[i]->key) {
	    if (2 == sscanf(channels[i]->key,"%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
			    ctrl,key))
		sprintf(str,"%s<Key>%s: SetChannel(%d)",ctrl,key,i);
	    else
		sprintf(str,"<Key>%s: SetChannel(%d)",channels[i]->key,i);
	    XtOverrideTranslations(tv,XtParseTranslationTable(str));
	    XtOverrideTranslations(opt_paned,XtParseTranslationTable(str));
	    sprintf(str,"%-*s %s",max+2,channels[i]->name,channels[i]->key);
	    cmenu[i].str=strdup(str);
	}
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
    
    
    c = XtVaCreateManagedWidget("cap", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&call_cap);
    
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
    int   width  = maxwidth[cur_norm];
    int   height = maxheight[cur_norm];

    if (*num_params > 0) {
	if (0 == strcasecmp(params[0],"jpeg"))
	    jpeg = 1;
	if (0 == strcasecmp(params[0],"ppm"))
	    jpeg = 0;
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
grabber_init()
{
    int sw,sh;
    void *base = NULL;
#ifdef HAVE_LIBXXF86DGA
    int  foo,bar,fred,flags;

    have_dga = 0;
    if (XF86DGAQueryExtension(dpy,&foo,&bar)) {
	XF86DGAQueryDirectVideo(dpy,XDefaultScreen(dpy),&flags);
	if (flags & XF86DGADirectPresent) {
	    have_dga = 1;
	    XF86DGAGetVideoLL(dpy,XDefaultScreen(dpy),(int*)&base,
			      &foo,&bar,&fred);
	    fprintf(stderr,"dga: base=%p\n",base);
	}
    }
#endif
    sw = XtScreen(app_shell)->width;
    sh = XtScreen(app_shell)->height;
    for (grabber = 0; grabber < sizeof(grabbers)/sizeof(struct GRABBERS*);
	 grabber++) {
	if (-1 != grabbers[grabber]->grab_open
	    (NULL,sw,sh,x11_native_format,base))
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

    progname = strdup(argv[0]);
    app_shell = XtAppInitialize(&app_context,
				"Xawtv",
				NULL, 0, /* opt_desc, 7, */
				&argc, argv,
				NULL /* fallback_res */,
				NULL, 0);

    /* parse options */
    debug = noconf = fullscreen = nomouse = 0;
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hnmfv:c:o:")))
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
	case 'b':
	    bpp = atoi(optarg);
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

    XtAppAddActions(app_context,actionTable,
		    sizeof(actionTable)/sizeof(XtActionsRec));
    wm_protocols[0] =
	XInternAtom(XtDisplay(app_shell), "WM_DELETE_WINDOW", False);
    wm_protocols[1] =
	XInternAtom(XtDisplay(app_shell), "WM_SAVE_YOURSELF", False);
    dpy = XtDisplay(app_shell);
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
