/*
 * console TV application.  Uses a framebuffer device.
 *
 *   (c) 1998-2001 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <X11/Intrinsic.h>
#include <curses.h>
#include <math.h>
#include <endian.h>

#include <linux/kd.h>
#include <linux/fb.h>

#include "config.h"

#include "grab-ng.h"
#include "fbtools.h"
#include "matrox.h"
#include "writefile.h"
#include "sound.h"
#include "channel.h"
#include "frequencies.h"
#include "commands.h"
#include "lirc.h"

#define MAX(x,y)        ((x)>(y)?(x):(y))
#define MIN(x,y)        ((x)<(y)?(x):(y))

/* ---------------------------------------------------------------------- */
/* framebuffer                                                            */

static char  *fbdev    = NULL;
static char  *fontfile = NULL;
static char  *mode     = NULL;
static char  *device   = NULL;

static unsigned short red[256],  green[256],  blue[256];
static struct fb_cmap cmap  = { 0, 256, red,  green,  blue };

static int switch_last,fb;
static int keep_dma_on = 0;

static int sig,quiet,matrox;
static int ww,hh;
static float fbgamma = 1.0;

static struct ng_video_buf buf;
static int dx,dy;

char v4l_conf[128] = "v4l-conf";
int have_config;
int x11_native_format,have_dga=1,debug;

/*--- channels ------------------------------------------------------------*/

struct KEYTAB {
    int  key;
    int  argc;
    char *argv[8];
};

static struct KEYTAB keytab[] = {
    { '+',       2, { "volume",     "inc"       }},
    { '-',       2, { "volume",     "dec"       }},
    { 10,        2, { "volume",     "mute"      }},
    { 13,        2, { "volume",     "mute"      }},
    { KEY_ENTER, 2, { "volume",     "mute"      }},

    { KEY_F(5),  2, { "bright",     "dec"       }},
    { KEY_F(6),  2, { "bright",     "inc"       }},
    { KEY_F(7),  2, { "hue",        "dec"       }},
    { KEY_F(8),  2, { "hue",        "inc"       }},
    { KEY_F(9),  2, { "contrast",   "dec"       }},
    { KEY_F(10), 2, { "contrast",   "inc"       }},
    { KEY_F(11), 2, { "color",      "dec"       }},
    { KEY_F(12), 2, { "color",      "inc"       }},

    { ' ',       2, { "setstation", "next"      }},
    { KEY_PPAGE, 2, { "setstation", "next"      }},
    { KEY_NPAGE, 2, { "setstation", "prev"      }},
    { KEY_RIGHT, 2, { "setchannel", "next"      }},
    { KEY_LEFT,  2, { "setchannel", "prev"      }},
    { KEY_UP,    2, { "setchannel", "fine_up"   }},
    { KEY_DOWN,  2, { "setchannel", "fine_down" }},

    { 'G',       2, { "snap",       "ppm"       }},
    { 'g',       2, { "snap",       "ppm"       }},
    { 'J',       2, { "snap",       "jpeg"      }},
    { 'j',       2, { "snap",       "jpeg"      }},

    { 'V',       2, { "capture",    "toggle"    }},
    { 'v',       2, { "capture",    "toggle"    }},

    { 'F',       2, { "fullscreen", "toggle"    }},
    { 'f',       2, { "fullscreen", "toggle"    }},

    { '0',       2, { "keypad",     "0"         }},
    { '1',       2, { "keypad",     "1"         }},
    { '2',       2, { "keypad",     "2"         }},
    { '3',       2, { "keypad",     "3"         }},
    { '4',       2, { "keypad",     "4"         }},
    { '5',       2, { "keypad",     "5"         }},
    { '6',       2, { "keypad",     "6"         }},
    { '7',       2, { "keypad",     "7"         }},
    { '8',       2, { "keypad",     "8"         }},
    { '9',       2, { "keypad",     "9"         }},
};

#define NKEYTAB (sizeof(keytab)/sizeof(struct KEYTAB))

static char              *snapbase;
static char              default_title[128] = "???";
static char              message[128] = "";

/* ---------------------------------------------------------------------- */
/* framebuffer stuff                                                      */

void
linear_palette(int bit)
{
    int             i, size = 256 >> (8 - bit);

    for (i = 0; i < size; i++)
	red[i] = green[i] = blue[i] = (unsigned short)(65535.0
		* pow(i/(size - 1.0), fbgamma));
}

void
dither_palette(int r, int g, int b)
{
    int             rs, gs, bs, i;

    rs = 256 / (r - 1);
    gs = 256 / (g - 1);
    bs = 256 / (b - 1);
    for (i = 0; i < r*g*b; i++) {
	green[i+16] = (gs * ((i / (r * b)) % g)) * 255;
	red[i+16]   = (rs * ((i / b) % r)) * 255;
	blue[i+16]  = (bs * ((i) % b)) * 255;
    }
}

void
fb_initcolors(int fd, int gray)
{
    /* get colormap */
    if (fb_var.bits_per_pixel == 8 ||
	fb_fix.visual == FB_VISUAL_DIRECTCOLOR) {
	if (-1 == ioctl(fd,FBIOGETCMAP,&cmap))
	    perror("ioctl FBIOGETCMAP");
    }
    
    switch (fb_var.bits_per_pixel) {
    case 8:
	if (gray) {
	    linear_palette(8);
	    x11_native_format = VIDEO_GRAY;
	} else {
	    dither_palette(5,9,5);
	    x11_native_format = VIDEO_RGB08;
	}
	break;
    case 15:
    case 16:
	if (fb_fix.visual == FB_VISUAL_DIRECTCOLOR)
	    linear_palette(5);
#if __BYTE_ORDER == __BIG_ENDIAN
	x11_native_format = (fb_var.green.length == 6) ?
	    VIDEO_RGB16_BE : VIDEO_RGB15_BE;
#else
	x11_native_format = (fb_var.green.length == 6) ?
	    VIDEO_RGB16_LE : VIDEO_RGB15_LE;
#endif
	break;
    case 24:
	if (fb_fix.visual == FB_VISUAL_DIRECTCOLOR)
	    linear_palette(8);
#if __BYTE_ORDER == __BIG_ENDIAN
	x11_native_format = VIDEO_RGB24;
#else
	x11_native_format = VIDEO_BGR24;
#endif
	break;
    case 32:
	if (fb_fix.visual == FB_VISUAL_DIRECTCOLOR)
	    linear_palette(8);
#if __BYTE_ORDER == __BIG_ENDIAN
	x11_native_format = VIDEO_RGB32;
#else
	x11_native_format = VIDEO_BGR32;
#endif
	break;
    default:
	fprintf(stderr, "Oops: %i bit/pixel ???\n",
		fb_var.bits_per_pixel);
	exit(1);
    }

    /* set colormap */
    if (fb_var.bits_per_pixel == 8 ||
	fb_fix.visual == FB_VISUAL_DIRECTCOLOR) {
	if (-1 == ioctl(fd,FBIOPUTCMAP,&cmap))
	    perror("ioctl FBIOPUTCMAP");
    }
}

void
tty_init(void)
{
    /* we use curses just for kbd input */
    initscr();
    cbreak();
    noecho();
    keypad(stdscr,1);
}

void
tty_cleanup(void)
{
    clear();
    refresh();
    endwin();
}


/* ---------------------------------------------------------------------- */

void
ctrlc(int signal)
{
    sig=1;
}

#if 0
void
change_audio(int mode)
{
    if (grabber->grab_audio)
	grabber->grab_audio(-1,-1,&mode);
}
#endif

void do_capture(int from, int to)
{
    /* off */
    switch (from) {
    case CAPTURE_GRABDISPLAY:
	if (grabber->grab_stop)
	    grabber->grab_stop();
	break;
    case CAPTURE_OVERLAY:
	grabber->grab_overlay(0,0,0,0,0,NULL,0);
	if (matrox)
	    gfx_scaler_off();
	break;
    }

    /* on */
    memset(&buf,0,sizeof(buf));
    switch (to) {
    case CAPTURE_GRABDISPLAY:
	if (ww && hh) {
	    buf.fmt.fmtid  = x11_native_format;
	    buf.fmt.width  = ww;
	    buf.fmt.height = hh;
	    buf.fmt.bytesperline = fb_fix.line_length;
	    ng_grabber_setparams(&buf.fmt, 0, 1);
	    dx  = fb_var.xres-buf.fmt.width;
	    dy  = 0;
	} else {
	    if (quiet) {
		dx  = 0;
		dy  = 0;
	    } else {
		dx  = 24;
		dy  = 16;
	    }
	    buf.fmt.fmtid  = x11_native_format;
	    buf.fmt.width  = fb_var.xres-dx;
	    buf.fmt.height = fb_var.yres-dy;
	    buf.fmt.bytesperline = fb_fix.line_length;
	    ng_grabber_setparams(&buf.fmt, 0, 1);
	    dx += (fb_var.xres-24-buf.fmt.width)/2;
	    dy += (fb_var.yres-16-buf.fmt.height)/2;
	}
	if (grabber->grab_start)
	    grabber->grab_start(-1,2);
	buf.data = fb_mem +
	    dy * fb_fix.line_length +
	    dx * ((fb_var.bits_per_pixel+7)/8);
	break;
    case CAPTURE_OVERLAY:
	buf.fmt.fmtid  = x11_native_format;
	if (ww && hh) {
	    buf.fmt.width  = ww;
	    buf.fmt.height = hh;
	    dx = fb_var.xres-buf.fmt.width;
	    dy = 0;
	} else if (quiet) {
	    buf.fmt.width  = fb_var.xres;
	    buf.fmt.height = fb_var.yres;
	    dx = 0;
	    dy = 0;
	} else {
	    buf.fmt.width  = fb_var.xres-24;
	    buf.fmt.height = fb_var.yres-16;
	    dx = 24;
	    dy = 16;
	}
	if (matrox && grabber->grab_offscreen) {
	    int width,height,starty,pitch;
#if 1
	    /* FIXME: need some kind of size negotiation */
	    /* hardcoded: PAL, half height (want no interleace) */
	    width  = 768;
	    height = 288;
	    starty = fb_var.yres;
#else
	    /* settings for debugging */
	    width  = 320;
	    height = 240;
	    starty = fb_var.yres-height;
#endif
	    if (width*2 > fb_fix.line_length)
		width = fb_fix.line_length/2;
	    pitch = fb_fix.line_length;
	    grabber->grab_offscreen(starty,width,height,VIDEO_YUV422);
	    gfx_scaler_on(starty*pitch,pitch,width,height,
			  dx,dx+buf.fmt.width,
			  dy,dy+buf.fmt.height);
	} else {
	    grabber->grab_overlay(dx, dy, buf.fmt.width, buf.fmt.height,
				  buf.fmt.fmtid, NULL, 0);
	}
	break;
    }
}

static void
do_exit(void)
{
    sig = 1;
}

void
new_title(char *txt)
{
    strcpy(default_title,txt);
}

static void
new_message(char *txt)
{
    strcpy(message,txt);
}

void
channel_menu(void)
{
    int  i,f;
    char key[32],ctrl[16];

    for (i = 0; i < count; i++) {
	if (channels[i]->key) {
	    if (2 != sscanf(channels[i]->key,"%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
			    ctrl,key))
		strcpy(key,channels[i]->key);
	    if (1 == sscanf(key,"F%d",&f)) {
		channels[i]->ckey = KEY_F(f);              /* Function keys */
	    } else if (strlen(key) == 1) {
		if (isalpha(key[0]))
		    key[0] = tolower(key[0]);
		channels[i]->ckey = (int)key[0];           /* single letter/digit */
	    }
	}
    }
}

static void
do_fullscreen(void)
{
    do_va_cmd(2,"capture","off");
    quiet = !quiet;
    fb_memset(fb_mem+fb_mem_offset,0,fb_fix.smem_len);
    do_va_cmd(2,"capture","on");
}

/*--- main ---------------------------------------------------------------*/

static void
grabber_init(void)
{
    grabber_open(device,
		 fb_var.xres_virtual,
		 fb_var.yres_virtual,
		 fb_fix.smem_start,
		 x11_native_format,
		 fb_fix.line_length);
}

void
console_switch(void)
{
    switch (fb_switch_state) {
    case FB_REL_REQ:
	if (!keep_dma_on)
	    do_va_cmd(2,"capture","off");
	switch_last = fb_switch_state;
	fb_switch_release();
	break;
    case FB_ACQ_REQ:
	switch_last = fb_switch_state;
        fb_switch_acquire();
	fb_memset(fb_mem+fb_mem_offset,0,fb_fix.smem_len);
	ioctl(fb,FBIOPAN_DISPLAY,&fb_var);
	do_va_cmd(2,"capture","on");
	break;
    case FB_ACTIVE:
    case FB_INACTIVE:
    default:
	switch_last = fb_switch_state;
	break;
    }
}

/* just a hook for some test code */
void
scaler_test(int off)
{
    if (!matrox) {
	matrox=1;
	if (-1 == gfx_init(fb))
	    matrox = 0;
    }

    if (matrox) {
	gfx_scaler_on(0,fb_fix.line_length,320,240,
		      fb_var.xres-320,fb_var.xres,0,240);
	sleep(2);
    }
}

int
main(int argc, char *argv[])
{
    int             key,i,c,gray=0,rc,vt=0,fps=0,t1,t2,lirc;
    unsigned long   freq;
    struct timeval  tv;
    time_t          t;
    char            text[80];
    fd_set          set;

    if (0 == geteuid() && 0 != getuid()) {
	fprintf(stderr,"fbtv /must not/ be installed suid root\n");
	exit(1);
    }
    
    for (;;) {
	double val;
	c = getopt(argc, argv, "Mgvqxkd:o:s:c:f:m:z:t:");
	if (c == -1)
	    break;
	switch (c) {
	case 'z':
	    if(sscanf(optarg, "%lf", &val) == 1) {
		if(val < 0.1 || val > 10)
		    fprintf(stderr, "gamma value is out of range.  must be "
			    "0.1 < value < 10.0\n");
		else
		    fbgamma = 1.0 / val;
	    }
	    break;
	case 'f':
	    fontfile = optarg;
	    break;
	case 'm':
	    mode = optarg;
	    break;
	case 'g':
	    gray = 1;
	    break;
	case 'M':
	    matrox = 1;
	    break;
	case 'k':
	    keep_dma_on = 1;
	    break;
	case 'v':
	    debug++;
	    break;
	case 'q':
	    quiet = 1;
	    break;
	case 'd':
	    fbdev = optarg;
	    break;
	case 'o':
	    snapbase = strdup(optarg);
	    break;
	case 's':
	    sscanf(optarg,"%dx%d",&ww,&hh);
	    break;
	case 'c':
	    device = optarg;
	    /* v4l-conf needs this too */
	    strcat(v4l_conf," -c ");
	    strcat(v4l_conf,device);
	    break;
	case 't':
	    if (optarg)
		vt = strtoul(optarg, 0, 0);
	    else
		vt = -1;
	    break;
	default:
	    exit(1);
	}
    }

    do_overlay = 1;
    fb = fb_init(fbdev,fontfile,mode,vt);
    fb_cleanup_fork();
    fb_initcolors(fb,gray);
    fb_switch_init();
    switch_last = fb_switch_state;

    grabber_init();
    read_config();
    channel_menu();

    if (matrox)
	if (-1 == gfx_init(fb))
	    matrox = 0;
    
    /* set hooks (command.c) */
    update_title      = new_title;
    display_message   = new_message;
    set_capture_hook  = do_capture;
    exit_hook         = do_exit;
    fullscreen_hook   = do_fullscreen;

    tty_init();
    atexit(tty_cleanup);
    signal(SIGINT,ctrlc);
    signal(SIGTSTP,SIG_IGN);

    /* init hardware */
    attr_init();
    audio_on();
    audio_init();
    do_va_cmd(2,"setfreqtab",chanlist_names[chantab].str);

    cur_capture = 0;
    do_va_cmd(2,"capture","overlay");
    if (optind+1 == argc) {
	do_va_cmd(2,"setstation",argv[optind]);
    } else {
	if (grabber->grab_tune && 0 != (freq = grabber->grab_tune(-1,-1))) {
	    for (i = 0; i < chancount; i++)
		if (chanlist[i].freq == freq*1000/16) {
		    do_va_cmd(2,"setchannel",chanlist[i].name);
		    break;
		}
	}
	if (-1 == cur_channel) {
	    if (count > 0)
		do_va_cmd(2,"setstation","0");
	    else
		set_defaults();
	}
    }

    /* lirc support */
    lirc = lirc_tv_init();

    fb_memset(fb_mem+fb_mem_offset,0,fb_fix.smem_len);
    for (;!sig;) {
	if ((fb_switch_state == FB_ACTIVE || keep_dma_on) && !quiet) {
	    /* clear first lines */
	    fb_memset(fb_mem+fb_mem_offset,0,16*fb_fix.line_length);
	    if (message[0] != '\0') {
		strcpy(text,message);
	    } else {
		sprintf(text,"Framebuffer TV - %s",default_title);
	    }
	    /* debugging + preformance monitoring */
	    switch (cur_capture) {
	    case CAPTURE_GRABDISPLAY:
		sprintf(text+strlen(text), " - grab %d.%d fps",fps/5,(fps*2)%10);
		break;
	    }
	    fb_puts(0,0,text);

	    if (dy > 0) {
		/* display time */
		time(&t);
		strftime(text,16,"%H:%M",localtime(&t));
		fb_puts((fb_var.xres/8)-5,0,text);
	    }
	}
	if (switch_last != fb_switch_state) {
	    console_switch();
	    continue;
	}

	t1 = time(NULL);
	fps = 0;
	message[0] = '\0';
	for (;;) {
	    FD_ZERO(&set);
	    FD_SET(0,&set);
	    if (lirc != -1)
		FD_SET(lirc,&set);
	    if (cur_capture == CAPTURE_GRABDISPLAY) {
		fps++;
		ng_grabber_capture(&buf);
		tv.tv_sec  = 0;
		tv.tv_usec = 0;
		rc = select(MAX(0,lirc)+1,&set,NULL,NULL,&tv);
	    } else {
		tv.tv_sec  = 6;
		tv.tv_usec = 0;
		rc = select(MAX(0,lirc)+1,&set,NULL,NULL,&tv);
	    }
	    if (switch_last != fb_switch_state) {
		console_switch();
		break;
	    }
	    if (rc > 0)
		break;
	    t2 = time(NULL);
	    if (t2 - t1 >= 5) {
		keypad_timeout();
		break;
	    }
	}

	if (FD_ISSET(0,&set)) {
	    /* keyboard input */
	    switch (key = getch()) {
	    case 27: /* ESC */
	    case 'q':
	    case 'Q':
		sig=1;
		break;
	    case -1:
		break;

#if 1
	    case 'y':
		/* scaler_test(1); */
		do_va_cmd(2,"capture","off");
		do_va_cmd(2,"capture","grab");
		break;
#endif

	    default:
		/* look for station hotkeys */
		if (isalpha(key))
		    key = tolower(key);
		for (i = 0; i < count; i++)
		    if (channels[i]->ckey == key) {
			cur_sender = i;
			break;
		    }
		if (i < count) {
		    do_va_cmd(2,"setstation",channels[i]->name);
		    break;
		}

		/* for commands */
		for (i = 0; i < NKEYTAB; i++) {
		    if (keytab[i].key == key)
			break;
		}
		if (i != NKEYTAB) {
		    do_command(keytab[i].argc,keytab[i].argv);
		    break;
		}
		
		/* nothing found -- some maybe useful debug output */
		sprintf(message,"key: %d 0x%x ",key,key);
		if (key > 0x20 && key < 128)
		    sprintf(message+strlen(message),"'%c' ",key);

	    }
	}  /* if (FD_ISSET(0,&set)) */

	if (lirc != -1 && FD_ISSET(lirc,&set)) {
	    if (-1 == lirc_tv_havedata()) {
		fprintf(stderr,"lirc: connection lost\n");
		close(lirc);
		lirc = -1;
	    }
	}
    }
    do_va_cmd(2,"capture","off");
    audio_off();
    grabber->grab_close();
    fb_memset(fb_mem+fb_mem_offset,0,fb_fix.smem_len);
    /* parent will clean up */
    exit(0);
}
