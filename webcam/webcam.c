/*
 * (c) 1998-2000 Gerd Knorr
 *
 *    capture a image, compress as jpeg and upload to the webserver
 *    using ftp the ftp utility
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <asm/types.h>
#include "videodev.h"	/* change this to "videodev2.h" for v4l2 */

#include "jpeglib.h"
#include "ftp.h"
#include "parseconfig.h"


/* ---------------------------------------------------------------------- */
/* configuration                                                          */

#define JPEG_FILE         "/tmp/webcam.jpeg"

char *ftp_host  = "www";
char *ftp_user  = "webcam";
char *ftp_pass  = "xxxxxx";
char *ftp_dir   = "public_html/images";
char *ftp_file  = "webcam.jpeg";
char *ftp_tmp   = "uploading.jpeg";
int ftp_passive = 1;
int ftp_auto    = 0;
int ftp_local   = 0;

char *grab_device = "/dev/video0";
char *grab_text   = "webcam %Y-%m-%d %H:%M:%S"; /* strftime */
int   grab_width  = 320;
int   grab_height = 240;
int   grab_delay  = 3;
int   grab_rotate = 0;
int   grab_top    = 0;
int   grab_left   = 0;
int   grab_bottom = -1;
int   grab_right  = -1;
int   grab_quality= 75;
int   grab_trigger=  0;
int   grab_once   =  0;

/* these work for v4l only, not v4l2 */
int   grab_input = 0;
int   grab_norm  = VIDEO_MODE_PAL;

/* ---------------------------------------------------------------------- */

void swap_rgb24(char *mem, int n);

/* ---------------------------------------------------------------------- */
/* jpeg stuff                                                             */

int
write_jpeg(char *filename, char *data, int width, int height)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *fp;
    int i;
    unsigned char *line;

    if (NULL == (fp = fopen(filename,"w"))) {
	fprintf(stderr,"can't open %s for writing: %s\n",
		filename,strerror(errno));
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
    jpeg_set_quality(&cinfo, grab_quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    for (i = 0, line = data; i < height; i++, line += width*3)
	jpeg_write_scanlines(&cinfo, &line, 1);
    
    jpeg_finish_compress(&(cinfo));
    jpeg_destroy_compress(&(cinfo));
    fclose(fp);

    return 0;
}

/* ---------------------------------------------------------------------- */
/* capture stuff  - v4l2                                                  */

#ifdef VIDIOC_QUERYCAP

static struct v4l2_capability    grab_cap;
static struct v4l2_format        grab_pix;
static int                       grab_fd, grab_size;
static unsigned char            *grab_data;

void
grab_init()
{
    if (-1 == (grab_fd = open(grab_device,O_RDWR))) {
	fprintf(stderr,"open %s: %s\n",grab_device,strerror(errno));
	exit(1);
    }
    if (-1 == ioctl(grab_fd,VIDIOC_QUERYCAP,&grab_cap)) {
	fprintf(stderr,"%s: no v4l2 device\n",grab_device);
	exit(1);
    }
    if (-1 == ioctl(grab_fd, VIDIOC_G_FMT, &grab_pix)) {
        perror("ioctl VIDIOC_G_FMT");
	exit(1);
    }
    grab_pix.type = V4L2_BUF_TYPE_CAPTURE;
    grab_pix.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;
    grab_pix.fmt.pix.depth  = 24;
    grab_pix.fmt.pix.width  = grab_width;
    grab_pix.fmt.pix.height = grab_height;
    if (-1 == ioctl(grab_fd, VIDIOC_S_FMT, &grab_pix)) {
	perror("ioctl VIDIOC_S_FMT");
	exit(1);
    }
    grab_size = grab_pix.fmt.pix.width * grab_pix.fmt.pix.height *
	((grab_pix.fmt.pix.depth+7)/8);
    fprintf(stderr,"grabber: using %dx%dx%d => %d byte\n",
	    grab_pix.fmt.pix.width,grab_pix.fmt.pix.height,
	    grab_pix.fmt.pix.depth,grab_size);
    if (NULL == (grab_data = malloc(grab_size))) {
	fprintf(stderr,"out of virtual memory\n");
	exit(1);
    }
}

unsigned char*
grab_one(int *width, int *height)
{
    int rc;

    for (;;) {
	rc = read(grab_fd,grab_data,grab_size);
	if (rc == grab_size) {
	    swap_rgb24(grab_data,grab_pix.fmt.
		       pix.width*grab_pix.fmt.pix.height);
	    *width  = grab_pix.fmt.pix.width;
	    *height = grab_pix.fmt.pix.height;
	    return grab_data;
	}
	fprintf(stderr,"grabber: read: %d != %d\n",grab_size,rc);
	if (-1 == rc)
	    perror("grabber: read");
    }
}

#endif


/* ---------------------------------------------------------------------- */
/* capture stuff  -  old v4l (bttv)                                       */

#ifdef VIDIOCGCAP

static struct video_capability   grab_cap;
static struct video_mmap         grab_buf;
static struct video_channel	 grab_chan;
static struct video_picture      grab_pict;
static struct video_window       grab_win;
static int                       grab_fd, grab_size, have_mmap;
static unsigned char            *grab_data;

void
grab_init()
{
    if (-1 == (grab_fd = open(grab_device,O_RDWR))) {
	fprintf(stderr,"open %s: %s\n",grab_device,strerror(errno));
	exit(1);
    }
    if (-1 == ioctl(grab_fd,VIDIOCGCAP,&grab_cap)) {
	fprintf(stderr,"%s: no v4l device\n",grab_device);
	exit(1);
    }

    /* set image source and TV norm */
    grab_chan.channel = grab_input;
    if (-1 == ioctl(grab_fd,VIDIOCGCHAN,&grab_chan)) {
	perror("ioctl VIDIOCGCHAN");
	exit(1);
    }
    grab_chan.channel = grab_input;
    grab_chan.norm    = grab_norm;
    if (-1 == ioctl(grab_fd,VIDIOCSCHAN,&grab_chan)) {
	perror("ioctl VIDIOCSCHAN");
	exit(1);
    }

    /* try to setup mmap-based capture */
    grab_buf.format = VIDEO_PALETTE_RGB24;
    grab_buf.frame  = 0;
    grab_buf.width  = grab_width;
    grab_buf.height = grab_height;
    grab_size = grab_buf.width * grab_buf.height * 3;
    grab_data = mmap(0,grab_size,PROT_READ|PROT_WRITE,MAP_SHARED,grab_fd,0);
    if (-1 != (int)grab_data) {
	have_mmap = 1;
	return;
    }

    /* fallback to read() */
    fprintf(stderr,"no mmap support available, using read()\n");
    have_mmap = 0;
    grab_pict.depth   = 24;
    grab_pict.palette = VIDEO_PALETTE_RGB24;
    if (-1 == ioctl(grab_fd,VIDIOCSPICT,&grab_pict)) {
	perror("ioctl VIDIOCSPICT");
	exit(1);
    }
    if (-1 == ioctl(grab_fd,VIDIOCGPICT,&grab_pict)) {
	perror("ioctl VIDIOCGPICT");
	exit(1);
    }
    memset(&grab_win,0,sizeof(struct video_window));
    grab_win.width  = grab_width;
    grab_win.height = grab_height;
    if (-1 == ioctl(grab_fd,VIDIOCSWIN,&grab_win)) {
	perror("ioctl VIDIOCSWIN");
	exit(1);
    }
    if (-1 == ioctl(grab_fd,VIDIOCGWIN,&grab_win)) {
	perror("ioctl VIDIOCGWIN");
	exit(1);
    }
    grab_size = grab_win.width * grab_win.height * 3;
    grab_data = malloc(grab_size);
}

unsigned char*
grab_one(int *width, int *height)
{
    int rc;
    
    for (;;) {
	if (have_mmap) {
	    if (-1 == ioctl(grab_fd,VIDIOCMCAPTURE,&grab_buf)) {
		perror("ioctl VIDIOCMCAPTURE");
	    } else {
		if (-1 == ioctl(grab_fd,VIDIOCSYNC,&grab_buf)) {
		    perror("ioctl VIDIOCSYNC");
		} else {
		    swap_rgb24(grab_data,grab_buf.width*grab_buf.height);
		    *width  = grab_buf.width;
		    *height = grab_buf.height;
		    return grab_data;
		}
	    }
	} else {
	    rc = read(grab_fd,grab_data,grab_size);
	    if (grab_size != rc) {
		fprintf(stderr,"grabber read error (rc=%d)\n",rc);
		return NULL;
	    } else {
		swap_rgb24(grab_data,grab_win.width*grab_win.height);
		*width  = grab_win.width;
		*height = grab_win.height;
		return grab_data;
	    }
        }
	sleep(1);
    }
}

#endif

/* ---------------------------------------------------------------------- */

#define CHAR_HEIGHT  11
#define CHAR_WIDTH   6
#define CHAR_START   4
#include "font_6x11.h"

void
add_text(char *image, int width, int height)
{
    time_t      t;
    struct tm  *tm;
    char        line[128],*ptr;
    int         i,x,y,f,len;

    time(&t);
    tm = localtime(&t);
    len = strftime(line,127,grab_text,tm);
    fprintf(stderr,"%s\n",line);

    for (y = 0; y < CHAR_HEIGHT; y++) {
	ptr = image + 3 * width * (height-CHAR_HEIGHT-2+y) + 12;
	for (x = 0; x < len; x++) {
	    f = fontdata[line[x] * CHAR_HEIGHT + y];
	    for (i = CHAR_WIDTH-1; i >= 0; i--) {
		if (f & (CHAR_START << i)) {
		    ptr[0] = 255;
		    ptr[1] = 255;
		    ptr[2] = 255;
		}
		ptr += 3;
	    }
	}
    }
}

void
swap_rgb24(char *mem, int n)
{
    char  c;
    char *p = mem;
    int   i = n;
    
    while (--i) {
	c = p[0]; p[0] = p[2]; p[2] = c;
	p += 3;
    }
}

unsigned char *
rotate_image(unsigned char * in, int *wp, int *hp, int rot,
	     int top, int left, int bottom, int right)
{
    static unsigned char * rotimg = NULL;

    int i, j;

    int w = *wp;
    int ow = (right-left);
    int oh = (bottom-top);

    if (rotimg == NULL && (rotimg = malloc(ow*oh*3)) == NULL ) {
	fprintf(stderr, "out of memory\n");
	exit(1);
    }
    switch ( rot ) {
    default:
    case 0:
	for (j = 0; j < oh; j++) {
	    int ir = (j+top)*w+left;
	    int or = j*ow;
	    for (i = 0; i < ow; i++) {
		rotimg[3*(or + i)]   = in[3*(ir+i)];
		rotimg[3*(or + i)+1] = in[3*(ir+i)+1];
		rotimg[3*(or + i)+2] = in[3*(ir+i)+2];
	    }
	}
	*wp = ow;
	*hp = oh;
	break;
    case 1:
	for (i = 0; i < ow; i++) {
	    int rr = (ow-1-i)*oh;
	    int ic = i+left;
	    for (j = 0; j < oh; j++) {
		rotimg[3*(rr+(oh-1-j))] = in[3*((j+top)*w+ic)];
		rotimg[3*(rr+(oh-1-j))+1] = in[3*((j+top)*w+ic)+1];
		rotimg[3*(rr+(oh-1-j))+2] = in[3*((j+top)*w+ic)+2];
	    }
	}
	*wp = oh;
	*hp = ow;
	break;
    case 2:
	for (j = 0; j < oh; j++) {
	    int ir = (j+top)*w;
	    for (i = 0; i < ow; i++) {
		rotimg[3*((oh-1-j)*ow + (ow-1-i))] = in[3*(ir+i+left)];
		rotimg[3*((oh-1-j)*ow + (ow-1-i))+1] = in[3*(ir+i+left)+1];
		rotimg[3*((oh-1-j)*ow + (ow-1-i))+2] = in[3*(ir+i+left)+2];
	    }
	}
	*wp = ow;
	*hp = oh;
	break;
    case 3:
	for (i = 0; i < ow; i++) {
	    int rr = i*oh;
	    int ic = i+left;
	    rr += oh-1;
	    for (j = 0; j < oh; j++) {
		rotimg[3*(rr-j)]   = in[3*((j+top)*w+ic)];
		rotimg[3*(rr-j)+1] = in[3*((j+top)*w+ic)+1];
		rotimg[3*(rr-j)+2] = in[3*((j+top)*w+ic)+2];
	    }
	}
	*wp = oh;
	*hp = ow;
	break;
    }
    return rotimg;	
}

unsigned int
compare_images(unsigned char *last, unsigned char *current,
	       int width, int height)
{
    unsigned char *p1 = last;
    unsigned char *p2 = current;
    int avg, diff, max, i = width*height*3;

    for (max = 0, avg = 0; --i; p1++,p2++) {
	diff = (*p1 < *p2) ? (*p2 - *p1) : (*p1 - *p2);
	avg += diff;
	if (diff > max)
	    max = diff;
    }
    avg = avg / width / height;
    fprintf(stderr,"compare: max=%d,avg=%d\n",max,avg);
    /* return avg */
    return max;
}

/* ---------------------------------------------------------------------- */

int
main(int argc, char *argv[])
{
    unsigned char *image,*val,*gimg,*lastimg = NULL;
    char filename[100];
    int width, height, i;

    /* read config */
    sprintf(filename,"%s/%s",getenv("HOME"),".webcamrc");
    fprintf(stderr,"reading config file: %s\n",filename);
    cfg_parse_file(filename);

    if (NULL != (val = cfg_get_str("ftp","host")))
	ftp_host = val;
    if (NULL != (val = cfg_get_str("ftp","user")))
	ftp_user = val;
    if (NULL != (val = cfg_get_str("ftp","pass")))
	ftp_pass = val;
    if (NULL != (val = cfg_get_str("ftp","dir")))
	ftp_dir = val;
    if (NULL != (val = cfg_get_str("ftp","file")))
	ftp_file = val;
    if (NULL != (val = cfg_get_str("ftp","tmp")))
	ftp_tmp = val;
    if (-1 != (i = cfg_get_int("ftp","passive")))
	ftp_passive = i;
    if (-1 != (i = cfg_get_int("ftp","auto")))
	ftp_auto = i;
    if (-1 != (i = cfg_get_int("ftp","debug")))
	ftp_debug = i;
    if (-1 != (i = cfg_get_int("ftp","local")))
	ftp_local = i;

    if (NULL != (val = cfg_get_str("grab","device")))
	grab_device = val;
    if (NULL != (val = cfg_get_str("grab","text")))
	grab_text = val;
    if (-1 != (i = cfg_get_int("grab","width")))
	grab_width = i;
    if (-1 != (i = cfg_get_int("grab","height")))
	grab_height = i;
    if (-1 != (i = cfg_get_int("grab","delay")))
	grab_delay = i;
    if (-1 != (i = cfg_get_int("grab","input")))
	grab_input = i;
    if (-1 != (i = cfg_get_int("grab","norm")))
	grab_norm = i;
    if (-1 != (i = cfg_get_int("grab","rotate")))
	grab_rotate = i;
    if (-1 != (i = cfg_get_int("grab","top")))
	grab_top = i;
    if (-1 != (i = cfg_get_int("grab","left")))
	grab_left = i;
    grab_bottom = cfg_get_int("grab","bottom");
    grab_right = cfg_get_int("grab","right");
    if (-1 != (i = cfg_get_int("grab","quality")))
	grab_quality = i;
    if (-1 != (i = cfg_get_int("grab","trigger")))
	grab_trigger = i;
    if (-1 != (i = cfg_get_int("grab","once")))
	grab_once = i;

    if ( grab_top < 0 ) grab_top = 0;
    if ( grab_left < 0 ) grab_left = 0;
    if ( grab_bottom > grab_height ) grab_bottom = grab_height;
    if ( grab_right > grab_width ) grab_right = grab_width;
    if ( grab_bottom < 0 ) grab_bottom = grab_height;
    if ( grab_right < 0 ) grab_right = grab_width;
    if ( grab_top >= grab_bottom ) grab_top = 0;
    if ( grab_left >= grab_right ) grab_left = 0;

    if ( ftp_local ) {
	if ( ftp_dir != NULL && ftp_dir[0] != '\0' ) {
	    char * t = malloc(strlen(ftp_tmp)+strlen(ftp_dir)+2);
	    sprintf(t, "%s/%s", ftp_dir, ftp_tmp);
	    ftp_tmp = t;
	
	    t = malloc(strlen(ftp_file)+strlen(ftp_dir)+2);
	    sprintf(t, "%s/%s", ftp_dir, ftp_file);
	    ftp_file = t;
	}
    }

    /* print config */
    fprintf(stderr,"video4linux webcam v1.3 - (c) 1998-2000 Gerd Knorr\n");
    fprintf(stderr,"grabber config: size %dx%d, input %d, norm %d, "
	    "jpeg quality %d\n",
	    grab_width,grab_height,grab_input,grab_norm, grab_quality);
    fprintf(stderr, "rotate=%d, top=%d, left=%d, bottom=%d, right=%d\n",
	   grab_rotate, grab_top, grab_left, grab_bottom, grab_right);

    if ( ftp_local )
	fprintf(stderr,"ftp config:\n  local transfer %s => %s\n",
		ftp_tmp,ftp_file);
    else 
	fprintf(stderr,"ftp config:\n  %s@%s:%s\n  %s => %s\n",
		ftp_user,ftp_host,ftp_dir,ftp_tmp,ftp_file);

    /* init everything */
    grab_init();
    if ( ! ftp_local ) {
	ftp_init(ftp_auto,ftp_passive);
	ftp_connect(ftp_host,ftp_user,ftp_pass,ftp_dir);
    }

    /* main loop */
    for (;;) {
	/* grab a new one */
	gimg = grab_one(&width,&height);
	image = rotate_image(gimg, &width, &height, grab_rotate,
			     grab_top, grab_left, grab_bottom, grab_right);

	if (grab_trigger) {
	    /* look if it has changed */
	    if (NULL != lastimg) {
		i = compare_images(lastimg,image,width,height);
		if (i < grab_trigger)
		    continue;
	    } else {
		lastimg = malloc(width*height*3);
	    }
	    memcpy(lastimg,image,width*height*3);
	}

	/* ok, label it and upload */
	add_text(image,width,height);
	if ( ftp_local ) {
	    write_jpeg(ftp_tmp, image, width, height);
	    if ( rename(ftp_tmp, ftp_file) ) {
		fprintf(stderr, "can't move %s -> %s\n", ftp_tmp, ftp_file);
	    }
	} else {
	    write_jpeg(JPEG_FILE, image, width, height);
	    if (!ftp_connected)
		ftp_connect(ftp_host,ftp_user,ftp_pass,ftp_dir);
	    ftp_upload(JPEG_FILE,ftp_file,ftp_tmp);
	}

	if (grab_once)
	    break;
	if (grab_delay > 0)
	    sleep(grab_delay);
    }
    return 0;
}
