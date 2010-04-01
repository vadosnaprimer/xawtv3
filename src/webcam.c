#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>

#include "grab-ng.h"
#include "writefile.h"
#include "colorspace.h"
#include "webcam.h"

extern int jpeg_quality;
char *webcam;

struct WEBCAM {
    pthread_mutex_t lock;
    pthread_cond_t  wait;
    char *filename;
    int format, size;
    int width, height;
    char *data;
};

static void*
webcam_writer(void *arg)
{
    struct WEBCAM *web = arg;
    int rename,fd,old;
    char tmpfilename[512];

    if (debug)
	fprintf(stderr,"webcam_writer start\n");
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,&old);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,&old);
    pthread_mutex_lock(&web->lock);
    for (;;) {
	while (web->data == NULL) {
	    if (debug)
		fprintf(stderr,"webcam_writer: waiting for data\n");
	    pthread_cond_wait(&web->wait, &web->lock);
	}
	if (debug)
	    fprintf(stderr,"webcam_writer: %d %dx%d \n",
		    web->format,web->width,web->height);
	rename = 1;
	sprintf(tmpfilename,"%s.$$$",web->filename);
	switch (web->format) {
	case VIDEO_MJPEG:
	    if (-1 == (fd = open(tmpfilename,O_CREAT|O_WRONLY,0666))) {
		fprintf(stderr,"open(%s): %s\n",tmpfilename,
			strerror(errno));
		goto done;
	    }
	    write(fd,web->data,web->size);
	    close(fd);
	    break;
	case VIDEO_BGR24:
	    swap_rgb24(web->data,web->width*web->height);
	    /* fall throuth */
	case VIDEO_RGB24:
	    write_jpeg(tmpfilename,web->data,
		       web->width,web->height,jpeg_quality,0);
	    break;
	default:
	    fprintf(stderr,"webcam_writer: can't deal with format=%d\n",
		    web->format);
	    rename = 0;
	}
	if (rename) {
	    unlink(web->filename);
	    if (-1 == link(tmpfilename,web->filename)) {
		fprintf(stderr,"link(%s,%s): %s\n",
			tmpfilename,web->filename,strerror(errno));
		goto done;
	    }
	    unlink(tmpfilename);
	}
	free(web->filename);
	free(web->data);
	web->data = NULL;
    }
 done:
    pthread_mutex_unlock(&web->lock);
    if (debug)
	fprintf(stderr,"webcam_writer done\n");
    return NULL;
}

/* ----------------------------------------------------------------------- */

static struct WEBCAM *web;
static pthread_t tweb;

void
webcam_init()
{
    web = malloc(sizeof(struct WEBCAM));
    memset(web,0,sizeof(struct WEBCAM));
    pthread_mutex_init(&web->lock, NULL);
    pthread_create(&tweb,NULL,webcam_writer,web);
    return;
}

void
webcam_exit()
{
    if (web) {
	pthread_cancel(tweb);
	free(web);
	web = NULL;
    }
}

int
webcam_put(char *filename, int format, int width, int height,
	   char *data, int size)
{
    int ret = 0;

    if (NULL == web)
	webcam_init();

    if (-1 == pthread_mutex_trylock(&web->lock)) {
	if (debug)
	    fprintf(stderr,"webcam_put: locked\n");
	return -1;
    }
    if (NULL != web->data) {
	if (debug)
	    fprintf(stderr,"webcam_put: still has data\n");
	ret = -1;
	goto done;
    }

    /* TODO: avoid this memcpy (needs buffer refcount) */
    web->filename = strdup(filename);
    web->data = malloc(size);
    memcpy(web->data,data,size);
    web->format = format;
    web->width  = width;
    web->height = height;
    web->size   = size;
    if (debug)
	fprintf(stderr,"webcam_put: ok\n");
    pthread_cond_signal(&web->wait);

 done:
    pthread_mutex_unlock(&web->lock);
    return ret;
}

