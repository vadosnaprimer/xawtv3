/*
 * interface to the v4l driver
 *
 *   (c) 1997-99 Gerd Knorr <kraxel@goldbach.in-berlin.de>
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

#include <X11/Intrinsic.h>

#include "grab-ng.h"

#ifndef __linux__
struct GRABBER grab_v4l = {};
#else /* __linux__ */

#include <asm/types.h>		/* XXX glibc */
#include "videodev.h"

#define SYNC_TIMEOUT 1

/* ---------------------------------------------------------------------- */

/* open+close */
static int   grab_open(char *filename);
static int   grab_close(void);

/* overlay */
static int   grab_setupfb(int sw, int sh, int format, void *base, int width);
static int   grab_overlay(int x, int y, int width, int height, int format,
			  struct OVERLAY_CLIP *oc, int count);
static int   grab_offscreen(int y, int width, int height, int format);

/* capture */
static int   grab_mm_setparams(int format, int *width, int *height,
			       int *linelength);
static void  grab_mm_start(int fps, int buffers);
static void* grab_mm_capture(void);
static void  grab_mm_stop(void);
static int   grab_read_setparams(int format, int *width, int *height,
				 int *linelength);
static void  grab_read_start(int fps, int buffers);
static void* grab_read_capture(void);
static void  grab_read_stop(void);

/* control */
static unsigned long grab_tune(unsigned long freq, int sat);
static int   grab_tuned(void);
static int   grab_input(int input, int norm);
static int   grab_hasattr(int id);
static int   grab_getattr(int id);
static int   grab_setattr(int id, int val);

/* internal helpers */
static int   grab_wait(void);

/* ---------------------------------------------------------------------- */

static char *device_cap[] = {
    "capture", "tuner", "teletext", "overlay", "chromakey", "clipping",
    "frameram", "scales", "monochrome", NULL
};

static char *device_pal[] = {
    "-", "grey", "hi240", "rgb16", "rgb24", "rgb32", "rgb15",
    "yuv422", "yuyv", "uyvy", "yuv420", "yuv411", "raw",
    "yuv422p", "yuv411p", "yuv420p", "yuv410p"
};
#define PALETTE(x) ((x < sizeof(device_pal)/sizeof(char*)) ? device_pal[x] : "UNKNOWN")

static struct STRTAB stereo[] = {
    {  0,                  "auto"    },
    {  VIDEO_SOUND_MONO,   "mono"    },
    {  VIDEO_SOUND_STEREO, "stereo"  },
    {  VIDEO_SOUND_LANG1,  "lang1"   },
    {  VIDEO_SOUND_LANG2,  "lang2"   },
    { -1, NULL },
};
static struct STRTAB norms[] = {
    {  VIDEO_MODE_PAL,     "PAL"   },
    {  VIDEO_MODE_NTSC,    "NTSC"  },
    {  VIDEO_MODE_SECAM,   "SECAM" },
    {  VIDEO_MODE_AUTO,    "AUTO"  },
    { -1, NULL }
};
static struct STRTAB norms_bttv[] = {
    {  VIDEO_MODE_PAL,   "PAL"     },
    {  VIDEO_MODE_NTSC,  "NTSC"    },
    {  VIDEO_MODE_SECAM, "SECAM"   },
    {  3,                "PAL-NC"  },
    {  4,                "PAL-M"   },
    {  5,                "PAL-N"   },
    {  6,                "NTSC-JP" },
    { -1, NULL }
};
static struct STRTAB *inputs;

static int    fd = -1;

/* generic informations */
static struct video_capability  capability;
static struct video_channel     *channels;
static struct video_audio       audio;
static struct video_tuner       *tuner;
static struct video_picture     pict;
static int                      cur_input;
static int                      cur_norm;

/* overlay */
static struct video_window      ov_win;
static struct video_picture     ov_pict;
static struct video_clip        ov_clips[32];
static struct video_buffer      ov_fbuf;

/* screen grab */
#define MAX_BUFFERS 16
static struct video_mmap        gb_buf[MAX_BUFFERS];
static int                      pixmap_bytes;
static int                      gb_grab,gb_sync,gb_bufcount = 1;
static struct video_mbuf        gb_buffers = { 2*0x151000, 0, {0,0x151000 }};
static int                      gb_pal[64];

static struct video_window      rd_win;
static struct video_picture     rd_pict;
static int                      rd_size;
static char                     *rd_buf;

/* rate control */
static struct timeval           grab_start;
static int                      grab_fps;
static int                      grab_frames;

static unsigned short format2palette[] = {
    0,				/* unused */
    VIDEO_PALETTE_HI240,	/* RGB8   */
    VIDEO_PALETTE_GREY,		/* GRAY8  */
#if 0 /* __BYTE_ORDER == __BIG_ENDIAN */
    0,
    0,
    VIDEO_PALETTE_RGB555,	/* RGB15_BE  */
    VIDEO_PALETTE_RGB565,	/* RGB16_BE  */
    0,
    0,
    VIDEO_PALETTE_RGB24,	/* RGB24     */
    VIDEO_PALETTE_RGB32,	/* RGB32     */
#else
    VIDEO_PALETTE_RGB555,	/* RGB15_LE  */
    VIDEO_PALETTE_RGB565,	/* RGB16_LE  */
    0,
    0,
    VIDEO_PALETTE_RGB24,	/* BGR24     */
    VIDEO_PALETTE_RGB32,	/* BGR32     */
    0,
    0,
#endif
    0,                          /* LUT 2    */
    0,                          /* LUT 4    */
    VIDEO_PALETTE_YUV422,       /* YUV422   */
    VIDEO_PALETTE_YUV422P,      /* YUV422P  */
#if 0 /* broken in bttv (fixed in 0.8.x) */
    VIDEO_PALETTE_YUV420P,      /* YUV420P  */
#else
    0,                          /* YUV420P  */
#endif
};

static char *map = NULL;

/* state */
static int                      swidth, sheight;
static int                      ov_enabled;  /* turned on? */
static int                      ov_on;       /* real state (for tmp off) */

/* pass 0/1 by reference */
static int                      one = 1, zero = 0;

struct GRABBER grab_v4l = {
    name:         "v4l",
    norms:        norms,
    audio_modes:  stereo,

    grab_open:    grab_open,
    grab_close:   grab_close,

    grab_setupfb: grab_setupfb,

    grab_tune:    grab_tune,
    grab_tuned:   grab_tuned,
    grab_input:   grab_input,
    grab_hasattr: grab_hasattr,
    grab_getattr: grab_getattr,
    grab_setattr: grab_setattr,
};

/* ---------------------------------------------------------------------- */

static struct GRAB_ATTR {
    int   id;
    int   have;
    int   get;
    int   set;
    void  *arg;
} grab_attr [] = {
    { GRAB_ATTR_VOLUME,   1, VIDIOCGAUDIO, VIDIOCSAUDIO, &audio   },
    { GRAB_ATTR_MUTE,     1, VIDIOCGAUDIO, VIDIOCSAUDIO, &audio   },
    { GRAB_ATTR_MODE,     1, VIDIOCGAUDIO, VIDIOCSAUDIO, &audio   },
    
    { GRAB_ATTR_COLOR,    1, VIDIOCGPICT,  VIDIOCSPICT,  &pict    },
    { GRAB_ATTR_BRIGHT,   1, VIDIOCGPICT,  VIDIOCSPICT,  &pict    },
    { GRAB_ATTR_HUE,      1, VIDIOCGPICT,  VIDIOCSPICT,  &pict    },
    { GRAB_ATTR_CONTRAST, 1, VIDIOCGPICT,  VIDIOCSPICT,  &pict    },
};

#define NUM_ATTR (sizeof(grab_attr)/sizeof(struct GRAB_ATTR))

/* ---------------------------------------------------------------------- */

static int alarms;

static void
sigalarm(int signal)
{
    alarms++;
    fprintf(stderr,"v4l: oops: got sigalarm\n");
}

static void
siginit(void)
{
    struct sigaction act,old;
    
    memset(&act,0,sizeof(act));
    act.sa_handler  = sigalarm;
    sigemptyset(&act.sa_mask);
    sigaction(SIGALRM,&act,&old);
}

/* ---------------------------------------------------------------------- */

#define PREFIX "ioctl: "

static int
xioctl(int fd, int cmd, void *arg)
{
    int rc;

    rc = ioctl(fd,cmd,arg);
    if (0 == rc && debug < 2)
	return 0;
    switch (cmd) {
    case VIDIOCGCAP:
    {
	struct video_capability *a = arg;
	fprintf(stderr,PREFIX "VIDIOCGCAP(%s,type=0x%x,chan=%d,audio=%d,"
		"size=%dx%d-%dx%d)",
		a->name,a->type,a->channels,a->audios,
		a->minwidth,a->minheight,a->maxwidth,a->maxheight);
	break;
    }
    case VIDIOCGCHAN:
    case VIDIOCSCHAN:
    {
	struct video_channel *a = arg;
	fprintf(stderr,PREFIX "%s(%d,%s,flags=0x%x,type=%d,norm=%d)",
		(cmd == VIDIOCGCHAN) ? "VIDIOCGCHAN" : "VIDIOCSCHAN",
		a->channel,a->name,a->flags,a->type,a->norm);
	break;
    }
    case VIDIOCGTUNER:
    case VIDIOCSTUNER:
    {
	struct video_tuner *a = arg;
	fprintf(stderr,PREFIX "%s(%d,%s,range=%ld-%ld,flags=0x%x,"
		"mode=%d,signal=%d)",
		(cmd == VIDIOCGTUNER) ? "VIDIOCGTUNER" : "VIDIOCSTUNER",
		a->tuner,a->name,a->rangelow,a->rangehigh,
		a->flags,a->mode,a->signal);
	break;
    }
    case VIDIOCGPICT:
    case VIDIOCSPICT:
    {
	struct video_picture *a = arg;
	fprintf(stderr,PREFIX "%s(params=%d/%d/%d/%d/%d,depth=%d,fmt=%d)",
		(cmd == VIDIOCGPICT) ? "VIDIOCGPICT" : "VIDIOCSPICT",
		a->brightness,a->hue,a->colour,a->contrast,a->whiteness,
		a->depth,a->palette);
	break;
    }
    case VIDIOCGAUDIO:
    case VIDIOCSAUDIO:
    {
	struct video_audio *a = arg;
	fprintf(stderr,PREFIX "%s(%d,%s,vol=%d,balance=%d,bass=%d,treble=%d,"
		"mode=0x%x,step=%d)",
		(cmd == VIDIOCGAUDIO) ? "VIDIOCGAUDIO" : "VIDIOCSAUDIO",
		a->audio,a->name,a->volume,a->balance,a->bass,
		a->treble,a->mode,a->step);
	break;
    }
    case VIDIOCGWIN:
    case VIDIOCSWIN:
    {
	struct video_window *a = arg;
	fprintf(stderr,PREFIX "%s(win=%dx%d+%d+%d,key=%d,flags=0x%x,clips=%d)",
		(cmd == VIDIOCGWIN) ? "VIDIOCGWIN" : "VIDIOCSWIN",
		a->width,a->height,a->x,a->y,
		a->chromakey,a->flags,a->clipcount);
	break;
    }
    case VIDIOCGFBUF:
    case VIDIOCSFBUF:
    {
	struct video_buffer *a = arg;
	fprintf(stderr,PREFIX "%s(base=%p,size=%dx%d,depth=%d,bpl=%d)",
		(cmd == VIDIOCGFBUF) ? "VIDIOCGFBUF" : "VIDIOCSFBUF",
		a->base,a->width,a->height,a->depth,a->bytesperline);
	break;
    }
    case VIDIOCGFREQ:
    case VIDIOCSFREQ:
    {
	unsigned long *a = arg;
	fprintf(stderr,PREFIX "%s(%.3f MHz)",
		(cmd == VIDIOCGFREQ) ? "VIDIOCGFREQ" : "VIDIOCSFREQ",
		(float)*a/16);
	break;
    }
    case VIDIOCCAPTURE:
    {
	int *a = arg;
	fprintf(stderr,PREFIX "VIDIOCCAPTURE(%s)",
		*a ? "on" : "off");
	break;
    }
    case VIDIOCGMBUF:
    {
	struct video_mbuf *a = arg;	
	fprintf(stderr,PREFIX "VIDIOCGMBUF(size=%d,frames=%d)",
		a->size,a->frames);
	break;
    }
    case VIDIOCMCAPTURE:
    {
	struct video_mmap *a = arg;	
	fprintf(stderr,PREFIX "VIDIOCMCAPTURE(%d,fmt=%d,size=%dx%d)",
		a->frame,a->format,a->width,a->height);
	break;
    }
    case VIDIOCSYNC:
    {
	int *a = arg;
	fprintf(stderr,PREFIX "VIDIOCSYNC(%d)",*a);
	break;
    }
    default:
	fprintf(stderr,PREFIX "UNKNOWN(cmd=0x%x)",cmd);
	break;
    }
    fprintf(stderr,": %s\n",(rc == 0) ? "ok" : strerror(errno));
    return rc;
}

/* ---------------------------------------------------------------------- */

static int
grab_open(char *filename)
{
    int i,rc;

    if (-1 != fd)
	goto err;

    grabber_run_v4l_conf();
    if (-1 == (fd = open(filename ? filename : "/dev/video",O_RDWR))) {
	fprintf(stderr,"open %s: %s\n",
		filename ? filename : "/dev/video",strerror(errno));
	goto err;
    }

    if (-1 == ioctl(fd,VIDIOCGCAP,&capability))
	goto err;

    if (debug)
	fprintf(stderr, "v4l: open\n");

    fcntl(fd,F_SETFD,FD_CLOEXEC);
    siginit();
    if (debug)
	fprintf(stderr,"v4l: device is %s\n",capability.name);    
    sprintf(grab_v4l.name = malloc(strlen(capability.name)+8),
	    "v4l: %s",capability.name);
    if (debug) {
	fprintf(stderr,"capabilities: ");
	for (i = 0; device_cap[i] != NULL; i++)
	    if (capability.type & (1 << i))
		fprintf(stderr," %s",device_cap[i]);
	fprintf(stderr,"\n");
    }

    /* input sources */
    if (debug)
	fprintf(stderr,"  channels: %d\n",capability.channels);
    channels = malloc(sizeof(struct video_channel)*capability.channels);
    memset(channels,0,sizeof(struct video_channel)*capability.channels);
    inputs = malloc(sizeof(struct STRTAB)*(capability.channels+1));
    memset(inputs,0,sizeof(struct STRTAB)*(capability.channels+1));
    for (i = 0; i < capability.channels; i++) {
	channels[i].channel = i;
	xioctl(fd,VIDIOCGCHAN,&channels[i]);
	inputs[i].nr  = i;
	inputs[i].str = channels[i].name;
	if (debug)
	    fprintf(stderr,"    %s: %d %s%s %s%s\n",
		    channels[i].name,
		    channels[i].tuners,
		    (channels[i].flags & VIDEO_VC_TUNER)   ? "tuner "  : "",
		    (channels[i].flags & VIDEO_VC_AUDIO)   ? "audio "  : "",
		    (channels[i].type & VIDEO_TYPE_TV)     ? "tv "     : "",
		    (channels[i].type & VIDEO_TYPE_CAMERA) ? "camera " : "");
    }
    inputs[i].nr  = -1;
    inputs[i].str = NULL;
    grab_v4l.inputs =inputs;

    /* audios */
    if (debug)
	fprintf(stderr,"  audios  : %d\n",capability.audios);
    if (capability.audios) {
	audio.audio = 0;
	xioctl(fd,VIDIOCGAUDIO,&audio);
	if (debug) {
	    fprintf(stderr,"    %d (%s): ",i,audio.name);
	    if (audio.flags & VIDEO_AUDIO_MUTABLE)
		fprintf(stderr,"muted=%s ",
			(audio.flags&VIDEO_AUDIO_MUTE) ? "yes":"no");
	    if (audio.flags & VIDEO_AUDIO_VOLUME)
		fprintf(stderr,"volume=%d ",audio.volume);
	    if (audio.flags & VIDEO_AUDIO_BASS)
		fprintf(stderr,"bass=%d ",audio.bass);
	    if (audio.flags & VIDEO_AUDIO_TREBLE)
		fprintf(stderr,"treble=%d ",audio.treble);
	    fprintf(stderr,"\n");
	}
	if (!(audio.flags & VIDEO_AUDIO_VOLUME)) {
	    grab_attr[0].have = 0; /* volume     */
	}
    } else {
	grab_attr[0].have = 0; /* volume     */
	grab_attr[1].have = 0; /* mute       */
	grab_attr[2].have = 0; /* audio mode */
    }

    if (debug)
	fprintf(stderr,"  size    : %dx%d => %dx%d\n",
		capability.minwidth,capability.minheight,
		capability.maxwidth,capability.maxheight);

    /* tuner (more than one???) */
    if (capability.type & VID_TYPE_TUNER) {
	tuner = malloc(sizeof(struct video_tuner));
	memset(tuner,0,sizeof(struct video_tuner));
	xioctl(fd,VIDIOCGTUNER,tuner);
	if (debug)
	    fprintf(stderr,"  tuner   : %s %lu-%lu",
		    tuner->name,tuner->rangelow,tuner->rangehigh);
	for (i = 0; norms[i].str != NULL; i++) {
	    if (tuner->flags & (1<<i)) {
		if (debug)
		    fprintf(stderr," %s",norms[i].str);
	    } else
		norms[i].nr = -1;
	}
	if (debug)
	    fprintf(stderr,"\n");
    } else {
	struct video_channel vchan;

	memcpy(&vchan, &channels[0], sizeof(struct video_channel));
	for (i = 0; norms[i].str != NULL; i++) {
	    vchan.norm = i;
	    if (-1 == xioctl(fd,VIDIOCSCHAN,&vchan))
		norms[i].nr = -1;
	    else if (debug)
		fprintf(stderr," %s",norms[i].str);
	}
	if (debug)
	    fprintf(stderr,"\n");
	if (-1 == xioctl(fd,VIDIOCSCHAN,&channels[0])) {
	    fprintf(stderr,"v4l: you need a newer bttv version (>= 0.5.14)\n");
	    goto err;
	}
	grab_v4l.grab_tune  = NULL;
	grab_v4l.grab_tuned = NULL;
    }
#if 1
#define BTTV_VERSION  	        _IOR('v' , BASE_VIDIOCPRIVATE+6, int)
    /* dirty hack time / v4l design flaw -- works with bttv only
     * this adds support for a few less common PAL versions */
    if (-1 != (rc = ioctl(fd,BTTV_VERSION,0))) {
	grab_v4l.norms = norms_bttv;
	if (debug || rc < 0x000700)
	    fprintf(stderr,"v4l: bttv version %d.%d.%d\n",
		    (rc >> 16) & 0xff,
		    (rc >> 8)  & 0xff,
		    rc         & 0xff);
	if (rc < 0x000700)
	    fprintf(stderr,
		    "v4l: prehistoric bttv version found, please try to\n"
		    "     upgrade the driver before mailing bug reports\n");
    }
#endif
    
    /* frame buffer */
    xioctl(fd,VIDIOCGFBUF,&ov_fbuf);
    if (debug)
	fprintf(stderr,"  fbuffer : base=0x%p size=%dx%d depth=%d bpl=%d\n",
		ov_fbuf.base, ov_fbuf.width, ov_fbuf.height,
		ov_fbuf.depth, ov_fbuf.bytesperline);

    /* chroma keying */
    if (capability.type & VID_TYPE_CHROMAKEY)
	grab_v4l.colorkey = 0x00cc00ff;

    /* picture parameters */
    xioctl(fd,VIDIOCGPICT,&ov_pict);
    rd_pict = ov_pict;

    if (debug) {
	fprintf(stderr,
		"  picture : brightness=%d hue=%d colour=%d contrast=%d\n",
		ov_pict.brightness, ov_pict.hue,
		ov_pict.colour, ov_pict.contrast);
	fprintf(stderr,
		"  picture : whiteness=%d depth=%d palette=%s\n",
		ov_pict.whiteness, ov_pict.depth, PALETTE(ov_pict.palette));
    }

    if (capability.type & VID_TYPE_CAPTURE) {
	/* map grab buffer */
	if (0 == xioctl(fd,VIDIOCGMBUF,&gb_buffers)) {
	    if (debug)
		fprintf(stderr,"  mbuf: size=%d frames=%d\n",
			gb_buffers.size,gb_buffers.frames);
	    map = mmap(0,gb_buffers.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
	    if ((char*)-1 == map)
		perror("mmap");
	} else {
	    map = (char*)-1;
	}
	if ((char*)-1 != map) {
	    if (debug)
		fprintf(stderr,"  v4l: using mapped buffers for capture\n");
	    grab_v4l.grab_setparams = grab_mm_setparams;
	    grab_v4l.grab_start     = grab_mm_start;
	    grab_v4l.grab_capture   = grab_mm_capture;
	    grab_v4l.grab_stop      = grab_mm_stop;
	} else {
	    if (debug)
		fprintf(stderr,"  v4l: using read() for capture\n");
	    grab_v4l.grab_setparams = grab_read_setparams;
	    grab_v4l.grab_start     = grab_read_start;
	    grab_v4l.grab_capture   = grab_read_capture;
	    grab_v4l.grab_stop      = grab_read_stop;
	}
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
grab_close(void)
{
    if (-1 == fd)
	return 0;

    while (gb_grab > gb_sync)
	grab_wait();

    if ((char*)-1 != map)
	munmap(map,gb_buffers.size);

    if (debug)
	fprintf(stderr, "v4l: close\n");

    close(fd);
    fd = -1;
    return 0;
}

/* ---------------------------------------------------------------------- */
/* do overlay                                                             */

static int
grab_setupfb(int sw, int sh, int format, void *base, int bpl)
{
    int settings_ok = 1;
    swidth  = sw;
    sheight = sh;

    /* overlay supported ?? */
    if (!(capability.type & VID_TYPE_OVERLAY)) {
	if (debug)
	    fprintf(stderr,"v4l: device has no overlay support\n");
	return -1;
    }

    /* double-check settings */
    fprintf(stderr,"v4l: %dx%d, %d bit/pixel, %d byte/scanline\n",
	    ov_fbuf.width,ov_fbuf.height,
	    ov_fbuf.depth,ov_fbuf.bytesperline);
    if ((bpl > 0 && ov_fbuf.bytesperline != bpl) ||
	(ov_fbuf.width  != sw) ||
	(ov_fbuf.height != sh)) {
	fprintf(stderr,"WARNING: v4l and dga disagree about the screen size\n");
	fprintf(stderr,"WARNING: Is v4l-conf installed correctly?\n");
	settings_ok = 0;
    }
    if (ng_vfmt_to_depth[format] != ((ov_fbuf.depth+7)&0xf8)) {
	fprintf(stderr,"WARNING: v4l and dga disagree about the color depth\n");
	fprintf(stderr,"WARNING: Is v4l-conf installed correctly?\n");
	fprintf(stderr,"%d %d\n",ng_vfmt_to_depth[format],ov_fbuf.depth);
	settings_ok = 0;
    }
    if (have_dga) {
	/* XXX: minor differences are legal... (matrox problems) */
	if ((void*)((unsigned long)ov_fbuf.base & 0xfffff000) !=
	    (void*)((unsigned long)base         & 0xfffff000)) {
	    fprintf(stderr,"WARNING: v4l and dga disagree about the framebuffer base\n");
	    fprintf(stderr,"WARNING: Is v4l-conf installed correctly?\n");
	    fprintf(stderr,"ov_fbuf.base=%p, base=%p\n",ov_fbuf.base,base);
	    settings_ok = 0;
	}
    }
    if (settings_ok) {
	grab_v4l.grab_overlay   = grab_overlay;
	grab_v4l.grab_offscreen = grab_offscreen;
	return 0;
    } else {
	fprintf(stderr,"WARNING: overlay mode disabled\n");
	return -1;
    }
}

static void
grab_overlay_set(int state)
{
    if (0 == state) {
	/* off */
	if (0 == ov_on)
	    return;
	xioctl(fd, VIDIOCCAPTURE, &zero);
	ov_on = 0;
    } else {
	/* on */
	xioctl(fd,VIDIOCSPICT,&ov_pict);
	xioctl(fd, VIDIOCSWIN, &ov_win);
	if (0 != ov_on)
	    return;
	xioctl(fd, VIDIOCCAPTURE, &one);
	ov_on = 1;
    }
}

static int
grab_overlay(int x, int y, int width, int height, int format,
	     struct OVERLAY_CLIP *oc, int count)
{
    int i,xadjust=0,yadjust=0;

    if (width == 0 || height == 0) {
	if (debug)
	    fprintf(stderr,"v4l: overlay off\n");
	ov_enabled = 0;
	grab_overlay_set(ov_enabled);
	return 0;
    }

    ov_win.x          = x;
    ov_win.y          = y;
    ov_win.width      = width;
    ov_win.height     = height;
    ov_win.flags      = 0;
    ov_win.chromakey  = grab_v4l.colorkey;

#if 1
    /* check against max. size */
    xioctl(fd,VIDIOCGCAP,&capability);
    if (ov_win.width > capability.maxwidth) {
	ov_win.width = capability.maxwidth;
	ov_win.x += (width - ov_win.width)/2;
    }
    if (ov_win.height > capability.maxheight) {
	ov_win.height = capability.maxheight;
	ov_win.y +=  (height - ov_win.height)/2;
    }
    grabber_fix_ratio(&ov_win.width,&ov_win.height,&ov_win.x,&ov_win.y);

    /* pass aligned values -- the driver does'nt get it right yet */
    ov_win.width  &= ~3;
    ov_win.height &= ~3;
    ov_win.x      &= ~3;
    if (ov_win.x              < x)        ov_win.x     += 4;
    if (ov_win.x+ov_win.width > x+width)  ov_win.width -= 4;

    /* fixups */
    xadjust = ov_win.x - x;
    yadjust = ov_win.y - y;
#endif

    if (capability.type & VID_TYPE_CLIPPING) {
	ov_win.clips      = ov_clips;
	ov_win.clipcount  = count;
	
	for (i = 0; i < count; i++) {
	    ov_clips[i].x      = oc[i].x1 - xadjust;
	    ov_clips[i].y      = oc[i].y1 - yadjust;
	    ov_clips[i].width  = oc[i].x2-oc[i].x1 /* XXX */;
	    ov_clips[i].height = oc[i].y2-oc[i].y1;
	    if (debug)
		fprintf(stderr,"v4l: clip=%dx%d+%d+%d\n",
			ov_clips[i].width,ov_clips[i].height,
			ov_clips[i].x,ov_clips[i].y);
	}
    }
    ov_pict.depth = ng_vfmt_to_depth[format];
    ov_pict.palette =
	(format < sizeof(format2palette)/sizeof(unsigned short))?
	format2palette[format] : 0;

    ov_enabled = 1;
    grab_overlay_set(ov_enabled);

    if (debug)
	fprintf(stderr,"v4l: overlay win=%dx%d+%d+%d, %d clips\n",
		width,height,x,y, count);

    return 0;
}

static int
grab_offscreen(int y, int width, int height, int format)
{
    if (width == 0 || height == 0) {
	if (debug)
	    fprintf(stderr,"v4l: offscreen off\n");
	ov_enabled = 0;
	grab_overlay_set(ov_enabled);
	return 0;
    }

    ov_win.x       = 0;
    ov_win.y       = y;
    ov_win.width   = width;
    ov_win.height  = height;
    ov_win.flags   = 0;
    ov_pict.depth  = ng_vfmt_to_depth[format];
    ov_pict.palette =
	(format < sizeof(format2palette)/sizeof(unsigned short))?
	format2palette[format] : 0;

    ov_enabled = 1;
    grab_overlay_set(ov_enabled);

    if (debug)
	fprintf(stderr,"v4l: offscreen size=%dx%d\n",
		width,height);
    return 0;
}

/* ---------------------------------------------------------------------- */
/* capture using mmaped buffers (with double-buffering, ...)              */

static int
grab_queue(void)
{
    int frame = gb_grab++ % gb_bufcount;

#if 0
    /* might be useful for debugging driver problems */
    memset(map + gb_buffers.offsets[frame],0,
	   gb_buffers.size/gb_buffers.frames);
#endif
    if (-1 == xioctl(fd,VIDIOCMCAPTURE,gb_buf+frame)) {
	if (errno == EAGAIN)
	    fprintf(stderr,"grabber chip can't sync (no station tuned in?)\n");
	return -1;
    }
    return 0;
}

static int
grab_wait(void)
{
    int frame = gb_sync++ % gb_bufcount;
    int ret = 0;

    alarms=0;
    alarm(SYNC_TIMEOUT);

 retry:
    if (-1 == xioctl(fd,VIDIOCSYNC,gb_buf+frame)) {
	if (errno == EINTR && !alarms)
	    goto retry;
	ret = -1;
    }
    alarm(0);
    return (ret == 0) ? frame : ret;
}

static int
grab_probe(int format)
{
    struct video_mmap gb;

    if (0 != gb_pal[format])
	goto done;

    gb.frame  = 0;
    gb.width  = 64;
    gb.height = 48;

    if (debug)
	fprintf(stderr, "v4l: capture probe %s...\t", device_pal[format]);
    gb.format = format;
    if (-1 == xioctl(fd,VIDIOCMCAPTURE,&gb)) {
	gb_pal[format] = 2;
	goto done;
    }
    if (-1 == xioctl(fd,VIDIOCSYNC,&gb)) {
	gb_pal[format] = 2;
	goto done;
    }
    if (debug)
	fprintf(stderr, "ok\n");
    gb_pal[format] = 1;

 done:
    return gb_pal[format] == 1;
}

static int
grab_mm_setparams(int format, int *width, int *height, int *linelength)
{
    int v4l_format,i;
    
    /* finish old stuff */
    while (gb_grab > gb_sync)
	grab_wait();

    /* verify parameters */
    xioctl(fd,VIDIOCGCAP,&capability);
    if (*width > capability.maxwidth)
	*width = capability.maxwidth;
    if (*height > capability.maxheight)
	*height = capability.maxheight;    
    *linelength = *width * ng_vfmt_to_depth[format] / 8;

#if 0
    /* XXX bttv bug workaround - it returns a larger size than it can handle */
    if (*width > 768+76) {
	*width = 768+76;
	*linelength = *width * format2depth[format] / 8;
    }
#endif

    /* check if we can handle the format */
    v4l_format = (format < sizeof(format2palette)/sizeof(unsigned short)) ?
	format2palette[format] : 0;
    if (v4l_format == 0 || !grab_probe(v4l_format))
	return -1;

    /* initialize everything */
    gb_grab = 0;
    gb_sync = 0;
    pixmap_bytes   = ng_vfmt_to_depth[format] / 8;
    for (i = 0; i < MAX_BUFFERS; i++) {
	gb_buf[i].format = v4l_format;
	gb_buf[i].frame  = i;
	gb_buf[i].width  = *width;
	gb_buf[i].height = *height;
    }

    return 0;
}

static void
grab_mm_start(int fps, int buffers)
{
    if (grab_fps) {
	fprintf(stderr,"grab_mm_start: aiee: grab_fps!=0\n");
	exit(1);
    }

    gb_bufcount = MAX_BUFFERS;
    if (gb_buffers.frames < gb_bufcount)
	gb_bufcount = gb_buffers.frames;
    if (buffers > 0 && buffers < gb_bufcount)
	gb_bufcount = buffers;

    while ((gb_grab - gb_sync) < gb_bufcount)
	grab_queue();
    gettimeofday(&grab_start,NULL);
    grab_fps = fps;
    grab_frames = 0;
}

static void*
grab_mm_capture(void)
{
    int frame,rc;

    if (0 == grab_fps) {
	/* grab one buffer */
	if (debug)
	    fprintf(stderr,"grab_mm_capture: one\n");
	if (-1 == grab_queue())
	    return NULL;
	frame = grab_wait();
	if (-1 == frame)
	    return NULL;
	return map + gb_buffers.offsets[frame];
    }

    /* next buffer (streaming) */
    if (debug > 1)
	fprintf(stderr,"grab_mm_capture: next\n");
 next_frame:
    while ((gb_grab - gb_sync) < gb_bufcount)
	if (-1 == grab_queue())
	    return NULL;
    frame = grab_wait();
    if (-1 == frame)
	return NULL;

    /* rate control */
    rc = grabber_sw_rate(&grab_start,grab_fps,grab_frames);
    if (rc <= 0)
	goto next_frame;
    grab_frames++;
    
    return map + gb_buffers.offsets[frame];
}

static void
grab_mm_stop(void)
{
    if (!grab_fps) {
	fprintf(stderr,"grab_mm_stop: aiee: grab_fps==0\n");
	exit(1);
    }
    /* stop streaming */
    while (gb_grab > gb_sync)
	grab_wait();
    grab_fps = 0;
}

/* ---------------------------------------------------------------------- */
/* capture using simple read()                                            */

static int
grab_read_setparams(int format, int *width, int *height, int *linelength)
{
    rd_pict.depth   = ng_vfmt_to_depth[format];
    rd_pict.palette = (format < sizeof(format2palette)/sizeof(unsigned short)) ?
	format2palette[format] : 0;
    if (rd_pict.palette == 0)
	return -1;

    fprintf(stderr,"set: palette=%d\n",rd_pict.palette);
    grab_overlay_set(0);

    /* set format */
    if (-1 == xioctl(fd,VIDIOCSPICT,&rd_pict))
	goto fail;
    if (-1 == xioctl(fd,VIDIOCGPICT,&rd_pict))
	goto fail;
	
    /* set size */
    xioctl(fd,VIDIOCGCAP,&capability);
    if (*width > capability.maxwidth)
	*width = capability.maxwidth;
    if (*height > capability.maxheight)
	*height = capability.maxheight;
    memset(&rd_win,0,sizeof(struct video_window));
    rd_win.width  = *width;
    rd_win.height = *height;
    if (-1 == xioctl(fd,VIDIOCSWIN,&rd_win))
	goto fail;
    if (-1 == xioctl(fd,VIDIOCGWIN,&rd_win))
	goto fail;

    *width  = rd_win.width;
    *height = rd_win.height;
    *linelength = *width * ng_vfmt_to_depth[format] / 8;

    /* alloc buffer */
    rd_size = *linelength * *height;
    fprintf(stderr,"format: %d  %dx%d  size=%d  depth=%d\n",
	    format,*width,*height,rd_size,ng_vfmt_to_depth[format]);
    if (rd_buf)
	free(rd_buf);
    rd_buf = NULL;
    grab_overlay_set(ov_enabled);
    return 0;

 fail:
    grab_overlay_set(ov_enabled);
    return -1;
}

static void
grab_read_start(int fps, int buffers)
{
    if (grab_fps) {
	fprintf(stderr,"grab_read_stop: aiee: grab_fps!=0\n");
	exit(1);
    }
    gettimeofday(&grab_start,NULL);
    grab_fps = fps;
    grab_frames = 0;
}

static void*
grab_read_capture()
{
    int rc;

    if (NULL == rd_buf)
	rd_buf = malloc(rd_size);
    if (NULL == rd_buf)
	return NULL;

    fprintf(stderr,"cap: palette=%d\n",rd_pict.palette);
    grab_overlay_set(0);
    if (-1 == xioctl(fd,VIDIOCSPICT,&rd_pict) ||
	-1 == xioctl(fd,VIDIOCSWIN,&rd_win))
	goto fail;

 next_frame:
    rc = read(fd,rd_buf,rd_size);
    if (rd_size != rc) {
	fprintf(stderr,"grabber read error (rc=%d, expect=%d, errno=%s)\n",
		rc,rd_size,strerror(errno));
	goto fail;
    }

    if (grab_fps > 0) {
	/* rate control */
	rc = grabber_sw_rate(&grab_start,grab_fps,grab_frames);
	if (rc <= 0)
	    goto next_frame;
	grab_frames++;
    }
    grab_overlay_set(ov_enabled);
    return rd_buf;

 fail:
    grab_overlay_set(ov_enabled);
    return NULL;
}

static void
grab_read_stop(void)
{
    if (!grab_fps) {
	fprintf(stderr,"grab_read_stop: aiee: grab_fps==0\n");
	exit(1);
    }
    if (rd_buf) {
	free(rd_buf);
	rd_buf = NULL;
    }
    grab_overlay_set(ov_enabled);
    grab_fps = 0;
}

/* ---------------------------------------------------------------------- */

static unsigned long
grab_tune(unsigned long freq, int sat)
{
    if (-1 == freq) {
	xioctl(fd, VIDIOCGFREQ, &freq);
	return freq;
    }
    if (debug)
	fprintf(stderr,"v4l: freq: %.3f\n",(float)freq/16);
    xioctl(fd, VIDIOCSFREQ, &freq);
    return 0;
}

static int
grab_tuned(void)
{
    usleep(10000);
    if (-1 == xioctl(fd,VIDIOCGTUNER,tuner))
	return 0;
    return tuner->signal ? 1 : 0;
}

static int
grab_input(int input, int norm)
{
    if (-1 != input) {
	if (debug)
	    fprintf(stderr,"v4l: input: %d\n",input);
	cur_input = input;
    }
    if (-1 != norm) {
	if (debug)
	    fprintf(stderr,"v4l: norm : %d\n",norm);
	cur_norm = norm;
    }

    grab_overlay_set(0);
    channels[cur_input].norm = cur_norm;
    xioctl(fd, VIDIOCSCHAN, &channels[cur_input]);
    grab_overlay_set(ov_enabled);
    return 0;
}

/* ---------------------------------------------------------------------- */

int grab_hasattr(int id)
{
    int i;

    for (i = 0; i < NUM_ATTR; i++)
	if (id == grab_attr[i].id && grab_attr[i].have)
	    break;
    if (i == NUM_ATTR)
	return 0;
    return 1;
}

int grab_getattr(int id)
{
    int i;

    for (i = 0; i < NUM_ATTR; i++)
	if (id == grab_attr[i].id && grab_attr[i].have)
	    break;
    if (i == NUM_ATTR)
	return -1;
    xioctl(fd,grab_attr[i].get,grab_attr[i].arg);

    switch (id) {
    case GRAB_ATTR_VOLUME:   return audio.volume;
    case GRAB_ATTR_MUTE:     return audio.flags & VIDEO_AUDIO_MUTE;
    case GRAB_ATTR_MODE:     return audio.mode;
    case GRAB_ATTR_COLOR:    return pict.colour;
    case GRAB_ATTR_BRIGHT:   return pict.brightness;
    case GRAB_ATTR_HUE:      return pict.hue;
    case GRAB_ATTR_CONTRAST: return pict.contrast;
    default: return -1;
    }
}

int grab_setattr(int id, int val)
{
    int i;
    
    /* read ... */
    for (i = 0; i < NUM_ATTR; i++)
	if (id == grab_attr[i].id && grab_attr[i].have)
	    break;
    if (i == NUM_ATTR)
	return -1;
    xioctl(fd,grab_attr[i].get,grab_attr[i].arg);

    /* ... modify ... */
    switch (id) {
    case GRAB_ATTR_VOLUME:   audio.volume = val; break;
    case GRAB_ATTR_MUTE:
	if (val)
	    audio.flags |= VIDEO_AUDIO_MUTE;
	else
	    audio.flags &= ~VIDEO_AUDIO_MUTE;
	break;
    case GRAB_ATTR_MODE:
	audio.mode = val;
	break;
    case GRAB_ATTR_COLOR:
	pict.colour     = val;
	break;
    case GRAB_ATTR_BRIGHT:
	pict.brightness = val;
	break;
    case GRAB_ATTR_HUE:
	pict.hue        = val;
	break;
    case GRAB_ATTR_CONTRAST:
	pict.contrast   = val;
	break;
    default: return -1;
    }

    /* ... write */
    xioctl(fd,grab_attr[i].set,grab_attr[i].arg);
    
    /* keep the others up-to-date */
    ov_pict.colour     = pict.colour;
    rd_pict.colour     = pict.colour;
    ov_pict.brightness = pict.brightness;
    rd_pict.brightness = pict.brightness;
    ov_pict.hue        = pict.hue;
    rd_pict.hue        = pict.hue;
    ov_pict.contrast   = pict.contrast;
    rd_pict.contrast   = pict.contrast;

    return 0;
}

#endif /* __linux__ */
