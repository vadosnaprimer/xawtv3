#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#ifdef HAVE_SOUNDCARD_H
# include <soundcard.h>
#endif
#ifdef HAVE_SYS_SOUNDCARD_H
# include <sys/soundcard.h>
#endif

#include "grab-ng.h"

/* -------------------------------------------------------------------- */

extern int  debug;
static const char *names[] = SOUND_DEVICE_NAMES;

static int mixer_read_attr(struct ng_attribute *attr);
static void mixer_write_attr(struct ng_attribute *attr, int val);

struct mixer_handle {
    int  mix;
    int  dev;
    int  volume;
    int  muted;
};

static struct ng_attribute mixer_attrs[] = {
    {
	id:       ATTR_ID_MUTE,
	name:     "mute",
	type:     ATTR_TYPE_BOOL,
	read:     mixer_read_attr,
	write:    mixer_write_attr,
    },{
	id:       ATTR_ID_VOLUME,
	name:     "volume",
	type:     ATTR_TYPE_INTEGER,
	min:      0,
	max:      100,
	read:     mixer_read_attr,
	write:    mixer_write_attr,
    },{
	/* end of list */
    }
};

static struct ng_attribute*
mixer_open(char *device, char *channel)
{
    struct mixer_handle *h;
    struct ng_attribute *attrs;
    int i, devmask;

    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));
    h->mix = -1;
    h->dev = -1;

    if (-1 == (h->mix = open(device,O_RDONLY))) {
	fprintf(stderr,"open %s: %s\n",device,strerror(errno));
	goto err;
    }
    fcntl(h->mix,F_SETFD,FD_CLOEXEC);

    if (-1 == ioctl(h->mix,MIXER_READ(SOUND_MIXER_DEVMASK),&devmask)) {
	fprintf(stderr,"%s: read devmask: %s",device,strerror(errno));
	goto err;
    }
    for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
	if ((1<<i) & devmask && strcasecmp(names[i],channel) == 0) {
	    if (-1 == ioctl(h->mix,MIXER_READ(i),&h->volume)) {
		fprintf(stderr,"%s: read volume: %s",
			device,strerror(errno));
		goto err;
	    } else {
		h->dev = i;
	    }
	}
    }

    if (-1 == h->dev) {
	fprintf(stderr,"%s: '%s' not found.\n%s: available: ",
		device,channel,device);
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
	    if ((1<<i) & devmask)
		fprintf(stderr," '%s'",names[i]);
	fprintf(stderr,"\n");
	goto err;
    }

    attrs = malloc(sizeof(mixer_attrs));
    memcpy(attrs,mixer_attrs,sizeof(mixer_attrs));
    for (i = 0; attrs[i].name != NULL; i++)
	attrs[i].handle = h;
    
    return attrs;

 err:
    if (h) {
	if (-1 != h->mix)
	    close(h->mix);
	free(h);
    }
    return NULL;
}

static int
mixer_read_attr(struct ng_attribute *attr)
{
    struct mixer_handle *h = attr->handle;
    int vol;

    switch (attr->id) {
    case ATTR_ID_VOLUME:
	if (-1 == ioctl(h->mix,MIXER_READ(h->dev),&h->volume))
	    perror("oss mixer read volume");
	vol = h->volume & 0x7f;
	return vol;
    case ATTR_ID_MUTE:
	return h->muted;
    default:
	return -1;
    }
}

static void
mixer_write_attr(struct ng_attribute *attr, int val)
{
    struct mixer_handle *h = attr->handle;

    switch (attr->id) {
    case ATTR_ID_VOLUME:
	val &= 0x7f;
	h->volume = val | (val << 8);
	if (-1 == ioctl(h->mix,MIXER_WRITE(h->dev),&h->volume))
	    perror("oss mixer write volume");
	h->muted = 0;
	break;
    case ATTR_ID_MUTE:
	h->muted = val;
	if (h->muted) {
	    int zero = 0;
	    if (-1 == ioctl(h->mix,MIXER_READ(h->dev),&h->volume))
		perror("oss mixer read volume");
	    if (-1 == ioctl(h->mix,MIXER_WRITE(h->dev),&zero))
		perror("oss mixer write volume");
	} else {
	    if (-1 == ioctl(h->mix,MIXER_WRITE(h->dev),&h->volume))
		perror("oss mixer write volume");
	}
	break;
    }
}

struct ng_mix_driver oss_mixer = {
    name:  "oss",
    init:  mixer_open,
};

/* ------------------------------------------------------------------- */

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
	if (ng_debug)
	    fprintf(stderr,"oss: SNDCTL_DSP_SETFMT(%d): %s\n",
		    afmt_to_oss[fmt->fmtid],strerror(errno));
	goto err;
    }

    /* channels */
    ioctl(h->fd, SNDCTL_DSP_CHANNELS, &h->channels);
    if (h->channels != ng_afmt_to_channels[fmt->fmtid]) {
	if (ng_debug)
	    fprintf(stderr,"oss: SNDCTL_DSP_CHANNELS(%d): %s\n",
		    ng_afmt_to_channels[fmt->fmtid],strerror(errno));
	goto err;
    }

    /* sample rate */
    h->rate = fmt->rate;
    ioctl(h->fd, SNDCTL_DSP_SPEED, &h->rate);
    ioctl(h->fd, SNDCTL_DSP_SETFRAGMENT, &frag);
    if (h->rate != fmt->rate) {
	fprintf(stderr, "oss: warning: got sample rate %d (asked for %d)\n",
		h->rate,fmt->rate);
	if (h->rate < fmt->rate * 1001 / 1000 &&
	    h->rate > fmt->rate *  999 / 1000) {
	    /* ignore very small differences ... */
	    h->rate = fmt->rate;
	}
    }

    if (-1 == ioctl(h->fd, SNDCTL_DSP_GETBLKSIZE,  &h->blocksize)) {
	if (ng_debug)
	    perror("SNDCTL_DSP_GETBLKSIZE");
        goto err;
    }
    if (0 == h->blocksize)
	/* dmasound bug compatibility */
	h->blocksize = 4096;

    if (ng_debug)
	fprintf(stderr,"oss: bs=%d rate=%d channels=%d bits=%d (%s)\n",
		h->blocksize,h->rate,
		ng_afmt_to_channels[fmt->fmtid],
		ng_afmt_to_bits[fmt->fmtid],
		ng_afmt_to_desc[fmt->fmtid]);
    return 0;
    
 err:
    if (ng_debug)
	fprintf(stderr,"oss: sound format not supported [%s]\n",
		ng_afmt_to_desc[fmt->fmtid]);
    return -1;
}

static void*
oss_open(char *device, struct ng_audio_fmt *fmt)
{
    struct oss_handle *h;
    struct ng_audio_fmt ifmt;

    h = malloc(sizeof(*h));
    if (NULL == h)
	return NULL;
    memset(h,0,sizeof(*h));

    if (-1 == (h->fd = open(device ? device : ng_dev.dsp, O_RDONLY))) {
	fprintf(stderr,"oss: open %s: %s\n",
		device ? device : ng_dev.dsp,
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
	if (ng_debug)
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

static int
oss_startrec(void *handle)
{
    struct oss_handle *h = handle;
    int trigger;

    if (ng_debug)
	fprintf(stderr,"oss: startrec\n");
    trigger = 0;
    ioctl(h->fd,SNDCTL_DSP_SETTRIGGER,&trigger);

#if 1
    /*
     * Try to clear the sound driver buffers.  IMHO this shouldn't
     * be needed, but looks like it is with some drivers ...
     */
    {
	int oflags,flags,rc;
	unsigned char buf[4096];

	oflags = fcntl(h->fd,F_GETFL);
	flags = oflags | O_NONBLOCK;
	fcntl(h->fd,F_SETFL,flags);
	for (;;) {
	    rc = read(h->fd,buf,sizeof(buf));
	    if (ng_debug)
		fprintf(stderr,"oss: clearbuf rc=%d errno=%s\n",rc,strerror(errno));
	    if (rc != sizeof(buf))
		break;
	}
	fcntl(h->fd,F_SETFL,oflags);
    }
#endif

    trigger = PCM_ENABLE_INPUT;
    ioctl(h->fd,SNDCTL_DSP_SETTRIGGER,&trigger);
    return 0;
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
	    if (EINTR == errno)
		continue;
	    perror("oss: read");
	    exit(1);
	}
	count += rc;
	if (count == blocksize)
	    return;
    }
    fprintf(stderr,"#");
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

static struct ng_audio_buf*
oss_read(void *handle, long long stopby)
{
    struct oss_handle *h = handle;
    struct ng_audio_buf* buf;
    int bytes;

    if (stopby) {
	bytes = stopby * h->bytes_per_sec / 1000000000 - h->bytes;
	if (ng_debug)
	    fprintf(stderr,"oss: left: %d bytes (%.3fs)\n",
		    bytes,(float)bytes/h->bytes_per_sec);
	if (bytes <= 0)
	    return NULL;
	bytes = (bytes + 3) & ~3;
	if (bytes > h->blocksize)
	    bytes = h->blocksize;
    } else {
	bytes = h->blocksize;
    }
    buf = oss_bufalloc(&h->ofmt,bytes);
    oss_bufread(h->fd,buf->data,bytes);
    if (h->byteswap)
	oss_bufswap(buf->data,bytes);
    h->bytes += bytes;
    buf->info.ts = (long long)h->bytes * 1000000000 / h->bytes_per_sec;
    return buf;
}

static int
oss_fd(void *handle)
{
    struct oss_handle *h = handle;
    return h->fd;
}

static void
oss_close(void *handle)
{
    struct oss_handle *h = handle;

    close(h->fd);
    free(h);
}

/* ------------------------------------------------------------------- */

static struct ng_dsp_driver oss_dsp = {
    name:      "oss",
    open:      oss_open,
    close:     oss_close,
    fd:        oss_fd,
    startrec:  oss_startrec,
    read:      oss_read,
};

extern void ng_plugin_init(void);
void ng_plugin_init(void)
{
    ng_dsp_driver_register(NG_PLUGIN_MAGIC,PLUGNAME,&oss_dsp);
    ng_mix_driver_register(NG_PLUGIN_MAGIC,PLUGNAME,&oss_mixer);
}
