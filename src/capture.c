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
#include "grab.h"
#include "commands.h"       /* FIXME: *drv globals */
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
    /* general */
    pthread_mutex_t         lock;
    const struct ng_writer  *writer;
    void                    *handle;
    pthread_t               tflush;
    long long               start;
    long long               rts;
    long long               stopby;
    int                     slots;

    /* video */
    struct ng_video_fmt     vfmt;
    int                     fps;
    int                     frames;
    struct FIFO             vfifo;
    pthread_t               tvideo;
    long long               vts;

    /* audio */
    void                    *sndhandle;
    struct ng_audio_fmt     afmt;
    unsigned long           bytes_per_sec;
    unsigned long           bytes;
    struct FIFO             afifo;
    pthread_t               taudio;
    pthread_t               raudio;
    long long               ats;
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

    if (debug)
	fprintf(stderr,"record_audio_thread start [pid=%d]\n",getpid());
    for (;;) {
	buf = oss_read(h->sndhandle,h->stopby);
	if (NULL == buf)
	    break;
	if (0 == buf->size)
	    continue;
	h->ats = buf->ts;
	fifo_put(&h->afifo,buf);
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
    if (audio->fmtid != AUDIO_NONE) {
	h->sndhandle = oss_open(NULL,audio);
	if (NULL == h->sndhandle) {
	    free(h);
	    return NULL;
	}
	fifo_init(&h->afifo,"audio",slots);
	pthread_create(&h->taudio,NULL,writer_audio_thread,h);
	h->bytes_per_sec = ng_afmt_to_bits[audio->fmtid] *
	    ng_afmt_to_channels[audio->fmtid] * audio->rate / 8;
	h->afmt = *audio;
    }

    /* video */
    if (video->fmtid != VIDEO_NONE) {
	if (-1 == ng_grabber_setparams(video,0)) {
	    if (h->afmt.fmtid != AUDIO_NONE)
		oss_close(h->sndhandle);
	    free(h);
	    return NULL;
	}
	fifo_init(&h->vfifo,"video",slots);
	pthread_create(&h->tvideo,NULL,writer_video_thread,h);
	h->vfmt = *video;
	h->fps  = fps;
    }	
    
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
	oss_close(h->sndhandle);
    }
    if (h->vfmt.fmtid != VIDEO_NONE) {
	pthread_cancel(h->tvideo);
	pthread_join(h->tvideo,&dummy);
    }
    free(h);
    return NULL;
}

int
movie_writer_start(struct movie_handle *h)
{
    if (debug)
	fprintf(stderr,"movie_writer_start\n");
    h->start = ng_get_timestamp();
    if (h->afmt.fmtid != AUDIO_NONE)
	oss_startrec(h->sndhandle);
    if (h->vfmt.fmtid != VIDEO_NONE)
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

    if (h->vfmt.fmtid != VIDEO_NONE && h->afmt.fmtid != AUDIO_NONE) {
	for (frames = 0; frames < 16; frames++) {
	    stopby = (long long)(h->frames + frames) * 1000000000 / h->fps;
	    if (stopby > h->ats)
		break;
	}
	frames++;
	h->stopby = (long long)(h->frames + frames) * 1000000000 / h->fps;
	while (frames) {
	    movie_grab_put_video(h);
	    frames--;
	}
    } else if (h->afmt.fmtid != AUDIO_NONE) {
	h->stopby = h->ats;
    }

    /* send EOF + join threads */
    fifo_put(&h->vfifo,NULL);
    if (h->afmt.fmtid != AUDIO_NONE) {
	pthread_join(h->raudio,&dummy);
	pthread_join(h->taudio,&dummy);
    }
    if (h->vfmt.fmtid != VIDEO_NONE)
	pthread_join(h->tvideo,&dummy);
    pthread_cancel(h->tflush);
    pthread_join(h->tflush,&dummy);

    /* close file */
    h->writer->wr_close(h->handle);
    if (h->afmt.fmtid != AUDIO_NONE)
	oss_close(h->sndhandle);
    if (h->vfmt.fmtid != VIDEO_NONE)
	drv->stopvideo(h_drv);
    free(h);
    return 0;
}

/*-------------------------------------------------------------------------*/

static void
movie_print_timestamps(struct movie_handle *h)
{
    h->rts = ng_get_timestamp() - h->start;
    fprintf(stderr,"real: %d.%03ds   audio: %d.%03ds   video: %d.%03ds \r",
	    (int)((h->rts / 1000000000)),
	    (int)((h->rts % 1000000000) / 1000000),
	    (int)((h->ats / 1000000000)),
	    (int)((h->ats % 1000000000) / 1000000),
	    (int)((h->vts / 1000000000)),
	    (int)((h->vts % 1000000000) / 1000000));
}

int
movie_grab_put_video(struct movie_handle *h)
{
    struct ng_video_buf *cap,*buf;
    int expected;

    if (debug > 1)
	fprintf(stderr,"grab_put_video\n");

    /* fetch next frame */
    cap = ng_grabber_getimage(0);
    if (NULL == cap)
	return -1;

    /* rate control + put into fifo */
    expected = cap->ts * h->fps / 1000000000;
    if (expected < h->frames) {
	if (debug > 1)
	    fprintf(stderr,"rate: ignoring frame\n");
	ng_release_video_buf(cap);
	return 0;
    }
    buf = ng_grabber_convert(NULL,cap);
    if (NULL == buf)
	return -1;
    if (expected > h->frames) {
	fprintf(stderr,"rate: queueing frame twice (%d)\n",
		expected-h->frames);
	buf->refcount++;
	fifo_put(&h->vfifo,buf);
	h->frames++;
    }
    h->vts = buf->ts;
    fifo_put(&h->vfifo,buf);
    h->frames++;

    /* feedback */
    movie_print_timestamps(h);
    return h->frames;
}
