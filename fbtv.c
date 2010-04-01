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
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <X11/Intrinsic.h>
#include <curses.h>

#include "config.h"

#ifdef HAVE_LIBJPEG
# include "jpeglib.h"
#endif

#include "mixer.h"
#include "channel.h"
#include "channels.h"
#include "grab.h"

#define MAX(x,y)        ((x)>(y)?(x):(y))
#define MIN(x,y)        ((x)<(y)?(x):(y))

#define DEFAULT_DEVICE  "/dev/fb0"

/* ---------------------------------------------------------------------- */
/* framebuffer                                                            */

char                       *fbdev = NULL;
int                        fd;
struct fb_fix_screeninfo   fix;
struct fb_var_screeninfo   var;
unsigned char              *map;

unsigned short red[256],  green[256],  blue[256];
unsigned short ored[256], ogreen[256], oblue[256];
struct fb_cmap cmap  = { 0, 256, red,  green,  blue };
struct fb_cmap ocmap = { 0, 256, ored, ogreen, oblue };

int x11_native_format,have_dga=1;
int debug,sig;
int fs_width,fs_height,fs_xoff,fs_yoff,pix_width,pix_height;
int ww,hh;

/* ---------------------------------------------------------------------- */

/*                            PAL  NTSC SECAM */
static int    maxwidth[]  = { 768, 640, 768 };
static int    maxheight[] = { 576, 480, 576 };
static int    maxx,maxy /* current */;

/*--- drivers -------------------------------------------------------------*/

extern struct GRABBER grab_v4l;
struct GRABBER *grabbers[] = {
    &grab_v4l,
};

int grabber;

/*--- channels ------------------------------------------------------------*/

struct STRTAB *cmenu = NULL;
char   title[256];

int cur_color;
int cur_bright;
int cur_hue;
int cur_contrast;
int cur_capture;

int cur_mute   = 0;
int cur_volume = 65535;

char              *ppmfile;
char              *jpegfile;

/* ---------------------------------------------------------------------- */

void
fb_gray_palette()
{
    int             i;

    for (i = 0; i < 256; i++) {
	red[i]   = i * 255;
	green[i] = i * 255;
	blue[i]  = i * 255;
    }
}

void
fb_dither_palette(int r, int g, int b)
{
    int             rs, gs, bs, i;

    rs = 256 / (r - 1);
    gs = 256 / (g - 1);
    bs = 256 / (b - 1);
    for (i = 0; i < 256-16; i++) {
	green[i+16] = (gs * ((i / (r * b)) % g)) * 255;
	red[i+16]   = (rs * ((i / b) % r)) * 255;
	blue[i+16]  = (bs * ((i) % b)) * 255;
    }
}

void
fb_cleanup(void)
{
    if (var.bits_per_pixel == 8)
	if (-1 == ioctl(fd,FBIOPUTCMAP,&ocmap))
	    perror("ioctl FBIOPUTCMAP");
    endwin();
}

void
fb_init(int gray)
{
    /* init framebuffer */
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
    map = mmap(NULL,fix.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if (-1 == (int)map) {
	perror("mmap");
	exit(1);
    }
    switch (var.bits_per_pixel) {
    case 8:
	if (-1 == ioctl(fd,FBIOGETCMAP,&ocmap))
	    perror("ioctl FBIOGETCMAP");
	if (-1 == ioctl(fd,FBIOGETCMAP,&cmap))
	    perror("ioctl FBIOGETCMAP");
	if (gray) {
	    fb_gray_palette();
	    x11_native_format = VIDEO_GRAY;
	} else {
	    fb_dither_palette(5,9,5);
	    x11_native_format = VIDEO_RGB08;
	}
	if (-1 == ioctl(fd,FBIOPUTCMAP,&cmap))
	    perror("ioctl FBIOPUTCMAP");
	break;
    case 15:
    case 16:
	x11_native_format = (var.green.length == 6) ?
	    VIDEO_RGB16 : VIDEO_RGB15;
	break;
    case 24:
	x11_native_format = VIDEO_RGB24;
	break;
    case 32:
	x11_native_format = VIDEO_RGB32;
	break;
    default:
	fprintf(stderr, "Oops: %i bit/pixel ???\n",
		var.bits_per_pixel);
	exit(1);
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr,1);
    refresh();
}

/* ---------------------------------------------------------------------- */

void
ctrlc(int signal)
{
    sig=1;
}

void
change_audio(int mode)
{
    if (grabbers[grabber]->grab_audio)
	grabbers[grabber]->grab_audio(-1,-1,&mode);
}

void set_capture(int j)
{
    int x,y,w,h;
    cur_capture = j;

    if (j) {
	if (ww && hh) {
	    w = ww;
	    h = hh;
	    x = var.xres_virtual-w;
	    y = 0;
	} else {
	    w = MIN(var.xres_virtual,maxx);
	    h = MIN(var.yres_virtual,maxy);
	    x = (var.xres_virtual-w)/2;
	    y = (var.yres_virtual-h)/2;
	}
	grabbers[grabber]->grab_overlay(x,y,w,h,x11_native_format,NULL,0);
    } else
	grabbers[grabber]->grab_overlay(0,0,0,0,0,NULL,0);
}

void set_norm(int j)
{
    cur_norm = j;

    grabbers[grabber]->grab_input(-1,cur_norm);
    maxx = maxwidth[cur_norm];
    maxy = maxheight[cur_norm];
}

void set_source(int j)
{
    cur_input = j;
    
    grabbers[grabber]->grab_input(cur_input,-1);
}

void set_freqtab(int j)
{
    chan_tab = j;
}

void
set_picparams(int color, int bright, int hue, int contrast)
{
    if (color != -1)
	cur_color = color;
    if (bright != -1)
	cur_bright = bright;
    if (hue != -1)
	cur_hue = hue;
    if (contrast != -1)
	cur_contrast = contrast;
    grabbers[grabber]->grab_picture(cur_color,cur_bright,cur_hue,cur_contrast);
}

void
set_channel(struct CHANNEL *channel)
{
    /* image parameters */
    set_picparams(channel->color, channel->bright,
		  channel->hue, channel->contrast);

    /* input source */
    if (cur_input   != channel->source)
	set_source(channel->source);
    if (cur_norm    != channel->norm)
	set_norm(channel->norm);

    /* station */
    cur_channel  = channel->channel;
    cur_fine     = channel->fine;
    grabbers[grabber]->grab_tune(channel->freq);

    set_capture(channel->capture);
}

void
set_volume()
{
    int vol;
    
    if (have_mixer) {
	/* sound card */
	vol = cur_volume * 100 / 65536;
	mixer_set_volume(vol);
	cur_mute ? mixer_mute() : mixer_unmute();
    } else {
	/* v4l */
	grabbers[grabber]->grab_audio(cur_mute,cur_volume,NULL);
    }
}

void
channel_menu()
{
    int  i,f,max,len;
    char key[32],ctrl[16];

    cmenu = malloc((count+1)*sizeof(struct STRTAB));
    memset(cmenu,0,(count+1)*sizeof(struct STRTAB));
    for (i = 0, max = 0; i < count; i++) {
	len = strlen(channels[i]->name);
	if (max < len)
	    max = len;
    }
    for (i = 0; i < count; i++) {
	cmenu[i].nr      = i+1;
	cmenu[i].str     = channels[i]->name;
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

/*--- grab (single frames) to file ---------------------------------------*/

void
patch_up(char *name)
{
    char *ptr;
    
    for (ptr = name+strlen(name); ptr >= name; ptr--)
	if (isdigit(*ptr))
	    break;
    if (ptr < name)
	return;
    while (*ptr == '9' && ptr >= name)
	*(ptr--) = '0';
    if (ptr < name)
	return;
    if (isdigit(*ptr))
	(*ptr)++;
}

#ifdef HAVE_LIBJPEG
static int write_jpeg(char *filename, char *data, int width, int height)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *fp;
    int i;
    unsigned char *line;

    if (NULL == (fp = fopen(filename,"w"))) {
	fprintf(stderr,"grab: can't open %s: %s\n",filename,strerror(errno));
	return -1;
    }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 75, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    for (i = 0, line = data; i < height; i++, line += width*3)
	jpeg_write_scanlines(&cinfo, &line, 1);
    
    jpeg_finish_compress(&(cinfo));
    jpeg_destroy_compress(&(cinfo));
    fclose(fp);

    return 0;
}
#endif

static int write_ppm(char *filename, char *data, int width, int height)
{
    FILE *fp;
    
    if (NULL == (fp = fopen(filename,"w"))) {
	fprintf(stderr,"grab: can't open %s: %s\n",filename,strerror(errno));
	return -1;
    }
    fprintf(fp,"P6\n%d %d\n255\n",width,height);
    fwrite(data,height,3*width,fp);
    fclose(fp);

    return 0;
}

void
snap(int jpeg)
{
    void *buffer;
    Dimension width  = maxwidth[cur_norm];
    Dimension height = maxheight[cur_norm];

    if (!grabbers[grabber]->grab_one)
	return;

    if (NULL == jpegfile) {
	jpegfile = malloc(32);
	strcpy(jpegfile, "snap0000.jpeg");
    }
    if (NULL == ppmfile) {
	ppmfile = malloc(32);
	strcpy(ppmfile, "snap0000.ppm");
    }
    
    mvprintw(0,0,"grabbing image to %s ... ", jpeg ? jpegfile : ppmfile);
    refresh();
    
    if (NULL == (buffer = grabbers[grabber]->grab_one(width,height))) {
	printw("FAILED");
    } else {
	if (jpeg) {
#ifdef HAVE_LIBJPEG
	    write_jpeg(jpegfile,buffer,width,height);
	    patch_up(jpegfile);
#endif
	} else {
	    write_ppm(ppmfile,buffer,width,height);
	    patch_up(ppmfile);
	}
	printw("OK");
    }
    refresh();
    sleep(1);
}

/*--- main ---------------------------------------------------------------*/

static void
grabber_init()
{
    int sw,sh;
    void *base;

    sw   = var.xres_virtual;
    sh   = var.yres_virtual;
    base = fix.smem_start;
    for (grabber = 0; grabber < sizeof(grabbers)/sizeof(struct GRABBERS*);
	 grabber++) {
	if (-1 != grabbers[grabber]->grab_open
	    (NULL,sw,sh,x11_native_format,x11_native_format,base,sw))
	    break;
    }
    if (grabber == sizeof(grabbers)/sizeof(struct GRABBERS*)) {
	fprintf(stderr,"no video grabber device available\n");
	exit(1);
    }
}

int
main(int argc, char *argv[])
{
    int             freq=0,key,i,c,gray=0;
    char            *line;

    if (NULL != (line = getenv("FRAMEBUFFER"))) {
	fbdev = line;
    }

    for (;;) {
	c = getopt(argc, argv, "gvd:o:j:s:");
	if (c == -1)
	    break;
	switch (c) {
	case 'g':
	    gray = 1;
	    break;
	case 'v':
	    debug = 1;
	    break;
	case 'd':
	    fbdev = optarg;
	    break;
	case 'o':
	    ppmfile = strdup(optarg);
	    break;
	case 'j':
	    jpegfile = strdup(optarg);
	    break;
	case 's':
	    sscanf(optarg,"%dx%d",&ww,&hh);
	    break;
	default:
	    exit(1);
	}
    }

    if (NULL == fbdev)
	fbdev = DEFAULT_DEVICE;

    switch (system("v4l-conf -q")) {
    case -1: /* can't run */
	fprintf(stderr,"could'nt start v4l-conf\n");
	break;
    case 0: /* ok */
	break;
    default: /* non-zero return */
	fprintf(stderr,"v4l-conf had some trouble, "
		"trying to continue anyway\n");
	break;
    }

    fb_init(gray);
    atexit(fb_cleanup);
    signal(SIGINT,ctrlc);
    
    grabber_init();
    read_config();
    set_freqtab(chan_tab);
    channel_menu();
    if (have_mixer)
	cur_volume = mixer_get_volume() * 65535/100;
    set_volume();
    cur_capture = 0;

    if (optind < argc)
	for (i = 0; i < count; i++)
	    if (0 == strcasecmp(channels[i]->name,argv[optind]))
		cur_sender = i;
    if (count) {
	if ((cur_sender < 0) || (cur_sender >= count))
	    cur_sender = 0;
	set_channel(channels[cur_sender]);
    } else {
	set_channel(&defaults);
    }

    for (;!sig;) {
	clear();
	if (cur_sender != -1) {
	    printw("Framebuffer TV  -  %s\n",channels[cur_sender]->name);
	} else {
	    printw("Framebuffer TV  -  channel %s", tvtuner[cur_channel].name);
	    if (cur_fine)
		printw(" (%+d)",cur_fine);
	    printw("  freq %.3f\n",(float)freq/16);
	}
	refresh();
	switch (key = getch()) {
	case 27: /* ESC */
	case 'q':
	case 'Q':
	    sig=1;
	    break;

	case '+':
	    cur_volume += 512;
	    if (cur_volume > 65535)
		cur_volume = 65535;
	    set_volume();
	    break;
	case '-':
	    cur_volume -= 512;
	    if (cur_volume < 0)
		cur_volume = 0;
	    set_volume();
	    break;
	case 10: /* ENTER/RETURN */
	case KEY_ENTER:
	    cur_mute = !cur_mute;
	    set_volume();
	    break;

	case 'G':
	case 'g':
	    snap(0);
	    break;
	case 'J':
	case 'j':
	    snap(1);
	    break;

	case 339 /* PgUp  --  is'nt in curses.h ??? */:
	    cur_sender = (cur_sender+1)%count;
	    set_channel(channels[cur_sender]);
	    break;
	case 338 /* PgDown */:
	    cur_sender = (cur_sender+count-1)%count;
	    set_channel(channels[cur_sender]);
	    break;

	case KEY_UP:
	    do {
		cur_channel = (cur_channel+1) % CHAN_ENTRIES;
	    } while (!tvtuner[cur_channel].freq[chan_tab]);
	    cur_fine = 0;
	    cur_sender = -1;
	    freq = get_freq(cur_channel)+cur_fine;
	    grabbers[grabber]->grab_tune(freq);
	    break;
	case KEY_DOWN:
	    do {
		cur_channel = (cur_channel+CHAN_ENTRIES-1) % CHAN_ENTRIES;
	    } while (!tvtuner[cur_channel].freq[chan_tab]);
	    cur_fine = 0;
	    cur_sender = -1;
	    freq = get_freq(cur_channel)+cur_fine;
	    grabbers[grabber]->grab_tune(freq);
	    break;
	case KEY_LEFT:
	    cur_fine--;
	    cur_sender = -1;
	    freq = get_freq(cur_channel)+cur_fine;
	    grabbers[grabber]->grab_tune(freq);
	    break;
	case KEY_RIGHT:
	    cur_fine++;
	    cur_sender = -1;
	    freq = get_freq(cur_channel)+cur_fine;
	    grabbers[grabber]->grab_tune(freq);
	    break;

	case -1:
	    break;

	default:
	    if (isalpha(key))
		key = tolower(key);
	    for (i = 0; i < count; i++)
		if (channels[i]->ckey == key) {
		    cur_sender = i;
		    break;
		}
	    if (i < count) {
		set_channel(channels[cur_sender]);
	    } else {
		printw("key: %d 0x%x ",key,key);
		if (key > 0x20 && key < 128) printw("'%c' ",key);
		refresh();
		sleep(2);
	    }
	}
    }
    grabbers[grabber]->grab_close();
    fprintf(stderr,"\033[H\033[J");
    exit(0);
}
