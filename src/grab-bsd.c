/*
 * interface to the bsd bktr driver
 *
 *   (c) 2000 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#endif
#include <pthread.h>

#include <X11/Intrinsic.h>

#include "grab.h"
#include "colorspace.h"

#if !defined(__OpenBSD__)
struct GRABBER grab_bsd = {};
#else /* BSD */

#include <machine/ioctl_bt848.h>
#include <machine/ioctl_meteor.h>

/* ---------------------------------------------------------------------- */
/* global variables                                                       */

static int fd = -1;
static int tfd = -1;

static int pf_count;
static struct meteor_pixfmt pf[64];
static int xawtv2pf[VIDEO_FMT_MAX];
static unsigned char *mmap_buf;

static int muted;

#if 0
static struct V4L2_ATTR {
    int id;
    int v4l2;
} v4l2_attr[] = {
    { GRAB_ATTR_VOLUME,   V4L2_CID_AUDIO_VOLUME },
    { GRAB_ATTR_MUTE,     V4L2_CID_AUDIO_MUTE   },
    { GRAB_ATTR_COLOR,    V4L2_CID_SATURATION   },
    { GRAB_ATTR_BRIGHT,   V4L2_CID_BRIGHTNESS   },
    { GRAB_ATTR_HUE,      V4L2_CID_HUE          },
    { GRAB_ATTR_CONTRAST, V4L2_CID_CONTRAST     },
};
#define NUM_ATTR (sizeof(v4l2_attr)/sizeof(struct V4L2_ATTR))
#endif

/* ---------------------------------------------------------------------- */
/* prototypes                                                             */

/* open/close */
static int bsd_open(char *filename);
static int bsd_close(void);

/* control */
static int bsd_input(int input, int norm);
static int bsd_hasattr(int id);
static int bsd_getattr(int id);
static int bsd_setattr(int id, int vol);

static unsigned long bsd_tune(unsigned long freq, int sat);
#if 0
static int bsd_tuned(void);
#endif

/* capture */
static int   bsd_setparm(int format, int *width, int *height, int *linelength);
static void  bsd_start(int fps, int buffers);
static void* bsd_capture(void);
static void  bsd_stop(void);

/* overlay */
static int bsd_setupfb(int sw, int sh, int format, void *base, int bpl);
static int bsd_overlay(int x, int y, int width, int height, int format,
		       struct OVERLAY_CLIP *oc, int count);

/* ---------------------------------------------------------------------- */

static struct STRTAB inputs[] = {
    {  0, "Television"   },
    {  1, "Composite1"   },
    {  2, "S-Video"      },
    {  3, "CSVIDEO"      },
    { -1, NULL }
};
static int inputs_map[] = {
    METEOR_INPUT_DEV1,
    METEOR_INPUT_DEV0,
    METEOR_INPUT_DEV_SVIDEO,
    METEOR_INPUT_DEV2,
};

static struct STRTAB norms[] = {
    {  0, "NTSC"      },
    {  1, "NTSC-JP"   },
    {  2, "PAL"       },
    {  3, "PAL-M"     },
    {  4, "PAL-N"     },
    {  5, "SECAM"     },
    {  6, "RSVD"      },
    { -1, NULL }
};
static int norms_map[] = {
    BT848_IFORM_F_NTSCM,
    BT848_IFORM_F_NTSCJ,
    BT848_IFORM_F_PALBDGHI,
    BT848_IFORM_F_PALM,
    BT848_IFORM_F_PALN,
    BT848_IFORM_F_SECAM,
    BT848_IFORM_F_RSVD,
};

struct GRABBER grab_bsd = {
    name:          "bktr",
    norms:         norms,
    inputs:        inputs,

    grab_open:     bsd_open,
    grab_close:    bsd_close,

    grab_setupfb:  bsd_setupfb,

    grab_input:    bsd_input,
    grab_hasattr:  bsd_hasattr,
    grab_getattr:  bsd_getattr,
    grab_setattr:  bsd_setattr,
};


/* ---------------------------------------------------------------------- */
/* debug output                                                           */

void
bsd_print_format(struct meteor_pixfmt *pf, int format)
{
    switch (pf->type) {
    case METEOR_PIXTYPE_RGB:
	fprintf(stderr,
		"bktr: pf: rgb bpp=%d mask=%ld,%ld,%ld sbytes=%d sshorts=%d",
		pf->Bpp,pf->masks[0],pf->masks[1],pf->masks[2],
		pf->swap_bytes,pf->swap_shorts);
	break;
    case METEOR_PIXTYPE_YUV:
	fprintf(stderr,"bktr: pf: yuv h422 v111 (planar)");
	break;
    case METEOR_PIXTYPE_YUV_PACKED:
	fprintf(stderr,"bktr: pf: yuyv h422 v111 (packed)");
	break;
    case METEOR_PIXTYPE_YUV_12:
	fprintf(stderr,"bktr: pf: yuv h422 v422 (planar)");
	break;
    default:
	fprintf(stderr,"bktr: pf: unknown");
    }
    fprintf(stderr," (fmt=%d)\n",format);
}


/* ---------------------------------------------------------------------- */

static int
bsd_open(char *filename)
{
    int format;

    if (-1 != fd)
	goto err;

#if 0
    if (NULL == filename)
#endif
	filename = "/dev/bktr0";
    if (-1 == (fd = open(filename,O_RDONLY))) {
	fprintf(stderr,"open %s: %s\n", filename,strerror(errno));
	goto err;
    }

    /* video formats */
    for (format = 0; format < VIDEO_FMT_MAX; format++)
	xawtv2pf[format] = -1;

    for (pf_count = 0; pf_count < 64; pf_count++) {
	pf[pf_count].index = pf_count;
	if (-1 == ioctl(fd, METEORGSUPPIXFMT,pf+pf_count)) {
	    perror("ioctl METEORGSUPPIXFMT");
	    if (0 == pf_count)
		goto err;
	    break;
	}
	format = -1;
	switch (pf[pf_count].type) {
	case METEOR_PIXTYPE_RGB:
	    switch(pf[pf_count].masks[0]) {
	    case 31744: /* 15 bpp */
	        format = pf[pf_count].swap_bytes
		    ? VIDEO_RGB15_LE : VIDEO_RGB15_BE;
		break;
	    case 63488: /* 16 bpp */
	        format = pf[pf_count].swap_bytes
		    ? VIDEO_RGB16_LE : VIDEO_RGB16_BE;
		break;
	    case 16711680: /* 24/32 bpp */
		if (pf[pf_count].Bpp == 3 &&
		    pf[pf_count].swap_bytes == 1) {
		    format = VIDEO_BGR24;
		} else if (pf[pf_count].Bpp == 4 &&
			   pf[pf_count].swap_bytes == 1 &&
			   pf[pf_count].swap_shorts == 1) {
		    format = VIDEO_BGR32;
		} else if (pf[pf_count].Bpp == 4 &&
			   pf[pf_count].swap_bytes == 0 &&
			   pf[pf_count].swap_shorts == 0) {
		    format = VIDEO_RGB32;
		}
	    }
	    break;
	case METEOR_PIXTYPE_YUV:
	    /* fixme */
	    break;
	case METEOR_PIXTYPE_YUV_PACKED:
	    /* fixme */
	    break;
	case METEOR_PIXTYPE_YUV_12:
	    /* fixme */
	    break;
	}
	if (-1 != format)
	    xawtv2pf[format] = pf_count;

	bsd_print_format(pf+pf_count,format);
    }

    mmap_buf = mmap(0,768*576*4, PROT_READ, MAP_SHARED, fd, 0);
    if ((unsigned char*)-1 == mmap_buf) {
	perror("bktr: mmap");
    } else {
	grab_bsd.grab_setparams = bsd_setparm;
	grab_bsd.grab_start     = bsd_start;
	grab_bsd.grab_capture   = bsd_capture;
	grab_bsd.grab_stop      = bsd_stop;
    }

    if (-1 == (tfd = open("/dev/tuner0",O_RDONLY))) {
	fprintf(stderr,"open %s: %s\n", "/dev/tuner",strerror(errno));	
    } else {
	grab_bsd.grab_tune = bsd_tune;
    }

    return fd;

 err:
    if (fd != -1) {
	close(fd);
	fd = -1;
    }
    return -1;
}

static int
bsd_close()
{
    if (-1 == fd)
	return 0;

    if (debug)
	fprintf(stderr, "bktr: close\n");

    close(fd);
    fd = -1;
    if (-1 != tfd) {
	close(tfd);
	tfd = -1;
    }
    return 0;
}

/* ---------------------------------------------------------------------- */

static int bsd_input(int input, int norm)
{
    if (-1 != norm)
	if (-1 == ioctl(fd,BT848SFMT,&norms_map[norm]))
	    perror("BT848SFMT");
    if (-1 != input)
	if (-1 == ioctl(fd,METEORSINPUT,&inputs_map[input]))
	    perror("METEORSINPUT");
    return 0;
}

static unsigned long bsd_tune(unsigned long freq, int sat)
{
    if (-1 == freq) {
	if (-1 == ioctl(tfd, TVTUNER_GETFREQ, &freq))
	    perror("ioctl TVTUNER_GETFREQ");
	if (debug)
	    fprintf(stderr,"bktr: get freq: %.3f\n",(float)freq/16);
	return 0;
    }
    if (debug)
	fprintf(stderr,"bktr: set freq: %.3f\n",(float)freq/16);
    if (-1 == ioctl(tfd, TVTUNER_SETFREQ, &freq))
	perror("ioctl TVTUNER_SETFREQ");
    return 0;
}

static int
bsd_hasattr(int id)
{
    switch (id) {
    case GRAB_ATTR_MUTE:
	return 1;
    default:
	return 0;
    }
}

static
int bsd_getattr(int id)
{
    switch (id) {
    case GRAB_ATTR_MUTE:
	return muted;
    default:
	return -1;
    }
}

static
int bsd_setattr(int id, int val)
{
    int arg;
    
    switch (id) {
    case GRAB_ATTR_MUTE:
	muted = val;
	arg = val ? AUDIO_MUTE : AUDIO_UNMUTE;
	if (-1 == ioctl(tfd, BT848_SAUDIO, &arg)) {
	    perror("ioctl BT848_SAUDIO");
	    return -1;
	}
	return 0;
    default:
	return -1;
    }
}

/* ---------------------------------------------------------------------- */
/* overlay                                                                */

static const int start  = METEOR_CAP_CONTINOUS;
static const int single = METEOR_CAP_SINGLE;
static const int stop   = METEOR_CAP_STOP_CONT;

static struct meteor_video   fb,pos;
static struct meteor_geomet  ovgeo;
static struct meteor_pixfmt  *ovfmt;
static struct bktr_clip      clip[BT848_MAX_CLIP_NODE];
static int                   overlay;

static int
bsd_setupfb(int sw, int sh, int format, void *base, int bpl)
{
    /* if error
    return -1;
    */

    fb.addr     = (long)base;
    fb.width    = bpl;
    fb.banksize = bpl * sh;
    fb.ramsize  = bpl * sh / 1024;

    grab_bsd.grab_overlay = bsd_overlay;
    return 0;
}

static int bsd_overlay(int x, int y, int width, int height, int format,
			struct OVERLAY_CLIP *oc, int count)
{
    int i,xadjust=0,yadjust=0,win_width,win_height,win_x,win_y;
    
    if (-1 == ioctl(fd, METEORCAPTUR, &stop))
	perror("ioctl METEORCAPTUR(stop)");
    overlay = 0;
    if (width == 0 || height == 0)
	return 0;

    if (-1 == xawtv2pf[format])
	return -1;

    /* fixups - fixme: no fixed max size */
    win_x      = x;
    win_y      = y;
    win_width  = width;
    win_height = height;
    if (win_width > 768) {
	win_width = 768;
	win_x += (width - win_width)/2;
    }
    if (win_height > 576) {
	win_height = 576;
	win_y +=  (height - win_height)/2;
    }
    grabber_fix_ratio(&win_width,&win_height,&win_x,&win_y);
    xadjust = win_x - x;
    yadjust = win_y - y;

    /* fill data */
    pos           = fb;
    pos.addr     += win_y*pos.width;
    pos.addr     += win_x*format2depth[format]>>3;
    ovgeo.rows    = win_height;
    ovgeo.columns = win_width;
    ovgeo.frames  = 1;
    ovgeo.oformat = 0x10000;

    if (debug)
	fprintf(stderr,"bktr: overlay win=%dx%d+%d+%d, %d clips\n",
		win_width,win_height,win_x,win_y,count);

    /* clipping */
    memset(clip,0,sizeof(clip));
    for (i = 0; i < count; i++) {
#if 0
	/* This way it *should* work IMHO ... */
	clip[i].x_min      = oc[i].x1 - xadjust;
	clip[i].x_max      = oc[i].x2 - xadjust;
	clip[i].y_min      = oc[i].y1 - yadjust;
	clip[i].y_max      = oc[i].y2 - yadjust;
#else
	/* This way it does work.  Sort of ... */
	clip[i].x_min      = (oc[i].y1 - yadjust) >> 1;
	clip[i].x_max      = (oc[i].y2 - yadjust) >> 1;
	clip[i].y_min      = oc[i].x1 - xadjust;
	clip[i].y_max      = oc[i].x2 - xadjust;
#endif
	if (debug)
	    fprintf(stderr,"bktr:   clip x=%d-%d y=%d-%d\n",
		    clip[i].x_min,clip[i].x_max,
		    clip[i].y_min,clip[i].y_max);
    }
    ovfmt = pf+xawtv2pf[format];

    if (-1 == ioctl(fd, METEORSVIDEO, &pos))
	perror("ioctl METEORSVIDEO");
    if (-1 == ioctl(fd, METEORSETGEO, &ovgeo))
	perror("ioctl METEORSETGEO");
    if (-1 == ioctl(fd, METEORSACTPIXFMT, ovfmt))
	perror("ioctl METEORSACTPIXFMT");
    if (-1 == ioctl(fd, BT848SCLIP, &clip))
	perror("ioctl BT848SCLIP");
    if (-1 == ioctl(fd, METEORCAPTUR, &start))
	perror("ioctl METEORCAPTUR(start)");
    overlay = 1;

    return 0;
}

/* ---------------------------------------------------------------------- */
/* capture                                                                */

static struct meteor_video   nofb;
static struct meteor_geomet  capgeo;
static struct meteor_pixfmt  *capfmt;
static int                   reenable_overlay,capfps;

static int
bsd_setparm(int format, int *width, int *height, int *linelength)
{
    if (-1 == xawtv2pf[format])
	return -1;

    if (*width > 768)
	*width = 768;
    if (*height > 576)
	*height = 576;
    capgeo.rows    = *height;
    capgeo.columns = *width;
    capgeo.frames  = 1;
    capgeo.oformat = 0x10000;
    capfmt = pf+xawtv2pf[format];
    return 0;
}

static void
bsd_setcapture(int on)
{
    if (on) {
	/* switch to capture */
	if (overlay) {
	    reenable_overlay = 1;
	    if (-1 == ioctl(fd, METEORCAPTUR, &stop))
		perror("ioctl METEORCAPTUR(stop)");
	    overlay = 0;
	}
	
	if (-1 == ioctl(fd, METEORSVIDEO, &nofb))
	    perror("ioctl METEORSVIDEO");
	if (-1 == ioctl(fd, METEORSETGEO, &capgeo))
	    perror("ioctl METEORSETGEO");
	if (-1 == ioctl(fd, METEORSACTPIXFMT, capfmt))
	    perror("ioctl METEORSACTPIXFMT");

    } else {
	/* switch to overlay */
	if (reenable_overlay) {
	    reenable_overlay = 0;
	    if (-1 == ioctl(fd, METEORSVIDEO, &pos))
		perror("ioctl METEORSVIDEO");
	    if (-1 == ioctl(fd, METEORSETGEO, &ovgeo))
		perror("ioctl METEORSETGEO");
	    if (-1 == ioctl(fd, METEORSACTPIXFMT, ovfmt))
		perror("ioctl METEORSACTPIXFMT");
	    if (-1 == ioctl(fd, BT848SCLIP, &clip))
		perror("ioctl BT848SCLIP");
	    if (-1 == ioctl(fd, METEORCAPTUR, &start))
		perror("ioctl METEORCAPTUR(start)");
	    overlay = 1;
	}
    }
}
    
static void
bsd_start(int fps, int buffers)
{
    bsd_setcapture(1);
    capfps = fps;
#if 0
    gettimeofday(&grab_start,NULL);
#endif
}

static void*
bsd_capture(void)
{
    if (0 == capfps) {
	/* grab one buffer */
	bsd_setcapture(1);
	if (-1 == ioctl(fd, METEORCAPTUR, &single))
	    perror("ioctl METEORCAPTUR(single)");
	usleep(100*1000); /* FIXME: use SIGUSR1 instead */
	bsd_setcapture(0);
	return mmap_buf;
    }

#if 0
    /* rate control */
    rc = grabber_sw_rate(&grab_start,grab_fps,grab_frames);
    if (rc <= 0)
	goto next_frame;
    grab_frames++;
#endif
    return NULL;
}

static void
bsd_stop(void)
{
    bsd_setcapture(0);
    capfps = 0;
}


#endif /* BSD */
