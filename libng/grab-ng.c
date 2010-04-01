/*
 * next generation[tm] xawtv capture interfaces
 *
 * (c) 2001 Gerd Knorr <kraxel@bytesex.org>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <fnmatch.h>
#include <dlfcn.h>
#include <errno.h>
#include <ctype.h>
#include <sys/time.h>
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#endif

#include "config.h"
#include "grab-ng.h"

int  ng_debug          = 0;
int  ng_chromakey      = 0x00ff00ff;
int  ng_jpeg_quality   = 75;
int  ng_ratio_x        = 4;
int  ng_ratio_y        = 3;

char ng_v4l_conf[256]  = "v4l-conf";

/* --------------------------------------------------------------------- */

const unsigned int ng_vfmt_to_depth[] = {
    0,               /* unused   */
    8,               /* RGB8     */
    8,               /* GRAY8    */
    16,              /* RGB15 LE */
    16,              /* RGB16 LE */
    16,              /* RGB15 BE */
    16,              /* RGB16 BE */
    24,              /* BGR24    */
    32,              /* BGR32    */
    24,              /* RGB24    */
    32,              /* RGB32    */
    16,              /* LUT2     */
    32,              /* LUT4     */
    16,		     /* YUV422   */
    16,		     /* YUV422P  */
    12,		     /* YUV420P  */
    0,		     /* MJPEG    */
    0,		     /* JPEG     */
};

const char* ng_vfmt_to_desc[] = {
    "none",
    "8 bit PseudoColor (dithering)",
    "8 bit StaticGray",
    "15 bit TrueColor (LE)",
    "16 bit TrueColor (LE)",
    "15 bit TrueColor (BE)",
    "16 bit TrueColor (BE)",
    "24 bit TrueColor (LE: bgr)",
    "32 bit TrueColor (LE: bgr-)",
    "24 bit TrueColor (BE: rgb)",
    "32 bit TrueColor (BE: -rgb)",
    "16 bit TrueColor (lut)",
    "32 bit TrueColor (lut)",
    "16 bit YUV 4:2:2 (packed)",
    "16 bit YUV 4:2:2 (planar)",
    "12 bit YUV 4:2:0 (planar)",
    "MJPEG (AVI)",
    "JPEG (JFIF)",
};

/* --------------------------------------------------------------------- */

const unsigned int   ng_afmt_to_channels[] = {
    0,  1,  2,  1,  2,  1,  2
};
const unsigned int   ng_afmt_to_bits[] = {
    0,  8,  8, 16, 16, 16, 16
};
const char* ng_afmt_to_desc[] = {
    "none",
    "8bit mono",
    "8bit stereo",
    "16bit mono (LE)",
    "16bit stereo (LE)",
    "16bit mono (BE)",
    "16bit stereo (BE)"
};

/* --------------------------------------------------------------------- */

const char* ng_attr_to_desc[] = {
    "none",
    "norm",
    "input",
    "volume",
    "mute",
    "audio mode",
    "color",
    "bright",
    "hue",
    "contrast",
};

/* --------------------------------------------------------------------- */

void ng_init_video_buf(struct ng_video_buf *buf)
{
    memset(buf,0,sizeof(*buf));
    pthread_mutex_init(&buf->lock,NULL);    
    pthread_cond_init(&buf->cond,NULL);
}

void ng_release_video_buf(struct ng_video_buf *buf)
{
    int release;

    pthread_mutex_lock(&buf->lock);
    buf->refcount--;
    release = (buf->refcount == 0);
    pthread_mutex_unlock(&buf->lock);
    if (release && NULL != buf->release)
	buf->release(buf);
}

void ng_wakeup_video_buf(struct ng_video_buf *buf)
{
    pthread_cond_signal(&buf->cond);
}

void ng_waiton_video_buf(struct ng_video_buf *buf)
{
    pthread_mutex_lock(&buf->lock);
    while (buf->refcount)
	pthread_cond_wait(&buf->cond, &buf->lock);
    pthread_mutex_unlock(&buf->lock);
}

static void ng_free_video_buf(struct ng_video_buf *buf)
{
    free(buf->data);
    free(buf);
}

struct ng_video_buf*
ng_malloc_video_buf(struct ng_video_fmt *fmt, int size)
{
    struct ng_video_buf *buf;

    buf = malloc(sizeof(*buf));
    if (NULL == buf)
	return NULL;
    ng_init_video_buf(buf);
    buf->fmt  = *fmt;
    buf->size = size;
    buf->data = malloc(size);
    if (NULL == buf->data) {
	free(buf);
	return NULL;
    }
    buf->refcount = 1;
    buf->release  = ng_free_video_buf;
    return buf;
}

/* --------------------------------------------------------------------- */

struct ng_attribute*
ng_attr_byid(struct ng_attribute *attrs, int id)
{
    for (;;) {
	if (NULL == attrs->name)
	    return NULL;
	if (attrs->id == id)
	    return attrs;
	attrs++;
    }
}

struct ng_attribute*
ng_attr_byname(struct ng_attribute *attrs, char *name)
{
    for (;;) {
	if (NULL == attrs->name)
	    return NULL;
	if (0 == strcasecmp(attrs->name,name))
	    return attrs;
	attrs++;
    }
}

const char*
ng_attr_getstr(struct ng_attribute *attr, int value)
{
    int i;
    
    if (NULL == attr)
	return NULL;
    if (attr->type != ATTR_TYPE_CHOICE)
	return NULL;

    for (i = 0; attr->choices[i].str != NULL; i++)
	if (attr->choices[i].nr == value)
	    return attr->choices[i].str;
    return NULL;
}

int
ng_attr_getint(struct ng_attribute *attr, char *value)
{
    int i,val;
    
    if (NULL == attr)
	return -1;
    if (attr->type != ATTR_TYPE_CHOICE)
	return -1;

    for (i = 0; attr->choices[i].str != NULL; i++)
	if (0 == strcasecmp(attr->choices[i].str,value))
	    return attr->choices[i].nr;

    if (isdigit(value[0])) {
	/* Hmm.  String not found, but starts with a digit.
	   Check if this is a valid number ... */
	val = atoi(value);
	for (i = 0; attr->choices[i].str != NULL; i++)
	    if (val == attr->choices[i].nr)
		return attr->choices[i].nr;
	
    }
    return -1;
}

void
ng_attr_listchoices(struct ng_attribute *attr)
{
    int i;
    
    fprintf(stderr,"valid choices for \"%s\": ",attr->name);
    for (i = 0; attr->choices[i].str != NULL; i++)
	fprintf(stderr,"%s\"%s\"",
		i ? ", " : "",
		attr->choices[i].str);
    fprintf(stderr,"\n");
}

/* --------------------------------------------------------------------- */

void
ng_ratio_fixup(int *width, int *height, int *xoff, int *yoff)
{
    int h = *height;
    int w = *width;

    if (0 == ng_ratio_x || 0 == ng_ratio_y)
	return;
    if (w * ng_ratio_y < h * ng_ratio_x) {
	*height = *width * ng_ratio_y / ng_ratio_x;
	if (yoff)
	    *yoff  += (h-*height)/2;
    } else if (w * ng_ratio_y > h * ng_ratio_x) {
	*width  = *height * ng_ratio_x / ng_ratio_y;
	if (yoff)
	    *xoff  += (w-*width)/2;
    }
}

void
ng_ratio_fixup2(int *width, int *height, int *xoff, int *yoff,
		int ratio_x, int ratio_y, int up)
{
    int h = *height;
    int w = *width;

    if (0 == ratio_x || 0 == ratio_y)
	return;
    if ((!up  &&  w * ratio_y < h * ratio_x) ||
	(up   &&  w * ratio_y > h * ratio_x)) {
	*height = *width * ratio_y / ratio_x;
	if (yoff)
	    *yoff  += (h-*height)/2;
    } else if ((!up  &&  w * ratio_y > h * ratio_x) ||
	       (up   &&  w * ratio_y < h * ratio_x)) {
	*width  = *height * ratio_x / ratio_y;
	if (yoff)
	    *xoff  += (w-*width)/2;
    }
}

/* --------------------------------------------------------------------- */

extern const struct ng_writer files_writer;
extern const struct ng_writer raw_writer;
extern const struct ng_writer avi_writer;
#ifdef HAVE_LIBQUICKTIME
extern const struct ng_writer qt_writer;
#endif

const struct ng_writer *ng_writers[] = {
    &files_writer,
    &raw_writer,
    &avi_writer,
#ifdef HAVE_LIBQUICKTIME
    &qt_writer,
#endif
    NULL
};

/* --------------------------------------------------------------------- */

extern const struct ng_driver v4l_driver;
extern const struct ng_driver v4l2_driver;
extern const struct ng_driver bsd_driver;
const struct ng_driver *ng_drivers[] = {
#ifdef __linux__
    &v4l2_driver,
    &v4l_driver,
#endif
#if defined(__OpenBSD__) || defined(__FreeBSD__)
    &bsd_driver,
#endif
    NULL
};

const struct ng_driver*
ng_grabber_open(char *device, struct ng_video_fmt *screen, void *base,
		void **handle)
{
    int i;

#ifdef __linux__
    if (NULL != screen) {
	switch (system(ng_v4l_conf)) {
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
    }
#endif

    /* check all grabber drivers */
    for (i = 0; NULL != ng_drivers[i]; i++) {
	if (NULL == ng_drivers[i]->name)
	    continue;
	if (ng_debug)
	    fprintf(stderr,"init: trying: %s... \n",ng_drivers[i]->name);
	if (NULL != (*handle = ng_drivers[i]->open(device)))
	    break;
	if (ng_debug)
	    fprintf(stderr,"init: failed: %s\n",ng_drivers[i]->name);
    }
    if (NULL == ng_drivers[i])
	return NULL;
    if (ng_debug)
	fprintf(stderr,"init: ok: %s\n",ng_drivers[i]->name);
    if (NULL != screen &&
	ng_drivers[i]->capabilities(*handle) & CAN_OVERLAY) {
	ng_drivers[i]->setupfb(*handle,screen,base);
    }
    return ng_drivers[i];
}

long long
ng_get_timestamp()
{
    struct timeval tv;
    long long ts;

    gettimeofday(&tv,NULL);
    ts  = tv.tv_sec;
    ts *= 1000000;
    ts += tv.tv_usec;
    ts *= 1000;
    return ts;
}

/* --------------------------------------------------------------------- */

static struct ng_video_conv *ng_conv;
static int                  ng_nconv;

void
ng_conv_register(struct ng_video_conv *list, int count)
{
    int size = sizeof(struct ng_video_conv) * (ng_nconv + count);

    ng_conv = realloc(ng_conv,size);
    memcpy(ng_conv + ng_nconv,list,sizeof(struct ng_video_conv)*count);
    ng_nconv += count;
}

struct ng_video_conv*
ng_conv_find(int out, int *i)
{
    struct ng_video_conv *ret = NULL;
    
    for (; *i < ng_nconv; (*i)++) {
#if 0
	fprintf(stderr,"\tconv:  %-28s =>  %s\n",
		ng_vfmt_to_desc[ng_conv[*i].fmtid_in],
		ng_vfmt_to_desc[ng_conv[*i].fmtid_out]);
#endif
	if (ng_conv[*i].fmtid_out == out) {
	    ret = &ng_conv[*i];
	    (*i)++;
	    break;
	}
    }
    return ret;
}

struct ng_filter **ng_filters;

void
ng_filter_register(struct ng_filter *list, int count)
{
    int n = 0;

    if (ng_filters)
	for (n = 0; NULL != ng_filters[n]; n++)
	    /* nothing */;
    ng_filters = realloc(ng_filters,sizeof(struct ng_filter*)*(n+count+1));
    memcpy(ng_filters+n,&list,sizeof(struct ng_filter*)*count);
    memset(ng_filters+n+count,0,sizeof(struct ng_filter*));

#if 0 /* DEBUG */
    for (n = 0; NULL != ng_filters[n]; n++)
	fprintf(stderr,"%s\n",ng_filters[n]->name);
    fprintf(stderr,"-- \n");
#endif
}

/* --------------------------------------------------------------------- */

static void ng_plugins(char *dirname)
{
    struct dirent *ent;
    char filename[1024];
    void *plugin;
    void (*initcall)(void);
    DIR *dir;

    dir = opendir(dirname);
    if (NULL == dir)
	return;
    while (NULL != (ent = readdir(dir))) {
	if (0 != fnmatch("*.so",ent->d_name,0))
	    continue;
	sprintf(filename,"%s/%s",dirname,ent->d_name);
	if (NULL == (plugin = dlopen(filename,RTLD_NOW))) {
	    fprintf(stderr,"dlopen: %s\n",dlerror());
	    continue;
	}
	if (NULL == (initcall = dlsym(plugin,"ng_plugin_init"))) {
	    fprintf(stderr,"dlsym: %s\n",dlerror());
	    continue;
	}
	initcall();
    }
    closedir(dir);
}

void
ng_init(void)
{
    static int once=0;

    if (once++) {
	fprintf(stderr,"panic: ng_init called twice\n");
	exit(1);
    }
    ng_device_init();
    ng_color_packed_init();
    ng_color_yuv2rgb_init();
    ng_mjpg_init();

    ng_plugins(LIBDIR);
}

/*
 * Local variables:
 * compile-command: "(cd ..; make)"
 * End:
 */
