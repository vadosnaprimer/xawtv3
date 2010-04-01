/*
 * this is a simple test app for playing around with the xvideo extention
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "config.h"

#ifdef HAVE_MITSHM
# include <sys/ipc.h>
# include <sys/shm.h>
# include <X11/extensions/XShm.h>
#endif
#ifdef HAVE_LIBXV
# include <X11/extensions/Xv.h>
# include <X11/extensions/Xvlib.h>
#endif

#include "grab.h"
#include "xv.h"

#ifndef HAVE_LIBXV
/* dummy stubs */
int have_xv;
int have_xv_scale;
void xv_init(int foo,int bar, int port)
{
    if (debug)
	fprintf(stderr,"Xv: compiled without Xvideo extention support\n");
}
void xv_video(Window win, int width, int height, int on) {}
#else

/* ********************************************************************* */
/* the real code                                                         */

extern Display    *dpy;
int               have_xv;
int               have_xv_scale;
int               vi_adaptor = -1, vi_port = -1;
int               im_adaptor = -1, im_port = -1;

static int              ver, rel, req, ev, err;
static int              adaptors;
static int              encodings;
static int              attributes;
static int              formats;
static unsigned int     im_format;
static XvAdaptorInfo        *ai;
static XvEncodingInfo       *ei;
static XvAttribute          *at;
static XvImageFormatValues  *fo;

static Atom xv_encoding   = None;
static Atom xv_color      = None;
static Atom xv_hue        = None;
static Atom xv_saturation = None;
static Atom xv_brightness = None;
static Atom xv_contrast   = None;
static Atom xv_freq       = None;
static Atom xv_mute       = None;
static Atom xv_volume     = None;
static Atom xv_colorkey   = None;

static struct STRTAB *norms;
static struct STRTAB *inputs;
static int my_norm=-1, my_input=-1, my_enc=-1;

static struct ENC_MAP {
    int norm;
    int input;
    int encoding;
} *enc_map;

static struct GRABBER xv = {
    "Xvideo",
    0,
};

static int
xv_overlay(int x, int y, int width, int height, int format,
	   struct OVERLAY_CLIP *oc, int count)
{
    if (debug)
	fprintf(stderr,"Ouch: xv_overlay called\n");
    return 0;
}

/* ********************************************************************* */

static struct XVATTR {
    int    id;
    Atom  *attr;
    int    isint;
} xvattr[] = {
    { GRAB_ATTR_VOLUME,    &xv_volume,     1 },
    { GRAB_ATTR_MUTE,      &xv_mute,       0 },
#if 0
    { GRAB_ATTR_MODE,      &xv,            0 },
#endif
    { GRAB_ATTR_COLOR,     &xv_color,      1 },
    { GRAB_ATTR_BRIGHT,    &xv_brightness, 1 },
    { GRAB_ATTR_HUE,       &xv_hue,        1 },
    { GRAB_ATTR_CONTRAST,  &xv_contrast,   1 },
};

#define NUM_ATTR (sizeof(xvattr)/sizeof(struct XVATTR))

/* map: 0 - 65536  =>  -1000 - 1000 */
static int me_to_xv(int val) {
    val = val * 2000 / 65536 - 1000;
    if (val < -1000)  val = -1000;
    if (val >  1000)  val =  1000;
    return val;
}

/* map: 0 - 65536  <=  -1000 - 1000 */
static int xv_to_me(int val) {
    val = (val+1000) * 65536 / 2000;
    if (val < 0)      val = 0;
    if (val > 65535)  val = 65535;
    return val;
}

static int
xv_hasattr(int id)
{
    int i;

    for (i = 0; i < NUM_ATTR; i++)
	if (id == xvattr[i].id && *(xvattr[i].attr) != None)
	    break;
    if (i == NUM_ATTR)
	return 0;
    return 1;
}

static int
xv_getattr(int id)
{
    int i,val;
    
    for (i = 0; i < NUM_ATTR; i++)
	if (id == xvattr[i].id && *(xvattr[i].attr) != None)
	    break;
    if (i == NUM_ATTR)
	return -1;
    if (debug)
	fprintf(stderr,"Xv: getattr %d\n",i);

    XvGetPortAttribute(dpy,vi_port,*(xvattr[i].attr),&val);
    switch (id) {
    case GRAB_ATTR_MUTE:
	return val;
    default:
	return xv_to_me(val);
    }
}

static int
xv_setattr(int id, int val)
{
    int i,xv;
    
    for (i = 0; i < NUM_ATTR; i++)
	if (id == xvattr[i].id && *(xvattr[i].attr) != None)
	    break;
    if (i == NUM_ATTR)
	return -1;

    switch (id) {
    case GRAB_ATTR_MUTE:
	xv = val;
	break;
    default:
	xv = me_to_xv(val);
	break;
    }
    XvSetPortAttribute(dpy,vi_port,*(xvattr[i].attr),xv);
    return 0;
}

/* ********************************************************************* */

static int
xv_input(int input, int norm)
{
    int i;
    
    if (-1 != norm)
	my_norm  = norm;
    if (-1 != input)
	my_input = input;
    for (i = 0; i < encodings; i++) {
	if (enc_map[i].norm  == my_norm &&
	    enc_map[i].input == my_input) {
	    my_enc = i;
	    XvSetPortAttribute(dpy,vi_port,xv_encoding,enc_map[i].encoding);
	    return 0;
	}
    }
    return 0;
}

static unsigned long
xv_tune(unsigned long freq, int sat)
{
    int f;

    if (-1 == freq) {
	if (debug)
	    fprintf(stderr,"Xv: tune getfreq\n");
	XvGetPortAttribute(dpy,vi_port,xv_freq,&f);
	return f;
    }
    XvSetPortAttribute(dpy,vi_port,xv_freq,freq);
    return 0;
}

void
xv_video(Window win, int width, int height, int on)
{
    static GC gc;
    
    if (debug)
	fprintf(stderr,"Xv: video: win=0x%lx, size=%dx%d, %s\n",
		win, width, height, on ? "on" : "off");
    if (on) {
	int w = width, h = height;
	if (-1 != my_enc) {
	    w = ei[my_enc].width;
	    h = ei[my_enc].height;
	}
	if (!gc)
	    gc = XCreateGC(dpy, win, 0, NULL);
	XvPutVideo(dpy,vi_port,win,gc,0,0,w,h,0,0,width,height);
    } else {
	XvStopVideo(dpy,vi_port,win);
    }
}

static int
xv_strlist_add(struct STRTAB **tab, char *str)
{
    int i;

    if (NULL == *tab) {
	*tab = malloc(sizeof(struct STRTAB)*2);
	i = 0;
    } else {
	for (i = 0; (*tab)[i].str != NULL; i++)
	    if (0 == strcasecmp((*tab)[i].str,str))
		return (*tab)[i].nr;
	*tab = realloc(*tab,sizeof(struct STRTAB)*(i+2));
    }
    (*tab)[i].nr  = i;
    (*tab)[i].str = strdup(str);
    (*tab)[i+1].nr  = -1;
    (*tab)[i+1].str = NULL;
    return i;
}

/* ********************************************************************* */

XvImage*
xv_create_ximage(Display *dpy, int width, int height, void **shm)
{
    XvImage         *xvimage = NULL;
    XShmSegmentInfo *shminfo = malloc(sizeof(XShmSegmentInfo));

    if (debug)
	fprintf(stderr,"xv: xv_create_ximage\n");

    memset(shminfo, 0, sizeof(XShmSegmentInfo));
    xvimage = XvShmCreateImage(dpy, im_port, im_format, 0,
			       width, height, shminfo);
    shminfo->shmid    = shmget(IPC_PRIVATE, xvimage->data_size,
			       IPC_CREAT | 0777);
    shminfo->shmaddr  = (char *) shmat(shminfo->shmid, 0, 0);
    shminfo->readOnly = False;
    xvimage->data = shminfo->shmaddr;

    XShmAttach(dpy, shminfo);
    XSync(dpy, False);
    shmctl(shminfo->shmid, IPC_RMID, 0);

    *shm = shminfo;
    return xvimage;
}

void
xv_destroy_ximage(Display *dpy, XvImage * xvimage, void *shm)
{
    XShmSegmentInfo *shminfo = shm;

    if (debug)
	fprintf(stderr,"video: x11_destroy_ximage\n");

    XShmDetach(dpy, shminfo);
    XFree(xvimage);
    shmdt(shminfo->shmaddr);
    free(shminfo);
}

/* ********************************************************************* */

void xv_init(int xvideo, int hwscale, int port)
{
    char *h;
    int i;
    
    if (Success != XvQueryExtension(dpy,&ver,&rel,&req,&ev,&err)) {
	if (debug)
	    fprintf(stderr,"Xv: Server has no Xvideo extention support\n");
	return;
    }
    if (Success != XvQueryAdaptors(dpy,DefaultRootWindow(dpy),&adaptors,&ai)) {
	fprintf(stderr,"Xv: XvQueryAdaptors failed");
	exit(1);
    }
    if (debug)
	fprintf(stderr,"Xv: %d adaptors available.\n",adaptors);
    for (i = 0; i < adaptors; i++) {
	if (debug)
	    fprintf(stderr,"Xv: %s:%s%s%s%s%s, ports %ld-%ld\n",
		    ai[i].name,
		    (ai[i].type & XvInputMask)  ? " input"  : "",
		    (ai[i].type & XvOutputMask) ? " output" : "",
		    (ai[i].type & XvVideoMask)  ? " video"  : "",
		    (ai[i].type & XvStillMask)  ? " still"  : "",
		    (ai[i].type & XvImageMask)  ? " image"  : "",
		    ai[i].base_id,
		    ai[i].base_id+ai[i].num_ports-1);

	if ((ai[i].type & XvInputMask) &&
	    (ai[i].type & XvVideoMask) &&
	    (vi_port == -1)) {
	    if (ai[i].base_id == port || 0 == port) {
		vi_port = ai[i].base_id;
		vi_adaptor = i;
	    } else {
		fprintf(stderr,"Xv: skipping port %ld (configured other: %d)\n",
			ai[i].base_id, port);
	    }
	}

	if ((ai[i].type & XvInputMask) &&
	    (ai[i].type & XvImageMask) &&
	    (im_port == -1)) {
	    im_port = ai[i].base_id;
	    im_adaptor = i;
	}
    }

    /* *** video port *** */
    if (!xvideo) {
	if (debug)
	    fprintf(stderr,"Xv: video disabled\n");
    } else if (vi_port == -1) {
	if (debug)
	    fprintf(stderr,"Xv: no usable video port found\n");
    } else {
	fprintf(stderr,"Xv: using port %d for video\n",vi_port);
	have_xv = 1;
	xv.grab_overlay = xv_overlay;
	grabber = &xv;
	
	/* query encoding list */
	if (Success != XvQueryEncodings(dpy, vi_port, &encodings, &ei)) {
	    fprintf(stderr,"Oops: XvQueryEncodings failed\n");
	    exit(1);
	}
	enc_map = malloc(sizeof(*enc_map)*encodings);
	for (i = 0; i < encodings; i++) {
#if 1
	    if (NULL != (h = strrchr(ei[i].name,'-'))) {
		*(h++) = 0;
		enc_map[i].input = xv_strlist_add(&inputs,h);
	    }
	    enc_map[i].norm = xv_strlist_add(&norms,ei[i].name);
	    enc_map[i].encoding = ei[i].encoding_id;
#else
	    if (2 == sscanf(ei[i].name,"%31[^-]-%31s",norm,input)) {
                enc_map[i].norm     = xv_strlist_add(&norms,norm);
                enc_map[i].input    = xv_strlist_add(&inputs,input);
                enc_map[i].encoding = ei[i].encoding_id;
	    }
#endif
	}
	xv.norms = norms;
	xv.inputs = inputs;
	
	/* build atoms */
	at = XvQueryPortAttributes(dpy,vi_port,&attributes);
	for (i = 0; i < attributes; i++) {
	    if (debug)
		fprintf(stderr,"  %s%s%s, %i -> %i\n",
			at[i].name,
			(at[i].flags & XvGettable) ? " get" : "",
			(at[i].flags & XvSettable) ? " set" : "",
			at[i].min_value,at[i].max_value);
	    if (0 == strcmp("XV_ENCODING",at[i].name))
		xv_encoding   = XInternAtom(dpy, "XV_ENCODING", False);
	    if (0 == strcmp("XV_COLOR",at[i].name))
		xv_color      = XInternAtom(dpy, "XV_COLOR", False);
	    if (0 == strcmp("XV_HUE",at[i].name))
		xv_hue        = XInternAtom(dpy, "XV_HUE", False);
	    if (0 == strcmp("XV_SATURATION",at[i].name))
		xv_saturation = XInternAtom(dpy, "XV_SATURATION", False);
	    if (0 == strcmp("XV_BRIGHTNESS",at[i].name))
		xv_brightness = XInternAtom(dpy, "XV_BRIGHTNESS", False);
	    if (0 == strcmp("XV_CONTRAST",at[i].name))
		xv_contrast   = XInternAtom(dpy, "XV_CONTRAST", False);
	    if (0 == strcmp("XV_FREQ",at[i].name))
		xv_freq       = XInternAtom(dpy, "XV_FREQ", False);
	    if (0 == strcmp("XV_MUTE",at[i].name))
		xv_mute       = XInternAtom(dpy, "XV_MUTE", False);
	    if (0 == strcmp("XV_VOLUME",at[i].name))
		xv_volume     = XInternAtom(dpy, "XV_VOLUME", False);
	    if (0 == strcmp("XV_COLORKEY",at[i].name))
		xv_colorkey   = XInternAtom(dpy, "XV_COLORKEY", False);
	}
	
	/* set hooks */
	xv.grab_hasattr = xv_hasattr;
	xv.grab_getattr = xv_getattr;
	xv.grab_setattr = xv_setattr;
	
	if (xv_encoding != None) {
	    xv.grab_input = xv_input;
	}
	if (xv_freq != None) {
	    xv.grab_tune = xv_tune;
	}
#if 0
	if (xv_colorkey != None) {
	    XvGetPortAttribute(dpy,vi_port,xv_colorkey,&xv.colorkey);
	    fprintf(stderr,"Xv: colorkey: %x\n",xv.colorkey);
	}
#endif
    }

    /* *** image scaler port *** */
    if (!hwscale) {
	if (debug)
	    fprintf(stderr,"Xv: hw scaler disabled\n");
    } else if (im_port == -1) {
	if (debug)
	    fprintf(stderr,"Xv: no usable hw scaler port found\n");
    } else {
	fo = XvListImageFormats(dpy, im_port, &formats);
	printf("  image format list for port %d\n",im_port);
	for(i = 0; i < formats; i++) {
	    fprintf(stderr, "    0x%x (%4.4s) %s\n",
		    fo[i].id,
		    (char*)&fo[i].id,
		    (fo[i].format == XvPacked) ? "packed" : "planar");
	    if (0x32595559 == fo[i].id) {
		im_format = fo[i].id;
		have_xv_scale = 1;
	    }
	}
	if (have_xv_scale) {
	    fprintf(stderr,"Xv: using port %d for hw scaling\n",
		    im_port);
	} else {
	    fprintf(stderr,"Xv: no usable image format found (port %d)\n",
		    im_port);
	}
    }
}

#endif
