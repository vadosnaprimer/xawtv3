#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <endian.h>
#include <string.h>

#include "config.h"

#include "grab.h"
#include "colorspace.h"
#include "mjpeg.h"
#include "commands.h"

extern struct GRABBER grab_v4l;
extern struct GRABBER grab_v4l2;

/*-------------------------------------------------------------------------*/

int   fd_grab;

struct        GRABBER *grabber;
static struct GRABBER *grabbers[] = {
    &grab_v4l2,
    &grab_v4l,
};

#define GRABBER_COUNT (sizeof(grabbers)/sizeof(struct GRABBERS*))

unsigned int format2depth[] = {
    0,               /* unused   */
    8,               /* RGB8     */
    8,               /* GRAY8    */
    16,              /* RGB15 LE */
    16,              /* RGB16 LE */
    16,              /* RGB15 BE */
    16,              /* RGB16 BE */
    24,              /* BGR24    */
    32,              /* BGR32    */
    24,              /* RGB24    */
    32,              /* RGB32    */
    16,              /* LUT2     */
    32,              /* LUT4     */
    16,		     /* YUV422   */
    16,		     /* YUV422P  */
    12,		     /* YUV420P  */
    0,		     /* MJPEG    */
};

unsigned char* format_desc[] = {
    "",
    "8 bit PseudoColor (dithering)",
    "8 bit StaticGray",
    "15 bit TrueColor (LE)",
    "16 bit TrueColor (LE)",
    "15 bit TrueColor (BE)",
    "16 bit TrueColor (BE)",
    "24 bit TrueColor (LE: bgr)",
    "32 bit TrueColor (LE: bgr-)",
    "24 bit TrueColor (BE: rgb)",
    "32 bit TrueColor (BE: -rgb)",
    "16 bit TrueColor (lut)",
    "32 bit TrueColor (lut)",
    "16 bit YUV 4:2:2",
    "16 bit YUV 4:2:2 (planar)",
    "12 bit YUV 4:2:0 (planar)",
    "MJPEG"
};

/*-------------------------------------------------------------------------*/

extern char v4l_conf[];

void
grabber_run_v4l_conf(void)
{    
    if (do_overlay) {
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
}

int
grabber_open(char *device, int sw, int sh, void *base, int format, int width)
{
    int i;

    /* check all grabber drivers */
    for (i = 0; i < GRABBER_COUNT; i++) {
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

static int         grabber_width;
static int         grabber_height;
static float       grabber_depth;
static int         grabber_linelength;
static int         grabber_format;
static color_conv  grabber_conv;
static void*       grabber_data;

struct CONV_LIST {
    int        format;
    int        lut;
    color_conv converter;
    void       (*init)(int width, int height);
    void       (*cleanup)();
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

#ifdef HAVE_LIBJPEG
static struct CONV_LIST mjpg_list[] = {
    { VIDEO_YUV420P, 0, mjpg_yuv420_compress, mjpg_yuv_init, mjpg_cleanup },
    { VIDEO_YUV422P, 0, mjpg_yuv422_compress, mjpg_yuv_init, mjpg_cleanup },
    { VIDEO_RGB24,   0, mjpg_rgb_compress,    mjpg_rgb_init, mjpg_cleanup },
    { VIDEO_BGR24,   0, mjpg_bgr_compress,    mjpg_rgb_init, mjpg_cleanup },
    { -1,0, NULL }
};
#endif


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
#ifdef HAVE_LIBJPEG
    mjpg_list,
#else
    NULL,
#endif
};

int
grabber_setparams(int format, int *width, int *height,
		  int *linelength, int lut_valid)
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
    grabber_depth = format2depth[format]/8;
    w = *width;
    h = *height;

    /* try native format first */
    grabber_width      = w;
    grabber_height     = h;
    grabber_linelength = 0;
    grabber_format     = format;
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
	    *width,*height,format_desc[format]);
    grabber_format = -1;
    return -1;

 found:
    if (debug)
	fprintf(stderr,"grab: req: %dx%d %s\n",
		*width,*height,format_desc[format]);
    *width      = grabber_width;
    *height     = grabber_height;
    if (grabber_linelength == 0)
	grabber_linelength = grabber_width*grabber_depth;
    *linelength = grabber_linelength;
    if (debug)
	fprintf(stderr,"grab: use: %dx%d %s\n",
		*width,*height,format_desc[grabber_format]);
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
grabber_capture(void *dest, int dest_linelength, int single, int *size)
{
    int rc;
    void *data;

    if (size) *size = 0;
    if (-1 == grabber_format)
	return NULL;
    if (NULL == (data = grabber->grab_capture(single)))
	return NULL;

    if (0 == dest_linelength)
	dest_linelength = grabber_width * grabber_depth;

    if (dest == NULL) {
	if (grabber_conv == NULL && dest_linelength == grabber_linelength)
	    return data;
	if (grabber_data == NULL)
	    grabber_data = malloc(grabber_height * dest_linelength);
	rc = grabber_copy(grabber_data, dest_linelength,
			  data, grabber_linelength,
			  grabber_width, grabber_height, grabber_depth);
	if (size) *size = rc;
	return grabber_data;
    }

    rc = grabber_copy(dest, dest_linelength,
		      data, grabber_linelength,
		      grabber_width, grabber_height, grabber_depth);
    if (size) *size = rc;
    return dest;
}
