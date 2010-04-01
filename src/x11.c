/*
 * misc x11 functions:  pixmap handling (incl. MIT SHMEM), event
 *                      tracking for the TV widget.
 *
 *  (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#endif
#include <pthread.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/extensions/XShm.h>
#ifdef HAVE_LIBXV
# include <X11/extensions/Xv.h>
# include <X11/extensions/Xvlib.h>
#endif

#include "grab-ng.h"
#include "capture.h"
#include "channel.h"
#include "x11.h"
#include "xv.h"
#include "commands.h"

#define DISPLAY             XtDisplay
#define SCREEN              XtScreen
#define WINDOW              XtWindow
#define DEL_TIMER(proc)     XtRemoveTimeOut(proc)
#define ADD_TIMER(proc)     XtAppAddTimeOut(app_context,200,proc,NULL)

/* ------------------------------------------------------------------------ */

extern XtAppContext    app_context;
extern int             have_shmem;
extern XVisualInfo     vinfo;

static int             display_bits = 0;
static int             display_bytes = 0;
static int             pixmap_bytes = 0;
static int             x11_error = 0;
static int             x11_byteswap = 0;

static struct SEARCHFORMAT {
    int           depth;
    unsigned long order;
    unsigned long red;
    unsigned long green;
    unsigned long blue;
    int           format;
} fmt[] = {
    { 2, MSBFirst, 0x7c00,     0x03e0,     0x001f,     VIDEO_RGB15_BE },
    { 2, MSBFirst, 0xf800,     0x07e0,     0x001f,     VIDEO_RGB16_BE },
    { 2, LSBFirst, 0x7c00,     0x03e0,     0x001f,     VIDEO_RGB15_LE },
    { 2, LSBFirst, 0xf800,     0x07e0,     0x001f,     VIDEO_RGB16_LE },

    { 3, LSBFirst, 0x00ff0000, 0x0000ff00, 0x000000ff, VIDEO_BGR24    },
    { 3, LSBFirst, 0x000000ff, 0x0000ff00, 0x00ff0000, VIDEO_RGB24    },
    { 3, MSBFirst, 0x00ff0000, 0x0000ff00, 0x000000ff, VIDEO_RGB24    },
    { 3, MSBFirst, 0x000000ff, 0x0000ff00, 0x00ff0000, VIDEO_BGR24    },

    { 4, LSBFirst, 0x00ff0000, 0x0000ff00, 0x000000ff, VIDEO_BGR32    },
    { 4, LSBFirst, 0x0000ff00, 0x00ff0000, 0xff000000, VIDEO_RGB32    },
    { 4, MSBFirst, 0x00ff0000, 0x0000ff00, 0x000000ff, VIDEO_RGB32    },
    { 4, MSBFirst, 0x0000ff00, 0x00ff0000, 0xff000000, VIDEO_BGR32    },

    { 2, -1,       0,          0,          0,          VIDEO_LUT2     },
    { 4, -1,       0,          0,          0,          VIDEO_LUT4     },
    { 0 /* END OF LIST */ },
};

Visual*
x11_visual(Display *dpy)
{
    XVisualInfo  *info, template;
    Visual*      vi = CopyFromParent;
    int          found,i;
    char         *class;

    template.screen = XDefaultScreen(dpy);
    info = XGetVisualInfo(dpy, VisualScreenMask,&template,&found);
    for (i = 0; i < found; i++) {
	switch (info[i].class) {
	case StaticGray:   class = "StaticGray";  break;
	case GrayScale:    class = "GrayScale";   break;
	case StaticColor:  class = "StaticColor"; break;
	case PseudoColor:  class = "PseudoColor"; break;
	case TrueColor:    class = "TrueColor";   break;
	case DirectColor:  class = "DirectColor"; break;
	default:           class = "UNKNOWN";     break;
	}
	if (debug)
	    fprintf(stderr,"visual: id=0x%lx class=%d (%s), depth=%d\n",
		    info[i].visualid,info[i].class,class,info[i].depth);
    }
    for (i = 0; vi == CopyFromParent && i < found; i++)
	if (info[i].class == TrueColor && info[i].depth >= 15)
	    vi = info[i].visual;
    for (i = 0; vi == CopyFromParent && i < found; i++)
	if (info[i].class == StaticGray && info[i].depth == 8)
	    vi = info[i].visual;
    return vi;
}

static int
x11_init(Display *dpy, XVisualInfo *vinfo)
{
    XPixmapFormatValues *pf;
    int                  i,n;
    int                  format = 0;

    if (XShmQueryExtension(dpy)) {
	have_shmem = 1;
    }

    display_bits = vinfo->depth;
    display_bytes = (display_bits+7)/8;

    pf = XListPixmapFormats(dpy,&n);
    for (i = 0; i < n; i++)
	if (pf[i].depth == display_bits)
	    pixmap_bytes = pf[i].bits_per_pixel/8;

    if (debug) {
	fprintf(stderr,"x11: color depth: "
		"%d bits, %d bytes - pixmap: %d bytes\n",
		display_bits,display_bytes,pixmap_bytes);
	if (vinfo->class == TrueColor || vinfo->class == DirectColor)
	    fprintf(stderr, "x11: color masks: "
		    "red=0x%08lx green=0x%08lx blue=0x%08lx\n",
		    vinfo->red_mask, vinfo->green_mask, vinfo->blue_mask);
	fprintf(stderr,"x11: server byte order: %s\n",
		ImageByteOrder(dpy)==LSBFirst ? "little endian":"big endian");
	fprintf(stderr,"x11: client byte order: %s\n",
		BYTE_ORDER==LITTLE_ENDIAN ? "little endian":"big endian");
    }
    if (ImageByteOrder(dpy)==LSBFirst && BYTE_ORDER!=LITTLE_ENDIAN)
	x11_byteswap=1;
    if (ImageByteOrder(dpy)==MSBFirst && BYTE_ORDER!=BIG_ENDIAN)
	x11_byteswap=1;
    if (vinfo->class == TrueColor /* || vinfo->class == DirectColor */) {
	/* pixmap format */
	for (i = 0; fmt[i].depth > 0; i++) {
	    if (fmt[i].depth  == pixmap_bytes                               &&
		(fmt[i].order == ImageByteOrder(dpy) || fmt[i].order == -1) &&
		(fmt[i].red   == vinfo->red_mask     || fmt[i].red   == 0)  &&
		(fmt[i].green == vinfo->green_mask   || fmt[i].green == 0)  &&
		(fmt[i].blue  == vinfo->blue_mask    || fmt[i].blue  == 0)) {
		x11_fmt.fmtid = fmt[i].format;
		break;
	    }
	}
	if (fmt[i].depth == 0) {
	    fprintf(stderr, "Huh?\n");
	    exit(1);
	}
	ng_lut_init(vinfo->red_mask, vinfo->green_mask, vinfo->blue_mask,
		    x11_fmt.fmtid,x11_byteswap);
	/* guess physical screen format */
	if (ImageByteOrder(dpy) == MSBFirst) {
	    switch (pixmap_bytes) {
	    case 2: format = (display_bits==15) ?
			VIDEO_RGB15_BE : VIDEO_RGB16_BE; break;
	    case 3: format = VIDEO_RGB24; break;
	    case 4: format = VIDEO_RGB32; break;
	    }
	} else {
	    switch (pixmap_bytes) {
	    case 2: format = (display_bits==15) ?
			VIDEO_RGB15_LE : VIDEO_RGB16_LE; break;
	    case 3: format = VIDEO_BGR24; break;
	    case 4: format = VIDEO_BGR32; break;
	    }
	}
    }
    if (vinfo->class == StaticGray && vinfo->depth == 8) {
	format = VIDEO_GRAY;
	x11_fmt.fmtid = VIDEO_GRAY;
    }
    if (0 == format) {
	if (vinfo->class == PseudoColor && vinfo->depth == 8) {
	    fprintf(stderr,
"\n"
"8-bit Pseudocolor Visual (256 colors) is *not* supported.\n"
"You can startup X11 either with 15 bpp (or more)...\n"
"	xinit -- -bpp 16\n"
"... or with StaticGray visual:\n"
"	xinit -- -cc StaticGray\n"
	    );
	} else {
	    fprintf(stderr, "Sorry, I can't handle your strange display\n");
	}
	exit(1);
    }
    return format;
}

static int
x11_error_dev_null(Display * dpy, XErrorEvent * event)
{
    x11_error++;
    if (debug > 1)
	fprintf(stderr," x11-error\n");
    return 0;
}

/* ------------------------------------------------------------------------ */
/* ximage handling for grab & display                                       */

XImage *
x11_create_ximage(Display *dpy, XVisualInfo *vinfo,
		  int width, int height, void **shm)
{
    XImage         *ximage = NULL;
    unsigned char  *ximage_data;
    XShmSegmentInfo *shminfo = NULL;
    void            *old_handler;

    if (debug)
	fprintf(stderr,"video: x11_create_ximage\n");
    if (have_shmem) {
	x11_error = 0;
	old_handler = XSetErrorHandler(x11_error_dev_null);
	(*shm) = shminfo = malloc(sizeof(XShmSegmentInfo));
	memset(shminfo, 0, sizeof(XShmSegmentInfo));
	ximage = XShmCreateImage(dpy,vinfo->visual,vinfo->depth,
				 ZPixmap, NULL,
				 shminfo, width, height);
	if (ximage) {
	    shminfo->shmid = shmget(IPC_PRIVATE,
				    ximage->bytes_per_line * ximage->height,
				    IPC_CREAT | 0777);
	    if (-1 == shminfo->shmid) {
		have_shmem = 0;
		perror("shmget");
		if (errno == ENOSYS)
		    fprintf(stderr, "WARNING: Your kernel has no support "
			    "for SysV IPC\n");
		XDestroyImage(ximage);
		ximage = NULL;
		goto no_sysvipc;
	    }
	    shminfo->shmaddr = (char *) shmat(shminfo->shmid, 0, 0);
	    if ((void *) -1 == shminfo->shmaddr) {
		perror("shmat");
		goto oom;
	    }
	    ximage->data = shminfo->shmaddr;
	    shminfo->readOnly = False;

	    XShmAttach(dpy, shminfo);
	    XSync(dpy, False);
	    shmctl(shminfo->shmid, IPC_RMID, 0);
	    if (x11_error) {
		have_shmem = 0;
		shmdt(shminfo->shmaddr);
		free(shminfo);
		shminfo = *shm = NULL;
		XDestroyImage(ximage);
		ximage = NULL;
	    }
	} else {
	    have_shmem = 0;
	    free(shminfo);
	    shminfo = *shm = NULL;
	}
	XSetErrorHandler(old_handler);
    }

no_sysvipc:
    if (ximage == NULL) {
	(*shm) = NULL;
	if (NULL == (ximage_data = malloc(width * height * pixmap_bytes))) {
	    fprintf(stderr,"out of memory\n");
	    goto oom;
	}
	ximage = XCreateImage(dpy, vinfo->visual, vinfo->depth,
			      ZPixmap, 0, ximage_data,
			      width, height,
			      8, 0);
    }
    memset(ximage->data, 0, ximage->bytes_per_line * ximage->height);

    return ximage;

oom:
    if (shminfo) {
	if (shminfo->shmid && shminfo->shmid != -1)
	    shmctl(shminfo->shmid, IPC_RMID, 0);
	free(shminfo);
    }
    if (ximage)
	XDestroyImage(ximage);
    return NULL;
}

void
x11_destroy_ximage(Display *dpy, XImage * ximage, void *shm)
{
    XShmSegmentInfo *shminfo = shm;

    if (shminfo) {
	XShmDetach(dpy, shminfo);
	XDestroyImage(ximage);
	shmdt(shminfo->shmaddr);
	free(shminfo);
    } else
	XDestroyImage(ximage);

    if (debug)
	fprintf(stderr,"video: x11_destroy_ximage\n");
}

Pixmap
x11_create_pixmap(Display *dpy, XVisualInfo *vinfo, Colormap colormap,
		  unsigned char *byte_data,
                  int width, int height, char *label)
{
    static XFontStruct    *font;
    static XColor          color,dummy;

    Pixmap          pixmap;
    XImage         *ximage;
    XGCValues       values;
    GC              gc;
    void           *shm;
    Screen         *scr = DefaultScreenOfDisplay(dpy);

    if (!font) {
	font = XLoadQueryFont(dpy,"fixed");
	XAllocNamedColor(dpy,colormap,"yellow",&color,&dummy);
    }
   
    pixmap = XCreatePixmap(dpy,RootWindowOfScreen(scr),
                           width, height, vinfo->depth);

    values.font       = font->fid;
    values.foreground = color.pixel;
    gc = XCreateGC(dpy, pixmap, GCFont | GCForeground, &values);

    if (NULL == (ximage = x11_create_ximage(dpy,vinfo,width,height,&shm))) {
	XFreePixmap(dpy, pixmap);
        XFreeGC(dpy, gc);
        return 0;
    }
    memcpy(ximage->data,byte_data,width*height*pixmap_bytes);
    XPUTIMAGE(dpy, pixmap, gc, ximage, 0, 0, 0, 0, width, height);

    if (label)
	XDrawString(dpy,pixmap,gc,5,height-5,label,strlen(label));

    x11_destroy_ximage(dpy, ximage, shm);
    XFreeGC(dpy, gc);
    return pixmap;
}

/* ------------------------------------------------------------------------ */
/* video grabdisplay stuff                                                  */

struct format_status {
    int status;
#define STATUS_UNKNOWN       0
#define STATUS_BROKEN        1
#define STATUS_CONVERT       2
#define STATUS_XVIDEO        3

    /* current ximage/xvimage */
    struct ng_video_buf      buf;
    void                     *shm;            /* MIT SHM */

    /* convert stuff to RGB */
    struct ng_video_conv     *conv;
    struct ng_convert_handle *chandle;
    XImage                   *ximage;

    /* image filtering */
    struct ng_filter         *filter;
    void                     *fhandle;

    /* Xvideo */
#ifdef HAVE_LIBXV
    int                      xv_id;
    XvImage                  *xv_image;
#endif
};

struct video_handle {
    Widget                win;
    Dimension             width,height;
    GC                    gc;
    XtWorkProcId          work_id;
    int                   suspend;         /* temporarely disabled */
    int                   nw,nh;           /* new size (suspend)   */
    int                   using_xv;

    struct ng_video_fmt   best;
    struct format_status  formats[VIDEO_FMT_COUNT];
};
struct video_handle vh;

Pixmap
freeze_image(Display *dpy, Colormap colormap)
{
    struct ng_video_buf *buf;
    struct ng_video_fmt fmt;
    Pixmap pix = 0;
    
    if (!(f_drv & CAN_CAPTURE))
	return 0;

    fmt = x11_fmt;
    if (NULL != (buf = ng_grabber_get_image(&fmt))) {
	buf = ng_filter_single(cur_filter,buf);
	pix = x11_create_pixmap(dpy,&vinfo,colormap,buf->data,
				buf->fmt.width, buf->fmt.height,
				NULL);
	ng_release_video_buf(buf);
    }
    return pix;
}

static void
video_initformat(struct format_status *st, int fmtid)
{
    struct ng_video_conv *conv;
    int i;
    
#ifdef HAVE_LIBXV
    if (0 != im_formats[fmtid]) {
	st->status = STATUS_XVIDEO;
	return;
    }
#endif
    if (x11_fmt.fmtid == fmtid) {
	st->status = STATUS_CONVERT;
	return;
    }
    for (i = 0;;) {
	conv = ng_conv_find(x11_fmt.fmtid, &i);
	if (NULL == conv) {
	    st->status = STATUS_BROKEN;
	    return;
	}
	if (conv->fmtid_in != fmtid)
	    continue;
	break;
    }
    st->status = STATUS_CONVERT;
    st->conv   = conv;
}

static struct ng_video_buf*
video_gd_filter(struct format_status *st, struct ng_video_buf *buf)
{
    if (NULL != st->filter && cur_filter != st->filter) {
	st->filter->fini(st->fhandle);
	st->filter  = NULL;
	st->fhandle = NULL;
    }
    if ((1 << buf->fmt.fmtid) & cur_filter->fmts) {
	if (NULL == st->filter) {
	    st->filter  = cur_filter;
	    st->fhandle = st->filter->init(&buf->fmt);
	}
	buf = cur_filter->frame(st->fhandle,buf);
    }
    return buf;
}

int
video_gd_blitframe(struct video_handle *h, struct ng_video_buf *buf)
{
    struct format_status *st;

    if (buf->fmt.width  > x11_fmt.width ||
	buf->fmt.height > x11_fmt.height)
	return -1;
    st = h->formats + buf->fmt.fmtid;

 again:
    switch (st->status) {
    case STATUS_UNKNOWN:
	video_initformat(st,buf->fmt.fmtid);
	goto again;

    case STATUS_CONVERT:
	if (debug > 1)
	    fprintf(stderr,"gd: convert\n");
	if (st->buf.fmt.width  != buf->fmt.width ||
	    st->buf.fmt.height != buf->fmt.height) {
	    if (st->chandle) {
		ng_convert_fini(st->chandle);
		st->chandle = NULL;
	    }
	    if (st->filter) {
		st->filter->fini(st->fhandle);
		st->filter  = NULL;
		st->fhandle = NULL;
	    }
	    if (st->ximage) {
		x11_destroy_ximage(XtDisplay(h->win),st->ximage,st->shm);
		st->ximage = NULL;
	    }
	}
	if (cur_filter)
	    buf = video_gd_filter(st,buf);
	if (NULL == st->chandle) {
	    st->buf.fmt.fmtid  = x11_fmt.fmtid;
	    st->buf.fmt.width  = buf->fmt.width;
	    st->buf.fmt.height = buf->fmt.height;
	    st->buf.fmt.bytesperline = 0;
	    st->chandle = ng_convert_alloc(st->conv,&buf->fmt,&st->buf.fmt);
	    ng_convert_init(st->chandle);
	}
	if (NULL == st->ximage) {
	    st->ximage = x11_create_ximage(XtDisplay(h->win),&vinfo,
					   st->buf.fmt.width,
					   st->buf.fmt.height,
					   &st->shm);
	    st->buf.data = st->ximage->data;
	}
	buf = ng_convert_frame(st->chandle,&st->buf,buf);
	XPUTIMAGE(XtDisplay(h->win), XtWindow(h->win), h->gc,st->ximage,0,0,
		  (x11_fmt.width  - st->buf.fmt.width)  >> 1,
		  (x11_fmt.height - st->buf.fmt.height) >> 1,
		  st->buf.fmt.width, st->buf.fmt.height);
	break;

#ifdef HAVE_LIBXV
    case STATUS_XVIDEO:
	if (debug > 1)
	    fprintf(stderr,"gd: xvideo\n");
	if (st->buf.fmt.width  != buf->fmt.width ||
	    st->buf.fmt.height != buf->fmt.height) {
	    if (st->filter) {
		st->filter->fini(st->fhandle);
		st->filter  = NULL;
		st->fhandle = NULL;
	    }
	    if (st->xv_image) {
		xv_destroy_ximage(XtDisplay(h->win),st->xv_image,st->shm);
		st->xv_image = NULL;
	    }
	}
	if (cur_filter)
	    buf = video_gd_filter(st,buf);
	if (NULL == st->xv_image) {
	    st->buf.fmt = buf->fmt;
	    st->xv_image = xv_create_ximage(XtDisplay(h->win),
					    st->buf.fmt.width,
					    st->buf.fmt.height,
					    im_formats[st->buf.fmt.fmtid],
					    &st->shm);
	}
	memcpy(st->xv_image->data,buf->data,buf->size);
	ng_release_video_buf(buf);
	XVPUTIMAGE(XtDisplay(h->win), im_port, XtWindow(h->win),
		   h->gc, st->xv_image,
		   0, 0,  st->buf.fmt.width, st->buf.fmt.height,
		   0, 0,  x11_fmt.width, x11_fmt.height);
	h->using_xv = 1;
	break;
#endif

    case STATUS_BROKEN:
    default:
	if (debug > 1)
	    fprintf(stderr,"gd: oops\n");
	ng_release_video_buf(buf);
	return -1;

    }
    return 0;
}

static Boolean
video_gd_idle(XtPointer data)
{
    struct video_handle *h = data;
    struct ng_video_buf *buf;
    
    if (!(f_drv & CAN_CAPTURE))
	goto oops;

    buf = ng_grabber_grab_image(0);
    if (NULL != buf) {
	video_gd_blitframe(h,buf);
    } else {
	goto oops;
    }
    
    if (debug) {
	static long count,lastsec;
	struct timeval  t;
	struct timezone tz;
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
    h->work_id = 0;
    if (f_drv & CAN_CAPTURE)
	drv->stopvideo(h_drv);
    return TRUE;
}

void
video_gd_start(void)
{
    struct video_handle *h = &vh;

    if (0 == h->best.fmtid)
	return;
    if (debug)
	fprintf(stderr,"gd: start\n");
    ng_grabber_setformat(&h->best,0);
    drv->startvideo(h_drv,-1,2);
    h->work_id = XtAppAddWorkProc(app_context, video_gd_idle, h);
}

void
video_gd_stop(void)
{
    struct video_handle *h = &vh;

    if (debug)
	fprintf(stderr,"gd: stop\n");
    if (h->work_id) {
	drv->stopvideo(h_drv);
	XtRemoveWorkProc(h->work_id);
	h->work_id = 0;
#ifdef HAVE_LIBXV
	if (h->using_xv)
	    XvStopVideo(XtDisplay(h->win), im_port, XtWindow(h->win));
#endif
    }
}

void
video_gd_suspend(void)
{
    struct video_handle *h = &vh;
    
    h->suspend = 1;
    if (cur_capture != CAPTURE_GRABDISPLAY)
	return;
    do_va_cmd(2, "capture", "off");
}

void
video_gd_restart(void)
{
    struct video_handle *h = &vh;

    if (!h->suspend)
	return;
    h->suspend = 0;
    if (h->nw && h->nh) {
	video_gd_configure(h->nw,h->nh);
	h->nw = 0;
	h->nh = 0;
    }
    if (cur_capture != CAPTURE_OFF)
	return;
    do_va_cmd(2, "capture", "grab");
}

void
video_gd_configure(int width, int height)
{
    struct ng_video_conv *conv;
    struct video_handle *h = &vh;
    int i;

    if (!(f_drv & CAN_CAPTURE))
	return;

    if (h->suspend) {
	if (debug)
	    fprintf(stderr,"gd: delay configure\n");
	h->nw = width;
	h->nh = height;
	return;
    }

    if (debug)
	fprintf(stderr,"gd: config %dx%d win=%lx\n",
		width,height,XtWindow(h->win));
	
    if (!XtWindow(h->win))
	return;

    if (!h->gc)
	h->gc = XCreateGC(XtDisplay(h->win),XtWindow(h->win),0,NULL);

    x11_fmt.width   = width;
    x11_fmt.height  = height;
    x11_fmt.bytesperline = 0;
    h->best.width   = width;
    h->best.height  = height;
    h->best.bytesperline = 0;
    ng_ratio_fixup(&x11_fmt.width, &x11_fmt.height, NULL, NULL);
    ng_ratio_fixup(&h->best.width, &h->best.height, NULL, NULL);

    if (0 == h->best.fmtid) {
#ifdef HAVE_LIBXV
	/* try yuv if we have hw scaling */
	if (0 != im_formats[VIDEO_YUV422]) {
	    h->best.fmtid = VIDEO_YUV422;
	    if (0 == ng_grabber_setformat(&h->best,0))
		goto done;
	}
	if (0 != im_formats[VIDEO_YUV420P]) {
	    h->best.fmtid = VIDEO_YUV420P;
	    if (0 == ng_grabber_setformat(&h->best,0))
		goto done;
	}
#endif

	/* try native rgb */
	h->best.fmtid = x11_fmt.fmtid;
	if (0 == ng_grabber_setformat(&h->best,0))
	    goto done;

	/* try to find something we can convert from */ 
	for (i = 0;;) {
	    conv = ng_conv_find(x11_fmt.fmtid, &i);
	    if (NULL == conv)
		break;
	    h->best.fmtid = conv->fmtid_in;
	    if (0 == ng_grabber_setformat(&h->best,0))
		goto done;
	}

	/* failed */
	h->best.fmtid = 0;
    }

 done:
    if (debug)
	fprintf(stderr,"grabdisplay: using \"%s\"\n",
		ng_vfmt_to_desc[h->best.fmtid]);
    if (cur_capture == CAPTURE_GRABDISPLAY) {
	do_va_cmd(2, "capture", "off");
	do_va_cmd(2, "capture", "grab");
    }
}

/* ------------------------------------------------------------------------ */
/* video overlay stuff                                                      */

int        x11_native_format;
int        swidth,sheight;                         /* screen  */

/* window  */
static Widget    video,video_parent;
static int       wx, wy, wmap;
static struct ng_video_fmt wfmt;

static XtIntervalId          overlay_refresh;
static int                   did_refresh, oc_count;
static int                   visibility = VisibilityFullyObscured;
static int                   conf = 1, move = 1;
static int                   overlay_on = 0, overlay_enabled = 0;
static struct OVERLAY_CLIP   oc[256];
static XtWorkProcId          conf_id;

/* ------------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------------ */

static void
add_clip(int x1, int y1, int x2, int y2)
{
    if (oc[oc_count].x1 != x1 || oc[oc_count].y1 != y1 ||
	oc[oc_count].x2 != x2 || oc[oc_count].y2 != y2) {
	conf = 1;
    }
    oc[oc_count].x1 = x1;
    oc[oc_count].y1 = y1;
    oc[oc_count].x2 = x2;
    oc[oc_count].y2 = y2;
    oc_count++;
} 

static void
get_clips(void)
{
    int x1,y1,x2,y2,lastcount;
    Display *dpy;
    XWindowAttributes wts;
    Window root, me, rroot, parent, *children;
    uint nchildren, i;
    void *old_handler = XSetErrorHandler(x11_error_dev_null);

    if (debug > 1)
	fprintf(stderr," getclips");
    lastcount = oc_count;
    oc_count = 0;
    dpy = DISPLAY(video);

    if (wx<0)
	add_clip(0, 0, (uint)(-wx), wfmt.height);
    if (wy<0)
	add_clip(0, 0, wfmt.width, (uint)(-wy));
    if ((wx+wfmt.width) > swidth)
	add_clip(swidth-wx, 0, wfmt.width, wfmt.height);
    if ((wy+wfmt.height) > sheight)
	add_clip(0, sheight-wy, wfmt.width, wfmt.height);
    
    root=DefaultRootWindow(dpy);
    me = WINDOW(video);
    for (;;) {
	XQueryTree(dpy, me, &rroot, &parent, &children, &nchildren);
	XFree((char *) children);
	/* fprintf(stderr,"me=0x%x, parent=0x%x\n",me,parent); */
	if (root == parent)
	    break;
	me = parent;
    }
    XQueryTree(dpy, root, &rroot, &parent, &children, &nchildren);

    for (i = 0; i < nchildren; i++)
	if (children[i]==me)
	    break;
    
    for (i++; i<nchildren; i++) {
	XGetWindowAttributes(dpy, children[i], &wts);
	if (!(wts.map_state & IsViewable))
	    continue;

	x1=wts.x-wx;
	y1=wts.y-wy;
	x2=x1+wts.width+2*wts.border_width;
	y2=y1+wts.height+2*wts.border_width;
	if ((x2 < 0) || (x1 > (int)wfmt.width) ||
	    (y2 < 0) || (y1 > (int)wfmt.height))
	    continue;
	
	if (x1<0)      	         x1=0;
	if (y1<0)                y1=0;
	if (x2>(int)wfmt.width)  x2=wfmt.width;
	if (y2>(int)wfmt.height) y2=wfmt.height;
	add_clip(x1, y1, x2, y2);
    }
    XFree((char *) children);

    if (lastcount != oc_count)
	conf = 1;
    XSetErrorHandler(old_handler);
}

static void
refresh_timer(XtPointer clientData, XtIntervalId *id)
{
    Window   win = RootWindowOfScreen(SCREEN(video));
    Display *dpy = DISPLAY(video);
    XSetWindowAttributes xswa;
    unsigned long mask;
    Window   tmp;

    if (!move && wmap && visibility == VisibilityUnobscured) {
	if (debug > 1)
	    fprintf(stderr,"video: refresh skipped\n");
	return;
    }

    if (debug > 1)
	fprintf(stderr,"video: refresh\n");
    overlay_refresh = 0;
    if (wmap && visibility != VisibilityFullyObscured)
	did_refresh = 1;

    xswa.override_redirect = True;
    xswa.backing_store = NotUseful;
    xswa.save_under = False;
    mask = (CWSaveUnder | CWBackingStore| CWOverrideRedirect );
    tmp = XCreateWindow(dpy,win, 0,0, swidth,sheight, 0,
			CopyFromParent, InputOutput, CopyFromParent,
			mask, &xswa);
    XMapWindow(dpy, tmp);
    XUnmapWindow(dpy, tmp);
    XDestroyWindow(dpy, tmp);
    move = 0;
}

static Boolean
configure_delayed(XtPointer data)
{
    if (debug > 1)
	fprintf(stderr,"video: configure delayed");
    if (wmap && visibility != VisibilityFullyObscured) {
	if (visibility == VisibilityPartiallyObscured)
	    get_clips();
	else
	    oc_count = 0;

	if (debug > 1)
	    fprintf(stderr," %s\n",conf ? "yes" : "no");
	if (conf) {
	    overlay_on = 1;
	    if (f_drv & CAN_OVERLAY)
		drv->overlay(h_drv,&wfmt,wx,wy,oc,oc_count,1);
	    if (overlay_refresh)
		DEL_TIMER(overlay_refresh);
	    overlay_refresh = ADD_TIMER(refresh_timer);
	    conf = 0;
	}
    } else {
	if (debug > 1)
	    fprintf(stderr," off\n");
	if (conf && overlay_on) {
	    overlay_on = 0;
	    if (f_drv & CAN_OVERLAY)
		drv->overlay(h_drv,NULL,0,0,NULL,0,0);
	    if (overlay_refresh)
		DEL_TIMER(overlay_refresh);
	    overlay_refresh = ADD_TIMER(refresh_timer);
	    conf = 0;
	}
    }
    conf_id = 0;
    return TRUE;
}

static void
configure_overlay(void)
{
    if (!overlay_enabled)
	return;

#ifdef HAVE_LIBXV
    if (have_xv) {
	if (wfmt.width && wfmt.height)
	    xv_video(XtWindow(video),wfmt.width,wfmt.height,1);
	return;
    }
#endif

    if (0 == conf_id)
	conf_id = XtAppAddWorkProc(app_context,configure_delayed,NULL);
}

void
video_new_size()
{
    Dimension x,y,w,h;

    XtVaGetValues(video_parent, XtNx, &x, XtNy, &y,
		  XtNwidth, &w, XtNheight, &h, NULL);
    wx          = x; if (wx > 32768)          wx          -= 65536;
    wy          = y; if (wy > 32768)          wy          -= 65536;
    wfmt.width  = w; if (wfmt.width > 32768)  wfmt.width  -= 65536;
    wfmt.height = h; if (wfmt.height > 32768) wfmt.height -= 65536;
    wfmt.fmtid  = x11_native_format;
    if (debug > 1)
	fprintf(stderr,"video: shell: size %dx%d+%d+%d\n",
		wfmt.width,wfmt.height,wx,wy);

    conf = 1;
    move = 1;
    configure_overlay();
}

/* ------------------------------------------------------------------------ */

static void
video_event(Widget widget, XtPointer client_data, XEvent *event, Boolean *d)
{
    if (widget == video_parent) {
	/* shell widget */
	switch(event->type) {
	case ConfigureNotify:
#if 0
	    wx      = event->xconfigure.x;
	    wy      = event->xconfigure.y;
	    wwidth  = event->xconfigure.width;
	    wheight = event->xconfigure.height;
	    if (debug > 1)
		fprintf(stderr,"video: shell: cfg %dx%d+%d+%d\n",
			wwidth,wheight,wx,wy);
#endif
	    video_new_size();
	    break;
	case MapNotify:
	    if (debug > 1)
		fprintf(stderr,"video: shell: map\n");
	    wmap = 1;
	    conf = 1;
	    configure_overlay();
	    break;
	case UnmapNotify:
	    if (debug > 1)
		fprintf(stderr,"video: shell: unmap\n");
	    wmap = 0;
	    conf = 1;
	    configure_overlay();
	    break;
	default:
	    if (debug > 1)
		fprintf(stderr,"video: shell: %s\n",
			events[event->type]);
	}
	return;

    } else {
	/* TV widget (+root window) */
	switch(event->type) {
	case Expose:
	    if (event->xvisibility.window == WINDOW(video)) {
		/* tv */
		if (!event->xexpose.count) {
		    if (did_refresh) {
			did_refresh = 0;
			if (debug > 1)
			    fprintf(stderr,"video: tv: last refresh expose\n");
		    } else {
			if (debug > 1)
			    fprintf(stderr,"video: tv: expose\n");
			conf = 1;
			configure_overlay();
		    }
		}
	    }
	    break;
	case VisibilityNotify:
	    if (event->xvisibility.window == WINDOW(video)) {
		/* tv */
		visibility = event->xvisibility.state;
		if (debug > 1)
		    fprintf(stderr,"video: tv: visibility %d%s\n",
			    event->xvisibility.state,
			    did_refresh?" (ignored)":"");
		if (did_refresh) {
		    if (event->xvisibility.state != VisibilityFullyObscured)
			did_refresh = 0;
		} else {
		    conf = 1;
		    configure_overlay();
		}
	    } else {
		/* root */
		if (debug > 1)
		    fprintf(stderr,"video: root: visibility\n");
	    }
	    break;
	case MapNotify:
	case UnmapNotify:
	case ConfigureNotify:
	    if (event->xvisibility.window != WINDOW(video)) {
		if (debug > 1)
		    fprintf(stderr,"video: root: %s%s\n",
			    events[event->type],did_refresh?" (ignored)":"");
		if (!did_refresh)
		    configure_overlay();
	    }
	    break;
	default:
	    if (debug > 1)
		fprintf(stderr,"video: tv(+root): %s\n",
			events[event->type]);
	    break;
	}	
    }
}

void
video_overlay(int state)
{
    if (state) {
	conf = 1;
	overlay_enabled = 1;
	configure_overlay();
    } else {
	if (1 == overlay_enabled) {
#ifdef HAVE_LIBXV
	    if (have_xv) {
		xv_video(XtWindow(video),0,0,0);
	    } else
#endif
	    {
		overlay_on = 0;
		if (f_drv & CAN_OVERLAY)
		    drv->overlay(h_drv,NULL,0,0,NULL,0,0);
		overlay_refresh = ADD_TIMER(refresh_timer);
	    }
	}
	overlay_enabled = 0;
    }
}

Widget
video_init(Widget parent, XVisualInfo *vinfo, WidgetClass class)
{
    Window root = DefaultRootWindow(DISPLAY(parent));

    swidth  = SCREEN(parent)->width;
    sheight = SCREEN(parent)->height;

    x11_native_format = x11_init(XtDisplay(parent),vinfo);
    video_parent = parent;
    video = XtVaCreateManagedWidget("tv",class,parent,
				    NULL);
    vh.win = video;

    /* Shell widget -- need map, unmap, configure */
    XtAddEventHandler(parent,
		      StructureNotifyMask,
		      True, video_event, NULL);

    if (!have_xv) {
	/* TV Widget -- need visibility, expose */
	XtAddEventHandler(video,
			  VisibilityChangeMask |
			  StructureNotifyMask,
			  False, video_event, NULL);
	
	/* root window -- need */
	XSelectInput(DISPLAY(video),root,
		     VisibilityChangeMask |
		     SubstructureNotifyMask |
		     StructureNotifyMask);
	
	XtRegisterDrawable(DISPLAY(video),root,video);
    }

    return video;
}

void
video_close(void)
{
    Window root = DefaultRootWindow(DISPLAY(video));

    if (overlay_refresh)
	DEL_TIMER(overlay_refresh);
    XSelectInput(DISPLAY(video),root,0);
    XtUnregisterDrawable(DISPLAY(video),root);
}
