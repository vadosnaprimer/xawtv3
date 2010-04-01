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
fifo_put(struct FIFO *fifo, unsigned char *data, int size)
{
    pthread_mutex_lock(&fifo->lock);
    if (debug > 1)
	fprintf(stderr,"put %s %p %d\n",fifo->name,data,size);
    if (NULL == data) {
	fifo->eof = 1;
	pthread_cond_signal(&fifo->hasdata);
	pthread_mutex_unlock(&fifo->lock);
	return 0;
    }
    if ((fifo->write + 1) % fifo->slots == fifo->read) {
	pthread_mutex_unlock(&fifo->lock);
	fprintf(stderr,"fifo %s is full\n",fifo->name);
	return 0;
    }
    fifo->data[fifo->write] = data;
    fifo->size[fifo->write] = size;
    fifo->write++;
    if (fifo->write >= fifo->slots)
	fifo->write = 0;
    pthread_cond_signal(&fifo->hasdata);
    pthread_mutex_unlock(&fifo->lock);
    return size;
}

int
fifo_get(struct FIFO *fifo, unsigned char **data)
{
    int size;

    pthread_mutex_lock(&fifo->lock);
    if (debug > 1)
	fprintf(stderr,"get %s\n",fifo->name);
    while (fifo->write == fifo->read && 0 == fifo->eof) {
	pthread_cond_wait(&fifo->hasdata, &fifo->lock);
    }
    if (fifo->write == fifo->read) {
	*data = NULL;
	pthread_cond_signal(&fifo->hasdata);
	pthread_mutex_unlock(&fifo->lock);
	return 0;
    }
    *data = fifo->data[fifo->read];
    size = fifo->size[fifo->read];
    fifo->read++;
    if (fifo->read >= fifo->slots)
	fifo->read = 0;
    pthread_cond_signal(&fifo->hasdata);
    pthread_mutex_unlock(&fifo->lock);
    return size;
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
    const struct ng_writer  *writer;
    void                    *handle;
    pthread_mutex_t         lock;
    struct ng_video_fmt     vfmt;
    struct ng_audio_fmt     afmt;
    int                     fps;

    struct FIFO             *vfifo;
    struct FIFO             *afifo;
};

static struct movie_handle movie_state;

static struct FIFO      faudio;
static struct FIFO      fvideo;
static pthread_t        taudio;
static pthread_t        tvideo;
static pthread_t        tflush;
static char             *baudio,*bvideo;
static int              saudio,svideo;
static int              iaudio,ivideo;
static int              naudio,nvideo;
static int              caudio,cvideo;

static void*
writer_audio_thread(void *arg)
{
    struct movie_handle *h = arg;
    struct ng_audio_buf buf;
    unsigned char *data;
    int size;

    if (debug)
	fprintf(stderr,"writer_audio_thread start\n");
    for (;;) {
	size = fifo_get(h->afifo,&data);
	if (NULL == data)
	    break;
	pthread_mutex_lock(&h->lock);
	buf.fmt  = h->afmt;
	buf.data = data;
	buf.size = size;
	h->writer->wr_audio(h->handle,&buf);
	pthread_mutex_unlock(&h->lock);
    }
    if (debug)
	fprintf(stderr,"writer_audio_thread done\n");
    return NULL;
}

static void *
writer_video_thread(void *arg)
{
    struct movie_handle *h = arg;
    struct ng_video_buf buf;
    unsigned char *data;
    int size;

    if (debug)
	fprintf(stderr,"writer_video_thread start\n");
    for (;;) {
	size = fifo_get(h->vfifo,&data);
	if (NULL == data)
	    break;
	pthread_mutex_lock(&h->lock);
	buf.fmt  = h->vfmt;
	buf.data = data;
	if (0 != size) {
	    buf.size = size;
	} else {
	    buf.size = buf.fmt.width * buf.fmt.height *
		ng_vfmt_to_depth[buf.fmt.fmtid] / 8;
	}
	h->writer->wr_video(h->handle,&buf);
	pthread_mutex_unlock(&h->lock);
    }
    if (debug)
	fprintf(stderr,"writer_video_thread done\n");
    return NULL;
}

int
movie_writer_init(char *moviename, char *audioname,
		  const struct ng_writer *writer, 
		  struct ng_video_fmt *video, const void *priv_video, int fps,
		  struct ng_audio_fmt *audio, const void *priv_audio,
		  int slots, int *sound)
{
    struct movie_handle *h = &movie_state;
    int linelength;

    if (debug)
	fprintf(stderr,"movie_init_writer start\n");
    memset(h,0,sizeof(*h));
    pthread_mutex_init(&h->lock, NULL);
    h->afifo  = &faudio;
    h->vfifo  = &fvideo;
    h->writer = writer;

    /* audio */
    *sound = -1;
    if (audio->fmtid != AUDIO_NONE)
	*sound = sound_open(audio);
    if (audio->fmtid != AUDIO_NONE) {
	fifo_init(&faudio,"audio",slots);
	pthread_create(&taudio,NULL,writer_audio_thread,h);
	iaudio = 0;
	naudio = slots+2;
	saudio = sound_bufsize();
	baudio = malloc(saudio*naudio);
	caudio = 0;
    }
    h->afmt = *audio;

    /* video */
    grabber_setparams(video->fmtid, &video->width, &video->height,
		      &linelength,0,0);
    fifo_init(&fvideo,"video",slots);
    pthread_create(&tvideo,NULL,writer_video_thread,h);
    ivideo  = 0;
    nvideo  = slots+2;
    svideo  = video->width * video->height *
	ng_vfmt_to_depth[video->fmtid]/8;
    if (0 == svideo)
	svideo = video->width * video->height * 3;
    bvideo  = malloc(svideo*nvideo);
    cvideo  = 0;
    h->vfmt = *video;
    h->fps  = fps;
    
    /* open file */
    h->handle = writer->wr_open(moviename,audioname,
				video,priv_video,fps,
				audio,priv_audio);
    if (debug)
	fprintf(stderr,"movie_init_writer end (h=%p)\n",h->handle);
    return 0;
}

int
movie_writer_start()
{
    struct movie_handle *h = &movie_state;

    if (debug)
	fprintf(stderr,"movie_writer_start\n");
    if (grabber->grab_start)
	grabber->grab_start(h->fps,0);
    if (h->afmt.fmtid != AUDIO_NONE)
	sound_startrec();
    pthread_create(&tflush,NULL,flushit,NULL);
    return 0;
}

int
movie_writer_stop()
{
    struct movie_handle *h = &movie_state;
    void *dummy;
    long long ausec,vusec;
    int soundbytes;

    if (debug)
	fprintf(stderr,"movie_writer_stop\n");

    if (h->afmt.fmtid != AUDIO_NONE) {
	vusec = cvideo * 1000 / h->fps;
	ausec = ((long long)caudio*saudio*8*1000) /
	    (h->afmt.rate * ng_afmt_to_bits[h->afmt.fmtid] *
	     ng_afmt_to_channels[h->afmt.fmtid]);
	while (vusec < ausec) {
	    grab_put_video();
	    vusec = cvideo * 1000 / h->fps;
	}
	soundbytes = (int)(vusec-ausec) * h->afmt.rate *
	    ng_afmt_to_bits[h->afmt.fmtid] *
	    ng_afmt_to_channels[h->afmt.fmtid]/8/1000;
	soundbytes = (soundbytes+4) & ~0x03;
	fprintf(stderr,"vs=%Ld as=%Ld %d/%d\n",vusec,ausec,
		soundbytes,saudio);
	while (soundbytes > saudio) {
	    grab_put_audio();
	    soundbytes -= saudio;
	}
	sound_read(baudio + iaudio*saudio);
	fifo_put(&faudio,baudio + iaudio*saudio,soundbytes);
    }

    /* send EOF + join threads */
    if (h->afmt.fmtid != AUDIO_NONE) {
	fifo_put(&faudio,NULL,0);
	pthread_join(taudio,&dummy);
    }
    fifo_put(&fvideo,NULL,0);
    pthread_join(tvideo,&dummy);
    pthread_cancel(tflush);
    pthread_join(tflush,&dummy);

    /* close file */
    h->writer->wr_close(h->handle);
    if (grabber->grab_stop)
	grabber->grab_stop();
    if (h->afmt.fmtid != AUDIO_NONE)
	sound_close();
    return 0;
}

/*-------------------------------------------------------------------------*/

int
grab_put_video()
{
    int size;

    if (debug > 1)
	fprintf(stderr,"grab_put_video\n");

    /* get next frame */
    grabber_capture(bvideo + ivideo*svideo,0,&size);

    fifo_put(&fvideo,bvideo + ivideo*svideo,size);
    ivideo = (ivideo+1) % nvideo;
    cvideo++;
    return 0;
}

int
grab_put_audio()
{
    if (debug > 1)
	fprintf(stderr,"grab_put_audio\n");

    sound_read(baudio + iaudio*saudio);
    fifo_put(&faudio,baudio + iaudio*saudio,saudio);
    iaudio = (iaudio+1) % naudio;
    caudio++;
    return 0;
}
