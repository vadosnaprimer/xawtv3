#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "grab-ng.h"
#include "commands.h"       /* FIXME: *drv globals */
#include "sound.h"
#include "capture.h"
#include "webcam.h"

#define MAX_THREADS   4
#define REORDER_SIZE  8

/*-------------------------------------------------------------------------*/
/* data fifos (audio/video)                                                */

void
fifo_init(struct FIFO *fifo, char *name, int slots, int writers)
{
    pthread_mutex_init(&fifo->lock, NULL);
    pthread_cond_init(&fifo->hasdata, NULL);
    fifo->name    = name;
    fifo->slots   = slots;
    fifo->writers = writers;
    fifo->read    = 0;
    fifo->write   = 0;
    fifo->eof     = 0;
    fifo->max     = 0;
}

int
fifo_put(struct FIFO *fifo, void *data)
{
    int full;
    
    pthread_mutex_lock(&fifo->lock);
    if (NULL == data) {
	fifo->eof++;
	if (debug)
	    fprintf(stderr,"fifo %s: EOF %d/%d\n",
		    fifo->name,fifo->eof,fifo->writers);
	if (fifo->writers == fifo->eof)
	    pthread_cond_broadcast(&fifo->hasdata);
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
    full = (fifo->write + fifo->slots - fifo->read) % fifo->slots;
    if (fifo->max < full)
	fifo->max = full;
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
    while (fifo->write == fifo->read && fifo->writers != fifo->eof) {
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
/* color space conversion / compression functions + thread                 */

struct ng_convert_handle {
    /* converter data / state */
    struct ng_video_fmt     ifmt;
    struct ng_video_fmt     ofmt;
    int                     isize;
    int                     osize;
    struct ng_video_conv    *conv;
    void                    *chandle;

    /* thread data */
    struct FIFO             *in;
    struct FIFO             *out;
};

struct ng_convert_handle*
ng_convert_alloc(struct ng_video_conv *conv,
		 struct ng_video_fmt *i,
		 struct ng_video_fmt *o)
{
    struct ng_convert_handle *h;
    
    h = malloc(sizeof(*h));
    if (NULL == h)
	return 0;
    memset(h,0,sizeof(*h));

    /* fixup output image size to match incoming */
    o->width  = i->width;
    o->height = i->height;
    if (0 == o->bytesperline)
	o->bytesperline = o->width * ng_vfmt_to_depth[o->fmtid] / 8;

    h->ifmt = *i;
    h->ofmt = *o;
    if (conv)
	h->conv = conv;
    return h;
}

void
ng_convert_init(struct ng_convert_handle *h)
{
    if (0 == h->ifmt.bytesperline)
	h->ifmt.bytesperline = h->ifmt.width *
	    ng_vfmt_to_depth[h->ifmt.fmtid] / 8;
    if (0 == h->ofmt.bytesperline)
	h->ofmt.bytesperline = h->ofmt.width *
	    ng_vfmt_to_depth[h->ofmt.fmtid] / 8;

    h->isize = h->ifmt.height * h->ifmt.bytesperline;
    if (0 == h->isize)
	h->isize = h->ifmt.width * h->ifmt.height * 3;
    h->osize = h->ofmt.height * h->ofmt.bytesperline;
    if (0 == h->osize)
	h->osize = h->ofmt.width * h->ofmt.height * 3;

    if (h->conv)
	h->chandle = h->conv->init(&h->ofmt,h->conv->priv);

    if (debug) {
	fprintf(stderr,"convert-in : %dx%d %s (size=%d)\n",
		h->ifmt.width, h->ifmt.height,
		ng_vfmt_to_desc[h->ifmt.fmtid], h->isize);
	fprintf(stderr,"convert-out: %dx%d %s (size=%d)\n",
		h->ofmt.width, h->ofmt.height,
		ng_vfmt_to_desc[h->ofmt.fmtid], h->osize);
    }
}

static void
ng_convert_copyframe(struct ng_video_buf *dest,
		     struct ng_video_buf *src)
{
    int i,sw,dw;
    unsigned char *sp,*dp;

    dw = dest->fmt.width * ng_vfmt_to_depth[dest->fmt.fmtid] / 8;
    sw = src->fmt.width * ng_vfmt_to_depth[src->fmt.fmtid] / 8;
    if (src->fmt.bytesperline == sw && dest->fmt.bytesperline == dw) {
	/* can copy in one go */
	memcpy(dest->data, src->data,
	       src->fmt.bytesperline * src->fmt.height);
    } else {
	/* copy line by line */
	dp = dest->data;
	sp = src->data;
	for (i = 0; i < src->fmt.height; i++) {
	    memcpy(dp,sp,dw);
	    dp += dest->fmt.bytesperline;
	    sp += src->fmt.bytesperline;
	}
    }
}

struct ng_video_buf*
ng_convert_frame(struct ng_convert_handle *h,
		 struct ng_video_buf *dest,
		 struct ng_video_buf *buf)
{
    if (NULL == buf)
	return NULL;

    if (NULL == dest && NULL != h->conv)
        dest = ng_malloc_video_buf(&h->ofmt,h->osize);

    if (NULL != dest) {
	dest->fmt  = h->ofmt;
	dest->size = h->osize;
	if (NULL != h->conv) {
	    h->conv->frame(h->chandle,dest,buf);
	} else {
	    ng_convert_copyframe(dest,buf);
	}
	dest->info = buf->info;
	ng_release_video_buf(buf);
	buf = dest;
    }

#if 1 /* FIXME */
    if (NULL != webcam && 0 == webcam_put(webcam,buf)) {
	free(webcam);
	webcam = NULL;
    }
#endif
    return buf;
}

void
ng_convert_fini(struct ng_convert_handle *h)
{
    if (h->conv)
	h->conv->fini(h->chandle);
    free(h);
}

struct ng_video_buf*
ng_convert_single(struct ng_convert_handle *h, struct ng_video_buf *in)
{
    struct ng_video_buf *out;

    ng_convert_init(h);
    out = ng_convert_frame(h,NULL,in);
    ng_convert_fini(h);
    return out;
}

void*
ng_convert_thread(void *arg)
{
    struct ng_convert_handle *h = arg;
    struct ng_video_buf *in, *out;
    
    if (debug)
	fprintf(stderr,"convert_thread start [pid=%d]\n",getpid());
    ng_convert_init(h);
    for (;;) {
	in  = fifo_get(h->in);
	if (NULL == in)
	    break;
	out = ng_convert_frame(h,NULL,in);
	fifo_put(h->out,out);
    }
    fifo_put(h->out,NULL);
    ng_convert_fini(h);
    if (debug)
	fprintf(stderr,"convert_thread done [pid=%d]\n",getpid());
    return NULL;
}

/*-------------------------------------------------------------------------*/
/* parameter negotiation -- look what the driver can do and what           */
/* convert functions are available                                         */

int
ng_grabber_setformat(struct ng_video_fmt *fmt, int fix_ratio)
{
    struct ng_video_fmt gfmt;
    int rc;
    
    /* no capture support */
    if (!(f_drv & CAN_CAPTURE))
	return -1;

    /* try setting the format */
    gfmt = *fmt;
    rc = drv->setformat(h_drv,&gfmt);
    if (debug)
	fprintf(stderr,"setformat: %s (%dx%d): %s\n",
		ng_vfmt_to_desc[gfmt.fmtid],
		gfmt.width,gfmt.height,
		(0 == rc) ? "ok" : "failed");
    if (0 != rc)
	return -1;

    if (fix_ratio) {
	/* fixup aspect ratio if needed */
	ng_ratio_fixup(&gfmt.width, &gfmt.height, NULL, NULL);
	gfmt.bytesperline = 0;
	if (0 != drv->setformat(h_drv,&gfmt)) {
	    fprintf(stderr,"Oops: ratio size renegotiation failed\n");
	    exit(1);
	}
    }

    /* return the real format the grabber uses now */
    *fmt = gfmt;
    return 0;
}

struct ng_video_conv*
ng_grabber_findconv(struct ng_video_fmt *fmt,
		    int fix_ratio)
{
    struct ng_video_fmt  gfmt;
    struct ng_video_conv *conv;
    int i;
    
    /* check all available conversion functions */
    for (i = 0;;) {
	conv = ng_conv_find(fmt->fmtid, &i);
	if (NULL == conv)
	    break;
	gfmt = *fmt;
	gfmt.fmtid = conv->fmtid_in;
	if (0 == ng_grabber_setformat(&gfmt,fix_ratio))
	    goto found;
    }
    fprintf(stderr,"no way to get: %dx%d %s\n",
	    fmt->width,fmt->height,ng_vfmt_to_desc[fmt->fmtid]);
    return NULL;

 found:
    *fmt = gfmt;
    return conv;
}

struct ng_video_buf*
ng_grabber_grab_image(int single)
{
    return single ? drv->getimage(h_drv) : drv->nextframe(h_drv);
}

struct ng_video_buf*
ng_grabber_get_image(struct ng_video_fmt *fmt)
{
    struct ng_video_fmt gfmt;
    struct ng_video_conv *conv;
    struct ng_convert_handle *ch;
    struct ng_video_buf *buf;
    
    if (0 == ng_grabber_setformat(fmt,1))
	return ng_grabber_grab_image(1);
    gfmt = *fmt;
    if (NULL == (conv = ng_grabber_findconv(&gfmt,1)))
	return NULL;
    ch = ng_convert_alloc(conv,&gfmt,fmt);
    if (NULL == (buf = ng_grabber_grab_image(1)))
	return NULL;
    buf = ng_convert_single(ch,buf);
    return buf;
}

/*-------------------------------------------------------------------------*/

struct movie_handle {
    /* general */
    pthread_mutex_t           lock;
    const struct ng_writer    *writer;
    void                      *handle;
    pthread_t                 tflush;
    long long                 start;
    long long                 rts;
    long long                 stopby;
    int                       slots;

    /* video */
    struct ng_video_fmt       vfmt;
    int                       fps;
    int                       frames;
    int                       seq;
    struct FIFO               vfifo;
    pthread_t                 tvideo;
    long long                 vts;

    /* video converter thread */
    struct FIFO               cfifo;
    int                       cthreads;
    struct ng_convert_handle  *hconv[MAX_THREADS];
    pthread_t                 tconv[MAX_THREADS];

    /* audio */
    const struct ng_dsp_driver *dsp;
    void                      *hdsp;
    struct ng_audio_fmt       afmt;
    unsigned long             bytes_per_sec;
    unsigned long             bytes;
    struct FIFO               afifo;
    pthread_t                 taudio;
    pthread_t                 raudio;
    long long                 ats;
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

/*
 * with multiple compression threads we might receive
 * the frames out-of-order
 */
static void*
writer_video_thread(void *arg)
{
    struct movie_handle *h = arg;
    struct ng_video_buf *buf;
    struct ng_video_buf *reorder[REORDER_SIZE];
    int seq,slot;

    if (debug)
	fprintf(stderr,"writer_video_thread start [pid=%d]\n",getpid());
    seq = 0;
    memset(&reorder,0,sizeof(reorder));
    for (;;) {
        buf = fifo_get(&h->vfifo);
	if (NULL == buf)
	    break;
	slot = buf->info.seq % REORDER_SIZE;
	if (debug > 1)
	    fprintf(stderr,"video write: get seq=%d [%d]\n",
		    buf->info.seq,slot);
	if (reorder[slot]) {
	    fprintf(stderr,"panic: reorder buffer full\n");
	    exit(1);
	}
	reorder[slot] = buf;
	
	for (;;) {
	    slot = seq % REORDER_SIZE;
	    if (NULL == reorder[slot])
		break;
	    buf = reorder[slot];
	    reorder[slot] = NULL;
	    if (debug > 1)
		fprintf(stderr,"video write: put seq=%d [%d/%d]\n",
			buf->info.seq,slot,seq);
	    seq++;

	    pthread_mutex_lock(&h->lock);
	    h->writer->wr_video(h->handle,buf);
	    if (buf->info.twice)
		h->writer->wr_video(h->handle,buf);
	    pthread_mutex_unlock(&h->lock);
	    ng_release_video_buf(buf);
	}
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
	buf = h->dsp->read(h->hdsp,h->stopby);
	if (NULL == buf)
	    break;
	if (0 == buf->size)
	    continue;
	h->ats = buf->info.ts;
	if (0 != fifo_put(&h->afifo,buf))
	    free(buf);
    }
    fifo_put(&h->afifo,NULL);
    if (debug)
	fprintf(stderr,"record_audio_thread done\n");
    return NULL;
}

struct movie_handle*
movie_writer_init(char *moviename, char *audioname,
		  const struct ng_writer *writer, 
		  struct ng_video_fmt *video,const void *priv_video,int fps,
		  struct ng_audio_fmt *audio,const void *priv_audio,char *dsp,
		  int slots, int threads)
{
    struct movie_handle *h;
    struct ng_video_conv *conv;
    void *dummy;
    int i;

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
	h->dsp = ng_dsp_open(dsp,audio,&h->hdsp);
	if (NULL == h->dsp) {
	    free(h);
	    return NULL;
	}
	fifo_init(&h->afifo,"audio",slots,1);
	pthread_create(&h->taudio,NULL,writer_audio_thread,h);
	h->bytes_per_sec = ng_afmt_to_bits[audio->fmtid] *
	    ng_afmt_to_channels[audio->fmtid] * audio->rate / 8;
	h->afmt = *audio;
    }

    /* video */
    if (video->fmtid != VIDEO_NONE) {
	if (0 == ng_grabber_setformat(video,1)) {
	    /* native format works -- no conversion needed */
	    fifo_init(&h->vfifo,"video",slots,1);
	    pthread_create(&h->tvideo,NULL,writer_video_thread,h);
	} else {
	    /* have to convert video frames */
	    struct ng_video_fmt gfmt = *video;
	    if (NULL == (conv = ng_grabber_findconv(&gfmt,1))) {
		if (h->afmt.fmtid != AUDIO_NONE)
		    h->dsp->close(h->hdsp);
		free(h);
		return NULL;
	    }
	    h->cthreads = threads;
	    if (h->cthreads < 1)
		h->cthreads = 1;
	    if (h->cthreads > MAX_THREADS)
		h->cthreads = MAX_THREADS;
	    fifo_init(&h->vfifo,"video",slots,h->cthreads);
	    fifo_init(&h->cfifo,"conv",slots,1);
	    pthread_create(&h->tvideo,NULL,writer_video_thread,h);
	    for (i = 0; i < h->cthreads; i++) {
		h->hconv[i] = ng_convert_alloc(conv,&gfmt,video);
		h->hconv[i]->in  = &h->cfifo;
		h->hconv[i]->out = &h->vfifo;
		pthread_create(&h->tconv[i],NULL,ng_convert_thread,
			       h->hconv[i]);
	    }
	}
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
	h->dsp->close(h->hdsp);
    }
    if (h->vfmt.fmtid != VIDEO_NONE) {
	pthread_cancel(h->tvideo);
	pthread_join(h->tvideo,&dummy);
    }
    for (i = 0; i < h->cthreads; i++) {
	pthread_cancel(h->tconv[i]);
	pthread_join(h->tconv[i],&dummy);
    }
    free(h);
    return NULL;
}

int
movie_writer_start(struct movie_handle *h)
{
    int rc = 0;

    if (debug)
	fprintf(stderr,"movie_writer_start\n");
    h->start = ng_get_timestamp();
    if (h->afmt.fmtid != AUDIO_NONE)
	if (0 != h->dsp->startrec(h->hdsp))
	    rc = -1;
    if (h->vfmt.fmtid != VIDEO_NONE)
	if (0 != drv->startvideo(h_drv,h->fps,h->slots))
	    rc = -1;
    if (h->afmt.fmtid != AUDIO_NONE)
	pthread_create(&h->raudio,NULL,record_audio_thread,h);
    pthread_create(&h->tflush,NULL,flushit,NULL);
    return rc;
}

int
movie_writer_stop(struct movie_handle *h)
{
    char line[128];
    long long stopby;
    int frames,i;
    void *dummy;

    if (debug)
	fprintf(stderr,"movie_writer_stop\n");

    if (h->vfmt.fmtid != VIDEO_NONE && h->afmt.fmtid != AUDIO_NONE) {
	for (frames = 0; frames < 16; frames++) {
	    stopby = (long long)(h->frames + frames) * 1000000000000 / h->fps;
	    if (stopby > h->ats)
		break;
	}
	frames++;
	h->stopby = (long long)(h->frames + frames) * 1000000000000 / h->fps;
	while (frames) {
	    movie_grab_put_video(h,NULL);
	    frames--;
	}
    } else if (h->afmt.fmtid != AUDIO_NONE) {
	h->stopby = h->ats;
    }

    /* send EOF */
    if (h->cthreads)
	fifo_put(&h->cfifo,NULL);
    else
	fifo_put(&h->vfifo,NULL);

    /* join threads */
    if (h->afmt.fmtid != AUDIO_NONE) {
	pthread_join(h->raudio,&dummy);
	pthread_join(h->taudio,&dummy);
    }
    if (h->vfmt.fmtid != VIDEO_NONE)
	pthread_join(h->tvideo,&dummy);
    for (i = 0; i < h->cthreads; i++)
	pthread_join(h->tconv[i],&dummy);
    pthread_cancel(h->tflush);
    pthread_join(h->tflush,&dummy);

    /* close file */
    h->writer->wr_close(h->handle);
    if (h->afmt.fmtid != AUDIO_NONE)
	h->dsp->close(h->hdsp);
    if (h->vfmt.fmtid != VIDEO_NONE)
	drv->stopvideo(h_drv);

    /* fifo stats */
    sprintf(line, "fifo max fill: audio %d/%d, video %d/%d, convert %d/%d",
	    h->afifo.max,h->afifo.slots,
	    h->vfifo.max,h->vfifo.slots,
	    h->cfifo.max,h->cfifo.slots);
    rec_status(line);

    free(h);
    return 0;
}

/*-------------------------------------------------------------------------*/

static void
movie_print_timestamps(struct movie_handle *h)
{
    long long adiff,vdiff;
    char line[128];

    if (NULL == rec_status)
	return;
    h->rts = ng_get_timestamp() - h->start;
    adiff = h->ats - h->rts;
    vdiff = h->vts - h->rts;
#if 0
    sprintf(line,"real: %d.%03ds   audio: %d.%03ds   video: %d.%03ds",
	    (int)((h->rts / 1000000000)),
	    (int)((h->rts % 1000000000) / 1000000),
	    (int)((h->ats / 1000000000)),
	    (int)((h->ats % 1000000000) / 1000000),
	    (int)((h->vts / 1000000000)),
	    (int)((h->vts % 1000000000) / 1000000));
#else
    sprintf(line,"real: %d.%03ds   audio: %c%d.%03ds   video: %c%d.%03ds",
            (int)((h->rts / 1000000000)),
            (int)((h->rts % 1000000000) / 1000000),
	    (adiff > 0) ? '+' : '-',
	    (int)((abs(adiff) / 1000000000)),
	    (int)((abs(adiff) % 1000000000) / 1000000),
	    (vdiff > 0) ? '+' : '-',
	    (int)((abs(vdiff) / 1000000000)),
	    (int)((abs(vdiff) % 1000000000) / 1000000));
#endif
    rec_status(line);
}

int
movie_grab_put_video(struct movie_handle *h, struct ng_video_buf **ret)
{
    struct ng_video_buf *buf;
    int expected;

    if (debug > 1)
	fprintf(stderr,"grab_put_video\n");

    /* fetch next frame */
    buf = ng_grabber_grab_image(0);
    if (NULL == buf)
	return -1;
#if 0 /* FIXME */
    buf = ng_filter_single(cur_filter,buf);
#endif

    /* rate control */
    expected = buf->info.ts * h->fps / 1000000000000;
    if (expected < h->frames) {
	if (debug > 1)
	    fprintf(stderr,"rate: ignoring frame\n");
	ng_release_video_buf(buf);
	return 0;
    }
    if (expected > h->frames) {
	fprintf(stderr,"rate: queueing frame twice (%d)\n",
		expected-h->frames);
	buf->info.twice++;
	h->frames++;
    }
    h->frames++;
    h->vts = buf->info.ts;
    buf->info.seq = h->seq++;

    /* return a pointer to the frame if requested */
    if (NULL != ret) {
	buf->refcount++;
	*ret = buf;
    }
    
    /* put into fifo */
    if (h->cthreads) {
	if (0 != fifo_put(&h->cfifo,buf))
	    ng_release_video_buf(buf);    
    } else {
	if (0 != fifo_put(&h->vfifo,buf))
	    ng_release_video_buf(buf);    
    }

    /* feedback */
    movie_print_timestamps(h);
    return h->frames;
}
