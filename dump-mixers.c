#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

char *labels[] = SOUND_DEVICE_LABELS;
char *names[]  = SOUND_DEVICE_NAMES;

int
dump_mixer(char *devname)
{
    struct mixer_info info;
    int               mix,i,devmask,recmask,recsrc,stereomask,volume;

    if (-1 == (mix = open(devname,O_RDONLY)))
	return -1;

    printf("%s",devname);
    if (-1 != ioctl(mix,SOUND_MIXER_INFO,&info))
	printf(" = %s (%s)",info.id,info.name);
    printf("\n");

    if (-1 == ioctl(mix,MIXER_READ(SOUND_MIXER_DEVMASK),&devmask) ||
	-1 == ioctl(mix,MIXER_READ(SOUND_MIXER_STEREODEVS),&stereomask) ||
	-1 == ioctl(mix,MIXER_READ(SOUND_MIXER_RECMASK),&recmask) ||
	-1 == ioctl(mix,MIXER_READ(SOUND_MIXER_RECSRC),&recsrc)) {
	perror("mixer ioctl");
	return -1;
    }

    for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
	if ((1<<i) & devmask) {
	    if (-1 == ioctl(mix,MIXER_READ(i),&volume)) {
		perror("mixer read volume");
		return -1;
	    }
	    printf("  %-10s (%2d) :  %s  %s%s",
		   names[i],i,
		   (1<<i) & stereomask ? "stereo" : "mono  ",
		   (1<<i) & recmask    ? "rec"    : "   ",
		   (1<<i) & recsrc     ? "*"      : " ");
	    if ((1<<i) & stereomask)
		printf("  %d/%d\n",volume & 0xff,(volume >> 8) & 0xff);
	    else
		printf("  %d\n",volume & 0xff);
	}
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    char devname[32];
    int i;

    for (i = 0;; i++) {
	sprintf(devname,"/dev/mixer%d",i);
	if (-1 == dump_mixer(devname))
	    break;
    }
    return 0;
}
