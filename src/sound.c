#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <sys/soundcard.h>

#include "writeavi.h"
#include "sound.h"

/* -------------------------------------------------------------------- */

static char *names[] = SOUND_DEVICE_NAMES;

static int   fd, blocksize;
static char  *buffer;

static int  mix;
static int  dev = -1;
static int  volume;
static int  muted;

/* -------------------------------------------------------------------- */

int
sound_open(struct MOVIE_PARAMS *params)
{
    int afmt,trigger,frag;
    
    if (-1 == (fd = open("/dev/dsp", O_RDONLY))) {
	perror("open /dev/dsp");
	goto err;
    }
    fcntl(fd,F_SETFD,FD_CLOEXEC);
    
    /* format */
    switch (params->bits) {
    case 16:
	afmt = AFMT_S16_LE;
	ioctl(fd, SNDCTL_DSP_SETFMT, &afmt);
	if (afmt == AFMT_S16_LE)
	    break;
	fprintf(stderr,"no 16 bit sound, trying 8 bit...\n");
	params->bits = 8;
	/* fall */
    case 8:
	afmt = AFMT_U8;
	ioctl(fd, SNDCTL_DSP_SETFMT, &afmt);
	if (afmt != AFMT_U8) {
	    fprintf(stderr,"Oops: no 8 bit sound ?\n");
	    goto err;
	}
	break;
    default:
	fprintf(stderr,"%d bit sound not supported\n",
		params->bits);
	goto err;
    }

    frag = 0x7fff000c; /* 4k */
    ioctl(fd, SNDCTL_DSP_SETFRAGMENT, &frag);

    /* channels */
    ioctl(fd, SNDCTL_DSP_CHANNELS, &params->channels);
    /* sample rate */
    ioctl(fd, SNDCTL_DSP_SPEED,    &params->rate);

    if (-1 == ioctl(fd, SNDCTL_DSP_GETBLKSIZE,  &blocksize))
        goto err;
    buffer = malloc(blocksize);

    /* trigger record */
    trigger = ~PCM_ENABLE_INPUT;
    ioctl(fd,SNDCTL_DSP_SETTRIGGER,&trigger);
    trigger = PCM_ENABLE_INPUT;
    ioctl(fd,SNDCTL_DSP_SETTRIGGER,&trigger);

    return fd;
    
 err:
    params->channels = 0;
    params->bits     = 0;
    params->rate     = 0;
    return -1;
}

int
sound_bufsize()
{
    return blocksize;
}

void*
sound_read()
{
    if (blocksize != read(fd,buffer,blocksize)) {
	perror("read /dev/dsp");
	exit(1);
    }
    return buffer;
}

void
sound_close()
{
    free(buffer);
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
