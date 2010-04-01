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
#include <limits.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "grab-ng.h"
#include "jpeglib.h"
#include "ftp.h"
#include "parseconfig.h"


/* ---------------------------------------------------------------------- */
/* configuration                                                          */

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

char  *grab_input;
char  *grab_norm;

/* ---------------------------------------------------------------------- */

void swap_rgb24(char *mem, int n);

/* ---------------------------------------------------------------------- */
/* jpeg stuff                                                             */

int
write_file(int fd, char *data, int width, int height)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *fp;
    int i;
    unsigned char *line;

    fp = fdopen(fd,"w");
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
/* capture stuff                                                          */

const struct ng_driver    *drv;
void                      *h_drv;
struct ng_video_fmt       fmt;
int                       need_swap = 0;

void
grab_init()
{
    struct ng_attribute *attr;
    int val;
    
    drv = ng_grabber_open(grab_device,NULL,0,&h_drv);
    if (NULL == drv) {
	fprintf(stderr,"no grabber device available\n");
	exit(1);
    }
    if (!(drv->capabilities(h_drv) & CAN_CAPTURE)) {
	fprintf(stderr,"device does'nt support capture\n");
	exit(1);
    }

    if (grab_input) {
	attr = ng_attr_byid(drv->list_attrs(h_drv),ATTR_ID_INPUT);
	val  = ng_attr_getint(attr,grab_input);
	if (-1 == val) {
	    fprintf(stderr,"invalid input: %s\n",grab_input);
	    exit(1);
	}
	drv->write_attr(h_drv,attr,val);
    }
    if (grab_norm) {
	attr = ng_attr_byid(drv->list_attrs(h_drv),ATTR_ID_NORM);
	val  = ng_attr_getint(attr,grab_norm);
	if (-1 == val) {
	    fprintf(stderr,"invalid norm: %s\n",grab_norm);
	    exit(1);
	}
	drv->write_attr(h_drv,attr,val);
    }
    
    fmt.fmtid  = VIDEO_RGB24;
    fmt.width  = grab_width;
    fmt.height = grab_height;
    if (0 != drv->setformat(h_drv,&fmt)) {
	fmt.fmtid = VIDEO_BGR24;
	need_swap = 1;
	if (0 != drv->setformat(h_drv,&fmt)) {
	    fprintf(stderr,"can't set capture format\n");
	    exit(1);
	}
    }
}

unsigned char*
grab_one(int *width, int *height)
{
    static struct ng_video_buf *buf;

    if (NULL != buf)
	ng_release_video_buf(buf);
    if (NULL == (buf = drv->getimage(h_drv))) {
	fprintf(stderr,"capturing image failed\n");
	exit(1);
    }
    *width  = buf->fmt.width;
    *height = buf->fmt.height;
    if (need_swap)
	swap_rgb24(buf->data, buf->fmt.width * buf->fmt.height);
    return buf->data;
}

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
		rotimg[3*(rr+j)]   = in[3*((j+top)*w+ic)];
		rotimg[3*(rr+j)+1] = in[3*((j+top)*w+ic)+1];
		rotimg[3*(rr+j)+2] = in[3*((j+top)*w+ic)+2];
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
    char filename[1024], *tmpdir;
    int width, height, i, fh;

    /* read config */
    if (argc > 1) {
	strcpy(filename,argv[1]);
    } else {
	sprintf(filename,"%s/%s",getenv("HOME"),".webcamrc");
    }
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
    if (NULL != (val = cfg_get_str("grab","input")))
	grab_input = val;
    if (NULL != (val = cfg_get_str("grab","norm")))
	grab_norm = val;
    if (-1 != (i = cfg_get_int("grab","width")))
	grab_width = i;
    if (-1 != (i = cfg_get_int("grab","height")))
	grab_height = i;
    if (-1 != (i = cfg_get_int("grab","delay")))
	grab_delay = i;
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
    fprintf(stderr,"grabber config: size %dx%d, input %s, norm %s, "
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
    tmpdir = (NULL != getenv("TMPDIR")) ? getenv("TMPDIR") : "/tmp";

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
	    if (-1 == (fh = open(ftp_tmp,O_WRONLY,0600))) {
		fprintf(stderr,"open %s: %s\n",ftp_tmp,strerror(errno));
		exit(1);
	    }
	    write_file(fh, image, width, height);
	    if ( rename(ftp_tmp, ftp_file) ) {
		fprintf(stderr, "can't move %s -> %s\n", ftp_tmp, ftp_file);
	    }
	} else {
	    sprintf(filename,"%s/webcamXXXXXX",tmpdir);
	    if (-1 == (fh = mkstemp(filename))) {
		perror("mkstemp");
		exit(1);
	    }
	    write_file(fh, image, width, height);
	    if (!ftp_connected)
		ftp_connect(ftp_host,ftp_user,ftp_pass,ftp_dir);
	    ftp_upload(filename,ftp_file,ftp_tmp);
	    unlink(filename);
	}

	if (grab_once)
	    break;
	if (grab_delay > 0)
	    sleep(grab_delay);
    }
    return 0;
}
