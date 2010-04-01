#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#ifdef HAVE_SOUNDCARD_H
# include <soundcard.h>
#endif
#ifdef HAVE_SYS_SOUNDCARD_H
# include <sys/soundcard.h>
#endif
#include <pthread.h>

#include "grab-ng.h"
#include "sound.h"

/* -------------------------------------------------------------------- */

static char *names[] = SOUND_DEVICE_NAMES;

static int  mix;
static int  dev = -1;
static int  volume;
static int  muted;
extern int  debug;

/* -------------------------------------------------------------------- */

int
mixer_open(char *filename, char *device)
{
    int i, devmask;

    if (-1 == (mix = open(filename,O_RDONLY))) {
	perror("mixer open");
	return -1;
    }
    fcntl(mix,F_SETFD,FD_CLOEXEC);

    if (-1 == ioctl(mix,MIXER_READ(SOUND_MIXER_DEVMASK),&devmask)) {
	perror("mixer read devmask");
	return -1;
    }
    for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
	if ((1<<i) & devmask && strcasecmp(names[i],device) == 0) {
	    if (-1 == ioctl(mix,MIXER_READ(i),&volume)) {
		perror("mixer read volume");
		return -1;
	    } else {
		dev = i;
		muted = 0;
	    }
	}
    }
    if (-1 == dev) {
	fprintf(stderr,"mixer: hav'nt found device '%s'\nmixer: available: ",device);
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
	    if ((1<<i) & devmask)
		fprintf(stderr," '%s'",names[i]);
	fprintf(stderr,"\n");
    }
    return (-1 != dev) ? 0 : -1;
}

void
mixer_close()
{
    close(mix);
    dev = -1;
}

int
mixer_get_volume()
{
    if (-1 == ioctl(mix,MIXER_READ(dev),&volume)) {
	perror("mixer write volume");
	return -1;
    }
    return (-1 == dev) ? -1 : (volume & 0x7f);
}

int
mixer_set_volume(int val)
{
    if (-1 == dev)
	return -1;
    val   &= 0x7f;
    volume = val | (val << 8);;
    if (-1 == ioctl(mix,MIXER_WRITE(dev),&volume)) {
	perror("mixer write volume");
	return -1;
    }
    muted = 0;
    return 0;
}

int
mixer_mute()
{
    int zero=0;
    
    muted = 1;
    if (-1 == dev)
	return -1;
    mixer_get_volume();
    if (-1 == ioctl(mix,MIXER_WRITE(dev),&zero))
	return -1;
    return 0;
}

int
mixer_unmute()
{
    muted = 0;
    if (-1 == dev)
	return -1;
    if (-1 == ioctl(mix,MIXER_WRITE(dev),&volume))
	return -1;
    return 0;
}

int
mixer_get_muted()
{
    return (-1 == dev) ? -1 : muted;
}

/* -------------------------------------------------------------------- */

struct oss_handle {
    int    fd;

    /* oss */
    struct ng_audio_fmt ifmt;
    int    afmt,channels,rate;
    int    blocksize;

    /* me */
    struct     ng_audio_fmt ofmt;
    int        byteswap;
    int        bytes;
    int        bytes_per_sec;
};

static const int afmt_to_oss[AUDIO_FMT_COUNT] = {
    0,
    AFMT_U8,
    AFMT_U8,
    AFMT_S16_LE,
    AFMT_S16_LE,
    AFMT_S16_BE,
    AFMT_S16_BE
};

static int
oss_setformat(struct oss_handle *h, struct ng_audio_fmt *fmt)
{
    int frag;

    if (0 == afmt_to_oss[fmt->fmtid])
	return -1;

    h->afmt     = afmt_to_oss[fmt->fmtid];
    h->channels = ng_afmt_to_channels[fmt->fmtid];
    frag        = 0x7fff000c; /* 4k */

    /* format */
    ioctl(h->fd, SNDCTL_DSP_SETFMT, &h->afmt);
    if (h->afmt != afmt_to_oss[fmt->fmtid]) {
	fprintf(stderr,"oss: SNDCTL_DSP_SETFMT(%d): %s\n",
		afmt_to_oss[fmt->fmtid],strerror(errno));
	goto err;
    }

    /* channels */
    ioctl(h->fd, SNDCTL_DSP_CHANNELS, &h->channels);
    if (h->channels != ng_afmt_to_channels[fmt->fmtid]) {
	fprintf(stderr,"oss: SNDCTL_DSP_CHANNELS(%d): %s\n",
		ng_afmt_to_channels[fmt->fmtid],strerror(errno));
	goto err;
    }

    /* sample rate */
    h->rate = fmt->rate;
    ioctl(h->fd, SNDCTL_DSP_SPEED, &h->rate);
    ioctl(h->fd, SNDCTL_DSP_SETFRAGMENT, &frag);

    if (-1 == ioctl(h->fd, SNDCTL_DSP_GETBLKSIZE,  &h->blocksize)) {
	perror("SNDCTL_DSP_GETBLKSIZE");
        goto err;
    }

    if (debug)
	fprintf(stderr,"oss: bs=%d rate=%d channels=%d bits=%d (%s)\n",
		h->blocksize,h->rate,
		ng_afmt_to_channels[fmt->fmtid],
		ng_afmt_to_bits[fmt->fmtid],
		ng_afmt_to_desc[fmt->fmtid]);
    return 0;
    
 err:
    if (debug)
	fprintf(stderr,"oss: sound format not supported [%s]\n",
		ng_afmt_to_desc[fmt->fmtid]);
    return -1;
}

void*
oss_open(char *device, struct ng_audio_fmt *fmt)
{
    struct oss_handle *h;
    struct ng_audio_fmt ifmt;

    h = malloc(sizeof(*h));
    if (NULL == h)
	return NULL;
    memset(h,0,sizeof(*h));

    if (-1 == (h->fd = open(device ? device : "/dev/dsp", O_RDONLY))) {
	fprintf(stderr,"oss: open %s: %s\n",
		device ? device : "/dev/dsp",
		strerror(errno));
	goto err;
    }
    fcntl(h->fd,F_SETFD,FD_CLOEXEC);

    if (0 == oss_setformat(h,fmt)) {
	/* fine, native format works */
	fmt->rate = h->rate;
	h->ifmt = *fmt;
	h->ofmt = *fmt;
	h->bytes_per_sec = ng_afmt_to_bits[h->ifmt.fmtid] *
	    ng_afmt_to_channels[h->ifmt.fmtid] * h->ifmt.rate / 8;
	return h;
    }

    /* try byteswapping */
    ifmt = *fmt;
    switch (fmt->fmtid) {
    case AUDIO_S16_LE_MONO:   ifmt.fmtid = AUDIO_S16_BE_MONO;   break;
    case AUDIO_S16_LE_STEREO: ifmt.fmtid = AUDIO_S16_BE_STEREO; break;
    case AUDIO_S16_BE_MONO:   ifmt.fmtid = AUDIO_S16_LE_MONO;   break;
    case AUDIO_S16_BE_STEREO: ifmt.fmtid = AUDIO_S16_LE_STEREO; break;
    }
    if (0 == oss_setformat(h,&ifmt)) {
	if (debug)
	    fprintf(stderr,"oss: byteswapping pcm data\n");
	h->byteswap = 1;
	ifmt.rate = h->rate;
	fmt->rate = h->rate;
	h->ifmt = ifmt;
	h->ofmt = *fmt;
	h->bytes_per_sec = ng_afmt_to_bits[h->ifmt.fmtid] *
	    ng_afmt_to_channels[h->ifmt.fmtid] * h->ifmt.rate / 8;
	return h;
    }

    fprintf(stderr,"oss: can't record %s\n",
	    ng_afmt_to_desc[fmt->fmtid]);
    
 err:
    fmt->rate  = 0;
    fmt->fmtid = AUDIO_NONE;
    if (h->fd)
	close(h->fd);
    free(h);
    return NULL;
}

void
oss_startrec(void *handle)
{
    struct oss_handle *h = handle;
    int trigger;
    
    trigger = PCM_ENABLE_INPUT;
    ioctl(h->fd,SNDCTL_DSP_SETTRIGGER,&trigger);
}

static struct ng_audio_buf*
oss_bufalloc(struct ng_audio_fmt *fmt, int size)
{
    struct ng_audio_buf *buf;

    buf = malloc(sizeof(*buf)+size);
    memset(buf,0,sizeof(*buf));
    buf->fmt  = *fmt;
    buf->size = size;
    buf->data = (char*)buf + sizeof(*buf);
    return buf;
}

static void
oss_bufread(int fd,char *buffer,int blocksize)
{
    int rc,count=0;

    /* why FreeBSD returns chunks smaller than blocksize? */
    for (;;) {
	rc = read(fd,buffer+count,blocksize-count);
	if (rc < 0) {
	    perror("read /dev/dsp");
	    exit(1);
	}
	count += rc;
	if (count == blocksize)
	    return;
    }
}

static void
oss_bufswap(void *ptr, int size)
{
    unsigned short *buf = ptr;
    int i;

    size = size >> 1;
    for (i = 0; i < size; i++)
	buf[i] = ((buf[i] >> 8) & 0xff) | ((buf[i] << 8) & 0xff00);
}

struct ng_audio_buf*
oss_read(void *handle, long long stopby)
{
    struct oss_handle *h = handle;
    struct ng_audio_buf* buf;
    int bytes;

    if (stopby) {
	bytes = stopby * h->bytes_per_sec / 1000000000 - h->bytes;
	if (debug)
	    fprintf(stderr,"oss: left: %d bytes (%.3fs)\n",
		    bytes,(float)bytes/h->bytes_per_sec);
	if (bytes > h->blocksize)
	    bytes = h->blocksize;
	if (0 == bytes)
	    return NULL;
    } else {
	bytes = h->blocksize;
    }
    buf = oss_bufalloc(&h->ofmt,bytes);
    oss_bufread(h->fd,buf->data,bytes);
    if (h->byteswap)
	oss_bufswap(buf->data,bytes);
    h->bytes += bytes;
    buf->ts = (long long)h->bytes * 1000000000 / h->bytes_per_sec;
    return buf;
}

void
oss_close(void *handle)
{
    struct oss_handle *h = handle;

    close(h->fd);
    free(h);
}
