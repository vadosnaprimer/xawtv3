/*
 * scan.c - (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 * test tool radio station scan
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
#include "../videodev.h"
#include "i2c.h"

#define DEVICE "/dev/radio"              /* major=81, minor=64 */

struct video_tuner t;

int
radio_setfreq(int fd, float freq)
{
    int ifreq = (freq+1.0/32)*16;
    return ioctl(fd, VIDIOCSFREQ, &ifreq);
}

int
main(int argc, char *argv[])
{
    unsigned char buf[16];
    int           freq,sfreq;
    int           fd,i2c,i,s,max;

    printf("bttv radio - station scan test (plays every station 10 sec)\n");
    if (-1 == (fd = open(DEVICE, O_RDONLY))) {
	perror("open " DEVICE);
	exit(1);
    }
    if (-1 == (i2c = open("/dev/i2c0",O_RDWR))) {
	perror("open /dev/i2c0");
	exit(1);
    }
    ioctl(i2c,I2C_SLAVE,0xc2>>1);

    for (freq = 8790, i = 0, max = 0; freq < 10800; freq += 5, i++) {
	radio_setfreq(fd,(float)freq/100);
	usleep(10*1000);
	read(i2c,buf+i%16,1);
	ioctl(fd,VIDIOCGTUNER,&t);
	printf("  ***  scan  %6.2f  %5d 0x%02x %*.*s\n",(float)freq/100,
	       t.signal,
	       buf[i%16],(buf[i%16]&7)+1,(buf[i%16]&7)+1,"--*++");
	fflush(stdout);
	buf[i%16] &= 7;

	if (buf[max%16] <= buf[i%16])
	    max = i;
	if (buf[i%16] > buf[(i+15)%16])
	    max = i;
	if (4 == buf[max%16]-buf[i%16]) {
	    for (s = i; buf[s%16] < 2; s--)
		;
	    sfreq = (freq-5*(i-s));
	    if (sfreq%10) /* adjust xx.x5 */
		sfreq -= 5;
	    max = i;

	    printf("  >>>  found %5.1f \n",(float)sfreq/100);
	    radio_setfreq(fd,(float)sfreq/100);
	    sleep(10);
	}
    }
    close(fd);
    return 0;
}
