#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#include "config.h"

#include <asm/types.h>          /* XXX glibc */
#if USE_KERNEL_VIDEODEV
# include <linux/videodev.h>
#else
# include "videodev.h"
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef HAVE_LIBXXF86DGA
# include <X11/extensions/xf86dga.h>
#endif

#define DEVICE "/dev/bttv"

int    verbose = 1;
int    bpp     = 0;
char  *display = ":0.0";
char  *device  = "/dev/bttv";
char  *fbdev   = "/dev/fb0";

struct fb_fix_screeninfo   fix;
struct fb_var_screeninfo   var;

int
main(int argc, char *argv[])
{
    Display                 *dpy;
    Screen                  *scr;
    Window                   root;
    XWindowAttributes        wts;
    struct video_capability  capability;
    struct video_buffer      fbuf;
    int                      fd,c;
#ifdef HAVE_LIBXXF86DGA
    int                      width,bar,foo,flags;
    void                    *base = 0;
#endif

    /* parse options */
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hqd:v:b:")))
	    break;
	switch (c) {
	case 'q':
	    verbose = 0;
	    break;
#if 0 /* disabled for security reasons */
	case 'd':
	    display = optarg;
	    break;
#endif
	case 'v':
	    device = optarg;
	    break;
	case 'b':
	    bpp = atoi(optarg);
	    break;
	case 'h':
	default:
	    fprintf(stderr,
		    "usage: %s  [ options ] \n"
		    "\n"
		    "options:\n"
		    "    -q        quiet\n"
#if 0
		    "    -d <dpy>  X11 Display     [%s]\n"
#endif
		    "    -v <dev>  video device    [%s]\n"
		    "    -b <n>    displays color depth is <n> bpp\n"
		    "              might be required for (and works\n"
		    "              only with) 24/32 bpp\n",
		    argv[0],
#if 0
		    display,
#endif
		    device);
	    exit(1);
	}
    }

    /* get screen params */
    if (NULL != (dpy = XOpenDisplay(display))) {
	scr  = DefaultScreenOfDisplay(dpy);
	root = DefaultRootWindow(dpy);
	XGetWindowAttributes(dpy, root, &wts);
	if ((bpp == 32 || bpp == 24) && (wts.depth == 32 || wts.depth == 24))
	    wts.depth = bpp;
	if (verbose)
	    fprintf(stderr,"x11: mode=%dx%dx%d\n",wts.width,wts.height,wts.depth);
	
#ifdef HAVE_LIBXXF86DGA
	if (XF86DGAQueryExtension(dpy,&foo,&bar)) {
	    XF86DGAQueryDirectVideo(dpy,XDefaultScreen(dpy),&flags);
	    if (flags & XF86DGADirectPresent) {
		XF86DGAGetVideoLL(dpy,XDefaultScreen(dpy),(int*)&base,&width,&foo,&bar);
		if (verbose)
		    fprintf(stderr,"dga: base=%p, width=%d\n", base, width);
	    }
	}
#endif
    } else {
	fprintf(stderr,"can't open x11 display %s\n",display);

	/* try framebuffer */
	if (-1 == (fd = open(fbdev,O_RDWR))) {
	    fprintf(stderr,"open %s: %s\n",fbdev,strerror(errno));
	    exit(1);
	}
	if (-1 == ioctl(fd,FBIOGET_FSCREENINFO,&fix)) {
	    perror("ioctl FBIOGET_FSCREENINFO");
	    exit(1);
	}
	if (-1 == ioctl(fd,FBIOGET_VSCREENINFO,&var)) {
	    perror("ioctl FBIOGET_VSCREENINFO");
	    exit(1);
	}
	if (fix.type != FB_TYPE_PACKED_PIXELS) {
	    fprintf(stderr,"can handle only packed pixel frame buffers\n");
	    exit(1);
	}
	close(fd);
	fprintf(stderr,"%s: %dx%dx%d @ %p\n",fbdev,
		var.xres_virtual,
		var.yres_virtual,
		var.bits_per_pixel,
		fix.smem_start);
    }

    /* open & check v4l device */
    if (-1 == (fd = open(DEVICE,O_RDWR))) {
	fprintf(stderr,"can't open %s: %s\n",device,strerror(errno));
	exit(1);
    }
    if (-1 == ioctl(fd,VIDIOCGCAP,&capability)) {
	fprintf(stderr,"%s: ioctl VIDIOCGCAP: %s\n",device,strerror(errno));
	exit(1);
    }
    if (!(capability.type & VID_TYPE_OVERLAY)) {
	fprintf(stderr,"%s: no overlay support\n",device);
	exit(1);
    }

    /* read-modify-write v4l screen parameters */
    if (-1 == ioctl(fd,VIDIOCGFBUF,&fbuf)) {
	fprintf(stderr,"%s: ioctl VIDIOCGFBUF: %s\n",device,strerror(errno));
	exit(1);
    }
    if (verbose)
	fprintf(stderr,"v4l: base=%p\n",fbuf.base);
#ifdef HAVE_LIBXXF86DGA
    if (base && fbuf.base != base) {
	fbuf.base = base;
	if (verbose)
	    fprintf(stderr,"setting v4l base to %p\n",fbuf.base);
    }
#endif
    if (dpy) {
	/* x11 */
	fbuf.depth        = (wts.depth+7) & 0xf8;
#ifdef HAVE_LIBXXF86DGA
	fbuf.width        = (flags & XF86DGADirectPresent) ? width : wts.width;
#else
	fbuf.width        = wts.width;
#endif
	fbuf.height       = wts.height;
	fbuf.bytesperline = fbuf.width * fbuf.depth/8;
    } else {
	/* framebuffer */
	fbuf.depth        = (var.bits_per_pixel+7) & 0xf8;
	fbuf.width        = var.xres_virtual;
	fbuf.height       = var.yres_virtual;
	fbuf.bytesperline = fix.line_length;
    }
    if (-1 == ioctl(fd,VIDIOCSFBUF,&fbuf)) {
	fprintf(stderr,"%s: ioctl VIDIOCSFBUF: %s\n",device,strerror(errno));
	exit(1);
    }
    if (verbose)
	fprintf(stderr,"ok\n");

    return 0;
}



