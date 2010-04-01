/*
 * radio.c - (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 * test tool for bttv + WinTV/Radio
 *
 */

/* Changes:
 * 20 Jun 99 - Juli Merino (JMMV) <jmmv@mail.com> - Added some features:
 *             visual menu, manual 'go to' function, negative symbol and a
 *             good interface. See code for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <curses.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <asm/types.h>          /* XXX glibc */
#include "videodev.h"

#define DEVICE "/dev/radio"              /* major=81, minor=64 */

/* JMMV: WINDOWS for radio */
WINDOW *wfreq, *woptions, *wstations, *wcommand;

/* Determine and return the appropriate frequency multiplier for
   the first tuner on the open video device with handle FD. */
static int get_freq_fact(int fd) 
{
    struct video_tuner tuner;

    tuner.tuner = 0;
    if (ioctl (fd, VIDIOCGTUNER, &tuner) < 0)
	return 16;
    if ((tuner.flags & VIDEO_TUNER_LOW) == 0)
	return 16;
    return 16000;
}

int
radio_setfreq(int fd, float freq)
{
    int ifreq = (freq+1.0/32)*get_freq_fact(fd);
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

int
select_wait(int sec)
{
    struct timeval  tv;
    fd_set          se;
    
    FD_ZERO(&se);
    FD_SET(0,&se);
    tv.tv_sec = sec;
    tv.tv_usec = 0;
    return select(1,&se,NULL,NULL,&tv);
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
    for (i = 0, x = 12; i < 5; i++, x+=4) {
	if (text[i] >= '0' && text[i] <= '9') {
	    for (y = 0; y < 3; y++)
		mvwprintw(wfreq,y+1,x,"%s",digit[y][text[i]-'0']);
	} else if (text[i] == '.') {
	    mvwprintw(wfreq,3,x,".");
	    x -= 2;
	} else {
	    for (y = 0; y < 3; y++)
		mvwprintw(wfreq,y+1,x,"   ");
	}
    }
    wrefresh(wfreq);
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
    if (NULL == (fp = fopen(file,"r"))) {
	sprintf(file,"%.225s/.radio",getenv("HOME"));
	if (NULL == (fp = fopen(file,"r")))
	    return;
    }
    while (NULL != fgets(file,255,fp)) {
	if (2 == sscanf(file,"%c=%d",&n,&ifreq) && n >= '1' && n <= '8') {
	    fkeys[n - '1'] = ifreq;
	} else if (2 == sscanf(file,"%d=%30[^\n]",&ifreq,name) && stations < 99) {
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
    /* JMMV: lastfreq set to 1 to start radio at 0.0 */
    int    fd,key,done,i,ifreq = 0,lastfreq = 1, mute=1;
    char   *name;
    /* Variables set by JMMV */
    float  ffreq, newfreq = 0;
    int    stset = 0, c;
    
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
    start_color();
    cbreak();
    noecho();
    keypad(stdscr,1);
    curs_set(0);
    
    /* JMMV: Set colors and windows */
    /* XXX: Color definitions are wrong! BLUE is RED, CYAN is YELLOW and
     * viceversa */
    init_pair(1,COLOR_WHITE,COLOR_BLACK);
    init_pair(2,COLOR_CYAN,COLOR_BLUE);
    init_pair(3,COLOR_WHITE,COLOR_RED);
    bkgd(A_BOLD | COLOR_PAIR(1));
    refresh();

    wfreq = newwin(7,32,1,2);
    wbkgd(wfreq,A_BOLD | COLOR_PAIR(2));
    werase(wfreq);
    box(wfreq, 0, 0);
    mvwprintw(wfreq, 0, 1, " Tuner ");
    
    woptions = newwin(7,COLS-38,1,36);
    wbkgd(woptions,A_BOLD | COLOR_PAIR(3));
    werase(woptions);
    box(woptions, 0, 0);
    mvwprintw(woptions, 0, 1, " Main menu ");

    wstations = newwin(LINES-14,COLS-4,9,2);
    wbkgd(wstations,A_BOLD | COLOR_PAIR(3));
    werase(wstations);
    box(wstations, 0, 0);
    mvwprintw(wstations, 0, 1, " Preset stations ");
	
    wcommand = newwin(3,COLS-4,LINES-4,2);
    wbkgd(wcommand,A_BOLD | COLOR_PAIR(3));
    werase(wcommand);
    box(wcommand,0,0);
    mvwprintw(wcommand, 0, 1, " Command window ");
    wrefresh(wcommand);    
    
    /* JMMV: Added key information and windows division */
    mvwprintw(woptions, 1, 1, "UP Key    - increment frequency");
    mvwprintw(woptions, 2, 1, "DOWN Key  - decrease frequency");
    mvwprintw(woptions, 3, 1, "g         - go to frequency...");
    mvwprintw(woptions, 4, 1, "x         - exit");
    mvwprintw(woptions, 5, 1, "ESC, q, e - mute and exit");
    wrefresh(woptions);
    for (i = 0, c = 1; i < 8; i++) {
	if (fkeys[i]) {
	    mvwprintw(wstations,c,2,"F%d: %s",i+1,make_label(fkeys[i]));
	    c++;
	    stset = 1;
	}
    }
    if (!stset) mvwprintw(wstations,1,1,"[none]");
    wrefresh(wstations);
    
    radio_unmute(fd);
    for (done = 0; done == 0;) {
	if (ifreq != lastfreq) {
	    lastfreq = ifreq;
	    ffreq = (float)ifreq / 1000000;
	    radio_setfreq(fd,ffreq);
	    print_freq(ffreq);
	    if (NULL != (name = find_label(ifreq)))
		mvwprintw(wfreq,5,2,"%-20.20s",name);
	    else
		mvwprintw(wfreq,5,2,"%-20.20s","");
	    wrefresh(wfreq);
	}

	wrefresh(wcommand);
	select_wait(3);
	mvwprintw(wcommand,1,1,"%50.50s","");
	wrefresh(wcommand);
	key = getch();
	switch (key) {
	case EOF: /* for noninteractive use: "radio 95.8 <>/dev/null" */
	case 'x':
	case 'X':
	    mute = 0;
	    /* fall throuth */
	case 27: /* ESC */
	case 'q':
	case 'Q':
	case 'e':
	case 'E':
	    done = 1;
	    break;
	case 'g':
	case 'G':
	    /* JMMV: Added 'go to frequency' function */
	    mvwprintw(wcommand,1,2,"GO: Enter frequency: ");
	    curs_set(1);
	    echo();
	    wrefresh(wcommand);
	    wscanw(wcommand,"%f",&newfreq);
	    noecho();
	    curs_set(0);
	    wrefresh(wcommand);
	    ifreq = newfreq * 1000000;
	    break;
	case KEY_UP:
	    ifreq += 100000;
	    mvwprintw(wcommand, 1, 2, "Increment frequency");
	    break;
	case KEY_DOWN:
	    ifreq -= 100000;
	    mvwprintw(wcommand, 1, 2, "Decrease frequency");
	    break;
	case KEY_F(1):
	case KEY_F(2):
	case KEY_F(3):
	case KEY_F(4):
	case KEY_F(5):
	case KEY_F(6):
	case KEY_F(7):
	case KEY_F(8):
	    if (fkeys[key - KEY_F(1)]) {
		ifreq = fkeys[key - KEY_F(1)];
		mvwprintw(wcommand, 1, 2, "Go to preset station %d", key - KEY_F(0));
	    }
	    break;	    
	}
    }
    if (mute)
	radio_mute(fd);
    close(fd);

    bkgd(0);
    clear();
    refresh();
    endwin();
    printf("radio\n");
    printf("copyright (c) 1998-99 Gerd Knorr <kraxel@goldbach.in-berlin.de>\n");
    printf("interface by Juli Merino <jmmv@mail.com>\n");

    return 0;
}
