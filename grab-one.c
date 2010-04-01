#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <asm/types.h>		/* XXX glibc */

#if USE_KERNEL_VIDEODEV
# include <linux/videodev.h>
#else
# include "videodev.h"
#endif

#define DEVNAME "/dev/video"

#ifndef BT848_COLOR_FMT_RGB32
# define BT848_COLOR_FMT_RGB32       0x00
# define BT848_COLOR_FMT_RGB24       0x11
# define BT848_COLOR_FMT_RGB16       0x22
# define BT848_COLOR_FMT_RGB15       0x33
# define BT848_COLOR_FMT_YUY2        0x44
# define BT848_COLOR_FMT_BtYUV       0x55
# define BT848_COLOR_FMT_Y8          0x66
# define BT848_COLOR_FMT_RGB8        0x77
# define BT848_COLOR_FMT_YCrCb422    0x88
# define BT848_COLOR_FMT_YCrCb411    0x99
# define BT848_COLOR_FMT_RAW         0xee
#endif

static int MEM_SIZE;
static int MEM_SIZE_try[] = { 0x151000, 0x144000 };

#define POST_NOTHING     0
#define POST_RGB_SWAP    1
#define POST_UNPACK422   2
#define POST_UNPACK411   3
#define POST_RAW         4

struct GRAB_FORMAT {
    char *name;
    int  bt848;
    char ptype;
    int  mul,div;
    int  post;
};

static struct GRAB_FORMAT formats[] = {
    { "rgb",  BT848_COLOR_FMT_RGB24,     '6',   3, 1,  POST_RGB_SWAP  },
    { "gray", BT848_COLOR_FMT_Y8,        '5',   1, 1,  POST_NOTHING   },
    { "411",  BT848_COLOR_FMT_YUY2,      '5',   3, 2,  POST_UNPACK411 },
    { "422",  BT848_COLOR_FMT_YUY2,      '5',   2, 1,  POST_UNPACK422 },
    { "raw",  BT848_COLOR_FMT_RAW,       '5',   1, 1,  POST_RAW       },
    /* end of table */
    { NULL,   0,                        '\0',   0, 0,  0              },
};

/* ---------------------------------------------------------------------- */

static struct video_capability  capability;
static struct video_channel     channel;
static struct video_audio       audio;
static struct video_tuner       tuner;
static struct video_picture     pict;

static struct video_mmap        gb;
static char *map = NULL;

/* ---------------------------------------------------------------------- */

static
void rgb_swap(char *mem, int n)
{
    char  c;
    char *p = mem;
    int   i = n;

    while (--i) {
	c = p[0]; p[0] = p[2]; p[2] = c;
	p += 3;
    }
}

static void
packed422_to_planar422(char *dest, char *src, int width, int height)
{
    int i;
    char *s, *y,*u,*v;

    i = (width * height)/2;
    s = src;
    y = dest;
    u = y + width * height;
    v = u + width * height / 2;
    
    while (--i) {
	*(y++) = *(src++);
	*(u++) = *(src++);
	*(y++) = *(src++);
        *(v++) = *(src++);
    }
}

static void
packed422_to_planar411(char *dest, char *src, int width, int height)
{
    int  a,b;
    char *s, *y,*u,*v;

    s = src;
    y = dest;
    u = y + width * height;
    v = u + width * height / 4;

    for (a = height; a > 0; a -= 2) {
	for (b = width; b > 0; b -= 2) {
	    
	    *(y++) = *(src++);
	    *(u++) = *(src++);
	    *(y++) = *(src++);
	    *(v++) = *(src++);
	}
	for (b = width; b > 0; b -= 2) {
	    *(y++) = *(src++);
	    src++;
	    *(y++) = *(src++);
	    src++;
	}
    }
}

/* ---------------------------------------------------------------------- */

void
usage()
{
    fprintf(stderr,
	    "usage: grab-one [ options] > file\n"
	    "\n"
	    "grab-one grabs one image from a bt848 card and "
	    "dumps it to stdout\n"
	    "\n"
	    "options:\n"
	    "  -f format   specify output format\n"
	    "  -s size     specify size (default is full-size)\n"
	    "  -r          raw data (default is ppm/pgm)\n"
	    "\n"
	    "formats:\n"
	    "  rgb (default), gray, 411, 422\n"
	    "\n"
	    "examples:\n"
	    "  grab-one > image.ppm\n"
	    "  grab-one -f gray -s 320x240 > no-color.pgm\n"
	    "  grab-one -f 411 -r | display -size 768x576 yuv:-\n"
	    "\n"
	    "--\n"
	    "(c) 1998 Gerd Knorr <kraxel@cs.tu-berlin.de>\n");
}

int
main(int argc, char **argv)
{
    int  format = 0, raw = 0;
    int  i,fd,width = 0, height = 0;
    char c,header[64],*buf;

    /* parse options */
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hrf:s:")))
	    break;
	switch (c) {
	case 'f':
	    for (i = 0; formats[i].name != NULL; i++)
		if (0 == strcasecmp(formats[i].name,optarg)) {
		    format = i;
		    break;
		}
	    if (formats[i].name == NULL) {
		fprintf(stderr,"unknown format %s (available:",optarg);
		for (i = 0; formats[i].name != NULL; i++)
		    fprintf(stderr," %s",formats[i].name);
		fprintf(stderr,")\n");
		exit(1);
	    }
	    break;
	case 's':
	    if (2 != sscanf(optarg,"%dx%d",&width,&height))
		width = height = 0;
	    break;
	case 'r':
	    raw = 1;
	    break;
	case 'h':
	default:
	    usage();
	    exit(1);
	}
    }

    /* open */
    if (-1 == (fd = open(DEVNAME,O_RDWR))) {
	perror("open " DEVNAME);
	exit(1);
    }

    /* get settings */
    if (-1 == ioctl(fd,VIDIOCGCAP,&capability)) {
	perror("ioctl VIDIOCGCAP");
	exit(1);
    }
    if (-1 == ioctl(fd,VIDIOCGCHAN,&channel))
	perror("ioctl VIDIOCGCHAN");
    if (-1 == ioctl(fd,VIDIOCGAUDIO,&audio))
	perror("ioctl VIDIOCGAUDIO");
    if (-1 == ioctl(fd,VIDIOCGTUNER,&tuner))
	perror("ioctl VIDIOCGTUNER");
    if (-1 == ioctl(fd,VIDIOCGPICT,&pict))
	perror("ioctl VIDIOCGPICT");

    /* mmap() buffer */
    for (i = 0; i < sizeof(MEM_SIZE_try)/sizeof(int); i++) {
	MEM_SIZE = MEM_SIZE_try[i];
	map = mmap(0,MEM_SIZE*2,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
	if (-1 != (int)map)
	    break;
    }
    if ((char*)-1 == map) {
	fprintf(stderr,"no mmap support\n");
	exit(1);
    } else {
	fprintf(stderr,"mmap()'ed buffer size = 0x%x\n",MEM_SIZE);
    }

    if (width == 0 || height == 0) {
	switch (tuner.mode) {
	case VIDEO_MODE_NTSC:
	    width  = 640;
	    height = 480;
	    break;
	default:
	    width  = 768;
	    height = 576;
	    break;
	}
    }

    gb.format = formats[format].bt848;
    gb.frame  = 0;
    gb.width  = width;
    gb.height = height;

    if (-1 == ioctl(fd,VIDIOCMCAPTURE,&gb)) {
	perror("ioctl VIDIOCMCAPTURE");
	exit(1);
    }
    if (-1 == ioctl(fd,VIDIOCSYNC,0)) {
	perror("ioctl VIDIOCSYNC");
	exit(1);
    }

    switch (formats[format].post) {
    case POST_RGB_SWAP:
	rgb_swap(map,width*height);
	buf = map;
	break;
    case POST_UNPACK422:
	buf = malloc(MEM_SIZE);
	packed422_to_planar422(buf,map,width,height);
	break;
    case POST_UNPACK411:
	buf = malloc(MEM_SIZE);
	packed422_to_planar411(buf,map,width,height);
	break;
    case POST_RAW:
	width  = 2270;
	height = 288;
	buf = map;
	break;
    default:
	buf = map;
	break;
    }

    sprintf(header,"P%c\n%d %d\n255\n",formats[format].ptype,width,height);
    if (!raw)
	write(1,header,strlen(header));
    write(1,buf,width*height*formats[format].mul/formats[format].div);

    /* done */
    close(fd);
    return 0;
}
