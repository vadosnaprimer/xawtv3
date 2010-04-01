/*
 * radio.c - (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
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
#include <curses.h>
#include <sys/ioctl.h>

#include <asm/types.h>          /* XXX glibc */
#include "videodev.h"

#define DEVICE "/dev/radio"              /* major=81, minor=64 */

int
radio_setfreq(int fd, float freq)
{
    int ifreq = (freq+1.0/32)*16;
    return ioctl(fd, VIDIOCSFREQ, &ifreq);
}

void
radio_unmute(int fd)
{
    struct video_audio vid_aud;

    if (ioctl(fd, VIDIOCGAUDIO, &vid_aud))
	perror("VIDIOCGAUDIO");
    if (vid_aud.volume == 0)
	vid_aud.volume = 65535;
    vid_aud.flags &= ~VIDEO_AUDIO_MUTE;
    if (ioctl(fd, VIDIOCSAUDIO, &vid_aud))
	perror("VIDIOCSAUDIO");
}

void
radio_mute(int fd)
{
    struct video_audio vid_aud;

    if (ioctl(fd, VIDIOCGAUDIO, &vid_aud))
	perror("VIDIOCGAUDIO");
    vid_aud.flags |= VIDEO_AUDIO_MUTE;
    if (ioctl(fd, VIDIOCSAUDIO, &vid_aud))
	perror("VIDIOCSAUDIO");
}

/* ---------------------------------------------------------------------- */

char *digit[3][10] = {
   { " _ ", "   ", " _ ", " _ ", "   ", " _ ", " _ ", " _ ", " _ ", " _ " },
   { "| |", " | ", " _|", " _|", "|_|", "|_ ", "|_ ", "  |", "|_|", "|_|" },
   { "|_|", " | ", "|_ ", " _|", "  |", " _|", "|_|", "  |", "|_|", " _|" }
};

void print_freq(float freq)
{
    int x,y,i;
    char text[10];
    
    sprintf(text,"%5.1f",freq);
    for (i = 0, x = 0; i < 5; i++, x+=4) {
	if (text[i] >= '0' && text[i] <= '9') {
	    for (y = 0; y < 3; y++)
		mvprintw(y+1,x,"%s",digit[y][text[i]-'0']);
	} else if (text[i] == '.') {
	    mvprintw(3,x,".");
	    x -= 2;
	} else {
	    for (y = 0; y < 3; y++)
		mvprintw(y+1,x,"   ");
	}
    }
    refresh();
}

/* ---------------------------------------------------------------------- */

int   fkeys[8];

int   freqs[99];
char *labels[99];
int   stations;

void
read_kradioconfig()
{
    char   name[80],file[256],n;
    int    ifreq;
    FILE   *fp;

    sprintf(file,"%.225s/.kde/share/config/kradiorc",getenv("HOME"));
    if (NULL == (fp = fopen(file,"r")))
	return;
    while (NULL != fgets(file,255,fp)) {
	if (2 == sscanf(file,"%c=%d",&n,&ifreq) && n >= '1' && n <= '8') {
	    fkeys[n - '1'] = ifreq;
	} else if (2 == sscanf(file,"%d=%79[^\n]",&ifreq,name) && stations < 99) {
	    freqs[stations]  = ifreq;
	    labels[stations] = strdup(name);
	    stations++;
	}
    }
}

char*
find_label(int ifreq)
{
    int i;

    for (i = 0; i < stations; i++) {
	if (ifreq == freqs[i])
	    return labels[i];
    }
    return NULL;
}

char *
make_label(int ifreq)
{
    static char text[20],*l;

    if (NULL != (l = find_label(ifreq)))
	return l;
    sprintf(text,"%5.1f MHz",(float)ifreq/1000000);
    return text;
}

int
main(int argc, char *argv[])
{
    int    fd,key,done,i,nomute=0,ifreq = 0,lastfreq = 0;
    char   *name;
    float  ffreq;
    
    if (argc > 1 && 1 == sscanf(argv[1],"%f",&ffreq)) {
	ifreq = (int)(ffreq * 1000000);
	ifreq += 50000;
	ifreq -= ifreq % 100000;
    }

    if (-1 == (fd = open(DEVICE, O_RDONLY))) {
	perror("open " DEVICE);
	exit(1);
    }

    read_kradioconfig();
    if (!ifreq && fkeys[0])
	ifreq = fkeys[0];

    initscr();
    cbreak();
    noecho();
    keypad(stdscr,1);
    curs_set(0);

    for (i = 0; i < 8; i++) {
	if (fkeys[i])
	    mvprintw(i,22,"F%d: %s",i+1,make_label(fkeys[i]));
    }

    radio_unmute(fd);
    for (done = 0; done == 0;) {
	if (ifreq != lastfreq) {
	    lastfreq = ifreq;
	    ffreq = (float)ifreq / 1000000;
	    radio_setfreq(fd,ffreq);
	    print_freq(ffreq);
	    if (NULL != (name = find_label(ifreq)))
		mvprintw(0,0,"%-20.20s",name);
	    else
		mvprintw(0,0,"%-20.20s","");
	}
	
	switch (key = getch()) {
	case 'x':
	case 'X':
	    nomute=1;
	    /* fall */
	case EOF:
	case 27: /* ESC */
	case 'q':
	case 'Q':
	case 'e':
	case 'E':
	    done = 1;
	    break;
	case KEY_UP:
	    ifreq += 100000;
	    break;
	case KEY_DOWN:
	    ifreq -= 100000;
	    break;
	case KEY_F(1):
	case KEY_F(2):
	case KEY_F(3):
	case KEY_F(4):
	case KEY_F(5):
	case KEY_F(6):
	case KEY_F(7):
	case KEY_F(8):
	    if (fkeys[key - KEY_F(1)])
		ifreq = fkeys[key - KEY_F(1)];
	    break;	    
	}
    }
    if (!nomute)
	radio_mute(fd);
    close(fd);

    clear();
    refresh();
    endwin();

    return 0;
}
