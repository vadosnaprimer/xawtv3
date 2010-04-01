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
#include <sys/time.h>
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#endif

#include "config.h"

#include "grab-ng.h"

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
    "16 bit YUV 4:2:2",
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

extern const struct ng_driver v4l_driver;
extern const struct ng_driver v4l2_driver;
extern const struct ng_driver bsd_driver;
const struct ng_driver *ng_drivers[] = {
    &v4l2_driver,
    &v4l_driver,
    &bsd_driver,
    NULL
};

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
    memset(buf,0,sizeof(*buf));
    buf->fmt  = *fmt;
    buf->size = size;
    buf->data = malloc(size);
    if (NULL == buf->data) {
	free(buf);
	return NULL;
    }
    buf->refcount = 1;
    buf->release  = ng_free_video_buf;
    pthread_mutex_init(&buf->lock,NULL);
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
    int i;
    
    if (NULL == attr)
	return -1;
    if (attr->type != ATTR_TYPE_CHOICE)
	return -1;

    for (i = 0; attr->choices[i].str != NULL; i++)
	if (0 == strcasecmp(attr->choices[i].str,value))
	    return attr->choices[i].nr;
    return -1;
}

/* --------------------------------------------------------------------- */

const struct ng_driver*
ng_grabber_open(char *device, struct ng_video_fmt *screen, void *base,
		void **handle)
{
    int i;

    /* check all grabber drivers */
    for (i = 0; NULL != ng_drivers[i]; i++) {
	if (NULL == ng_drivers[i]->name)
	    continue;
	if (debug)
	    fprintf(stderr,"init: trying: %s... \n",ng_drivers[i]->name);
	if (NULL != (*handle = ng_drivers[i]->open(device)))
	    break;
	if (debug)
	    fprintf(stderr,"init: failed: %s\n",ng_drivers[i]->name);
    }
    if (NULL == ng_drivers[i])
	return NULL;
    if (debug)
	fprintf(stderr,"init: ok: %s\n",ng_drivers[i]->name);
    if (NULL != screen &&
	ng_drivers[i]->capabilities(*handle) & CAN_OVERLAY) {
	ng_drivers[i]->setupfb(*handle,screen,base);
    }
    return ng_drivers[i];
}

/* --------------------------------------------------------------------- */
