#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/soundcard.h>

#include "mixer.h"

static char *names[] = SOUND_DEVICE_NAMES;

static int  mix;
static int  dev = -1;
static int  volume;
static int  muted;

int
mixer_open(char *filename, char *device)
{
    int i, devmask;

    if (-1 == (mix = open(filename,O_RDONLY))) {
	perror("mixer open");
	return -1;
    }
    if (-1 == ioctl(mix,MIXER_READ(SOUND_MIXER_DEVMASK),&devmask)) {
	perror("mixer read devmask");
	return -1;
    }
    for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
	if ((1<<i) & devmask && strcasecmp(names[i],device) == 0)
	    if (-1 == ioctl(mix,MIXER_READ(i),&volume)) {
		perror("mixer read volume");
		return -1;
	    } else {
		dev = i;
		muted = 0;
	    }
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
