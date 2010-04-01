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

static int   fd, blocksize;

static int  mix;
static int  dev = -1;
static int  volume;
static int  muted;
extern int  debug;

/* -------------------------------------------------------------------- */

static const int afmt_to_oss[] = {
    0,
    AFMT_U8,
    AFMT_U8,
    AFMT_S16_LE,
    AFMT_S16_LE,
    AFMT_S16_BE,
    AFMT_S16_BE
};

int
sound_open(struct ng_audio_fmt *fmt)
{
    int afmt,channels,frag;
    
    if (-1 == (fd = open("/dev/dsp", O_RDONLY))) {
	perror("open /dev/dsp");
	goto err;
    }
    fcntl(fd,F_SETFD,FD_CLOEXEC);
    
    afmt     = afmt_to_oss[fmt->fmtid];
    channels = ng_afmt_to_channels[fmt->fmtid];
    frag     = 0x7fff000c; /* 4k */

    /* format */
    ioctl(fd, SNDCTL_DSP_SETFMT, &afmt);
    if (afmt != afmt_to_oss[fmt->fmtid]) {
	fprintf(stderr,"SNDCTL_DSP_SETFMT(%d): %s\n",
		afmt_to_oss[fmt->fmtid],strerror(errno));
	goto err;
    }

    /* channels */
    ioctl(fd, SNDCTL_DSP_CHANNELS, &channels);
    if (channels != ng_afmt_to_channels[fmt->fmtid]) {
	fprintf(stderr,"SNDCTL_DSP_CHANNELS(%d): %s\n",
		ng_afmt_to_channels[fmt->fmtid],strerror(errno));
	goto err;
    }

    /* sample rate */
    ioctl(fd, SNDCTL_DSP_SPEED, &fmt->rate);
    ioctl(fd, SNDCTL_DSP_SETFRAGMENT, &frag);

    if (-1 == ioctl(fd, SNDCTL_DSP_GETBLKSIZE,  &blocksize)) {
	perror("SNDCTL_DSP_GETBLKSIZE");
        goto err;
    }

    if (debug)
	fprintf(stderr,"sound rec: rate=%d channels=%d bits=%d (%s)\n",
		fmt->rate,
		ng_afmt_to_channels[fmt->fmtid],
		ng_afmt_to_bits[fmt->fmtid],
		ng_afmt_to_desc[fmt->fmtid]);
    return fd;
    
 err:
    fprintf(stderr,"oss: requested sound format not supported by driver\n");
    fmt->rate  = 0;
    fmt->fmtid = AUDIO_NONE;
    return -1;
}

int
sound_bufsize()
{
    return blocksize;
}

void
sound_startrec()
{
    int trigger;
    
    /* trigger record */
    trigger = PCM_ENABLE_INPUT;
    ioctl(fd,SNDCTL_DSP_SETTRIGGER,&trigger);
}

void
sound_read(char *buffer)
{
    if (blocksize != read(fd,buffer,blocksize)) {
	perror("read /dev/dsp");
	exit(1);
    }
}

void
sound_close()
{
    close(fd);
}

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
