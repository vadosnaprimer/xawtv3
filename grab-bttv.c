#include "config.h"
#ifdef HAVE_BTTV

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

#define OLDBTTV
#include "bttv.h"
#include "bt848.h"
#include "videodev.h" /* audio */

#include "grab.h"

/* ---------------------------------------------------------------------- */

static struct bttv_window  wtw;
static struct cliprec      cr[MAX_CLIPRECS];
static int    overlay;
static int    fwintv = -1;
static int    framecount;
static struct timeval start;

/*                            PAL  NTSC */
static int    maxwidth[]  = { 768, 640 };
static int    maxheight[] = { 576, 480 };

static struct STRTAB normtab[] = {
    {  0, "PAL" },
    {  1, "NTSC" },
    {  2, "SECAM" },
    { -1, NULL }
};

static struct STRTAB srctab[] = {
    {  0, "Television" },
    {  1, "Composite1" },
    {  2, "Composite2" },
    {  3, "SVHS" },
    { -1, NULL }
};

/* ---------------------------------------------------------------------- */

static int
grab_open(char *filename, int sw, int sh, int format, void *base)
{
    int val;

    if (-1 != fwintv)
	goto err;
    
    if (-1 == (fwintv=open(filename ? filename : "/dev/bttv",O_RDWR)))
	goto err;

    if (debug)
	fprintf(stderr, "bttv: open\n");

    /* init bttv */
    ioctl(fwintv, BTTV_GETWTW, &wtw);
    wtw.x         = 0;
    wtw.y         = 0;
    wtw.cropx     = 0;
    wtw.cropy     = 0;
    wtw.swidth    = sw;
    wtw.sheight   = sh;

    wtw.norm      = 0;
    wtw.cropwidth = maxwidth[wtw.norm];
    wtw.cropheight= maxheight[wtw.norm];
    wtw.width     = maxwidth[wtw.norm];
    wtw.height    = maxheight[wtw.norm];

    /* defaults */
    val = 254;  ioctl(fwintv, BTTV_COLOR,    &val);
    val = 254;  ioctl(fwintv, BTTV_CONTRAST, &val);
    val = 0;    ioctl(fwintv, BTTV_BRIGHT,   &val);
    val = 0;    ioctl(fwintv, BTTV_HUE,      &val);
    if (NULL != base)
	ioctl(fwintv, BTTV_SETVIRTADR, &base);

    gettimeofday(&start,NULL);
    framecount = 0;
    return 0;

err:
    if (fwintv != -1) {
	close(fwintv);
	fwintv = -1;
    }
    return -1;
}

static int
grab_close()
{
    if (-1 == fwintv)
	return 0;
    
    if (debug)
	fprintf(stderr, "bttv: close\n");

    close(fwintv);
    fwintv = -1;
    return 0;
}

/* ---------------------------------------------------------------------- */

static int
grab_overlay(int x, int y, int width, int height, int format,
	     struct OVERLAY_CLIP *oc, int count)
{
    int i;
    
    ioctl(fwintv, BTTV_CAP_OFF, NULL);
    if (width == 0 || height == 0) {
	if (debug)
	    fprintf(stderr, "bttv: overlay off\n");
	overlay = 0;
	return 0;
    }

    switch (format) {
    case VIDEO_RGB16:
	wtw.bpp       = 2;
	wtw.color_fmt = BT848_COLOR_FMT_RGB16;
	break;
    case VIDEO_RGB24:
	wtw.bpp       = 3;
	wtw.color_fmt = BT848_COLOR_FMT_RGB24;
	break;
    case VIDEO_RGB32:
	wtw.bpp       = 4;
	wtw.color_fmt = BT848_COLOR_FMT_RGB32;
	break;
    default:
	TRAP("unsupported video format (overlay)");
    }

    wtw.bpl       = wtw.swidth * wtw.bpp;
    wtw.x         = x;
    wtw.y         = y;
#if 0
    wtw.cropx     = 0;
    wtw.cropy     = 0;
    wtw.cropwidth = width;
    wtw.cropheight= height;
#endif
    wtw.width     = width;
    wtw.height    = height;
    wtw.interlace = ((wtw.height > maxheight[wtw.norm]/2) ||
		     (wtw.width  > maxwidth[wtw.norm]/2)) ? 1:0;
    ioctl(fwintv, BTTV_SETWTW,  &wtw);

    for (i = 0; i < count; i++) {
	cr[i].x    = oc[i].x1;
	cr[i].y    = oc[i].y1;
	cr[i].x2   = oc[i].x2-1;
	cr[i].y2   = oc[i].y2-1;
	if (debug)
	    fprintf(stderr,"bttv: clip=%d,%d->%d,%d\n",
		    cr[i].x ,cr[i].y, cr[i].x2,cr[i].y2);
    }
    cr[MAX_CLIPRECS-1].x = count;
    ioctl(fwintv, BTTV_SETCLIP, cr);

    if (debug)
	fprintf(stderr,
		"bttv: overlay win=%dx%d+%d+%d, screen=%dx%d, %d clips\n",
		wtw.width,wtw.height, wtw.x,wtw.y,
		wtw.swidth,wtw.sheight,count);

    ioctl(fwintv, BTTV_CAP_ON, NULL);
    overlay = 1;

    return 0;
}

static int
grab_tune(unsigned long freq)
{
    if (debug)
	fprintf(stderr,"bttv: freq %.3f\n",(float)freq/16);
    ioctl(fwintv, BTTV_SETFREQ,  &freq);
    return 0;
}

static int
grab_input(int input, int norm)
{
    if (-1 != input) {
	if (debug)
	    fprintf(stderr,"bttv: input %s\n",srctab[input].str);
	ioctl(fwintv, BTTV_INPUT, &input);
    }
    if (-1 != norm) {
	if (debug)
	    fprintf(stderr,"bttv: norm %s\n",normtab[norm].str);
	wtw.norm = norm;
	if (overlay)
	    ioctl(fwintv, BTTV_SETWTW, &wtw);
    }
    return 0;
}

int
grab_picture(int color, int bright, int hue, int contrast)
{
    int val;

    if (color != -1) {
	val = color / 128;
	ioctl(fwintv,BTTV_COLOR,&val);
    }
    if (contrast != -1) {
	val = contrast / 128;
	ioctl(fwintv,BTTV_CONTRAST,&val);
    }
    if (bright != -1) {
	val = (bright / 256) - 128;
	ioctl(fwintv,BTTV_BRIGHT,&val);
    }
    if (hue != -1) {
	val = (hue / 256) - 128;
	ioctl(fwintv,BTTV_HUE,&val);
    }
    return 0;
}

static struct video_audio audio;

int
grab_audio(int mute, int volume, int *mode)
{
    if (!audio.flags)
	ioctl(fwintv,VIDIOCGAUDIO,&audio);
    if (mute != -1) {
	if (mute)
	    audio.flags |= VIDEO_AUDIO_MUTE;
	else
	    audio.flags &= ~VIDEO_AUDIO_MUTE;
    }
    if (volume != -1)
	audio.volume = volume;

    audio.mode = mode ? *mode : 0;
    if (-1 == ioctl(fwintv,VIDIOCSAUDIO,&audio))
	perror("ioctl VIDIOCSAUDIO");

    if (mode) {
	if (-1 == ioctl(fwintv,VIDIOCGAUDIO,&audio))
	    perror("ioctl VIDIOCGAUDIO");
	*mode = audio.mode;
    }
    return 0;
}

/* ---------------------------------------------------------------------- */

struct GRABBER grab_bttv = {
    "bttv",
    VIDEO_RGB16 | VIDEO_RGB24 | VIDEO_RGB32,
    CAN_AUDIO_VOLUME,
    normtab,srctab,

    grab_open,grab_close,
    grab_overlay,
    NULL,
    grab_tune,
    grab_input,
    grab_picture,
    grab_audio
};

#endif
