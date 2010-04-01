#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#endif
#include <pthread.h>

#include "config.h"

#include "grab-ng.h"
#include "colorspace.h"
#include "mjpeg.h"
#include "commands.h"
#include "webcam.h"

#if 0
extern struct GRABBER grab_v4l;
extern struct GRABBER grab_v4l2;
extern struct GRABBER grab_bsd;
#endif

/*-------------------------------------------------------------------------*/

int   fd_grab;
int   grab_ratio_x;
int   grab_ratio_y;

#if 0
struct        GRABBER *grabber;
static struct GRABBER *grabbers[] = {
    &grab_v4l2,
    &grab_v4l,
    &grab_bsd,
};
#endif

/*-------------------------------------------------------------------------*/

extern char v4l_conf[];

void
grabber_run_v4l_conf(void)
{
    static int once = 0;
    if (!do_overlay)
	return;

    /* start v4l-conf once only */
    if (once)
	return;
    once++;

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
}

/*-------------------------------------------------------------------------*/
/* parameter negotation for capture                                        */

#if 0
static int         grabber_width;
static int         grabber_height;
static float       grabber_depth;
static int         grabber_linelength;
static int         grabber_format;
static int         output_format;
static color_conv  grabber_conv;
static void*       grabber_data;
#endif

struct CONV_LIST {
    int        format;
    int        lut;
    color_conv converter;
    void       (*init)(int width, int height);
    void       (*cleanup)(void);
};

static struct CONV_LIST gray_list[] = {
#if __BYTE_ORDER == __BIG_ENDIAN
    { VIDEO_RGB15_BE, 0, rgb15_native_gray  },
#else
    { VIDEO_RGB15_LE, 0, rgb15_native_gray  },
#endif
    { VIDEO_RGB15_BE, 0, rgb15_be_gray  },
    { VIDEO_RGB15_LE, 0, rgb15_le_gray  },
    { -1,0, NULL }
};

static struct CONV_LIST rgb15_le_list[] = {
    { VIDEO_RGB15_BE, 0, byteswap_short  },
    { VIDEO_RGB24,    1, rgb24_to_lut2   },
    { VIDEO_BGR24,    1, bgr24_to_lut2   },
    { VIDEO_RGB32,    1, rgb32_to_lut2   },
    { VIDEO_BGR32,    1, bgr32_to_lut2   },
    { VIDEO_GRAY,     1, gray_to_lut2    },
    { -1,0, NULL }
};
static struct CONV_LIST rgb16_le_list[] = {
    { VIDEO_RGB16_BE, 0, byteswap_short  },
    { VIDEO_RGB24,    1, rgb24_to_lut2   },
    { VIDEO_BGR24,    1, bgr24_to_lut2   },
    { VIDEO_RGB32,    1, rgb32_to_lut2   },
    { VIDEO_BGR32,    1, bgr32_to_lut2   },
    { VIDEO_GRAY,     1, gray_to_lut2    },
    { -1,0, NULL }
};

static struct CONV_LIST rgb15_be_list[] = {
    { VIDEO_RGB15_LE, 0, byteswap_short  },
    { VIDEO_RGB24,    1, rgb24_to_lut2   },
    { VIDEO_BGR24,    1, bgr24_to_lut2   },
    { VIDEO_RGB32,    1, rgb32_to_lut2   },
    { VIDEO_BGR32,    1, bgr32_to_lut2   },
    { VIDEO_GRAY,     1, gray_to_lut2    },
    { -1,0, NULL }
};
static struct CONV_LIST rgb16_be_list[] = {
    { VIDEO_RGB16_LE, 0, byteswap_short  },
    { VIDEO_RGB24,    1, rgb24_to_lut2   },
    { VIDEO_BGR24,    1, bgr24_to_lut2   },
    { VIDEO_RGB32,    1, rgb32_to_lut2   },
    { VIDEO_BGR32,    1, bgr32_to_lut2   },
    { VIDEO_GRAY,     1, gray_to_lut2    },
    { -1,0, NULL }
};

static struct CONV_LIST bgr24_list[] = {
    { VIDEO_RGB24, 0, rgb24_to_bgr24 },
    { VIDEO_RGB32, 0, rgb32_to_bgr24 },
    { -1,0, NULL }
};
static struct CONV_LIST rgb24_list[] = {
    { VIDEO_BGR24, 0, rgb24_to_bgr24 },
    { VIDEO_RGB32, 0, rgb32_to_rgb24 },
    { -1,0, NULL }
};

static struct CONV_LIST bgr32_list[] = {
    { VIDEO_BGR24, 0, bgr24_to_bgr32 },
    { VIDEO_RGB24, 1, rgb24_to_lut4  },
    { VIDEO_BGR24, 1, bgr24_to_lut4  },
    { VIDEO_RGB32, 1, rgb32_to_lut4  },
    { VIDEO_GRAY,  1, gray_to_lut4   },
    { -1,0, NULL }
};
static struct CONV_LIST rgb32_list[] = {
    { VIDEO_BGR24, 0, bgr24_to_rgb32 },
    { VIDEO_RGB24, 1, rgb24_to_lut4  },
    { VIDEO_BGR24, 1, bgr24_to_lut4  },
    { VIDEO_RGB32, 1, rgb32_to_lut4  },
    { VIDEO_GRAY,  1, gray_to_lut4   },
    { -1,0, NULL }
};

static struct CONV_LIST lut2_list[] = {
    { VIDEO_RGB24, 1, rgb24_to_lut2  },
    { VIDEO_BGR24, 1, bgr24_to_lut2  },
    { VIDEO_RGB32, 1, rgb32_to_lut2  },
    { VIDEO_BGR32, 1, bgr32_to_lut2  },
    { VIDEO_GRAY,  1, gray_to_lut2   },
    { -1,0, NULL }
};
static struct CONV_LIST lut4_list[] = {
    { VIDEO_RGB24, 1, rgb24_to_lut4  },
    { VIDEO_BGR24, 1, bgr24_to_lut4  },
    { VIDEO_RGB32, 1, rgb32_to_lut4  },
    { VIDEO_BGR32, 1, bgr32_to_lut4  },
    { VIDEO_GRAY,  1, gray_to_lut4   },
    { -1,0, NULL }
};

static struct CONV_LIST yuv422p_list[] = {
    { VIDEO_YUV422, 0, packed422_to_planar422 },
    { -1,0, NULL }
};

static struct CONV_LIST yuv420p_list[] = {
    { VIDEO_YUV422, 0, packed422_to_planar420 }, /* FIXME: incomplete */
    { -1,0, NULL }
};

static struct CONV_LIST mjpg_list[] = {
    { VIDEO_YUV422P, 0, mjpg_422_422_compress, mjpg_422_init, mjpg_cleanup },
    { -1,0, NULL }
};

static struct CONV_LIST jpeg_list[] = {
    { VIDEO_YUV420P, 0, mjpg_420_420_compress, mjpg_420_init, mjpg_cleanup },
    { VIDEO_YUV422P, 0, mjpg_422_420_compress, mjpg_420_init, mjpg_cleanup },
    { VIDEO_RGB24,   0, mjpg_rgb_compress,     mjpg_rgb_init, mjpg_cleanup },
    { VIDEO_BGR24,   0, mjpg_bgr_compress,     mjpg_rgb_init, mjpg_cleanup },
    { -1,0, NULL }
};


static struct CONV_LIST *conv_lists[] = {
    NULL  /* unused      */,
    NULL  /* 8bit dither */,
    gray_list,
    rgb15_le_list,
    rgb16_le_list,
    rgb15_be_list,
    rgb16_be_list,
    bgr24_list,
    bgr32_list,
    rgb24_list,
    rgb32_list,
    lut2_list,
    lut4_list,
    NULL,
    yuv422p_list,
    yuv420p_list,
    mjpg_list,
    jpeg_list,
};

int
grabber_sw_rate(struct timeval *start, int fps, int count)
{
    struct timeval now;
    int msecs,frames;
    static int lasterr;

    if (-1 == fps)
	return 1;

    gettimeofday(&now,NULL);
    msecs  = now.tv_usec/1000 - start->tv_usec/1000;
    msecs += now.tv_sec*1000  - start->tv_sec*1000;
    frames = msecs * fps / 1000;
    if (debug > 1)
	fprintf(stderr,"rate: msecs=%d fps=%d -> frames=%d  |"
		"  count=%d  ret=%d\n",
		msecs,fps,frames,count,frames-count+1);
    if (frames-count > 3  &&  frames-count != lasterr) {
	lasterr = frames-count;
	fprintf(stderr,"rate control: video lags %d frames behind\n",lasterr);
    }
    return frames-count+1;
}

void
grabber_fix_ratio(int *width, int *height, int *xoff, int *yoff)
{
    int h = *height;
    int w = *width;

    if (0 == grab_ratio_x || 0 == grab_ratio_y)
	return;
    if (w * grab_ratio_y < h * grab_ratio_x) {
	*height = *width * grab_ratio_y / grab_ratio_x;
	if (yoff)
	    *yoff  += (h-*height)/2;
    } else if (w * grab_ratio_y > h * grab_ratio_x) {
	*width  = *height * grab_ratio_x / grab_ratio_y;
	if (yoff)
	    *xoff  += (w-*width)/2;
    }
}

/*-------------------------------------------------------------------------*/
/* parameter negotation for capture                                        */

static struct ng_video_fmt gfmt;
static struct ng_video_fmt ofmt;
static color_conv gconv;
static int gsize;
static int osize;

int
ng_grabber_setparams(struct ng_video_fmt *fmt, int lut_valid, int fix_ratio)
{
    struct CONV_LIST *list;
    int i;
    
    /* no capture support */
    if (!(f_drv & CAN_CAPTURE)) {
	gfmt.fmtid = -1;
	return -1;
    }
    gconv  = NULL;

    /* try native format first */
    gfmt = *fmt;
    if (0 == drv->setformat(h_drv,&gfmt))
	goto found;

    /* check all available conversion functions */
    list = conv_lists[fmt->fmtid];
    for (i = 0; list && list[i].converter; i++) {
	if (list[i].lut && !lut_valid)
	    continue;
	gfmt = *fmt;
	gfmt.fmtid = list[i].format;
	gconv = list[i].converter;
	if (0 == drv->setformat(h_drv,&gfmt)) {
	    if (list[i].init)
		list[i].init(gfmt.width,gfmt.height);
	    goto found;
	}
    }
    fprintf(stderr,"grab: no match for: %dx%d %s\n",
	    fmt->width,fmt->height,ng_vfmt_to_desc[fmt->fmtid]);
    gfmt.fmtid = -1;
    return -1;

 found:
    if (fix_ratio) {
	grabber_fix_ratio(&gfmt.width, &gfmt.height, NULL, NULL);
	gfmt.bytesperline = 0;
	if (0 != drv->setformat(h_drv,&gfmt)) {
	    fprintf(stderr,"Oops: ratio size renegotiation failed\n");
	    exit(1);
	}
    }
    fmt->width  = gfmt.width;
    fmt->height = gfmt.height;
    if (0 == fmt->bytesperline)
	fmt->bytesperline = fmt->width * ng_vfmt_to_depth[fmt->fmtid] / 8;
    ofmt = *fmt;

    osize = ofmt.height * ofmt.bytesperline;
    if (0 == osize)
	osize = ofmt.width * ofmt.height * 3;
    gsize = gfmt.height * gfmt.bytesperline;
    if (0 == gsize)
	gsize = gfmt.width * gfmt.height * 3;

    if (debug) {
	fprintf(stderr,"grab: use: %dx%d %s (size=%d)\n",
		gfmt.width,gfmt.height,ng_vfmt_to_desc[gfmt.fmtid],gsize);
	fprintf(stderr,"grab: req: %dx%d %s (size=%d)\n",
		fmt->width,fmt->height,ng_vfmt_to_desc[fmt->fmtid],osize);
    }
    return 0;
}

static void
ng_grabber_copy(struct ng_video_buf *dest,
		struct ng_video_buf *src)
{
    int i,sw,dw;
    unsigned char *sp,*dp;

    if (gconv && 0 == dest->fmt.bytesperline) {
	/* compressed output */
	dest->size = gconv(dest->data, src->data,
			   src->fmt.width * src->fmt.height);
	return;
    }

    dw = dest->fmt.width * ng_vfmt_to_depth[dest->fmt.fmtid] / 8;
    sw = src->fmt.width * ng_vfmt_to_depth[src->fmt.fmtid] / 8;
    if (src->fmt.bytesperline == sw && dest->fmt.bytesperline == dw) {
	/* can copy in one go */
	if (gconv == NULL) {
	    memcpy(dest->data, src->data,
		   src->fmt.bytesperline * src->fmt.height);
	} else {
	    gconv(dest->data, src->data, src->fmt.width * src->fmt.height);
	}
    } else {
	/* copy line by line */
	dp = dest->data;
	sp = src->data;
	for (i = 0; i < src->fmt.height; i++) {
	    if (gconv == NULL) {
		memcpy(dp,sp,dw);
	    } else {
		gconv(dp,sp,src->fmt.width);
	    }
	    dp += dest->fmt.bytesperline;
	    sp += src->fmt.bytesperline;
	}
    }
}

struct ng_video_buf*
ng_grabber_capture(struct ng_video_buf *dest, int single)
{
    struct ng_video_buf *buf;
    
    if (-1 == gfmt.fmtid)
	return NULL;

    buf = single ? drv->getimage(h_drv) : drv->nextframe(h_drv);
    if (NULL == buf)
	return NULL;

    if (NULL == dest && NULL != gconv)
        dest = ng_malloc_video_buf(&ofmt,osize);

    if (NULL != dest) {
	dest->fmt  = ofmt;
	dest->size = osize;
	ng_grabber_copy(dest,buf);
	ng_release_video_buf(buf);
	buf = dest;
    }

    if (NULL != webcam && 0 == webcam_put(webcam,buf)) {
	free(webcam);
	webcam = NULL;
    }
    return buf;
}

