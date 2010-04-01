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
#include <endian.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <X11/Intrinsic.h>

#include "grab.h"
#include "colorspace.h"
#include "commands.h"

#include <asm/types.h>		/* XXX glibc */
#include "videodev.h"

#define SYNC_TIMEOUT 1

/* ---------------------------------------------------------------------- */

/* open+close */
static int   grab_open(char *filename);
static int   grab_close();

/* overlay */
static int   grab_setupfb(int sw, int sh, int format, void *base, int width);
static int   grab_overlay(int x, int y, int width, int height, int format,
			  struct OVERLAY_CLIP *oc, int count);
static int   grab_offscreen(int y, int width, int height, int format);

/* capture */
static int   grab_mm_setparams(int format, int *width, int *height,
			       int *linelength);
static void* grab_mm_capture(int single);
static void  grab_mm_cleanup();
static int   grab_read_setparams(int format, int *width, int *height,
				 int *linelength);
static void* grab_read_capture(int single);
static void  grab_read_cleanup();

/* control */
static int   grab_tune(unsigned long freq);
static int   grab_tuned();
static int   grab_input(int input, int norm);
static int   grab_hasattr(int id);
static int   grab_getattr(int id);
static int   grab_setattr(int id, int val);

/* internal helpers */
static int   grab_wait(struct video_mmap *gb);

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
    {  0, "auto"    },
    {  1, "mono"    },
    {  2, "stereo"  },
    {  4, "lang1"   },
    {  8, "lang2"   },
    { -1, NULL,     },
};
static struct STRTAB norms[] = {
    {  0, "PAL" },
    {  1, "NTSC" },
    {  2, "SECAM" },
    {  3, "AUTO" },
    { -1, NULL }
};
static struct STRTAB norms_bttv[] = {
    {  0, "PAL" },
    {  1, "NTSC" },
    {  2, "SECAM" },
    {  3, "PAL-NC" },
    {  4, "PAL-M" },
    {  5, "PAL-N" },
    {  6, "NTSC-JP" },
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
static struct video_clip        ov_clips[32];
static struct video_buffer      ov_fbuf;

/* screen grab */
static struct video_mmap        gb_even;
static struct video_mmap        gb_odd;
static int                      even,pixmap_bytes;
static int                      gb_grab,gb_sync;
static struct video_mbuf        gb_buffers = { 2*0x151000, 0, {0,0x151000 }};
static int gb_pal[] = {
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0
};

static int   grab_read_size;
static char *grab_read_buf;

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
#if 0 /* broken in bttv */
    VIDEO_PALETTE_YUV420P,      /* YUV420P  */
#else
    0,                          /* YUV420P  */
#endif
};

static char *map = NULL;

/* state */
static int                      overlay, swidth, sheight;

/* pass 0/1 by reference */
static int                      one = 1, zero = 0;

struct GRABBER grab_v4l = {
    "v4l",
    0,
    norms,NULL,stereo,

    grab_open,
    grab_close,

    grab_setupfb,
    NULL /* grab_overlay */,
    NULL /* grab_offscreen */,

    NULL /* grab_setparams */,
    NULL /* grab_capture */,
    NULL /* grab_cleanup */,

    grab_tune,
    grab_tuned,
    grab_input,
    grab_hasattr,
    grab_getattr,
    grab_setattr
};

/* ---------------------------------------------------------------------- */

static struct GRAB_ATTR {
    int   id;
    int   have;
    int   get;
    int   set;
    void  *arg;
} grab_attr [] = {
    { GRAB_ATTR_VOLUME,   1, VIDIOCGAUDIO, VIDIOCSAUDIO, &audio },
    { GRAB_ATTR_MUTE,     1, VIDIOCGAUDIO, VIDIOCSAUDIO, &audio },
    { GRAB_ATTR_MODE,     1, VIDIOCGAUDIO, VIDIOCSAUDIO, &audio },
    
    { GRAB_ATTR_COLOR,    1, VIDIOCGPICT,  VIDIOCSPICT,  &pict  },
    { GRAB_ATTR_BRIGHT,   1, VIDIOCGPICT,  VIDIOCSPICT,  &pict  },
    { GRAB_ATTR_HUE,      1, VIDIOCGPICT,  VIDIOCSPICT,  &pict  },
    { GRAB_ATTR_CONTRAST, 1, VIDIOCGPICT,  VIDIOCSPICT,  &pict  },
};

#define NUM_ATTR (sizeof(grab_attr)/sizeof(struct GRAB_ATTR))

/* ---------------------------------------------------------------------- */

static void
sigalarm(int signal)
{
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

static int
grab_open(char *filename)
{
    int i;

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
	if (-1 == ioctl(fd,VIDIOCGCHAN,&channels[i]))
	    perror("ioctl VIDIOCGCHAN"), exit(0);
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

    /* ioctl probe, switch to input 0 */
    if (-1 == ioctl(fd,VIDIOCSCHAN,&channels[0])) {
	fprintf(stderr,"v4l: you need a newer bttv version (>= 0.5.14)\n");
	goto err;
    }

    /* audios */
    if (debug)
	fprintf(stderr,"  audios  : %d\n",capability.audios);
    if (capability.audios) {
	audio.audio = 0;
	if (-1 == ioctl(fd,VIDIOCGAUDIO,&audio))
	    perror("ioctl VIDIOCGCAUDIO") /* , exit(0) */ ;
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
	if (-1 == ioctl(fd,VIDIOCGTUNER,tuner))
	    perror("ioctl VIDIOCGTUNER");
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
	    if (-1 == ioctl(fd,VIDIOCSCHAN,&vchan))
		norms[i].nr = -1;
	    else if (debug)
		fprintf(stderr," %s",norms[i].str);
	}
	if (debug)
	    fprintf(stderr,"\n");
	if (-1 == ioctl(fd,VIDIOCSCHAN,&channels[0])) {
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
    if (-1 != ioctl(fd,BTTV_VERSION,0)) {
	grab_v4l.norms = norms_bttv;
    }
#endif
    
    /* frame buffer */
    if (-1 == ioctl(fd,VIDIOCGFBUF,&ov_fbuf))
	perror("ioctl VIDIOCGFBUF");
    if (debug)
	fprintf(stderr,"  fbuffer : base=0x%p size=%dx%d depth=%d bpl=%d\n",
		ov_fbuf.base, ov_fbuf.width, ov_fbuf.height,
		ov_fbuf.depth, ov_fbuf.bytesperline);

    /* picture parameters */
    if (-1 == ioctl(fd,VIDIOCGPICT,&pict))
	perror("ioctl VIDIOCGPICT");

    if (debug) {
	fprintf(stderr,
		"  picture : brightness=%d hue=%d colour=%d contrast=%d\n",
		pict.brightness, pict.hue, pict.colour, pict.contrast);
	fprintf(stderr,
		"  picture : whiteness=%d depth=%d palette=%s\n",
		pict.whiteness, pict.depth, PALETTE(pict.palette));
    }

    /* map grab buffer */
    if (-1 == ioctl(fd,VIDIOCGMBUF,&gb_buffers)) {
	if (debug)
	    perror("ioctl VIDIOCGMBUF");
    }
    map = mmap(0,gb_buffers.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if ((char*)-1 != map) {
	grab_v4l.grab_setparams = grab_mm_setparams;
	grab_v4l.grab_capture   = grab_mm_capture;
	grab_v4l.grab_cleanup   = grab_mm_cleanup;
    } else {
	if (debug)
	    perror("mmap");
	grab_v4l.grab_setparams = grab_read_setparams;
	grab_v4l.grab_capture   = grab_read_capture;
	grab_v4l.grab_cleanup   = grab_read_cleanup;
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
grab_close()
{
    if (-1 == fd)
	return 0;

    if (gb_grab > gb_sync)
	grab_wait(even ? &gb_even : &gb_odd);

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
    if (format2depth[format] != ((ov_fbuf.depth+7)&0xf8)) {
	fprintf(stderr,"WARNING: v4l and dga disagree about the color depth\n");
	fprintf(stderr,"WARNING: Is v4l-conf installed correctly?\n");
	fprintf(stderr,"%d %d\n",format2depth[format],ov_fbuf.depth);
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

static int
grab_overlay(int x, int y, int width, int height, int format,
	     struct OVERLAY_CLIP *oc, int count)
{
    int i,xadjust=0,yadjust=0;

    if (width == 0 || height == 0) {
	if (debug)
	    fprintf(stderr,"v4l: overlay off\n");
	ioctl(fd, VIDIOCCAPTURE, &zero);
	overlay = 0;
	return 0;
    }

    ov_win.x          = x;
    ov_win.y          = y;
    ov_win.width      = width;
    ov_win.height     = height;
    ov_win.flags      = 0;

#if 1
    /* check against max. size */
    ioctl(fd,VIDIOCGCAP,&capability);
    if (ov_win.width > capability.maxwidth) {
	ov_win.width = capability.maxwidth;
	ov_win.x += (width - ov_win.width)/2;
    }
    if (ov_win.height > capability.maxheight) {
	ov_win.height = capability.maxheight;
	ov_win.y +=  (height - ov_win.height)/2;
    }

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
#if 0
	    if (ov_clips[i].x < 0 || ov_clips[i].y < 0 ||
		ov_clips[i].width < 0 || ov_clips[i].height < 0) {
		fprintf(stderr,"v4l: bug trap - overlay off\n");
		ioctl(fd, VIDIOCCAPTURE, &zero);
		overlay = 0;
		return 0;		
	    }
#endif
	}
    }
    if (capability.type & VID_TYPE_CHROMAKEY) {
	ov_win.chromakey  = 0;    /* XXX */
    }
    pict.palette =
	(format < sizeof(format2palette)/sizeof(unsigned short))?
	format2palette[format] : 0;

    if (-1 == ioctl(fd,VIDIOCSPICT,&pict))
	perror("ioctl VIDIOCSPICT");
    if (-1 == ioctl(fd, VIDIOCSWIN, &ov_win))
	perror("ioctl VIDIOCSWIN");

    if (!overlay) {
	if (-1 == ioctl(fd, VIDIOCCAPTURE, &one))
	    perror("ioctl VIDIOCCAPTURE");
	overlay = 1;
    }

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
	ioctl(fd, VIDIOCCAPTURE, &zero);
	overlay = 0;
	return 0;
    }

    ov_win.x       = 0;
    ov_win.y       = y;
    ov_win.width   = width;
    ov_win.height  = height;
    ov_win.flags   = 0;
    pict.palette   = 
	(format < sizeof(format2palette)/sizeof(unsigned short))?
	format2palette[format] : 0;

    if (-1 == ioctl(fd,VIDIOCSPICT,&pict))
	perror("ioctl VIDIOCSPICT");
    if (-1 == ioctl(fd, VIDIOCSWIN, &ov_win))
	perror("ioctl VIDIOCSWIN");
    if (-1 == ioctl(fd_grab,VIDIOCCAPTURE,&one))
	perror("ioctl VIDIOCCAPTURE");
    if (debug)
	fprintf(stderr,"v4l: offscreen size=%dx%d\n",
		width,height);
    return 0;
}

/* ---------------------------------------------------------------------- */
/* capture using mmaped buffers (with double-buffering, ...)              */

static int
grab_queue(struct video_mmap *gb, int probe)
{
    if (debug > 1)
	fprintf(stderr,"g%d",gb->frame);
#if 0
    /* might be useful for debugging driver problems */
    memset(map + gb_buffers.offsets[gb->frame],0,
	   gb_buffers.size/gb_buffers.frames);
#endif
    if (-1 == ioctl(fd,VIDIOCMCAPTURE,gb)) {
	if (errno == EAGAIN)
	    fprintf(stderr,"grabber chip can't sync (no station tuned in?)\n");
	else
	    if (!probe || debug)
		fprintf(stderr,"ioctl VIDIOCMCAPTURE(%d,%s,%dx%d): %s\n",
			gb->frame,PALETTE(gb->format),gb->width,gb->height,
			strerror(errno));
	return -1;
    }
    if (debug > 1)
	fprintf(stderr,"* ");
    gb_grab++;
    return 0;
}

static int
grab_wait(struct video_mmap *gb)
{
    int ret = 0;
    
    alarm(SYNC_TIMEOUT);
    if (debug > 1)
	fprintf(stderr,"s%d",gb->frame);

    if (-1 == ioctl(fd,VIDIOCSYNC,&(gb->frame))) {
	perror("ioctl VIDIOCSYNC");
	ret = -1;
    }
    gb_sync++;
    if (debug > 1)
	fprintf(stderr,"* ");
    alarm(0);
    return ret;
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
    if (-1 == grab_queue(&gb,1)) {
	gb_pal[format] = 2;
	goto done;
    }
    if (-1 == grab_wait(&gb)) {
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
    /* finish old stuff */
    if (gb_grab > gb_sync)
	grab_wait(even ? &gb_even : &gb_odd);

    /* verify parameters */
    ioctl(fd,VIDIOCGCAP,&capability);
    if (*width > capability.maxwidth)
	*width = capability.maxwidth;
    if (*height > capability.maxheight)
	*height = capability.maxheight;    
    *linelength = *width * format2depth[format] / 8;

#if 1
    /* XXX bttv bug workaround - it returns a larger size than it can handle */
    if (*width > 768+76) {
	*width = 768+76;
	*linelength = *width * format2depth[format] / 8;
    }
#endif

    /* initialize everything */
    gb_even.format = gb_odd.format =
	(format < sizeof(format2palette)/sizeof(unsigned short)) ?
	format2palette[format] : 0;
    if (gb_even.format == 0 || !grab_probe(gb_even.format)) {
	return -1;
    }
    pixmap_bytes   = format2depth[format] / 8;
    gb_even.frame  = 0;
    gb_odd.frame   = 1;
    gb_even.width  = *width;
    gb_even.height = *height;
    gb_odd.width   = *width;
    gb_odd.height  = *height;
    even = 0;

    return 0;
}

static void*
grab_mm_capture(int single)
{
    void *buf;

    if (!single && gb_grab == gb_sync)
	/* streaming capture started */
	if (-1 == grab_queue(even ? &gb_even : &gb_odd,0))
	    return NULL;

    if (single && gb_grab > gb_sync)
	/* clear streaming capture */
	grab_wait(even ? &gb_even : &gb_odd);

    /* queue */
    if (-1 == grab_queue(even ? &gb_odd : &gb_even,0))
	return NULL;
    if (gb_grab > gb_sync+1) {
	/* wait -- streaming */
	grab_wait(even ? &gb_even : &gb_odd);
	buf = map + gb_buffers.offsets[even ? 0 : 1];
    } else {
	/* wait -- single */
	grab_wait(even ? &gb_odd : &gb_even);
	buf = map + gb_buffers.offsets[even ? 1 : 0];
    }
    even = !even;

    return buf;
}

static void
grab_mm_cleanup()
{
    if (gb_grab > gb_sync)
	grab_wait(even ? &gb_even : &gb_odd);
}

/* ---------------------------------------------------------------------- */
/* capture using simple read()                                            */

static int
grab_read_setparams(int format, int *width, int *height, int *linelength)
{
    struct video_window win;
    
    pict.depth   = format2depth[format];
    pict.palette = format2palette[format];

    /* set format */
    if (-1 == ioctl(fd,VIDIOCSPICT,&pict)) {
	if (debug)
	    perror("ioctl VIDIOCSPICT");
	return -1;
    }
    if (-1 == ioctl(fd,VIDIOCGPICT,&pict)) {
	if (debug)
	    perror("ioctl VIDIOCGPICT");
	return -1;
    }
	
    /* set size */
    ioctl(fd,VIDIOCGCAP,&capability);
    if (*width > capability.maxwidth)
	*width = capability.maxwidth;
    if (*height > capability.maxheight)
	*height = capability.maxheight;
    memset(&win,0,sizeof(struct video_window));
    win.width  = *width;
    win.height = *height;
    if (-1 == ioctl(fd,VIDIOCSWIN,&win)) {
	if (debug)
	    perror("ioctl VIDIOCSWIN");
	return -1;
    }
    if (-1 == ioctl(fd,VIDIOCGWIN,&win)) {
	if (debug)
	    perror("ioctl VIDIOCGWIN");
	return -1;
    }

    *width  = win.width;
    *height = win.height;
    *linelength = *width * format2depth[format] / 8;

    /* alloc buffer */
    grab_read_size = *linelength * *height;
    if (grab_read_buf)
	free(grab_read_buf);
    grab_read_buf = malloc(grab_read_size);
    if (NULL == grab_read_buf)
	return -1;
    
    return 0;
}

static void*
grab_read_capture(int single)
{
    int rc;

    rc = read(fd,grab_read_buf,grab_read_size);
    if (grab_read_size != rc) {
	fprintf(stderr,"grabber read error (rc=%d)\n",rc);
	return NULL;
    }
    return grab_read_buf;
}

static void
grab_read_cleanup()
{
    if (grab_read_buf) {
	free(grab_read_buf);
	grab_read_buf = NULL;
    }
}

/* ---------------------------------------------------------------------- */

static int
grab_tune(unsigned long freq)
{
    if (debug)
	fprintf(stderr,"v4l: freq: %.3f\n",(float)freq/16);
    if (-1 == ioctl(fd, VIDIOCSFREQ, &freq))
	perror("ioctl VIDIOCSFREQ");
    return 0;
}

static int
grab_tuned()
{
    usleep(10000);
    if (-1 == ioctl(fd,VIDIOCGTUNER,tuner)) {
	perror("ioctl VIDIOCGTUNER");
	return 0;
    }
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

    channels[cur_input].norm = cur_norm;
    if (-1 == ioctl(fd, VIDIOCSCHAN, &channels[cur_input]))
	perror("ioctl VIDIOCSCHAN");
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
    if (-1 == ioctl(fd,grab_attr[i].get,grab_attr[i].arg))
	perror("ioctl get");

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
    if (-1 == ioctl(fd,grab_attr[i].set,grab_attr[i].arg))
	perror("ioctl get");

    /* ... modify ... */
    switch (id) {
    case GRAB_ATTR_VOLUME:   audio.volume = val; break;
    case GRAB_ATTR_MUTE:
	if (val)
	    audio.flags |= VIDEO_AUDIO_MUTE;
	else
	    audio.flags &= ~VIDEO_AUDIO_MUTE;
	break;
    case GRAB_ATTR_MODE:     audio.mode      = val; break;
    case GRAB_ATTR_COLOR:    pict.colour     = val; break;
    case GRAB_ATTR_BRIGHT:   pict.brightness = val; break;
    case GRAB_ATTR_HUE:      pict.hue        = val; break;
    case GRAB_ATTR_CONTRAST: pict.contrast   = val; break;
    default: return -1;
    }

#if 0
    /* bttv: avoid input switch */
    audio.audio = cur_input;
#endif
    
    /* ... write */
    if (-1 == ioctl(fd,grab_attr[i].set,grab_attr[i].arg))
	perror("ioctl set");
    return 0;
}
