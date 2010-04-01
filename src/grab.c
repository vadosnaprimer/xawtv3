#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#endif

#include "config.h"

#include "grab-ng.h"
#include "colorspace.h"
#include "mjpeg.h"
#include "commands.h"
#include "webcam.h"

extern struct GRABBER grab_v4l;
extern struct GRABBER grab_v4l2;
extern struct GRABBER grab_bsd;

/*-------------------------------------------------------------------------*/

int   fd_grab;
int   grab_ratio_x;
int   grab_ratio_y;

struct        GRABBER *grabber;
static struct GRABBER *grabbers[] = {
    &grab_v4l2,
    &grab_v4l,
    &grab_bsd,
};

#define GRABBER_COUNT (sizeof(grabbers)/sizeof(struct GRABBERS*))

/*-------------------------------------------------------------------------*/

extern char v4l_conf[];

void
grabber_run_v4l_conf(void)
{
    if (!do_overlay)
	return;

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

int
grabber_open(char *device, int sw, int sh, void *base, int format, int width)
{
    int i;

    /* check all grabber drivers */
    for (i = 0; i < GRABBER_COUNT; i++) {
	if (NULL == grabbers[i]->name)
	    continue;
	if (debug)
	    fprintf(stderr,"init: trying: %s... \n",grabbers[i]->name);
	if (-1 != (fd_grab = grabbers[i]->grab_open(device))) {
	    grabber = grabbers[i];
	    break;
	}
	if (debug)
	    fprintf(stderr,"init: failed: %s\n",grabbers[i]->name);
    }
    if (i == GRABBER_COUNT) {
	fprintf(stderr,"no video grabber device available\n");
	exit(1);
    }
    if (debug)
	fprintf(stderr,"init: ok: %s\n",grabber->name);
    if (sw && sh && grabber->grab_setupfb)
	grabber->grab_setupfb(sw,sh,format,base,width);
    return 0;
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
    { VIDEO_YUV420P, 0, mjpg_yuv420_compress, mjpg_yuv_init, mjpg_cleanup },
    { VIDEO_YUV422P, 0, mjpg_yuv422_compress, mjpg_yuv_init, mjpg_cleanup },
    { VIDEO_RGB24,   0, mjpg_rgb_compress,    mjpg_rgb_init, mjpg_cleanup },
    { VIDEO_BGR24,   0, mjpg_bgr_compress,    mjpg_rgb_init, mjpg_cleanup },
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
};

#if 0
int
grabber_setparams(int format, int *width, int *height,
		  int *linelength, int lut_valid, int fix_ratio)
{
    int w,h,i;
    struct CONV_LIST *list;

    /* no capture support */
    if (NULL == grabber->grab_setparams ||
	NULL == grabber->grab_capture) {
	grabber_format = -1;
	return -1;
    }
    
    if (grabber_data)
	free(grabber_data);
    grabber_data  = NULL;
    grabber_conv  = NULL;
    grabber_depth = ng_vfmt_to_depth[format]/8;
    w = *width;
    h = *height;

    /* try native format first */
    grabber_width      = w;
    grabber_height     = h;
    grabber_linelength = 0;
    grabber_format     = format;
    output_format      = -1;
    if (0 == grabber->grab_setparams
	(grabber_format,&grabber_width,&grabber_height,&grabber_linelength)) {
	goto found;
    }

    /* check all available conversion functions */
    list = conv_lists[format];
    for (i = 0; list && list[i].converter; i++) {
	if (list[i].lut && !lut_valid)
	    continue;
	grabber_width      = w;
	grabber_height     = h;
	grabber_linelength = 0;
	grabber_format     = list[i].format;
	grabber_conv       = list[i].converter;
	if (0 == grabber->grab_setparams
	    (grabber_format,&grabber_width,&grabber_height,&grabber_linelength)) {
	    if (list[i].init)
		list[i].init(grabber_width,grabber_height);
	    goto found;
	}
    }
    fprintf(stderr,"grab: no match for: %dx%d %s\n",
	    *width,*height,ng_vfmt_to_desc[format]);
    grabber_format = -1;
    return -1;

 found:
    if (debug)
	fprintf(stderr,"grab: req: %dx%d %s\n",
		*width,*height,ng_vfmt_to_desc[format]);
    if (fix_ratio) {
	grabber_fix_ratio(&grabber_width,&grabber_height,NULL,NULL);
	grabber_linelength = 0;
	if (0 != grabber->grab_setparams
	    (grabber_format,&grabber_width,&grabber_height,&grabber_linelength)) {
	    fprintf(stderr,"Oops: ratio size renegotiation failed\n");
	    exit(1);
	}
    }
    *width      = grabber_width;
    *height     = grabber_height;
    if (grabber_linelength == 0)
	grabber_linelength = grabber_width*grabber_depth;
    *linelength = grabber_linelength;
    if (debug)
	fprintf(stderr,"grab: use: %dx%d %s\n",
		*width,*height,ng_vfmt_to_desc[grabber_format]);
    output_format = format;
    return 0;
}

int
grabber_copy(unsigned char *dest, int dll,
	     unsigned char *src,  int sll,
	     int width, int height, int depth)
{
    int i,n,size = 0;

    if (0 == dll || (dll == sll && sll == width*depth)) {
	if (grabber_conv == NULL) {
	    memcpy(dest,src,width*height*depth);
	} else {
	    size = grabber_conv(dest,src,width*height);
	}
    } else {
	if (grabber_conv == NULL) {
	    n = width*depth;
	    for (i = 0; i < height; i++) {
		memcpy(dest,src,n);
		dest += dll;
		src  += sll;
	    }
	} else {
	    for (i = 0; i < height; i++) {
		grabber_conv(dest,src,width);
		dest += dll;
		src  += sll;
	    }
	}
    }
    return size;
}

void*
grabber_capture(void *dest, int dest_linelength, int *size)
{
    int rc = 0;
    void *data;

    if (size) *size = 0;
    if (-1 == grabber_format)
	return NULL;
    if (NULL == (data = grabber->grab_capture()))
	return NULL;

    if (0 == dest_linelength)
	dest_linelength = grabber_width * grabber_depth;

    if (dest == NULL) {
	if (grabber_conv == NULL && dest_linelength == grabber_linelength)
	    goto done;
	if (grabber_data == NULL)
	    grabber_data = malloc(grabber_height * dest_linelength);
	rc = grabber_copy(grabber_data, dest_linelength,
			  data, grabber_linelength,
			  grabber_width, grabber_height, grabber_depth);
	if (size) *size = rc;
	data = grabber_data;
	goto done;
    }

    rc = grabber_copy(dest, dest_linelength,
		      data, grabber_linelength,
		      grabber_width, grabber_height, grabber_depth);
    data = dest;

 done:
    if (NULL != webcam &&
	0 == webcam_put(webcam,output_format,grabber_width,grabber_height,
			data,rc ? rc : (dest_linelength * grabber_height))) {
	free(webcam);
	webcam = NULL;
    }
    if (size) *size = rc;
    return data;
}
#endif

int
grabber_sw_rate(struct timeval *start, int fps, int count)
{
    struct timeval now;
    int msecs,frames;

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
    if (frames-count > 3)
	fprintf(stderr,"rate control: video lags %d frames behind\n",frames-count);
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
    if (NULL == grabber->grab_setparams ||
	NULL == grabber->grab_capture) {
	gfmt.fmtid = -1;
	return -1;
    }
    gconv  = NULL;

    /* try native format first */
    gfmt = *fmt;
    if (0 == grabber->grab_setparams
	(gfmt.fmtid, &gfmt.width, &gfmt.height, &gfmt.bytesperline)) {
	goto found;
    }

    /* check all available conversion functions */
    list = conv_lists[fmt->fmtid];
    for (i = 0; list && list[i].converter; i++) {
	if (list[i].lut && !lut_valid)
	    continue;
	gfmt = *fmt;
	gfmt.fmtid = list[i].format;
	gconv = list[i].converter;
	if (0 == grabber->grab_setparams
	    (gfmt.fmtid, &gfmt.width, &gfmt.height, &gfmt.bytesperline)) {
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
	if (0 != grabber->grab_setparams
	    (gfmt.fmtid, &gfmt.width, &gfmt.height, &gfmt.bytesperline)) {
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
		struct ng_video_fmt *fmt,
		unsigned char *data)
{
    int i,sw,dw;
    unsigned char *sp,*dp;

    if (gconv && 0 == dest->fmt.bytesperline) {
	/* compressed output */
	dest->size = gconv(dest->data, data, fmt->width * fmt->height);
	return;
    }

    dw = dest->fmt.width * ng_vfmt_to_depth[dest->fmt.fmtid] / 8;
    sw = fmt->width * ng_vfmt_to_depth[fmt->fmtid] / 8;
    if (fmt->bytesperline == sw && dest->fmt.bytesperline == dw) {
	/* can copy in one go */
	if (gconv == NULL) {
	    memcpy(dest->data, data, fmt->bytesperline * fmt->height);
	} else {
	    gconv(dest->data, data, fmt->width * fmt->height);
	}
    } else {
	/* copy line by line */
	dp = dest->data;
	sp = data;
	for (i = 0; i < fmt->height; i++) {
	    if (gconv == NULL) {
		memcpy(dp,sp,dw);
	    } else {
		gconv(dp,sp,fmt->width);
	    }
	    dp += dest->fmt.bytesperline;
	    sp += fmt->bytesperline;
	}
    }
}

struct ng_video_buf*
ng_grabber_capture(struct ng_video_buf *dest)
{
    unsigned char *data;
    
    if (-1 == gfmt.fmtid)
	return NULL;
    if (NULL == (data = grabber->grab_capture()))
	return NULL;

    if (NULL == dest) {
	dest = ng_malloc_video_buf(&ofmt,osize);
    } else {
	dest->fmt  = ofmt;
	dest->size = osize;
    }
    ng_grabber_copy(dest, &gfmt, data);

    if (NULL != webcam && 0 == webcam_put(webcam,dest)) {
	free(webcam);
	webcam = NULL;
    }
    return dest;
}

