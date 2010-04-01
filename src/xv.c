/*
 * (most) Xvideo extention code is here.
 *
 * (c) 2001 Gerd Knorr <kraxel@bytesex.org>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#ifdef HAVE_LIBXV
# include <X11/Xlib.h>
# include <X11/Xatom.h>
# include <X11/Intrinsic.h>
# include <X11/Shell.h>
# include <X11/extensions/XShm.h>
# include <X11/extensions/Xv.h>
# include <X11/extensions/Xvlib.h>
#endif

#include "grab-ng.h"
#include "commands.h"    /* FIXME: global *drv vars */
#include "xv.h"

#ifndef HAVE_LIBXV
/* dummy stubs */
int have_xv;
void xv_init(int foo,int bar, int port, int hwscan)
{
    if (debug)
	fprintf(stderr,"Xvideo: compiled without Xvideo extention support\n");
}
//void xv_video(Window win, int width, int height, int on) {}
#else

/* ********************************************************************* */
/* the real code                                                         */

extern Display    *dpy;
int               have_xv;
int               im_adaptor = -1, im_port = -1;
unsigned int      im_formats[VIDEO_FMT_COUNT];

const struct ng_driver xv_driver;

static int              ver, rel, req, ev, err;
static int              adaptors;
static int              attributes;
static int              formats;
static XvAdaptorInfo        *ai;
static XvEncodingInfo       *ei;
static XvAttribute          *at;
static XvImageFormatValues  *fo;

static int
xv_overlay(void *handle, struct ng_video_fmt *fmt, int x, int y,
	   struct OVERLAY_CLIP *oc, int count, int aspect)
{
    if (debug)
	fprintf(stderr,"Ouch: xv_overlay called\n");
    return 0;
}

/* ********************************************************************* */

struct ENC_MAP {
    int norm;
    int input;
    int encoding;
};

struct xv_handle {
    /* port */
    int                  vi_adaptor;
    XvPortID             vi_port;
    GC                   vi_gc;
    
    /* attributes */
    int                  nattr;
    struct ng_attribute  *attr;
    Atom xv_encoding;
    Atom xv_freq;
    Atom xv_colorkey;

    /* encoding */
    struct ENC_MAP       *enc_map;
    int                  norm, input, enc;
    int                  encodings;
};

static const struct XVATTR {
    int   id;
    int   type;
    char  *atom;
} xvattr[] = {
    { ATTR_ID_COLOR,    ATTR_TYPE_INTEGER, "XV_COLOR"       },
    { ATTR_ID_COLOR,    ATTR_TYPE_INTEGER, "XV_SATURATION"  },
    { ATTR_ID_HUE,      ATTR_TYPE_INTEGER, "XV_HUE",        },
    { ATTR_ID_BRIGHT,   ATTR_TYPE_INTEGER, "XV_BRIGHTNESS", },
    { ATTR_ID_CONTRAST, ATTR_TYPE_INTEGER, "XV_CONTRAST",   },
    { ATTR_ID_MUTE,     ATTR_TYPE_BOOL,    "XV_MUTE",       },
    { ATTR_ID_VOLUME,   ATTR_TYPE_INTEGER, "XV_VOLUME",     },
    { -1,               -1,                "XV_COLORKEY",   },
    { -1,               -1,                "XV_FREQ",       },
    { -1,               -1,                "XV_ENCODING",   },
    {}
};

static int xv_read_attr(struct ng_attribute *attr)
{
    struct xv_handle *h   = attr->handle;
    const XvAttribute *at = attr->priv;
    Atom atom;
    int range, value = 0;

    if (NULL != at) {
	atom = XInternAtom(dpy, at->name, False);
	XvGetPortAttribute(dpy, h->vi_port,atom,&value);
	if (ATTR_TYPE_INTEGER == attr->type) {
	    range = at->max_value - at->min_value;
	    value = (value - at->min_value) * 65536 / range;
	    if (value < 0)      value = 0;
	    if (value > 65535)  value = 65535;
	}
	if (debug)
	    fprintf(stderr,"xv: get %s: %d\n",at->name,value);
	
    } else if (attr->id == ATTR_ID_NORM) {
	value = h->norm;
	
    } else if (attr->id == ATTR_ID_INPUT) {
	value = h->input;

    }
    return value;
}

static void xv_write_attr(struct ng_attribute *attr, int value)
{
    struct xv_handle *h   = attr->handle;
    const XvAttribute *at = attr->priv;
    Atom atom;
    int range,i;

    if (NULL != at) {
	atom = XInternAtom(dpy, at->name, False);
	if (ATTR_TYPE_INTEGER == attr->type) {
	    range = at->max_value - at->min_value;
	    value = value * range / 65536 + at->min_value;
	    if (value < at->min_value)  value = at->min_value;
	    if (value > at->max_value)  value = at->max_value;
	}
	XvSetPortAttribute(dpy, h->vi_port,atom,value);
	if (debug)
	    fprintf(stderr,"xv: set %s: %d\n",at->name,value);

    } else if (attr->id == ATTR_ID_NORM || attr->id == ATTR_ID_INPUT) {
	if (attr->id == ATTR_ID_NORM)
	    h->norm  = value;
	if (attr->id == ATTR_ID_INPUT)
	    h->input = value;
	for (i = 0; i < h->encodings; i++) {
	    if (h->enc_map[i].norm  == h->norm &&
		h->enc_map[i].input == h->input) {
		h->enc = i;
		XvSetPortAttribute(dpy,h->vi_port,h->xv_encoding,h->enc);
		break;
	    }
	}
    }
    /* needed for proper timing on the
       "mute - wait - switch - wait - unmute" channel switches */
    XSync(dpy,False);
}

static void
xv_add_attr(struct xv_handle *h, int id, int type,
	    int defval, struct STRTAB *choices, XvAttribute *at)
{
    int i;
    
    h->attr = realloc(h->attr,(h->nattr+2) * sizeof(struct ng_attribute));
    memset(h->attr+h->nattr,0,sizeof(struct ng_attribute)*2);
    if (at) {
	h->attr[h->nattr].priv    = at;
	for (i = 0; xvattr[i].atom != NULL; i++)
	    if (0 == strcmp(xvattr[i].atom,at->name))
		break;
	if (-1 == xvattr[i].type)
	    /* ignore this one*/
	    return;
	if (NULL != xvattr[i].atom) {
	    h->attr[h->nattr].id      = xvattr[i].id;
	    h->attr[h->nattr].type    = xvattr[i].type;
	    h->attr[h->nattr].priv    = at;
	} else {
	    /* unknown */
	    return;
	}
    }

    if (id)
	h->attr[h->nattr].id      = id;
    if (type)
	h->attr[h->nattr].type    = type;
    if (defval)
	h->attr[h->nattr].defval  = defval;
    if (choices)
	h->attr[h->nattr].choices = choices;
    if (h->attr[h->nattr].id < ATTR_ID_COUNT)
	h->attr[h->nattr].name    = ng_attr_to_desc[h->attr[h->nattr].id];

    h->attr[h->nattr].read    = xv_read_attr;
    h->attr[h->nattr].write   = xv_write_attr;
    h->attr[h->nattr].handle  = h;
    h->nattr++;
}

static unsigned long
xv_getfreq(void *handle)
{
    struct xv_handle *h = handle;
    unsigned int freq;

    XvGetPortAttribute(dpy,h->vi_port,h->xv_freq,&freq);
    return freq;
}

static void
xv_setfreq(void *handle, unsigned long freq)
{
    struct xv_handle *h = handle;

    XvSetPortAttribute(dpy,h->vi_port,h->xv_freq,freq);
    XSync(dpy,False);
}

static int
xv_tuned(void *handle)
{
    /* don't know ... */
    return 0;
}

void
xv_video(Window win, int dw, int dh, int on)
{
    struct xv_handle *h = h_drv; /* FIXME */
    int sx,sy,dx,dy;
    int sw,sh;
    
    if (on) {
	sx = sy = dx = dy = 0;
	sw = dw;
	sh = dh;
	if (-1 != h->enc) {
	    sw = ei[h->enc].width;
	    sh = ei[h->enc].height;
	}
	if (NULL == h->vi_gc)
	    h->vi_gc = XCreateGC(dpy, win, 0, NULL);
#if 1
	ng_ratio_fixup(&dw,&dh,&dx,&dy);
#endif
#if 0
	ng_ratio_fixup2(&sw,&sh,&sx,&sy,dw,dh);
#endif
	XvPutVideo(dpy,h->vi_port,win,h->vi_gc,
		   sx,sy,sw,sh, dx,dy,dw,dh);
	if (debug)
	    fprintf(stderr,"Xvideo: video: win=0x%lx, "
		    "src=%dx%d+%d+%d dst=%dx%d+%d+%d\n",
		    win, sw,sh,sx,sy, dw,dh,dx,dy);
    } else {
	XClearArea(dpy,win,0,0,0,0,False);
	XvStopVideo(dpy,h->vi_port,win);
	if (debug)
	    fprintf(stderr,"Xvideo: video off\n");
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

static int xv_close(void *handle) { return 0; }

static int xv_flags(void *handle)
{
    struct xv_handle *h = handle;
    int ret = 0;

    ret |= CAN_OVERLAY;
    if (h->xv_freq != None)
	ret |= CAN_TUNE;
    return ret;
}

static struct ng_attribute* xv_attrs(void *handle)
{
    struct xv_handle *h  = handle;
    return h->attr;
}

/* ********************************************************************* */

extern int             have_shmem;
static int             x11_error = 0;
static int
x11_error_dev_null(Display * dpy, XErrorEvent * event)
{
    x11_error++;
    if (debug > 1)
	fprintf(stderr," x11-error\n");
    return 0;
}

XvImage*
xv_create_ximage(Display *dpy, int width, int height,
		 int format, void **shm)
{
    XvImage         *xvimage = NULL;
    unsigned char   *ximage_data;
    XShmSegmentInfo *shminfo;
    void            *old_handler;

    if (debug)
	fprintf(stderr,"Xvideo: xv_create_ximage %dx%d\n",width,height);

    if (have_shmem) {
	x11_error = 0;
	old_handler = XSetErrorHandler(x11_error_dev_null);
	shminfo = malloc(sizeof(XShmSegmentInfo));
	memset(shminfo, 0, sizeof(XShmSegmentInfo));
	xvimage = XvShmCreateImage(dpy, im_port, format, 0,
				   width, height, shminfo);
	if (xvimage) {
	    shminfo->shmid = shmget(IPC_PRIVATE, xvimage->data_size,
				    IPC_CREAT | 0777);
	    if (-1 == shminfo->shmid) {
		have_shmem = 0;
		XFree(xvimage);
		xvimage = NULL;
		free(shminfo);
		shminfo = *shm = NULL;
		goto no_sysvipc;
	    }
	    shminfo->shmaddr  = (char *) shmat(shminfo->shmid, 0, 0);
	    shminfo->readOnly = False;
	    xvimage->data = shminfo->shmaddr;
	    XShmAttach(dpy, shminfo);
	    XSync(dpy, False);
	    shmctl(shminfo->shmid, IPC_RMID, 0);
	    if (x11_error) {
		have_shmem = 0;
		XFree(xvimage);
		xvimage = NULL;
		shmdt(shminfo->shmaddr);
		free(shminfo);
		shminfo = *shm = NULL;
		goto no_sysvipc;
	    }
	} else {
	    have_shmem = 0;
	    free(shminfo);
	    shminfo = *shm = NULL;
	    goto no_sysvipc;
	}
    	XSetErrorHandler(old_handler);
	*shm = shminfo;
	return xvimage;
    }

 no_sysvipc:
    *shm = NULL;
    if (NULL == (ximage_data = malloc(width * height * 2))) {
	fprintf(stderr,"out of memory\n");
	return NULL;
    }
    xvimage = XvCreateImage(dpy, im_port, format, ximage_data,
			    width, height);
    return xvimage;
}

void
xv_destroy_ximage(Display *dpy, XvImage * xvimage, void *shm)
{
    XShmSegmentInfo *shminfo = shm;

    if (debug)
	fprintf(stderr,"Xvideo: x11_destroy_ximage\n");

    if (shminfo) {
	XShmDetach(dpy, shminfo);
	XFree(xvimage);
	shmdt(shminfo->shmaddr);
	free(shminfo);
    } else
	XFree(xvimage);
}

/* ********************************************************************* */

void xv_init(int xvideo, int hwscale, int port, int hwscan)
{
    struct xv_handle *handle;
    struct STRTAB *norms  = NULL;
    struct STRTAB *inputs = NULL;
    char *h;
    int n, i, vi_port = -1, vi_adaptor = -1;

    if (!xvideo && !hwscale)
	return;

    if (Success != XvQueryExtension(dpy,&ver,&rel,&req,&ev,&err)) {
	if (debug)
	    fprintf(stderr,"Xvideo: Server has no Xvideo extention support\n");
	return;
    }
    if (Success != XvQueryAdaptors(dpy,DefaultRootWindow(dpy),&adaptors,&ai)) {
	fprintf(stderr,"Xvideo: XvQueryAdaptors failed");
	exit(1);
    }
    if (debug)
	fprintf(stderr,"Xvideo: %d adaptors available.\n",adaptors);
    for (i = 0; i < adaptors; i++) {
	if (debug)
	    fprintf(stderr,"Xvideo: %s:%s%s%s%s%s, ports %ld-%ld\n",
		    ai[i].name,
		    (ai[i].type & XvInputMask)  ? " input"  : "",
		    (ai[i].type & XvOutputMask) ? " output" : "",
		    (ai[i].type & XvVideoMask)  ? " video"  : "",
		    (ai[i].type & XvStillMask)  ? " still"  : "",
		    (ai[i].type & XvImageMask)  ? " image"  : "",
		    ai[i].base_id,
		    ai[i].base_id+ai[i].num_ports-1);
	if (hwscan) {
	    /* just print some info's about the Xvideo port */
	    n = fprintf(stderr,"port %ld-%ld",
			ai[i].base_id,ai[i].base_id+ai[i].num_ports-1);
	    if ((ai[i].type & XvInputMask) &&
		(ai[i].type & XvVideoMask))
		fprintf(stderr,"%*s[ -xvport %ld ]",40-n,"",ai[i].base_id);
	    fprintf(stderr,"\n");
	    if ((ai[i].type & XvInputMask) &&
		(ai[i].type & XvVideoMask))
		fprintf(stderr,"    type : Xvideo, video overlay\n");
	    if ((ai[i].type & XvInputMask) &&
		(ai[i].type & XvImageMask))
		fprintf(stderr,"    type : Xvideo, image scaler\n");
	    fprintf(stderr,"    name : %s\n",ai[i].name);
	    fprintf(stderr,"\n");
	    continue;
	}
	
	if ((ai[i].type & XvInputMask) &&
	    (ai[i].type & XvVideoMask) &&
	    (vi_port == -1)) {
	    if (0 == port) {
		vi_port = ai[i].base_id;
		vi_adaptor = i;
	    } else if (port >= ai[i].base_id  &&
		       port <  ai[i].base_id+ai[i].num_ports) {
		vi_port = port;
		vi_adaptor = i;
	    } else {
		if (debug)
		    fprintf(stderr,"Xvideo: skipping ports %ld-%ld (configured other: %d)\n",
			    ai[i].base_id, ai[i].base_id+ai[i].num_ports-1, port);
	    }
	}

	if ((ai[i].type & XvInputMask) &&
	    (ai[i].type & XvImageMask) &&
	    (im_port == -1)) {
	    im_port = ai[i].base_id;
	    im_adaptor = i;
	}
    }
    if (hwscan)
	return;

    /* *** video port *** */
    if (!xvideo) {
	if (debug)
	    fprintf(stderr,"Xvideo: video disabled\n");
    } else if (vi_port == -1) {
	if (debug)
	    fprintf(stderr,"Xvideo: no usable video port found\n");
    } else {
	if (debug)
	    fprintf(stderr,"Xvideo: using port %d for video\n",vi_port);
	handle = malloc(sizeof(struct xv_handle));
	memset(handle,0,sizeof(struct xv_handle));
	handle->vi_port     = vi_port;
	handle->vi_adaptor  = vi_adaptor;
	handle->xv_encoding = None;
	handle->xv_freq     = None;
	handle->xv_colorkey = None;
	handle->enc         = -1;
	handle->norm        = -1;
	handle->input       = -1;

	/* query encoding list */
	if (Success != XvQueryEncodings(dpy, vi_port,
					&handle->encodings, &ei)) {
	    fprintf(stderr,"Oops: XvQueryEncodings failed\n");
	    exit(1);
	}
	handle->enc_map = malloc(sizeof(struct ENC_MAP)*handle->encodings);
	for (i = 0; i < handle->encodings; i++) {
	    if (NULL != (h = strrchr(ei[i].name,'-'))) {
		*(h++) = 0;
		handle->enc_map[i].input = xv_strlist_add(&inputs,h);
	    }
	    handle->enc_map[i].norm = xv_strlist_add(&norms,ei[i].name);
	    handle->enc_map[i].encoding = ei[i].encoding_id;
	}

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
                handle->xv_encoding = XInternAtom(dpy, "XV_ENCODING", False);
            if (0 == strcmp("XV_FREQ",at[i].name))
                handle->xv_freq     = XInternAtom(dpy, "XV_FREQ", False);
#if 0
            if (0 == strcmp("XV_COLORKEY",at[i].name))
                handle->xv_colorkey = XInternAtom(dpy, "XV_COLORKEY", False);
#endif
	    xv_add_attr(handle, 0, 0, 0, NULL, at+i);
	}

	if (handle->xv_encoding != None) {
	    xv_add_attr(handle, ATTR_ID_NORM, ATTR_TYPE_CHOICE,
			0, norms, NULL);
	    xv_add_attr(handle, ATTR_ID_INPUT, ATTR_TYPE_CHOICE,
			0, inputs, NULL);
	}
#if 0
	if (xv_colorkey != None) {
	    XvGetPortAttribute(dpy,vi_port,xv_colorkey,&xv.colorkey);
	    fprintf(stderr,"Xvideo: colorkey: %x\n",xv.colorkey);
	}
#endif
	have_xv = 1;
	drv   = &xv_driver;
	h_drv = handle;
	f_drv = xv_flags(h_drv);
	add_attrs(xv_attrs(h_drv));
    }

    /* *** image scaler port *** */
    if (!hwscale) {
	if (debug)
	    fprintf(stderr,"Xvideo: hw scaler disabled\n");
    } else if (im_port == -1) {
	if (debug)
	    fprintf(stderr,"Xvideo: no usable hw scaler port found\n");
    } else {
	fo = XvListImageFormats(dpy, im_port, &formats);
	if (debug)
	    fprintf(stderr,"  image format list for port %d\n",im_port);
	for(i = 0; i < formats; i++) {
	    if (debug)
		fprintf(stderr, "    0x%x (%c%c%c%c) %s",
			fo[i].id,
			(fo[i].id)       & 0xff,
			(fo[i].id >>  8) & 0xff,
			(fo[i].id >> 16) & 0xff,
			(fo[i].id >> 24) & 0xff,
			(fo[i].format == XvPacked) ? "packed" : "planar");
	    if (0x32595559 == fo[i].id) {
		if (debug)
		    fprintf(stderr," [ok]");
		im_formats[VIDEO_YUV422] = fo[i].id;
	    }
	    if (0x30323449 == fo[i].id) {
		if (debug)
		    fprintf(stderr," [ok]");
		im_formats[VIDEO_YUV420P] = fo[i].id;
	    }
	    if (debug)
		fprintf(stderr,"\n");
	}
    }
}

/* ********************************************************************* */

#if 0
static Window icon_win;
static int icon_width,icon_height;

static void
icon_event(Widget widget, XtPointer client_data, XEvent *event, Boolean *d)
{
    switch (event->type) {
    case Expose:
	if (debug)
	    fprintf(stderr,"icon expose\n");
	xv_video(icon_win, icon_width, icon_height, 1);
	break;
    case MapNotify:
	if (debug)
	    fprintf(stderr,"icon map\n");
	xv_video(icon_win, icon_width, icon_height, 1);
	break;
    case UnmapNotify:
	if (debug)
	    fprintf(stderr,"icon unmap\n");
	break;
    default:
	fprintf(stderr,"icon other\n");
	break;
    }
}

void
init_icon_window(Widget shell,WidgetClass class)
{
    Window root = RootWindowOfScreen(XtScreen(shell));
    Widget widget;
    XIconSize *is;
    int i,count;

    if (XGetIconSizes(XtDisplay(shell),root,&is,&count)) {
	for (i = 0; i < count; i++) {
	    fprintf(stderr,"icon size: min=%dx%d - max=%dx%d - inc=%dx%d\n",
		    is[i].min_width, is[i].min_height,
		    is[i].max_width, is[i].max_height,
		    is[i].width_inc, is[i].height_inc);
	}
	icon_width  = is[0].max_width;
	icon_height = is[0].max_height;
	if (icon_width * 3 > icon_height * 4) {
	    while (icon_width * 3 > icon_height * 4 &&
		   icon_width - is[0].width_inc > is[0].min_width)
		icon_width -= is[0].width_inc;
	} else {
	    while (icon_width * 3 < icon_height * 4 &&
		   icon_height - is[0].height_inc > is[0].min_height)
		icon_height -= is[0].height_inc;
	}
    } else {
	icon_width  = 64;
	icon_height = 48;
    }
    fprintf(stderr,"icon init %dx%d\n",icon_width,icon_height);
    
    icon_win = XCreateWindow(XtDisplay(shell),root,
			     0,0,icon_width,icon_height,1,
			     CopyFromParent,InputOutput,CopyFromParent,
			     0,NULL);
    widget = XtVaCreateWidget("icon",class,shell,NULL);
    XtRegisterDrawable(XtDisplay(shell),icon_win,widget);
    XtAddEventHandler(widget,StructureNotifyMask | ExposureMask,
		      False,icon_event,NULL);
    XSelectInput(XtDisplay(shell),icon_win,
		 StructureNotifyMask | ExposureMask);
    XtVaSetValues(shell,XtNiconWindow,icon_win,NULL);
}
#endif

/* ********************************************************************* */

const struct ng_driver xv_driver = {
    name:          "Xvideo",
    close:         xv_close,

    capabilities:  xv_flags,
    list_attrs:    xv_attrs,

    overlay:       xv_overlay,

    getfreq:       xv_getfreq,
    setfreq:       xv_setfreq,
    is_tuned:      xv_tuned,
};

#endif
