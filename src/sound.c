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
	read:     mixer_read_attr,
	write:    mixer_write_attr,
    },{
	/* end of list */
    }
};

/* -------------------------------------------------------------------- */

struct ng_attribute*
mixer_open(char *filename, char *device)
{
    struct mixer_handle *h;
    struct ng_attribute *attrs;
    int i, devmask;

    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));
    h->mix = -1;
    h->dev = -1;

    if (-1 == (h->mix = open(filename,O_RDONLY))) {
	fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
	goto err;
    }
    fcntl(h->mix,F_SETFD,FD_CLOEXEC);

    if (-1 == ioctl(h->mix,MIXER_READ(SOUND_MIXER_DEVMASK),&devmask)) {
	fprintf(stderr,"%s: read devmask: %s",filename,strerror(errno));
	goto err;
    }
    for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
	if ((1<<i) & devmask && strcasecmp(names[i],device) == 0) {
	    if (-1 == ioctl(h->mix,MIXER_READ(i),&h->volume)) {
		fprintf(stderr,"%s: read volume: %s",
			filename,strerror(errno));
		goto err;
	    } else {
		h->dev = i;
	    }
	}
    }

    if (-1 == h->dev) {
	fprintf(stderr,"%s: '%s' not found.\n%s: available: ",
		filename,device,filename);
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
	vol = (h->volume & 0x7f) * 65535 / 100;
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
	val = val * 100 / 65535;
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
	if (debug)
	    fprintf(stderr,"oss: SNDCTL_DSP_SETFMT(%d): %s\n",
		    afmt_to_oss[fmt->fmtid],strerror(errno));
	goto err;
    }

    /* channels */
    ioctl(h->fd, SNDCTL_DSP_CHANNELS, &h->channels);
    if (h->channels != ng_afmt_to_channels[fmt->fmtid]) {
	if (debug)
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
	if (debug)
	    perror("SNDCTL_DSP_GETBLKSIZE");
        goto err;
    }
    if (0 == h->blocksize)
	/* dmasound bug compatibility */
	h->blocksize = 4096;

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

int
oss_startrec(void *handle)
{
    struct oss_handle *h = handle;
    int trigger;

    if (debug)
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
	    if (debug)
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

void
oss_levels(struct ng_audio_buf *buf, int *left, int *right)
{
    int lmax,rmax,i,level;
    signed char *s = buf->data;
    unsigned char *u = buf->data;

    lmax = 0;
    rmax = 0;
    switch (buf->fmt.fmtid) {
    case AUDIO_U8_MONO:
	i = 0;
	while (i < buf->size) {
	    level = abs((int)u[i++] - 128);
	    if (lmax < level)
		lmax = level, rmax = level;
	}
	break;
    case AUDIO_U8_STEREO:
	i = 0;
	while (i < buf->size) {
	    level = abs((int)u[i++] - 128);
	    if (lmax < level)
		lmax = level;
	    level = abs((int)u[i++] - 128);
	    if (rmax < level)
		rmax = level;
	}
	break;
    case AUDIO_S16_BE_MONO:
    case AUDIO_S16_LE_MONO:
	i = (AUDIO_S16_BE_MONO == buf->fmt.fmtid) ? 0 : 1;
	while (i < buf->size) {
	    level = abs((int)s[i]);
	    i += 2;
	    if (lmax < level)
		lmax = level, rmax = level;
	}
	break;
    case AUDIO_S16_LE_STEREO:
    case AUDIO_S16_BE_STEREO:
	i = (AUDIO_S16_BE_STEREO == buf->fmt.fmtid) ? 0 : 1;
	while (i < buf->size) {
	    level = abs((int)s[i]);
	    i += 2;
	    if (lmax < level)
		lmax = level;
	    level = abs((int)s[i]);
	    i += 2;
	    if (rmax < level)
		rmax = level;
	}
	break;
    }
    *left  = lmax;
    *right = rmax;
}

int
oss_fd(void *handle)
{
    struct oss_handle *h = handle;
    return h->fd;
}

void
oss_close(void *handle)
{
    struct oss_handle *h = handle;

    close(h->fd);
    free(h);
}
