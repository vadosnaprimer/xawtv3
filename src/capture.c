#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "grab-ng.h"
#include "commands.h"       /* FIXME: *drv globals */
#include "colorspace.h"
#include "sound.h"
#include "capture.h"

/*-------------------------------------------------------------------------*/

void
fifo_init(struct FIFO *fifo, char *name, int slots)
{
    pthread_mutex_init(&fifo->lock, NULL);
    pthread_cond_init(&fifo->hasdata, NULL);
    fifo->name  = name;
    fifo->slots = slots;
    fifo->read  = 0;
    fifo->write = 0;
    fifo->eof   = 0;
}

int
fifo_put(struct FIFO *fifo, void *data)
{
    pthread_mutex_lock(&fifo->lock);
    if (NULL == data) {
	fifo->eof = 1;
	pthread_cond_signal(&fifo->hasdata);
	pthread_mutex_unlock(&fifo->lock);
	return 0;
    }
    if ((fifo->write + 1) % fifo->slots == fifo->read) {
	pthread_mutex_unlock(&fifo->lock);
	fprintf(stderr,"fifo %s is full\n",fifo->name);
	return -1;
    }
    if (debug > 1)
	fprintf(stderr,"put %s %d=%p [pid=%d]\n",
		fifo->name,fifo->write,data,getpid());
    fifo->data[fifo->write] = data;
    fifo->write++;
    if (fifo->write >= fifo->slots)
	fifo->write = 0;
    pthread_cond_signal(&fifo->hasdata);
    pthread_mutex_unlock(&fifo->lock);
    return 0;
}

void*
fifo_get(struct FIFO *fifo)
{
    void *data;

    pthread_mutex_lock(&fifo->lock);
    while (fifo->write == fifo->read && 0 == fifo->eof) {
	pthread_cond_wait(&fifo->hasdata, &fifo->lock);
    }
    if (fifo->write == fifo->read) {
	pthread_cond_signal(&fifo->hasdata);
	pthread_mutex_unlock(&fifo->lock);
	return NULL;
    }
    if (debug > 1)
	fprintf(stderr,"get %s %d=%p [pid=%d]\n",
		fifo->name,fifo->read,fifo->data[fifo->read],getpid());
    data = fifo->data[fifo->read];
    fifo->read++;
    if (fifo->read >= fifo->slots)
	fifo->read = 0;
    pthread_mutex_unlock(&fifo->lock);
    return data;
}

static void*
flushit(void *arg)
{
    int old;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,&old);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,&old);
    for (;;) {
	sleep(1);
	sync();
    }
    return NULL;
}

/*-------------------------------------------------------------------------*/

struct movie_handle {
    pthread_mutex_t         lock;
    const struct ng_writer  *writer;
    void                    *handle;
    pthread_t               tflush;
    struct timeval          start;
    long long               rusec;
    long long               stopby;
    int                     slots;

    struct ng_video_fmt     vfmt;
    int                     fps;
    int                     frames;
    struct FIFO             vfifo;
    pthread_t               tvideo;
    long long               vusec;

    struct ng_audio_fmt     afmt;
    unsigned long           bytes_per_sec;
    unsigned long           bytes;
    struct FIFO             afifo;
    pthread_t               taudio;
    pthread_t               raudio;
    long long               ausec;
};

static void*
writer_audio_thread(void *arg)
{
    struct movie_handle *h = arg;
    struct ng_audio_buf *buf;

    if (debug)
	fprintf(stderr,"writer_audio_thread start [pid=%d]\n",getpid());
    for (;;) {
	buf = fifo_get(&h->afifo);
	if (NULL == buf)
	    break;
	pthread_mutex_lock(&h->lock);
	h->writer->wr_audio(h->handle,buf);
	pthread_mutex_unlock(&h->lock);
#if 0
	free(buf->data);
#endif
	free(buf);
    }
    if (debug)
	fprintf(stderr,"writer_audio_thread done\n");
    return NULL;
}

static void *
writer_video_thread(void *arg)
{
    struct movie_handle *h = arg;
    struct ng_video_buf *buf;

    if (debug)
	fprintf(stderr,"writer_video_thread start [pid=%d]\n",getpid());
    for (;;) {
        buf = fifo_get(&h->vfifo);
	if (NULL == buf)
	    break;
	pthread_mutex_lock(&h->lock);
	h->writer->wr_video(h->handle,buf);
	pthread_mutex_unlock(&h->lock);
	ng_release_video_buf(buf);
    }
    if (debug)
	fprintf(stderr,"writer_video_thread done\n");
    return NULL;
}

static void*
record_audio_thread(void *arg)
{
    struct movie_handle *h = arg;
    struct ng_audio_buf *buf;
    int size,left,last;

    if (debug)
	fprintf(stderr,"record_audio_thread start [pid=%d]\n",getpid());
    size = sound_bufsize();
    for (last = 0; !last;) {
	buf = malloc(sizeof(*buf)+size);
	buf->fmt = h->afmt;
	buf->size = size;
	buf->data = ((char*)buf) + sizeof(*buf);
	memset(buf->data,0,size);
	sound_read(buf->data);
	if (h->stopby) {
	    left = (h->stopby - h->ausec) * h->bytes_per_sec / 1000000;
	    if (left <= size) {
		size = left;
		last = 1;
	    }
	}
	fifo_put(&h->afifo,buf);
	h->bytes += size;
	h->ausec = (long long)h->bytes * 1000000 / h->bytes_per_sec;
    }
    fifo_put(&h->afifo,NULL);
    if (debug)
	fprintf(stderr,"record_audio_thread done\n");
    return NULL;
}

struct movie_handle*
movie_writer_init(char *moviename, char *audioname,
		  const struct ng_writer *writer, 
		  struct ng_video_fmt *video, const void *priv_video, int fps,
		  struct ng_audio_fmt *audio, const void *priv_audio,
		  int slots)
{
    struct movie_handle *h;
    void *dummy;

    if (debug)
	fprintf(stderr,"movie_init_writer start\n");
    h = malloc(sizeof(*h));
    if (NULL == h)
	return NULL;
    memset(h,0,sizeof(*h));
    pthread_mutex_init(&h->lock, NULL);
    h->writer = writer;
    h->slots = slots;

    /* audio */
    if (audio->fmtid != AUDIO_NONE)
	sound_open(audio);
    if (audio->fmtid != AUDIO_NONE) {
	fifo_init(&h->afifo,"audio",slots);
	pthread_create(&h->taudio,NULL,writer_audio_thread,h);
	h->bytes_per_sec = ng_afmt_to_bits[audio->fmtid] *
	    ng_afmt_to_channels[audio->fmtid] * audio->rate / 8;
    }
    h->afmt = *audio;

    /* video */
    if (-1 == ng_grabber_setparams(video,0,0)) {
	free(h);
	return NULL;
    }
    fifo_init(&h->vfifo,"video",slots);
    pthread_create(&h->tvideo,NULL,writer_video_thread,h);
    h->vfmt = *video;
    h->fps  = fps;
    
    /* open file */
    h->handle = writer->wr_open(moviename,audioname,
				video,priv_video,fps,
				audio,priv_audio);
    if (debug)
	fprintf(stderr,"movie_init_writer end (h=%p)\n",h->handle);
    if (NULL != h->handle)
	return h;

    /* Oops -- wr_open() didn't work.  cleanup.  */
    if (h->afmt.fmtid != AUDIO_NONE) {
	pthread_cancel(h->taudio);
	pthread_join(h->taudio,&dummy);
	sound_close();
    }
    pthread_cancel(h->tvideo);
    pthread_join(h->tvideo,&dummy);
    free(h);

    return NULL;
}

int
movie_writer_start(struct movie_handle *h)
{
    if (debug)
	fprintf(stderr,"movie_writer_start\n");
    gettimeofday(&h->start,NULL);
    if (h->afmt.fmtid != AUDIO_NONE)
	sound_startrec();
    drv->startvideo(h_drv,h->fps,h->slots);
    if (h->afmt.fmtid != AUDIO_NONE)
	pthread_create(&h->raudio,NULL,record_audio_thread,h);
    pthread_create(&h->tflush,NULL,flushit,NULL);
    return 0;
}

int
movie_writer_stop(struct movie_handle *h)
{
    long long stopby;
    int frames;
    void *dummy;

    if (debug)
	fprintf(stderr,"movie_writer_stop\n");

    if (h->afmt.fmtid != AUDIO_NONE) {
	for (frames = 0; frames < 16; frames++) {
	    stopby = (long long)(h->frames + frames) * 1000000 / h->fps;
	    if (stopby > h->ausec)
		break;
	}
	frames++;
	h->stopby = (long long)(h->frames + frames) * 1000000 / h->fps;
	while (frames) {
	    movie_grab_put_video(h);
	    frames--;
	}
    }

    /* send EOF + join threads */
    fifo_put(&h->vfifo,NULL);
    if (h->afmt.fmtid != AUDIO_NONE) {
	pthread_join(h->raudio,&dummy);
	pthread_join(h->taudio,&dummy);
    }
    pthread_join(h->tvideo,&dummy);
    pthread_cancel(h->tflush);
    pthread_join(h->tflush,&dummy);

    /* close file */
    h->writer->wr_close(h->handle);
    drv->stopvideo(h_drv);
    if (h->afmt.fmtid != AUDIO_NONE)
	sound_close();
    return 0;
}

/*-------------------------------------------------------------------------*/

static void
movie_check_times(struct movie_handle *h)
{
    struct timeval now;

    gettimeofday(&now,NULL);
    h->rusec  = 1000000 * (now.tv_sec - h->start.tv_sec);
    h->rusec += (now.tv_usec - h->start.tv_usec);

    fprintf(stderr,"real: %d.%03ds   audio: %d.%03ds   video: %d.%03ds\r",
	    (int)((h->rusec / 1000000)),
	    (int)((h->rusec % 1000000) / 1000),
	    (int)((h->ausec / 1000000)),
	    (int)((h->ausec % 1000000) / 1000),
	    (int)((h->vusec / 1000000)),
	    (int)((h->vusec % 1000000) / 1000));
}

int
movie_grab_put_video(struct movie_handle *h)
{
    struct ng_video_buf *buf;

    if (debug > 1)
	fprintf(stderr,"grab_put_video\n");

    /* get next frame */
    buf = ng_grabber_capture(NULL,0);
    fifo_put(&h->vfifo,buf);
    h->frames++;
    h->vusec = (long long)h->frames * 1000000 / h->fps;
    movie_check_times(h);
    return 0;
}
