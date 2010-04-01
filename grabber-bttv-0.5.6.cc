/* =========================================================================

     Copyright (c) 1997 Regents of Koji OKAMURA, oka@kobe-u.ac.jp
     All rights reserved.

     largely rewritten for new bttv-0.5.6 interface
     by Gerd Knorr <kraxel@cs.tu-berlin.de>

   ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/fcntl.h>  
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/mman.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>


extern "C" {
#include <videodev.h>
#include <bttv.h>
#include <bt848.h>
}

#include "grabber.h"
#include "Tcl.h"
#include "device-input.h"
#include "module.h"

#define MEM_SIZE   (PAL_WIDTH * PAL_HEIGHT * 3)
#define DEVICE     "/dev/bttv"
#define DEBUG(x)
//#define DEBUG(x) (x)

#define NTSC_WIDTH  640
#define NTSC_HEIGHT 480
#define PAL_WIDTH   756
#define PAL_HEIGHT  576
#define CIF_WIDTH   352
#define CIF_HEIGHT  288

#define CF_422 0
#define CF_411 1
#define CF_CIF 2

class BTTVGrabber : public Grabber {
public:
    BTTVGrabber(const char * cformat);
    virtual ~BTTVGrabber();

    virtual int  command(int argc, const char*const* argv);
    virtual void start();
    virtual void stop();
    virtual int  grab();

protected:
    void format();
    void setsize();

    void packed422_to_planar422(char *, char*);
    void packed422_to_planar411(char *, char*);

    struct video_capability  capability;
    struct video_channel     *channels;
    struct video_tuner       tuner;
    struct video_picture     pict;
    struct gbuf              gb;
    char                     *mem;
    int                      even;

    int fd_;
    int format_;
    int cformat_;
    int port_;
    
    unsigned char *tm_;
    int width_;
    int height_;
    int max_width_;
    int max_height_;
    int decimate_;
};

/* ----------------------------------------------------------------- */

class BTTVDevice : public InputDevice {
public:
    BTTVDevice(const char*);
    virtual int command(int argc, const char*const* argv);
};

static BTTVDevice BTTV_device("bttv");

BTTVDevice::BTTVDevice(const char *name) : InputDevice(name)
{
    if(access(DEVICE,R_OK) != -1) {
	attributes_ = 
	    "format { 411 422 cif } "
	    "size { small large cif } "
	    "port { Television Composite1 Composite2 SVHS } ";
    } else
	attributes_ = "disabled";
}

int BTTVDevice::command(int argc, const char*const* argv)
{
    Tcl& tcl = Tcl::instance();
    if (argc == 3) {
	if (strcmp(argv[1], "open") == 0) {
	    TclObject* o = 0;
	    o = new BTTVGrabber(argv[2]);
	    if (o != 0)
		tcl.result(o->name());
	    return (TCL_OK);
	}
    }
    return (InputDevice::command(argc, argv));
}

/* ----------------------------------------------------------------- */

BTTVGrabber::BTTVGrabber(const char *cformat)
{
    int i,zero=0;
    
    DEBUG(fprintf(stderr,"bttv: constructor %s\n",cformat));

    fd_ = open(DEVICE, O_RDWR);
    if (fd_ < 0) {
	perror("open " DEVICE);
	exit(1);
    }

    /* ask for capabilities */
    if (-1 == ioctl(fd_,VIDIOCGCAP,&capability)) {
	perror("ioctl " DEVICE " VIDIOCGCAP");
	exit(1);
    }
    memset(&tuner,0,sizeof(tuner));
    if (-1 == ioctl(fd_,VIDIOCGTUNER,&tuner)) {
	perror("ioctl " DEVICE " VIDIOCGTUNER");
	exit(1);
    }
    channels = (struct video_channel*)
	calloc(capability.channels,sizeof(struct video_channel));
    for (i = 0; i < capability.channels; i++) {
	channels[i].channel = i;
	if (-1 == ioctl(fd_,VIDIOCGCHAN,&channels[i])) {
	    perror("ioctl " DEVICE " VIDIOCGCHAN");
	    exit(1);
	}
    }
    fprintf(stderr,DEVICE ": %s\n",capability.name);

    /* map grab buffer */
    mem = (char*)mmap(0,MEM_SIZE * 2,PROT_READ|PROT_WRITE,MAP_SHARED,fd_,0);
    if (-1 == (int)mem) {
	perror("mmap " DEVICE);
	exit(1);
    }

    /* fill in defaults */
    if(!strcmp(cformat, "411"))
	cformat_ = CF_411;
    if(!strcmp(cformat, "422"))
	cformat_ = CF_422;
    if(!strcmp(cformat, "cif"))
	cformat_ = CF_CIF;
    
    port_     = 0;
    decimate_ = 2;
}

BTTVGrabber::~BTTVGrabber()
{
    DEBUG(fprintf(stderr,"bttv: destructor %s\n"));

    munmap(mem,MEM_SIZE * 2);
    close(fd_);
}

int BTTVGrabber::command(int argc, const char*const* argv)
{
    if (argc == 3) {
	if (strcmp(argv[1], "decimate") == 0) {
	    decimate_ = atoi(argv[2]);
	    if (running_)
		format();
	}

	if (strcmp(argv[1], "port") == 0) {
	    if(!strcmp(argv[2], "Television")) port_ = 0;
	    if(!strcmp(argv[2], "Composite1")) port_ = 1;
	    if(!strcmp(argv[2], "Composite2")) port_ = 2;
	    if(!strcmp(argv[2], "SVHS")) port_ = 3;
	    if (running_)
		format();
    	    return (TCL_OK);	
	}
    }
    
    return (Grabber::command(argc, argv));
}

void BTTVGrabber::start()
{
    DEBUG(fprintf(stderr,"bttv: start\n"));

    format();

    even = 1;
    gb.adr = 0;
    ioctl(fd_, BTTV_GRAB, &gb);

    Grabber::start();
}

void BTTVGrabber::stop()
{
    DEBUG(fprintf(stderr,"bttv: stop\n"));

#if 0
    ioctl(fd_, BTTV_SYNC, 0);
#endif
    Grabber::stop();
}

int BTTVGrabber::grab()
{
    DEBUG(fprintf(stderr,"*"));

    ioctl(fd_, BTTV_SYNC, 0);

    even = !even;
    gb.adr = even ? 0 : MEM_SIZE;
    ioctl(fd_, BTTV_GRAB, &gb);

    switch (cformat_) {
    case CF_411:
    case CF_CIF:
	packed422_to_planar411((char*)frame_,mem + (even ? MEM_SIZE : 0));
	break;
    case CF_422:
	packed422_to_planar422((char*)frame_,mem + (even ? MEM_SIZE : 0));
	break;
    }

    suppress(frame_);
    saveblks(frame_);
    YuvFrame f(media_ts(), frame_, crvec_, outw_, outh_);
    return (target_->consume(&f));
}

void BTTVGrabber::packed422_to_planar422(char *dest, char *src)
{
    int i;
    char *s, *y,*u,*v;

    i = (width_ * height_)/2;
    s = src;
    y = dest;
    u = y + width_ * height_;
    v = u + width_ * height_ / 2;
    
    while (--i) {
	*(y++) = *(src++);
	*(u++) = *(src++);
	*(y++) = *(src++);
        *(v++) = *(src++);
    }
}

void BTTVGrabber::packed422_to_planar411(char *dest, char *src)
{
    int  a,b;
    char *s, *y,*u,*v;

    s = src;
    y = dest;
    u = y + width_ * height_;
    v = u + width_ * height_ / 4;

    for (a = height_; a > 0; a -= 2) {
	for (b = width_; b > 0; b -= 2) {
	    
	    *(y++) = *(src++);
	    *(u++) = *(src++);
	    *(y++) = *(src++);
	    *(v++) = *(src++);
	}
	for (b = width_; b > 0; b -= 2) {
	    *(y++) = *(src++);
	    /* *(u++) = */ *(src++);
	    *(y++) = *(src++);
	    /* *(v++) = */ *(src++);
	}
    }
}

void BTTVGrabber::format()
{
    DEBUG(fprintf(stderr,"\nbttv: format"));

    if(tuner.mode == VIDEO_MODE_NTSC){
	max_height_ = NTSC_HEIGHT;
	max_width_  = NTSC_WIDTH;
	DEBUG(fprintf(stderr," NTSC"));
    } else {
	max_height_ = PAL_HEIGHT;
	max_width_  = PAL_WIDTH;
	DEBUG(fprintf(stderr," PAL"));
    }

    width_  = CIF_WIDTH  *2  / decimate_;
    height_ = CIF_HEIGHT *2 / decimate_;
    
    switch (cformat_) {
    case CF_CIF:
	set_size_411(width_, height_);
	gb.fmt = BT848_COLOR_FMT_YUY2;
	DEBUG(fprintf(stderr," cif"));
	break;
    case CF_411:
	set_size_411(width_, height_);
	gb.fmt = BT848_COLOR_FMT_YUY2;
	DEBUG(fprintf(stderr," 411"));
	break;
    case CF_422:
	set_size_422(width_, height_);
	gb.fmt    = BT848_COLOR_FMT_YUY2;
	DEBUG(fprintf(stderr," 422"));
	break;
    }

    gb.adr    = 0;
    gb.width  = width_;
    gb.height = height_;
    
    DEBUG(fprintf(stderr," size=%dx%d",width_,height_));

    if (-1 == ioctl(fd_, VIDIOCSCHAN, &port_))
	perror("ioctl " DEVICE " VIDIOCSCHAN");
    DEBUG(fprintf(stderr," port=%d\n",port_));

    allocref();
}
