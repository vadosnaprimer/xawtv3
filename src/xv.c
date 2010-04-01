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

#include "grab-ng.h"
#include "commands.h"    /* FIXME: global *drv vars */
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
int               im_adaptor = -1, im_port = -1;

const struct ng_driver xv_driver;

static int              ver, rel, req, ev, err;
static int              adaptors;
static int              attributes;
static int              formats;
static unsigned int     im_format;
static XvAdaptorInfo        *ai;
static XvEncodingInfo       *ei;
static XvAttribute          *at;
static XvImageFormatValues  *fo;

static int
xv_overlay(void *handle, struct ng_video_fmt *fmt, int x, int y,
	   struct OVERLAY_CLIP *oc, int count)
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
    char  *name;
} xvattr[] = {
    { ATTR_ID_COLOR,    ATTR_TYPE_INTEGER, "XV_COLOR"       "color"    },
    { ATTR_ID_HUE,      ATTR_TYPE_INTEGER, "XV_HUE",        "hue"      },
    { ATTR_ID_BRIGHT,   ATTR_TYPE_INTEGER, "XV_BRIGHTNESS", "bright"   },
    { ATTR_ID_CONTRAST, ATTR_TYPE_INTEGER, "XV_CONTRAST",   "contrast" },
    { ATTR_ID_MUTE,     ATTR_TYPE_BOOL,    "XV_MUTE",       "mute"     },
    { ATTR_ID_VOLUME,   ATTR_TYPE_INTEGER, "XV_VOLUME",     "volume"   },
    { -1,               -1,                "XV_COLORKEY",   NULL       },
    { -1,               -1,                "XV_FREQ",       NULL       },
    { -1,               -1,                "XV_ENCODING",   NULL       },
    {}
};

static void
xv_add_attr(struct xv_handle *h, int id, int type, char *name,
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
	if (NULL == xvattr[i].name)
	    /* ignore this one*/
	    return;
	if (NULL != xvattr[i].atom) {
	    h->attr[h->nattr].id      = xvattr[i].id;
	    h->attr[h->nattr].type    = xvattr[i].type;
	    h->attr[h->nattr].name    = xvattr[i].name;
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
    if (name)
	h->attr[h->nattr].name    = name;
    if (defval)
	h->attr[h->nattr].defval  = defval;
    if (choices)
	h->attr[h->nattr].choices = choices;

    h->nattr++;
}

static int xv_read_attr(void *handle, struct ng_attribute *attr)
{
    struct xv_handle *h  = handle;
    XvAttribute      *at = attr->priv;
    Atom atom;
    int range, value = 0;

    if (NULL != at) {
	atom = XInternAtom(dpy, at->name, False);
	XvGetPortAttribute(dpy, h->vi_port,atom,&value);
	if (ATTR_TYPE_INTEGER == attr->type) {
	    range = at->max_value - at->min_value;
	    value = (value + at->min_value) * 65536 / range ;
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

static void xv_write_attr(void *handle, struct ng_attribute *attr, int value)
{
    struct xv_handle *h  = handle;
    XvAttribute      *at = attr->priv;
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
}

static int
xv_tuned(void *handle)
{
    /* don't know ... */
    return 0;
}

void
xv_video(Window win, int width, int height, int on)
{
    struct xv_handle *h = h_drv; /* FIXME */
    
    if (debug)
	fprintf(stderr,"Xv: video: win=0x%lx, size=%dx%d, %s\n",
		win, width, height, on ? "on" : "off");
    if (on) {
	int ww = width, hh = height;
	if (-1 != h->enc) {
	    ww = ei[h->enc].width;
	    hh = ei[h->enc].height;
	}
	if (NULL == h->vi_gc)
	    h->vi_gc = XCreateGC(dpy, win, 0, NULL);
	XvPutVideo(dpy,h->vi_port,win,h->vi_gc,0,0,ww,hh,0,0,width,height);
    } else {
	XvStopVideo(dpy,h->vi_port,win);
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
    struct xv_handle *handle;
    struct STRTAB *norms  = NULL;
    struct STRTAB *inputs = NULL;
    char *h;
    int i, vi_port = -1, vi_adaptor = -1;
    
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
	    xv_add_attr(handle, 0, 0, NULL, 0, NULL, at+i);
	}

	if (handle->xv_encoding != None) {
	    xv_add_attr(handle, ATTR_ID_NORM, ATTR_TYPE_CHOICE, "norm",
			0, norms, NULL);
	    xv_add_attr(handle, ATTR_ID_INPUT, ATTR_TYPE_CHOICE, "input",
			0, inputs, NULL);
	}
#if 0
	if (xv_colorkey != None) {
	    XvGetPortAttribute(dpy,vi_port,xv_colorkey,&xv.colorkey);
	    fprintf(stderr,"Xv: colorkey: %x\n",xv.colorkey);
	}
#endif
	have_xv = 1;
	drv   = &xv_driver;
	h_drv = handle;
	f_drv = xv_flags(h_drv);
	a_drv = xv_attrs(h_drv);
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

/* ********************************************************************* */

const struct ng_driver xv_driver = {
    name:          "Xvideo",
    close:         xv_close,

    capabilities:  xv_flags,
    list_attrs:    xv_attrs,
    read_attr:     xv_read_attr,
    write_attr:    xv_write_attr,

    overlay:       xv_overlay,

    getfreq:       xv_getfreq,
    setfreq:       xv_setfreq,
    is_tuned:      xv_tuned,
};

#endif
