/*
 * interface to the v4l2 driver
 *
 *   (c) 1998,99 Gerd Knorr <kraxel@goldbach.in-berlin.de>
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

#include "grab-ng.h"

#ifndef __linux__
struct GRABBER grab_v4l2 = {};
#else /* __linux__ */

#include <asm/types.h>		/* XXX glibc */
#include "videodev2.h"

/* ---------------------------------------------------------------------- */
/* global variables                                                       */

#define WANTED_BUFFERS 8

static int fd = -1;

static int    ninputs,nstds,nfmts;
static struct v4l2_capability	cap;
static struct v4l2_streamparm	streamparm;
static struct v4l2_input	inp[16];
static struct v4l2_enumstd	std[16];
static struct v4l2_fmtdesc	fmt[64];
static struct v4l2_queryctrl	ctl[32];

static int                           str_fps = 0;
static struct v4l2_format            str_format;
static struct v4l2_requestbuffers    str_bufdesc;
static struct v4l2_buffer            str_buffers[WANTED_BUFFERS];
static unsigned char                *str_bufdata[WANTED_BUFFERS];
static unsigned char                *read_buf;
static int                           str_lastdequeued;

static int                           preview_state;
static int                           preview_reenable;
static int                           preview_and_streaming_works = 1;

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

/* ---------------------------------------------------------------------- */
/* prototypes                                                             */

/* open/close */
static int v4l2_open(char *filename);
static int v4l2_close(void);

/* control */
static int v4l2_input(int input, int norm);
static int v4l2_hasattr(int id);
static int v4l2_getattr(int id);
static int v4l2_setattr(int id, int vol);

static unsigned long v4l2_tune(unsigned long freq, int sat);
static int v4l2_tuned(void);


/* capture */
static int   v4l2_setparm(int format, int *width, int *height, int *linelength);
static void  v4l2_start(int fps, int buffers);
static void* v4l2_capture(void);
static void* v4l2_streaming(void);
static void  v4l2_stop(void);
static void  v4l2_stop_streaming(void);

/* overlay */
static int v4l2_setupfb(int sw, int sh, int format, void *base, int bpl);
static int v4l2_overlay(int x, int y, int width, int height, int format,
			struct OVERLAY_CLIP *oc, int count);

/* ---------------------------------------------------------------------- */

struct GRABBER grab_v4l2 = {
    name:           "v4l2",
    
    grab_open:      v4l2_open,
    grab_close:     v4l2_close,
    
    grab_setupfb:   v4l2_setupfb,
    grab_setparams: v4l2_setparm,
    
    grab_input:     v4l2_input,
    grab_hasattr:   v4l2_hasattr,
    grab_getattr:   v4l2_getattr,
    grab_setattr:   v4l2_setattr,
};

/* xawtv => v4l */

static __u32 xawtv_pixelformat[] = {
    0,                    /* unused   */
    0,                    /* RGB8     */
    V4L2_PIX_FMT_GREY,    /* GRAY8    */
    V4L2_PIX_FMT_RGB555,  /* RGB15_LE */
    V4L2_PIX_FMT_RGB565,  /* RGB16_LE */
    0,                    /* RGB15_BE */
    0,                    /* RGB16_BE */
    V4L2_PIX_FMT_BGR24,   /* BGR24    */
    V4L2_PIX_FMT_BGR32,   /* BGR32    */
    V4L2_PIX_FMT_RGB24,   /* RGB24    */
    0,                    /* RGB32    */
    0,                    /* LUT 2    */
    0,                    /* LUT 4    */
    V4L2_PIX_FMT_YUYV,    /* YUV422   */
    0,                    /* YUV422P  */
    V4L2_PIX_FMT_YUV420,  /* YUV420P  */
};

/* ---------------------------------------------------------------------- */
/* debug output                                                           */

static void
print_bits(char *title, char **names, int count, int value)
{
    int i;
    
    fprintf(stderr,"%s: ",title);
    for (i = 0; i < count; i++) {
	if (value & (1 << i))
	    fprintf(stderr,"%s ",names[i]);
    }
    fprintf(stderr,"\n");
}
    

static void
print_device_capabilities(void)
{
    static char *cap_type[] = {
	"capture",
	"codec",
	"output",
	"fx",
	"vbi",
	"vtr",
	"vtx",
	"radio",
    };
    static char *cap_flags[] = {
	"read",
	"write",
	"streaming",
	"preview",
	"select",
	"tuner",
	"monochrome",
	"teletext"
    };
    static char *ctl_type[] = {
	"integer",
	"boolean",
	"menu"
    };
    static char *cap_parm[] = {
	"highquality",
	"vflip",
	"hflip"
    };

    int i;

    fprintf(stderr,"\n*** v4l2: video device capabilities ***\n");

    /* capabilities */
    fprintf(stderr,"type: %s\n",
	    cap.type < sizeof(cap_type)/sizeof(char*) ?
	    cap_type[cap.type] : "unknown");
    print_bits("flags",cap_flags,sizeof(cap_flags)/sizeof(char*),cap.flags);
    fprintf(stderr,"\n");
    fprintf(stderr,"inputs: %d\naudios: %d\n",cap.inputs,cap.audios);
    fprintf(stderr,"size: %dx%d => %dx%d\n",
	    cap.minwidth,cap.minheight,cap.maxwidth,cap.maxheight);
    fprintf(stderr,"fps: %d max\n",cap.maxframerate);

    /* inputs */
    fprintf(stderr,"video inputs:\n");
    for (i = 0; i < ninputs; i++) {
	printf("  %d: \"%s\", tuner: %s, audio: %s\n", i, inp[i].name,
	       (inp[i].type       == V4L2_INPUT_TYPE_TUNER) ? "yes" : "no",
	       (inp[i].capability &  V4L2_INPUT_CAP_AUDIO)  ? "yes" : "no");
    }

    /* video standards */
    fprintf(stderr,"video standards:\n");
    for (i = 0; i < nstds; i++) {
	printf("  %d: \"%s\"\n", i, std[i].std.name);
    }

    /* capture formats */
    fprintf(stderr,"capture formats:\n");
    for (i = 0; i < nfmts; i++) {
	printf("  %d: %c%c%c%c, depth=%d,%s \"%s\"\n", i,
	       fmt[i].pixelformat & 0xff,
	       (fmt[i].pixelformat >>  8) & 0xff,
	       (fmt[i].pixelformat >> 16) & 0xff,
	       (fmt[i].pixelformat >> 24) & 0xff,
	       fmt[i].depth,
	       (fmt[i].flags & V4L2_FMT_FLAG_COMPRESSED) ? " compressed" : "",
	       fmt[i].description);
    }

    /* capture parameters */
    fprintf(stderr,"capture parameters:\n");
    print_bits("  cap",cap_parm,sizeof(cap_parm)/sizeof(char*),
	       streamparm.parm.capture.capability);
    print_bits("  cur",cap_parm,sizeof(cap_parm)/sizeof(char*),
	       streamparm.parm.capture.capturemode);
    fprintf(stderr,"  timeperframe=%ld\n",
	    streamparm.parm.capture.timeperframe);

    /* controls */
    printf("supported controls:\n");
    for (i = 0; i < 32; i++) {
	if (ctl[i].id == -1)
	    continue;
	fprintf(stderr,"  %2d: \"%s\", [%d .. %d], step=%d, def=%d, type=%s\n",
		i, ctl[i].name,
		ctl[i].minimum,ctl[i].maximum,
		ctl[i].step,ctl[i].default_value,
		ctl_type[ctl[i].type]);
    }
    fprintf(stderr,"\n");
}

static void
print_bufinfo(struct v4l2_buffer *buf, void *addr)
{
    static char *type[] = {
	"",
	"capture",
	"codec in",
	"codec out",
	"effects in1",
	"effects in2",
	"effects out",
	"video out"
    };

    fprintf(stderr,"v4l2: buf %d: %s 0x%x+%d, used %d",
		   buf->index,
	    	   buf->type < sizeof(type)/sizeof(char*) ?
			type[buf->type] : "unknown",
		   buf->offset,buf->length,buf->bytesused);
    if (NULL != addr)
	fprintf(stderr,", mapped to: %p",addr);
    fprintf(stderr,"\n");
}

static void
print_fbinfo(struct v4l2_framebuffer *fb)
{
    static char *fb_cap[] = {
	"extern",
	"chromakey",
	"clipping",
	"scale-up",
	"scale-down"
    };
    static char *fb_flags[] = {
	"primary",
	"overlay",
	"chromakey"
    };

    /* capabilities */
    fprintf(stderr,"v4l2: framebuffer info\n");
    print_bits("  cap",fb_cap,sizeof(fb_cap)/sizeof(char*),fb->capability);
    print_bits("  flags",fb_cap,sizeof(fb_flags)/sizeof(char*),fb->flags);
    fprintf(stderr,"  base: %p %p %p\n",fb->base[0],fb->base[1],fb->base[2]);
    fprintf(stderr,"  format: %dx%d, %c%c%c%c, %d byte\n",
	    fb->fmt.width, fb->fmt.height,
	    fb->fmt.pixelformat & 0xff,
	    (fb->fmt.pixelformat >>  8) & 0xff,
	    (fb->fmt.pixelformat >> 16) & 0xff,
	    (fb->fmt.pixelformat >> 24) & 0xff,
	    fb->fmt.sizeimage);
}

/* ---------------------------------------------------------------------- */
/* helpers                                                                */

static void
get_device_capabilities(void)
{
    int i;
    
    for (ninputs = 0; ninputs < cap.inputs; ninputs++) {
	inp[ninputs].index = ninputs;
	if (-1 == ioctl(fd, VIDIOC_ENUMINPUT, &inp[ninputs]))
	    break;
    }
    for (nstds = 0; nstds < 16; nstds++) {
	std[nstds].index = nstds;
	if (-1 == ioctl(fd, VIDIOC_ENUMSTD, &std[nstds]))
	    break;
    }
    for (nfmts = 0; nfmts < 64; nfmts++) {
	fmt[nfmts].index = nfmts;
	if (-1 == ioctl(fd, VIDIOC_ENUM_PIXFMT, &fmt[nfmts]))
	    break;
    }

    streamparm.type = V4L2_BUF_TYPE_CAPTURE;
    ioctl(fd,VIDIOC_G_PARM,&streamparm);

    /* controls */
    for (i = 0; i < 16; i++) {
	ctl[i].id = V4L2_CID_BASE+i;
	if (-1 == ioctl(fd, VIDIOC_QUERYCTRL, &ctl[i]) ||
	    (ctl[i].flags & V4L2_CTRL_FLAG_DISABLED))
	    ctl[i].id = -1;
    }
    for (i = 0; i < 16; i++) {
	ctl[i+16].id = V4L2_CID_PRIVATE_BASE+i;
	if (-1 == ioctl(fd, VIDIOC_QUERYCTRL, &ctl[i+16]) ||
	    (ctl[i+16].flags & V4L2_CTRL_FLAG_DISABLED))
	    ctl[i+16].id = -1;
    }
}

static struct STRTAB *
build_norms(void)
{
    struct STRTAB *norms;
    int i;

    norms = malloc(sizeof(struct STRTAB) * (nstds+1));
    for (i = 0; i < nstds; i++) {
	norms[i].nr  = i;
	norms[i].str = std[i].std.name;
    }
    norms[i].nr  = -1;
    norms[i].str = NULL;
    return norms;
}

static struct STRTAB *
build_inputs(void)
{
    struct STRTAB *inputs;
    int i;

    inputs = malloc(sizeof(struct STRTAB) * (ninputs+1));
    for (i = 0; i < ninputs; i++) {
	inputs[i].nr  = i;
	inputs[i].str = inp[i].name;
    }
    inputs[i].nr  = -1;
    inputs[i].str = NULL;
    return inputs;
}

/* ---------------------------------------------------------------------- */

static int
v4l2_open(char *filename)
{
    if (-1 != fd)
	goto err;

    grabber_run_v4l_conf();
    if (-1 == (fd = open(filename ? filename : "/dev/video",O_RDWR))) {
	fprintf(stderr,"open %s: %s\n",
		filename ? filename : "/dev/video",strerror(errno));
	goto err;
    }

    if (-1 == ioctl(fd,VIDIOC_QUERYCAP,&cap))
	goto err;
    if (debug)
	fprintf(stderr, "v4l2: open\n");
    fcntl(fd,F_SETFD,FD_CLOEXEC);
    fprintf(stderr,"v4l2: device is %s\n",cap.name);
    sprintf(grab_v4l2.name = malloc(strlen(cap.name)+8),"v4l2: %s",cap.name);

    get_device_capabilities();
    if (debug)
	print_device_capabilities();
    grab_v4l2.norms  = build_norms();
    grab_v4l2.inputs = build_inputs();

    /* setup capture */
    if (cap.flags & V4L2_FLAG_STREAMING) {
	grab_v4l2.grab_start   = v4l2_start;
	grab_v4l2.grab_capture = v4l2_streaming;
	grab_v4l2.grab_stop    = v4l2_stop;
    } else if (cap.flags & V4L2_FLAG_READ) {
	grab_v4l2.grab_start   = v4l2_start;
	grab_v4l2.grab_capture = v4l2_capture;
	grab_v4l2.grab_stop    = v4l2_stop;
    }

    /* setup tuner */
    if (cap.flags & V4L2_FLAG_TUNER) {
	grab_v4l2.grab_tune  = v4l2_tune;
	grab_v4l2.grab_tuned = v4l2_tuned;
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
v4l2_close()
{
    if (-1 == fd)
	return 0;

    if (str_fps) {
	if (debug)
	    fprintf(stderr,"v4l2: stopping streaming capture\n");
	v4l2_stop_streaming();
	str_fps = 0;
    }

    if (debug)
	fprintf(stderr, "v4l2: close\n");

    close(fd);
    fd = -1;
    return 0;
}

/* ---------------------------------------------------------------------- */

static int
v4l2_input(int input, int norm)
{
    if (norm != -1) {
	if (debug)
	    fprintf(stderr,"v4l2: norm %s\n",std[norm].std.name);
	if (-1 == ioctl(fd,VIDIOC_S_STD,&std[norm].std))
	    perror("ioctl VIDIOC_S_STD");
    }
    if (input != -1) {
	if (debug)
	    fprintf(stderr,"v4l2: input %s\n",inp[input].name);
	if (-1 == ioctl(fd,VIDIOC_S_INPUT,&input))
	    perror("ioctl VIDIOC_S_INPUT");
    }
    return 0;
}

static int
v4l2_getcontrol(int i)
{
    int value;
    struct v4l2_control c;

    if (-1 == ctl[i].id)
	return -1;
    c.id    = ctl[i].id;
    if (-1 == ioctl(fd,VIDIOC_G_CTRL,&c))
	perror("ioctl VIDIOC_G_CTRL");
    if (ctl[i].type == V4L2_CTRL_TYPE_INTEGER) {
	value = (c.value-ctl[i].minimum)*65536/(ctl[i].maximum-ctl[i].minimum);
	if (value < 0)     value = 0;
	if (value > 65535) value = 65535;
    } else {
	value = c.value;
    }
    return value;
}

static void
v4l2_setcontrol(int i, int value)
{
    struct v4l2_control c;

    if (-1 == ctl[i].id)
	return;
    c.id    = ctl[i].id;
    
    if (ctl[i].type == V4L2_CTRL_TYPE_INTEGER) {
	c.value = (int)((float)value*(ctl[i].maximum-ctl[i].minimum)
			/ 65536 + ctl[i].minimum);
	if (c.value < ctl[i].minimum) c.value = ctl[i].minimum;
	if (c.value > ctl[i].maximum) c.value = ctl[i].maximum;
    } else {
	c.value = value;
    }
    if (debug)
	fprintf(stderr,"v4l2: %s = %d [%d..%d]\n",ctl[i].name,
		c.value,ctl[i].minimum,ctl[i].maximum);
    if (-1 == ioctl(fd,VIDIOC_S_CTRL,&c))
	perror("ioctl VIDIOC_S_CTRL");
}

static int
v4l2_hasattr(int id)
{
    int i;

    for (i = 0; i < NUM_ATTR; i++)
	if (id == v4l2_attr[i].id)
	    break;
    if (i == NUM_ATTR)
	return 0;
    if (-1 == ctl[v4l2_attr[i].v4l2 - V4L2_CID_BASE].id)
	return 0;
    return 1;
}

static int
v4l2_getattr(int id)
{
    int i;

    for (i = 0; i < NUM_ATTR; i++)
	if (id == v4l2_attr[i].id)
	    break;
    if (i == NUM_ATTR)
	return -1;
    return v4l2_getcontrol(v4l2_attr[i].v4l2 - V4L2_CID_BASE);
}

static int
v4l2_setattr(int id, int val)
{
    int i;

    for (i = 0; i < NUM_ATTR; i++)
	if (id == v4l2_attr[i].id)
	    break;
    if (i == NUM_ATTR)
	return -1;
    v4l2_setcontrol(v4l2_attr[i].v4l2 - V4L2_CID_BASE, val);
    return 0;
}

static unsigned long
v4l2_tune(unsigned long freq, int sat)
{
    if (-1 == freq) {
	if (-1 == ioctl(fd, VIDIOC_G_FREQ, &freq))
	    perror("ioctl VIDIOC_G_FREQ");
	return freq;
    }
    if (debug)
	fprintf(stderr,"v4l2: freq: %.3f\n",(float)freq/16);
    if (-1 == ioctl(fd, VIDIOC_S_FREQ, &freq))
	perror("ioctl VIDIOC_S_FREQ");
    return 0;
}

static int
v4l2_tuned()
{
    struct v4l2_tuner tuner;

    memset(&tuner,0,sizeof(tuner));
    if (-1 == ioctl(fd,VIDIOC_G_TUNER,&tuner)) {
	perror("ioctl VIDIOC_G_TUNER");
	return 0;
    }
    if (debug)
	fprintf(stderr,"v4l2: tuner.signal=%d\n",tuner.signal);
    return tuner.signal ? 1 : 0;
}

/* ---------------------------------------------------------------------- */
/* capture                                                                */

/* helpers for streaming capture */
static int
v4l2_start_streaming(int buffers)
{
    int i;
    
    /* setup buffers */
    str_bufdesc.count = buffers;
    str_bufdesc.type  = V4L2_BUF_TYPE_CAPTURE;
    if (-1 == ioctl(fd, VIDIOC_REQBUFS, &str_bufdesc)) {
	perror("ioctl VIDIOC_REQBUFS");
	return -1;
    }
#if 0
    if (str_bufdesc.count < 2) {
	fprintf(stderr, "error: need >= 2 buffers for streaming capture\n");
	return -1;
    }
#endif
    for (i = 0; i < str_bufdesc.count; i++) {
	str_buffers[i].index = i;
	str_buffers[i].type  = V4L2_BUF_TYPE_CAPTURE;
	if (-1 == ioctl(fd, VIDIOC_QUERYBUF, &str_buffers[i])) {
	    perror("ioctl VIDIOC_QUERYBUF");
	    return -1;
	}
	str_bufdata[i] = mmap(NULL, str_buffers[i].length,
			      PROT_READ | PROT_WRITE, MAP_SHARED,
			      fd, str_buffers[i].offset);
	if ((void*)-1 == str_bufdata[i]) {
	    perror("mmap");
	    return -1;
	}
	if (debug)
	    print_bufinfo(&str_buffers[i],str_bufdata[i]);
    }

    /* queue up all but one buffers */
    for (i = 0; i < str_bufdesc.count-1; i++) {
	if (-1 == ioctl(fd,VIDIOC_QBUF,&str_buffers[i])) {
	    perror("ioctl VIDIOC_QBUF");
	    return -1;
	}
    }
    str_lastdequeued = str_bufdesc.count-1; /* the buffer that isn't queued */

 try_again:
    /* turn off preview (if needed) */
    if (preview_state && !preview_and_streaming_works) {
	preview_reenable = 1;
	preview_state = 0;
	if (-1 == ioctl(fd, VIDIOC_PREVIEW, &preview_state))
	    perror("ioctl VIDIOC_PREVIEW");
	if (debug)
	    fprintf(stderr,"v4l2: overlay off (start_streaming)\n");
    }

    /* start capture */
    if (-1 == ioctl(fd,VIDIOC_STREAMON,&str_format.type)) {
	if (preview_and_streaming_works && preview_state && errno == EBUSY) {
	    preview_and_streaming_works = 0;
	    goto try_again;
	}
	perror("ioctl VIDIOC_STREAMON");
	return -1;
    }
    return 0;
}

static void
v4l2_stop_streaming()
{
    int i;
    
    /* stop capture */
    if (-1 == ioctl(fd,VIDIOC_STREAMOFF,&str_format.type))
	perror("ioctl VIDIOC_STREAMOFF");
    
    /* free buffers */
    for (i = 0; i < str_bufdesc.count; i++) {
	if (-1 == munmap(str_bufdata[i],str_buffers[i].length))
	    perror("munmap");
    }

    /* turn on preview (if needed) */
    if (preview_reenable) {
	preview_reenable = 0;
	preview_state = 1;
	if (-1 == ioctl(fd, VIDIOC_PREVIEW, &preview_state))
	    perror("ioctl VIDIOC_PREVIEW");
	if (debug)
	    fprintf(stderr,"v4l2: overlay on (stop_streaming)\n");
    }
}

static unsigned char *
v4l2_get_streaming(int *size)
{
    struct v4l2_buffer buf;
    struct timeval tv;
    fd_set rdset;

    /* queue up the free buffer */
    if (-1 == ioctl(fd,VIDIOC_QBUF,&str_buffers[str_lastdequeued])) {
	perror("ioctl VIDIOC_QBUF");
	return NULL;
    }

    /* wait for the next one */
    tv.tv_sec  = 1;
    tv.tv_usec = 0;
    FD_ZERO(&rdset);
    FD_SET(fd, &rdset);
    switch (select(fd + 1, &rdset, NULL, NULL, &tv)) {
    case -1: perror("v4l2: select"); break;
    case  0: fprintf(stderr,"v4l2: oops: select timeout\n"); break;
    }

    /* get it */
    memset(&buf,0,sizeof(buf));
    buf.type = V4L2_BUF_TYPE_CAPTURE;
    if (-1 == ioctl(fd,VIDIOC_DQBUF,&buf)) {
	perror("ioctl VIDIOC_DQBUF");
	return NULL;
    }
    if (debug > 1)
	print_bufinfo(&buf,NULL);	
    *size = buf.bytesused;
    str_lastdequeued = buf.index; /* queue this one up next time */

    return str_bufdata[buf.index];
}

/* set capture parameters */
static int
v4l2_setparm(int format, int *width, int *height, int *linelength)
{
    if (str_fps) {
	if (debug)
	    fprintf(stderr,"v4l2: stopping streaming capture\n");
	v4l2_stop_streaming();
	str_fps = 0;
    }
    if (read_buf)
	free(read_buf);
    read_buf = NULL;
    
    str_format.type = V4L2_BUF_TYPE_CAPTURE;
    str_format.fmt.pix.pixelformat  = xawtv_pixelformat[format];
    str_format.fmt.pix.flags        = V4L2_FMT_FLAG_INTERLACED;
    str_format.fmt.pix.depth        = ng_vfmt_to_depth[format];
    str_format.fmt.pix.width        = *width;
    str_format.fmt.pix.height       = *height;
    str_format.fmt.pix.bytesperline = *linelength;

    if (-1 == ioctl(fd, VIDIOC_S_FMT, &str_format)) {
	perror("ioctl VIDIOC_S_FMT");
	return -1;
    }
    if (str_format.fmt.pix.pixelformat != xawtv_pixelformat[format])
	return -1;
    *width      = str_format.fmt.pix.width;
    *height     = str_format.fmt.pix.height;
    *linelength = str_format.fmt.pix.bytesperline;
    if (debug)
	fprintf(stderr,"v4l2: new capture params (%dx%d, %c%c%c%c, %d byte)\n",
		*width,*height,
		str_format.fmt.pix.pixelformat & 0xff,
		(str_format.fmt.pix.pixelformat >>  8) & 0xff,
		(str_format.fmt.pix.pixelformat >> 16) & 0xff,
		(str_format.fmt.pix.pixelformat >> 24) & 0xff,
		str_format.fmt.pix.sizeimage);
    return 0;
}

static void
v4l2_start(int fps, int buffers)
{
    if (cap.flags & V4L2_FLAG_STREAMING) {
	if (str_fps) {
	    fprintf(stderr,"v4l2_start: aiee: str_fps!=0\n");
	    exit(1);
	}
	str_fps = fps;
	v4l2_start_streaming(buffers);
    }
}

/* do simple capture with read() */
static void*
v4l2_capture()
{
    int rc;

    if (NULL == read_buf)
	read_buf = malloc(str_format.fmt.pix.sizeimage);
    if (str_format.fmt.pix.sizeimage != 
	(rc=read(fd,read_buf,str_format.fmt.pix.sizeimage))) {
	if (-1 == rc) {
	    perror("read");
	    return NULL;
	} else {
	    fprintf(stderr,"read error: got %d bytes (expected %d)\n",
		    rc, str_format.fmt.pix.sizeimage);
	    return NULL;
	}
    }
    return read_buf;
}

/* do streaming capture */
static void*
v4l2_streaming()
{
    int  size;
    char *frame;

    if (0 == str_fps) {
	/* single frame */
	if (NULL == read_buf)
	    read_buf = malloc(str_format.fmt.pix.sizeimage);
	v4l2_start_streaming(1);
	frame = v4l2_get_streaming(&size);
	memcpy(read_buf,frame,str_format.fmt.pix.sizeimage);
	v4l2_stop_streaming(); /* this unmaps the memory frame points to */
	return read_buf;
    } else {
	return v4l2_get_streaming(&size);
    }
}

static void
v4l2_stop()
{
    if (cap.flags & V4L2_FLAG_STREAMING) {
	if (!str_fps) {
	    fprintf(stderr,"v4l2_stop: aiee: grab_fps==0\n");
	    exit(1);
	}
	v4l2_stop_streaming();
	str_fps = 0;
    }
    if (read_buf) {
	free(read_buf);
	read_buf = NULL;
    }
}

/* ---------------------------------------------------------------------- */
/* overlay                                                                */

static struct v4l2_framebuffer ov_fb;
static struct v4l2_window      ov_win;
static struct v4l2_clip        ov_clips[256];

static int
v4l2_setupfb(int sw, int sh, int format, void *base, int bpl)
{
    if (!(cap.flags & V4L2_FLAG_PREVIEW))
	return -1;

    if (-1 == ioctl(fd, VIDIOC_G_FBUF, &ov_fb)) {
	perror("ioctl VIDIOC_G_FBUF");
	return -1;
    }
#if 0
    /* v4l-conf must do: -EPERM ... */
    if (have_dga && base)
	ov_fb.base[0] = base;
    ov_fb.fmt.width       = sw;
    ov_fb.fmt.height      = sh;
    ov_fb.fmt.pixelformat = xawtv_pixelformat[format];
    ov_fb.fmt.depth       = format2depth[format];
    if (bpl) {
	ov_fb.fmt.flags |= V4L2_FMT_FLAG_BYTESPERLINE;
	ov_fb.fmt.bytesperline = bpl;
    }
    /* without X-Server help we can DMA only directly to the screen */
    ov_fb.flags = V4L2_FBUF_FLAG_PRIMARY;
    if (-1 == ioctl(fd, VIDIOC_S_FBUF, &ov_fb)) {
	perror("ioctl VIDIOC_S_FBUF");
    }
    if (-1 == ioctl(fd, VIDIOC_G_FBUF, &ov_fb)) {
	perror("ioctl VIDIOC_G_FBUF");
	return -1;
    }
#endif
    if (1 /* debug */)
	print_fbinfo(&ov_fb);

    /* double-check settings */
    if (have_dga && ov_fb.base[0] != base) {
	fprintf(stderr,"v4l2: WARNING: framebuffer base address mismatch\n");
	fprintf(stderr,"v4l2: %p %p\n",base,ov_fb.base);
	return -1;
    }
    if (ov_fb.fmt.width != sw || ov_fb.fmt.height != sh) {
	fprintf(stderr,"v4l2: WARNING: framebuffer size mismatch\n");
	return -1;
    }
    if ((ov_fb.fmt.flags & V4L2_FMT_FLAG_BYTESPERLINE) &&
	bpl > 0 && (ov_fb.fmt.bytesperline != bpl)) {
	fprintf(stderr,"v4l2: WARNING: framebuffer bpl mismatch\n");
	return -1;
    }
    if (ov_fb.fmt.pixelformat != xawtv_pixelformat[format]) {
	fprintf(stderr,"v4l2: WARNING: framebuffer format mismatch\n");
	return -1;
    }

    grab_v4l2.grab_overlay = v4l2_overlay;
    return 0;
}

static int v4l2_overlay(int x, int y, int width, int height, int format,
			struct OVERLAY_CLIP *oc, int count)
{
    int i,xadjust=0,yadjust=0;
    
    if (width == 0 || height == 0) {
	if (debug)
	    fprintf(stderr,"v4l2: overlay off\n");
	preview_state = 0;
	if (-1 == ioctl(fd, VIDIOC_PREVIEW, &preview_state))
	    perror("ioctl VIDIOC_PREVIEW");
	return 0;
    }

    if (debug)
	fprintf(stderr,"v4l2: overlay win=%dx%d+%d+%d, %d clips\n",
		width,height,x,y,count);
    ov_win.x          = x;
    ov_win.y          = y;
    ov_win.width      = width;
    ov_win.height     = height;

    /* check against max. size */
    ioctl(fd,VIDIOC_QUERYCAP,&cap);
    if (ov_win.width > cap.maxwidth) {
	ov_win.width = cap.maxwidth;
	ov_win.x += (width - ov_win.width)/2;
    }
    if (ov_win.height > cap.maxheight) {
	ov_win.height = cap.maxheight;
	ov_win.y +=  (height - ov_win.height)/2;
    }
    grabber_fix_ratio(&ov_win.width,&ov_win.height,&ov_win.x,&ov_win.y);

    /* fixups */
    xadjust = ov_win.x - x;
    yadjust = ov_win.y - y;

    if (ov_fb.capability & V4L2_FBUF_CAP_CLIPPING) {
	ov_win.clips      = ov_clips;
	ov_win.clipcount  = count;
	
	for (i = 0; i < count; i++) {
	    ov_clips[i].next   = (i+1 == count) ? NULL : &ov_clips[i+1];
	    ov_clips[i].x      = oc[i].x1 - xadjust;
	    ov_clips[i].y      = oc[i].y1 - yadjust;
	    ov_clips[i].width  = oc[i].x2-oc[i].x1;
	    ov_clips[i].height = oc[i].y2-oc[i].y1;
	    if (debug)
		fprintf(stderr,"v4l2: clip=%dx%d+%d+%d\n",
			ov_clips[i].width,ov_clips[i].height,
			ov_clips[i].x,ov_clips[i].y);
	}
    }
    if (ov_fb.flags & V4L2_FBUF_FLAG_CHROMAKEY) {
	ov_win.chromakey  = 0;    /* FIXME */
    }
    if (-1 == ioctl(fd, VIDIOC_S_WIN, &ov_win))
	perror("ioctl VIDIOC_S_WIN");

    preview_state = 1;
    if (-1 == ioctl(fd, VIDIOC_PREVIEW, &preview_state))
	perror("ioctl VIDIOC_PREVIEW");

    return 0;
}

#endif /* __linux__ */
