/*
 * radio.c - (c) 1998-2001 Gerd Knorr <kraxel@bytesex.org>
 *
 * test tool for bttv + WinTV/Radio
 *
 */

/* Changes:
 * 20 Jun 99 - Juli Merino (JMMV) <jmmv@mail.com> - Added some features:
 *             visual menu, manual 'go to' function, negative symbol and a
 *             good interface. See code for more details.
 * 30 Aug 2001 - Gunther Mayer <Gunther.Mayer@t-online.de>
 *             Scan for Stations, ad-hoc algorithm for signal strength
 *             analysis.  My Temic 4009FR5 finds all 19 stations here,
 *             a Samsung TPI8PSB02P misses two stations below 90MHz,
 *             which are received fine, but the tuner doesn't indicate
 *             signal strength.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <curses.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include "videodev.h"

#define FREQ_MIN    87500000
#define FREQ_MAX   108000000
#define FREQ_STEP      50000

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))

/* JMMV: WINDOWS for radio */
int ncurses = 0;
int debug = 0;
char *device = "/dev/radio";
WINDOW *wfreq, *woptions, *wstations, *wcommand;
int freqfact = 16;

static int
radio_setfreq(int fd, float freq)
{
    int ifreq = (freq + .5/freqfact) * freqfact;
    return ioctl(fd, VIDIOCSFREQ, &ifreq);
}

static int radio_getfreq(int fd, float *freq)
{
    int ioctl_status;
    int ifreq;
    ioctl_status = ioctl(fd,VIDIOCGFREQ, &ifreq);
    if (ioctl_status == -1)
        return ioctl_status;
    *freq = (float) ifreq / freqfact;
    return 0;
}

static void
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

static void
radio_mute(int fd)
{
    struct video_audio vid_aud;

    if (ioctl(fd, VIDIOCGAUDIO, &vid_aud))
	perror("VIDIOCGAUDIO");
    vid_aud.flags |= VIDEO_AUDIO_MUTE;
    if (ioctl(fd, VIDIOCSAUDIO, &vid_aud))
	perror("VIDIOCSAUDIO");
}

static void
radio_getstereo(int fd)
{
    struct video_audio va;
    va.mode=-1;

    if (!ncurses)
	return;
    
    if (ioctl (fd, VIDIOCGAUDIO, &va) < 0)
	mvwprintw(wfreq,2,1,"     ");
    mvwprintw(wfreq,2,1,"%s", va.mode == VIDEO_SOUND_STEREO ?
	      "STEREO":" MONO ");
}

static int
radio_getsignal(int fd)
{
    struct video_tuner vt;
    int i,signal;

    memset(&vt,0,sizeof(vt));
    ioctl (fd, VIDIOCGTUNER, &vt);
    signal=vt.signal>>13;

    if (!ncurses)
	return signal;

    for(i=0;i<8;i++)
        mvwprintw(wfreq,3,i+1,"%s", signal>i ? "*":" ");
    return signal;
}

static int
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

static void print_freq(float freq)
{
    int x,y,i;
    char text[10]; 
    sprintf(text,"%6.2f",freq);
    for (i = 0, x = 8; i < 6; i++, x+=4) {
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

static void
read_kradioconfig(void)
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

static char*
find_label(int ifreq)
{
    int i;

    for (i = 0; i < stations; i++) {
	if (ifreq == freqs[i])
	    return labels[i];
    }
    return NULL;
}

static char *
make_label(int ifreq)
{
    static char text[20],*l;

    if (NULL != (l = find_label(ifreq)))
	return l;
    sprintf(text,"%6.2f MHz",(float)ifreq/1000000);
    return text;
}

/* ---------------------------------------------------------------------- */
/* autoscan                                                               */

float g[411],baseline;
int astation[100],max_astation=0,current_astation=-1;
int write_config;

static void
foundone(int m)
{
    int i;
    
    for (i=0; i<100 && astation[i]; i++) {
        if(abs(astation[i]-m) <5 )  // 20 kHz width 
	    break;
    }
    if (g[m] > g[astation[i]]) {  //  select bigger signal
	astation[i]=m;
	max_astation=i;
	fprintf(stderr,"Station %2d: %6.2f MHz - %.2f\n",i,87.5+m*0.05,g[m]);
	if (write_config)
	    printf("%d0000=scan-%d\n",(int)((87.5+m*0.05)*100),i);
    }
}

static void 
maxi(int m)
{
    int i,l,r;
    float halbwert;

    if (debug)
	fprintf(stderr,"maxi i %d %f %f\n",m,87.5+m*0.05,g[m]);
    if(g[m]<baseline)
        return;
    halbwert=(g[m]-baseline)/2+baseline;
    
    for(i=m;i>0;i--)
	if(g[i]< halbwert)
	    break;
    l=i;
    if (debug)
	fprintf(stderr,"Left   i %d %f %f\n",i,87.5+i*0.05,g[i]);
    
    for(i=m;i<411;i++)
	if(g[i]< halbwert)
	    break;
    if (debug)
	fprintf(stderr,"Right  i %d %f %f\n",i,87.5+i*0.05,g[i]);
    r=i;
    m=(l+r)/2;
    if (debug)
	fprintf(stderr,"Middle %d %f %f\n",m,87.5+m*0.05,g[m]);
    foundone(m);
}

static void 
findmax(void)
{
    int i;
    
    for (i = 0; i < ARRAY_SIZE(g)-1; i++){
        if (g[i+1] < g[i])
	    maxi(i);
    }
}

// find the baseline for this tuners signal strength
static float
get_baseline(float ming, float maxg)
{
    int unt,i,nullfound=0;
    float nullinie=0,u;

    if (debug)
	fprintf(stderr,"get_baseline:  min=%f max=%f\n",ming,maxg);
    for(u=ming;u<maxg; u+=0.1) {
	unt=0;
	for (i=0; i< ARRAY_SIZE(g); i++)
	    if (g[i] < u) {
		unt++;
	    }
	if(unt>300 && !nullfound) {
	    fprintf(stderr,"baseline at %.2f\n",u);
	    nullinie=u;
	    nullfound=1;
	}
	if (debug)
	    fprintf(stderr,"%f %d\n",u,unt);
    }
    return nullinie;
}

static void 
findstations(void)
{
    float maxg=0,ming=8;
    int i;

    for (i=0; i< ARRAY_SIZE(g); i++) {
	if (g[i]<ming) ming=g[i];
	if (g[i]>maxg) maxg=g[i];
    }

    if (write_config)
	printf("[Stations]\n");
    baseline=get_baseline(ming,maxg);
    findmax();
}

static void do_scan(int fd,int scan)
{
    FILE * fmap=NULL;
    float freq,s; 
    int i,j;

    if(scan > 1)
	fmap=fopen("radio.fmmap","w");
    for (i=0; i< ARRAY_SIZE(g); i++) {
	freq = (FREQ_MIN + i * FREQ_STEP)/1e6;
	s = 0;
	radio_setfreq(fd,freq);
	usleep(10000); /* give the tuner some time to settle */
	for(j=0;j<5;j++) {
	    s+=radio_getsignal(fd);
	    radio_getstereo(fd);
	    usleep(1000);
	}
	g[i]=s/5; // average
	if (scan > 1)
	    fprintf(fmap,"%f %f\n", freq,s);
	fprintf(stderr,"scanning: %6.2f MHz - %.2f\r", freq,s);
    }
    fprintf(stderr,"%40s\r","");
    if (scan > 1)
	fclose(fmap);
    findstations();
}

/* ---------------------------------------------------------------------- */

static void
usage(FILE *out)
{
    fprintf(out,
	    "radio -- interactive ncurses radio application\n"
	    "usage:\n"
	    "  radio [ options ]\n"
	    "\n"
	    "options:\n"
	    "  -h       print this text\n"
	    "  -d       enable debug output\n"
	    "  -m       mute radio\n"
	    "  -f freq  tune given frequency (also unmutes)\n"
	    "  -c dev   use given device        [default: %s]\n"
	    "  -s       scan\n"
	    "  -S       scan + write radio.fmmap\n"
	    "  -i       scan, write initial ~/.radio config file to\n"
	    "           stdout and quit\n"
	    "  -q       quit.  Useful with other options to control the\n"
	    "           radio device without entering interactive mode,\n"
	    "           i.e. \"radio -qf 91.4\"\n"
	    "\n"
	    "(c) 1998-2001 Gerd Knorr <kraxel@bytesex.org>\n"
	    "interface by Juli Merino <jmmv@mail.com>\n"
	    "channel scan by Gunther Mayer <Gunther.Mayer@t-online.de>\n",
	    device);
}

int
main(int argc, char *argv[])
{
    /* JMMV: lastfreq set to 1 to start radio at 0.0 */
    int    fd,key=0,done,i,ifreq = 0,lastfreq = 1, mute=1;
    char   *name;
    /* Variables set by JMMV */
    float  ffreq, newfreq = 0;
    int    stset = 0, c;
    int    quit=0, scan=0, arg_mute=0;
    struct video_tuner tuner;

    setlocale(LC_ALL,"");

    /* parse args */
    for (;;) {
	c = getopt(argc, argv, "mhiqdsSf:c:");
	if (c == -1)
	    break;
	switch (c) {
	case 'm':
	    arg_mute = 1;
	    break;
	case 'q':
	    quit = 1;
	    break;
	case 'd':
	    debug= 1;
	    break;
	case 'S':
	    scan = 2;
	    break;
	case 's':
	    scan = 1;
	    break;
	case 'i':
	    write_config = 1;
	    scan = 1;
	    quit = 1;
	    break;
	case 'f':
	    if (1 == sscanf(optarg,"%f",&ffreq)) {
		ifreq = (int)(ffreq * 1000000);
		ifreq += FREQ_STEP/2;
		ifreq -= ifreq % FREQ_STEP;
	    }
	    break;
	case 'c':
	    device = optarg;
	    break;
	case 'h':
	    usage(stdout);
	    exit(0);
	default:
	    usage(stderr);
	    exit(1);
	}
    }
    
    if (-1 == (fd = open(device, O_RDONLY))) {
	fprintf(stderr,"open %s: %s\n",device,strerror(errno));
	exit(1);
    }

    memset(&tuner,0,sizeof(tuner));
    if (0 == ioctl(fd, VIDIOCGTUNER, &tuner) &&
	(tuner.flags & VIDEO_TUNER_LOW))
	freqfact = 16000;

    /* non-interactive stuff */
    if (scan) {
	do_scan(fd,scan);
	if (!ifreq  &&  max_astation) {
	    current_astation = 0;
            ifreq = FREQ_MIN + astation[current_astation]*50000;
	}
    }
    if (ifreq) {
	ffreq = (float)ifreq / 1000000;
	fprintf(stderr,"tuned %.2f MHz\n",ffreq);
	radio_setfreq(fd,ffreq);
	radio_unmute(fd);
    }
    if (arg_mute) {
	fprintf(stderr,"muted radio\n");
	radio_mute(fd);
    }
    if (quit)
	exit(0);

    read_kradioconfig();
    if (!ifreq && fkeys[0])
	ifreq = fkeys[0];

    /* enter interactive mode -- init ncurses */
    ncurses=1;
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
    mvwprintw(woptions, 1, 1, "Up/Down     - inc/dec frequency");
    mvwprintw(woptions, 2, 1, "PgUp/PgDown - next/prev station");
    mvwprintw(woptions, 3, 1, "g           - go to frequency...");
    mvwprintw(woptions, 4, 1, "x           - exit");
    mvwprintw(woptions, 5, 1, "ESC, q, e   - mute and exit");
    wrefresh(woptions);
    for (i = 0, c = 1; i < 8; i++) {
	if (fkeys[i]) {
	    mvwprintw(wstations,c,2,"F%d: %s",i+1,make_label(fkeys[i]));
	    c++;
	    stset = 1;
	}
    }
    if (!stset)
	mvwprintw(wstations,1,1,"[none]");
    wrefresh(wstations);

    if (ifreq == 0) {
	float ffreq;
	radio_getfreq(fd,&ffreq);
	ifreq = ffreq * 1000000;
    }
    
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
	}
	radio_getstereo(fd);
	radio_getsignal(fd);
	wrefresh(wfreq);
	wrefresh(wcommand);

	if (0 == select_wait(1)) {
	    mvwprintw(wcommand,1,1,"%50.50s","");
	    wrefresh(wcommand);
	    continue;
	}
	key = getch();
	switch (key) {
	case EOF:
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
	    if ((newfreq >= FREQ_MIN/1e6) && (newfreq <= FREQ_MAX/1e6) )
		ifreq = newfreq * 1000000;
	    else
		mvwprintw(wcommand, 1, 2,
			  "Frequency out of range (87.5-108 MHz)");
	    break;
	case KEY_UP:
            ifreq += FREQ_STEP;
            if (ifreq > FREQ_MAX)
		ifreq = FREQ_MIN;
	    mvwprintw(wcommand, 1, 2, "Increment frequency");
	    break;
	case KEY_DOWN:
            ifreq -= FREQ_STEP;
            if (ifreq < FREQ_MIN)
		ifreq = FREQ_MAX;
	    mvwprintw(wcommand, 1, 2, "Decrease frequency");
	    break;
	case KEY_PPAGE:
	case KEY_NPAGE:
	case ' ':
	    if (max_astation) {
		current_astation += (key == KEY_NPAGE) ? -1 : 1;
		if(current_astation<0)
		    current_astation=max_astation;
		if(current_astation>max_astation)
		    current_astation=0;
		ifreq=FREQ_MIN+astation[current_astation]*FREQ_STEP;
	    } else {
		for (i = 0; i < stations; i++) {
		    if (ifreq == freqs[i])
			break;
		}
		if (i != stations) {
		    i += (key == KEY_NPAGE) ? -1 : 1;
		    if (i < 0 || i >= stations)
			i = 0;
		    ifreq = freqs[i];
		}
	    }
	    break;
        case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case KEY_F(1):
	case KEY_F(2):
	case KEY_F(3):
	case KEY_F(4):
	case KEY_F(5):
	case KEY_F(6):
	case KEY_F(7):
	case KEY_F(8):
	    i = (key >= '1' && key <= '8')  ?  key - '1' : key - KEY_F(1);
	    if (fkeys[i]) {
		ifreq = fkeys[i];
		mvwprintw(wcommand, 1, 2, "Go to preset station %d", i+1);
	    }
	    break;
	case 'L' & 0x1f:  /* Ctrl-L */
	    redrawwin(stdscr);
	    redrawwin(wfreq);
	    redrawwin(woptions);
	    redrawwin(wstations);
	    redrawwin(wcommand);
	    wrefresh(stdscr);
	    wrefresh(wfreq);
	    wrefresh(woptions);
	    wrefresh(wstations);
	    wrefresh(wcommand);
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
    return 0;
}
