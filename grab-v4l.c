#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>

#include <X11/Intrinsic.h>

#include "grab.h"

#include <asm/types.h>		/* XXX glibc */

#if USE_KERNEL_VIDEODEV
# include <linux/videodev.h>
#else
# include "videodev.h"
#endif

#define MEM_SIZE 0x144000

/* ---------------------------------------------------------------------- */

/* prototypes */
static int   grab_open(char *filename, int sw, int sh,
		       int format, int pixmap, void *base, int width);
static int   grab_close();
static int   grab_overlay(int x, int y, int width, int height, int format,
			  struct OVERLAY_CLIP *oc, int count);
static void* grab_scr(void *dest, int width, int height);
static void* grab_one(int width, int height);
static int   grab_tune(unsigned long freq);
static int   grab_input(int input, int norm);
static int   grab_picture(int color, int bright, int hue, int contrast);
static int   grab_audio(int mute, int volume, int *mode);

/* ---------------------------------------------------------------------- */

static char *device_cap[] = {
    "capture", "tuner", "teletext", "overlay", "chromakey", "clipping",
    "frameram", "scales", "monochrome", NULL
};

static char *device_pal[] = {
    "-", "grey", "hi240", "rgb16", "rgb24", "rgb32", "rgb15", NULL
};

static struct STRTAB norms[] = {
    {  0, "PAL" },
    {  1, "NTSC" },
    {  2, "SECAM" },
    {  3, "AUTO" },
    { -1, NULL }
};
static struct STRTAB *inputs;

static int    fd = -1;

/* generic informations */
static struct video_capability  capability;
static struct video_channel     *channels;
static struct video_audio       *audios;
static struct video_tuner       tuner;
static struct video_picture     pict;

/* overlay */
static struct video_window      ov_win;
static struct video_clip        ov_clips[32];
static struct video_buffer      ov_fbuf;

/* screen grab */
static struct video_mmap        gb_even;
static struct video_mmap        gb_odd;
static int                      gb_count,even,pixmap_bytes;

static char *map = NULL;

/* state */
static int                      overlay, swidth, sheight;

struct GRABBER grab_v4l = {
    "video4linux",
    VIDEO_RGB16 | VIDEO_RGB24 | VIDEO_RGB32,
    0,
    norms,NULL,

    grab_open,grab_close,
    grab_overlay,
    grab_scr,
    grab_one,
    grab_tune,
    grab_input,
    grab_picture,
    grab_audio
};

/* ---------------------------------------------------------------------- */

static int
grab_open(char *filename, int sw, int sh,
	  int format, int pixmap, void *base, int width)
{
    int i;

    if (-1 != fd)
	goto err;
    
    if (-1 == (fd = open(filename ? filename : "/dev/bttv",O_RDWR))) {
	perror("open /dev/bttv");
	goto err;
    }

    if (-1 == ioctl(fd,VIDIOCGCAP,&capability))
	goto err;

    if (debug)
	fprintf(stderr, "v4l: open\n");

    /* XXX set input to television (avoid some trouble while
       asking for settings) */
    grab_input(0,-1);

    swidth  = sw;
    sheight = sh;

    if (debug) {
	fprintf(stderr,"%s:",capability.name);
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
    memset(channels,0,sizeof(struct STRTAB)*(capability.channels+1));
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

    /* audios */
    if (debug)
	fprintf(stderr,"  audios  : %d\n",capability.audios);
    audios = malloc(sizeof(struct video_audio)*capability.audios);
    memset(audios,0,sizeof(struct video_audio)*capability.audios);
    for (i = 0; i < capability.audios; i++) {
	audios[i].audio = i;
	if (-1 == ioctl(fd,VIDIOCGAUDIO,&audios[i]))
	    perror("ioctl VIDIOCGCAUDIO") /* , exit(0) */ ;
	if (debug) {
	    fprintf(stderr,"    %d (%s): ",i,audios[i].name);
	    if (audios[i].flags & VIDEO_AUDIO_MUTABLE)
		fprintf(stderr,"muted=%s ",
			(audios[i].flags&VIDEO_AUDIO_MUTE) ? "yes":"no");
	    if (audios[i].flags & VIDEO_AUDIO_VOLUME)
		fprintf(stderr,"volume=%d ",audios[i].volume);
	    if (audios[i].flags & VIDEO_AUDIO_BASS)
		fprintf(stderr,"bass=%d ",audios[i].bass);
	    if (audios[i].flags & VIDEO_AUDIO_TREBLE)
		fprintf(stderr,"treble=%d ",audios[i].treble);
	    fprintf(stderr,"\n");
	}
    }
    if (audios[0].flags & VIDEO_AUDIO_VOLUME)
	grab_v4l.flags |= CAN_AUDIO_VOLUME;

    if (debug)
	fprintf(stderr,"  size    : %dx%d => %dx%d\n",
		capability.minwidth,capability.minheight,
		capability.maxwidth,capability.maxheight);

    /* tuner (more than one???) */
    if (-1 == ioctl(fd,VIDIOCGTUNER,&tuner))
	perror("ioctl VIDIOCGCAUDIO");
    if (debug)
	fprintf(stderr,"  tuner   : %s %lu-%lu",
		tuner.name,tuner.rangelow,tuner.rangehigh);
    for (i = 0; norms[i].str != NULL; i++) {
	if (tuner.flags & (1<<i)) {
	    if (debug)
		fprintf(stderr," %s",norms[i].str);
	} else
	    norms[i].nr = -1;
	if (tuner.mode == i && debug)
	    fprintf(stderr,"*");
    }
    if (debug)
	fprintf(stderr,"\n");
    
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
		pict.whiteness, pict.depth, device_pal[pict.palette]);
    }

    /* double-check settings */
    fprintf(stderr,"v4l: base=%p\n", ov_fbuf.base);
    if (ov_fbuf.base != base && have_dga) {
	fprintf(stderr,"v4l and dga disagree about the framebuffer address\n");
	fprintf(stderr,"you probably want to check out the vidmem "
		"argument of the bttv module\n");
	exit(1);
    }
    fprintf(stderr,"v4l: %d x %d x %d bit\n",
	    ov_fbuf.width, ov_fbuf.height,ov_fbuf.depth);
    if ((sw && ov_fbuf.width  != sw) ||
	(sh && ov_fbuf.height != sh)) {
	fprintf(stderr,"v4l and dga disagree about the screen size\n");
	fprintf(stderr,"RTFM, the README section about v4l-conf\n");
	exit(1);
    }
    if (format &&
	((format == VIDEO_RGB16 && ov_fbuf.depth != 16) ||
	 (format == VIDEO_RGB24 && ov_fbuf.depth != 24) ||
	 (format == VIDEO_RGB32 && ov_fbuf.depth != 32))) {
	fprintf(stderr,"v4l and dga disagree about the color depth\n");
	fprintf(stderr,"RTFM, the README section about v4l-conf\n");
	exit(1);
    }

    if (0 == strncmp(capability.name,"BT848",5)) {
	map = mmap(0,MEM_SIZE*2,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
	if ((char*)-1 == map)
	    perror("mmap");
    }
    else
	map = (char*)-1;

    switch (pixmap) {
    case VIDEO_RGB15:
	gb_even.format = gb_odd.format = 0x33;  /* FIXME */
	pixmap_bytes = 2;
	break;
    case VIDEO_RGB16:
	gb_even.format = gb_odd.format = 0x22;  /* FIXME */
	pixmap_bytes = 2;
	break;
    case VIDEO_RGB24:
	gb_even.format = gb_odd.format = 0x11;  /* FIXME */
	pixmap_bytes = 3;
	break;
    case VIDEO_RGB32:
	gb_even.format = gb_odd.format = 0x11;  /* FIXME (RGB24 too) */
	pixmap_bytes = 4;
	break;
    }
    gb_even.frame = 0;
    gb_odd.frame  = 1;

    return 0;

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
    while (gb_count > 0) {
	if (-1 == ioctl(fd,VIDIOCSYNC,0))
	    perror("ioctl VIDIOCSYNC");
	gb_count--;
    }

    if ((char*)-1 != map)
	munmap(map,MEM_SIZE*2);

    if (-1 == fd)
	return 0;

    if (debug)
	fprintf(stderr, "v4l: close\n");

    close(fd);
    fd = -1;
    return 0;
}

/* ---------------------------------------------------------------------- */

static int
grab_overlay(int x, int y, int width, int height, int format,
	     struct OVERLAY_CLIP *oc, int count)
{
    int one = 1, zero = 0;
    int i;

    while (gb_count > 0) {
	if (-1 == ioctl(fd,VIDIOCSYNC,0))
	    perror("ioctl VIDIOCSYNC");
	gb_count--;
    }

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

    if (capability.type & VID_TYPE_CLIPPING) {
	ov_win.clips      = ov_clips;
	ov_win.clipcount  = count;
	
	for (i = 0; i < count; i++) {
	    ov_clips[i].x      = oc[i].x1;
	    ov_clips[i].y      = oc[i].y1;
	    ov_clips[i].width  = oc[i].x2-oc[i].x1-1 /* XXX */;
	    ov_clips[i].height = oc[i].y2-oc[i].y1-1;
	    if (debug)
		fprintf(stderr,"v4l: clip=%dx%d+%d+%d\n",
			ov_clips[i].width,ov_clips[i].height,
			ov_clips[i].x,ov_clips[i].y);
	    if (ov_clips[i].x < 0 || ov_clips[i].y < 0 ||
		ov_clips[i].width < 0 || ov_clips[i].height < 0) {
		fprintf(stderr,"v4l: bug trap - overlay off\n");
		ioctl(fd, VIDIOCCAPTURE, &zero);
		overlay = 0;
		return 0;
	    }
	}
    }
    if (capability.type & VID_TYPE_CHROMAKEY) {
	ov_win.chromakey  = 0;    /* XXX */
    }

    if (-1 == ioctl(fd, VIDIOCSWIN, &ov_win))
	perror("ioctl VIDIOCSWIN");
    if (!overlay) {
	switch (format) {
	case VIDEO_RGB15:
	    pict.palette  = VIDEO_PALETTE_RGB555;
	    break;
	case VIDEO_RGB16:
	    pict.palette  = VIDEO_PALETTE_RGB565;
	    break;
	case VIDEO_RGB24:
	    pict.palette  = VIDEO_PALETTE_RGB24;
	    break;
	case VIDEO_RGB32:
	    pict.palette  = VIDEO_PALETTE_RGB32;
	    break;
	default:
	    TRAP("unsupported video format (overlay)");
	}
	
	if (-1 == ioctl(fd,VIDIOCSPICT,&pict))
	    perror("ioctl VIDIOCSPICT");
	if (-1 == ioctl(fd, VIDIOCCAPTURE, &one))
	    perror("ioctl VIDIOCCAPTURE");
	overlay = 1;
    }

    if (debug)
	fprintf(stderr,"v4l: overlay win=%dx%d+%d+%d, %d clips\n",
		width,height,x,y, count);

    return 0;
}

static void rgb_swap(char *mem, int n)
{
    char  c;
    char *p = mem;
    int   i = n;

    while (--i) {
	c = p[0]; p[0] = p[2]; p[2] = c;
	p += 3;
    }
}

void
rgb24_to_rgb32(void *d, void *s, int p)
{
    unsigned char  *dest = d;
    unsigned char  *src  = s;
    int             i    = p;

    while (i--) {
        *(dest++) = *(src++);
        *(dest++) = *(src++);
        *(dest++) = *(src++);
	*(dest++) = 0;
    }
}

static void*
grab_scr(void *dest, int width, int height)
{
    void *buf;

    if ((char*)-1 == map)
	return NULL;
    if (!gb_even.format)
	return NULL;
    
    gb_even.width  = width;
    gb_even.height = height;
    gb_odd.width  = width;
    gb_odd.height = height;

    if (gb_count == 0) {
	if (-1 == ioctl(fd,VIDIOCMCAPTURE,even ? &gb_even : &gb_odd))
	    perror("ioctl VIDIOCMCAPTURE");
	gb_count++;
    }

    if (-1 == ioctl(fd,VIDIOCMCAPTURE,even ? &gb_odd : &gb_even))
	perror("ioctl VIDIOCMCAPTURE");
    if (-1 == ioctl(fd,VIDIOCSYNC,0))
	perror("ioctl VIDIOCSYNC");
    buf = even ? map : map + MEM_SIZE;
    even = !even;

    if (pixmap_bytes == 4) {
	rgb24_to_rgb32(dest, buf, width*height);
    } else {
	memcpy(dest, buf, width*height*pixmap_bytes);
    }
    
    return dest;
}

static void*
grab_one(int width, int height)
{
    struct video_mmap gb;

    if ((char*)-1 == map)
	return NULL;
    
    while (gb_count > 0) {
	if (-1 == ioctl(fd,VIDIOCSYNC,0))
	    perror("ioctl VIDIOCSYNC");
	gb_count--;
    }

    gb.format = 0x11; /* FIXME: BT848_COLOR_FMT_RGB24 */
    gb.frame  = 0;
    gb.width  = width;
    gb.height = height;

    memset(map,0,width*height*3);
    if (-1 == ioctl(fd,VIDIOCMCAPTURE,&gb)) {
	perror("ioctl VIDIOCMCAPTURE");
	return NULL;
    }
    if (-1 == ioctl(fd,VIDIOCSYNC,0)) {
	perror("ioctl VIDIOCSYNC");
	return NULL;
    }

    rgb_swap(map,width*height);
    return map;
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
grab_input(int input, int norm)
{
    if (-1 != input) {
	if (debug)
	    fprintf(stderr,"v4l: input: %d\n",input);
	if (-1 == ioctl(fd, VIDIOCSCHAN, &input))
	    perror("ioctl VIDIOCSCHAN");
    }
    if (-1 != norm) {
	if (debug)
	    fprintf(stderr,"v4l: norm : %d\n",norm);
	tuner.mode = norm;
	if (-1 == ioctl(fd, VIDIOCSTUNER, &tuner))
	    perror("ioctl VIDIOCSTUNER");
    }
    return 0;
}

int
grab_picture(int color, int bright, int hue, int contrast)
{
    if (color != -1)
	pict.colour = color;
    if (contrast != -1)
	pict.contrast = contrast;
    if (bright != -1)
	pict.brightness = bright;
    if (hue != -1)
	pict.hue = hue;

    if (-1 == ioctl(fd,VIDIOCSPICT,&pict))
	perror("ioctl VIDIOCSPICT");

    return 0;
}

int
grab_audio(int mute, int volume, int *mode)
{
    if (mute != -1) {
	if (mute)
	    audios[0].flags |= VIDEO_AUDIO_MUTE;
	else
	    audios[0].flags &= ~VIDEO_AUDIO_MUTE;
    }
    if (volume != -1)
	audios[0].volume = volume;

    audios[0].mode = mode ? *mode : 0;
    if (-1 == ioctl(fd,VIDIOCSAUDIO,&audios[0]))
	perror("ioctl VIDIOCSAUDIO");

    if (mode) {
	if (-1 == ioctl(fd,VIDIOCGAUDIO,&audios[0]))
	    perror("ioctl VIDIOCGAUDIO");
	*mode = audios[0].mode;
    }
    return 0;
}
