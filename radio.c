/*
 * radio.c - (c) 1998 Gerd Knorr <kraxel@cs.tu-berlin.de>
 *
 * test tool for bttv + WinTV/Radio
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <asm/types.h>          /* XXX glibc */

#include "config.h"
#if USE_KERNEL_VIDEODEV
# include <linux/videodev.h>
#else
# include "videodev.h"
#endif

#define DEVICE "/dev/radio"              /* major=81, minor=64 */

int
radio_setfreq(int fd, float freq)
{
    int ifreq = freq*16;
    return ioctl(fd, VIDIOCSFREQ, &ifreq);
}

int
main(int argc, char *argv[])
{
    char   line[80];
    float  freq;
    int    fd;

    printf("bttv radio\n");
    if (-1 == (fd = open(DEVICE, O_RDONLY))) {
	perror("open " DEVICE);
	exit(1);
    }

    for (;;) {
	printf("freq? ");fflush(stdout);
	if (NULL == fgets(line,79,stdin))
	    break;
	if (1 != sscanf(line,"%f",&freq))
	    break;
	radio_setfreq(fd,freq);
    }
    return 0;
}
