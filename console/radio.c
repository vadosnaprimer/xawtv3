/*
 * radio.c - (c) 1998-2001 Gerd Knorr <kraxel@bytesex.org>
 *           (c) 2012 Hans de Goede <hdegoede@redhat.com>
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
 * 19 May 2012 - Hans de Goede - Add support for looping back sound using alsa
 * 28 May 2012 - Hans de Goede - Various UI improvements
 */

#include "config.h"

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

#include <linux/types.h>

#include "videodev2.h"
#include "alsa_stream.h"
#include "get_media_devices.h"

#define FREQ_MIN       (tuner.rangelow * (1e6 / freqfact))
#define FREQ_MAX       (tuner.rangehigh * (1e6 / freqfact))
#define FREQ_STEP      50000
#define FREQ_MIN_MHZ   ((float)tuner.rangelow / freqfact)
#define FREQ_MAX_MHZ   ((float)tuner.rangehigh / freqfact)
#define FREQ_STEP_MHZ  ((FREQ_STEP) / 1e6)

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

#define MAX_STATIONS 100

/* Latency is not a big problem for radio (no video to sync with), and
   USB radio devices benefit from a larger default latency */
#define DEFAULT_LATENCY 500

#if defined(HAVE_ALSA)
int alsa_loopback = 1;
char *alsa_playback = NULL;
char *alsa_capture = NULL;
int alsa_latency = DEFAULT_LATENCY;
#endif

/* JMMV: WINDOWS for radio */
int ncurses = 0;
int debug = 0;
char *device = "/dev/radio0";
WINDOW *wfreq, *woptions, *wstations, *wcommand, *whelp;
struct v4l2_tuner tuner;
int freqfact = 16; /* ffreq-in-Mhz * freqfact == v4l2-freq */

static int radio_getfreq(int fd, int *ifreq)
{
    struct v4l2_frequency frequency;

    memset (&frequency, 0, sizeof(frequency));
    frequency.type = V4L2_TUNER_RADIO;

    if (-1  == ioctl(fd, VIDIOC_G_FREQUENCY, &frequency)) {
	perror("VIDIOC_G_FREQUENCY");
	return errno;
    }

    *ifreq = frequency.frequency * (1e6 / freqfact);
    return 0;
}

static int radio_setfreq(int fd, int *ifreq)
{
    struct v4l2_frequency frequency;

    memset (&frequency, 0, sizeof(frequency));
    frequency.type = V4L2_TUNER_RADIO;
    frequency.frequency = *ifreq * (freqfact / 1e6);
    if (ioctl(fd, VIDIOC_S_FREQUENCY, &frequency) == -1) {
	perror("VIDIOC_S_FREQUENCY");
	return errno;
    }

    radio_getfreq(fd, ifreq);
    return 0;
}

/*
 * Perform a hw_seek, dir = 0 = down, 1 = up, Returns 0 and the new freq
 * in ifreq on a successful seek. Returns non 0 and leaves ifreq untouched
 * if the seek fails.
 */
static int radio_seek(int fd, int dir, int *ifreq)
{
    struct v4l2_hw_freq_seek seek;

    memset(&seek, 0, sizeof(seek));
    seek.type = V4L2_TUNER_RADIO;
    seek.seek_upward = dir;
    if (tuner.capability & V4L2_TUNER_CAP_HWSEEK_WRAP)
	seek.wrap_around = 1;
    seek.spacing = FREQ_STEP_MHZ * freqfact;
    if (seek.spacing < 1)
	seek.spacing = 1;
    if (-1  == ioctl(fd, VIDIOC_S_HW_FREQ_SEEK, &seek)) {
	if (errno != ENODATA)
	    perror("VIDIOC_G_FREQUENCY");
	return errno;
    }

    return radio_getfreq(fd, ifreq);
}

static void
radio_mute(int fd, int mute)
{
    struct v4l2_control ctrl;
    int res;

    memset (&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_AUDIO_MUTE;
    ctrl.value = mute;

    res = ioctl(fd, VIDIOC_S_CTRL, &ctrl);
    if (res == -1 && errno != EINVAL && errno != ENOTTY)
	perror("VIDIOC_S_CTRL");

#if defined(HAVE_ALSA)
    if (alsa_loopback) {
	if (mute)
	    alsa_thread_stop();
	else
	    alsa_thread_startup(alsa_playback, alsa_capture,
				alsa_latency, stderr, debug);
    }
#endif
}

static int
radio_getsignal_n_stereo(int fd)
{
    struct v4l2_tuner tuner;
    int i, asterisks;

    memset (&tuner, 0, sizeof(tuner));
    if (ioctl (fd, VIDIOC_G_TUNER, &tuner) == -1) {
	if (ncurses) {
	    mvwprintw(wfreq, 2, 1, "      ");
	    mvwprintw(wfreq, 3, 1, "      ");
	}
	perror("VIDIOC_G_TUNER");
	return 0;
    }

    if (ncurses) {
	mvwprintw(wfreq, 2, 1, (tuner.rxsubchans & V4L2_TUNER_SUB_STEREO) ?
		  "STEREO" : " MONO ");
	/* Draw 0 - 6 asterisks for signal strength */
	asterisks = (tuner.signal * 6 + 32767) / 65535;
	for (i = 0; i < 6; i++)
	    mvwprintw(wfreq, 3, i + 1, "%s", i < asterisks ? "*" : " ");
    }

    return tuner.signal;
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

int   fkeys[8];             /* Hotkey preset frequencies in Hz! */
int   freqs[MAX_STATIONS];  /* Preset frequencies in Hz! */
char *labels[MAX_STATIONS]; /* Preset labels */
int   stations;             /* Number of valid presets */

static char *find_label(int ifreq)
{
    int i;

    for (i = 0; i < stations; i++) {
	if (ifreq == freqs[i])
	    return labels[i];
    }
    return NULL;
}

char *digit[3][10] = {
   { " _ ", "   ", " _ ", " _ ", "   ", " _ ", " _ ", " _ ", " _ ", " _ " },
   { "| |", " | ", " _|", " _|", "|_|", "|_ ", "|_ ", "  |", "|_|", "|_|" },
   { "|_|", " | ", "|_ ", " _|", "  |", " _|", "|_|", "  |", "|_|", " _|" }
};

static void print_freq(int ifreq)
{
    int x,y,i;
    char *name, text[10];
    float ffreq;

    ffreq = ifreq / 1e6;
    sprintf(text, "%6.2f", ffreq);
    for (i = 0, x = 8; i < 6; i++, x+=4) {
	if (text[i] >= '0' && text[i] <= '9') {
	    for (y = 0; y < 3; y++)
		mvwprintw(wfreq, y + 1, x, "%s", digit[y][text[i] - '0']);
	} else if (text[i] == '.') {
	    mvwprintw(wfreq, 3, x, ".");
	    x -= 2;
	} else {
	    for (y = 0; y < 3; y++)
		mvwprintw(wfreq, y + 1, x, "   ");
	}
    }
    if (NULL != (name = find_label(ifreq)))
	mvwprintw(wfreq, 5, 2, "%-20.20s", name);
    else
	mvwprintw(wfreq, 5, 2, "%-20.20s", "");
    wrefresh(wfreq);
}

/* ---------------------------------------------------------------------- */

static void
read_kradioconfig(void)
{
    char   name[80],file[256],n;
    int    i, ifreq;
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
	} else if (2 == sscanf(file,"%d=%30[^\n]",&ifreq,name)) {
	    if (stations < MAX_STATIONS) {
		freqs[stations]  = ifreq;
		labels[stations] = strdup(name);
		stations++;
	    } else {
		fprintf(stderr,
			"Station limit (%d) exceeded, ignoring station '%s'\n",
			MAX_STATIONS, name);
	    }
	}
    }
    fclose(fp);

    /* If no hotkeys were specified, bind the first 8 presets to 1 - 8 */
    for (i = 0; i < 8; i++)
	if (fkeys[i])
	    break;
    if (i == 8) {
	for (i = 0; i < 8 && i < stations; i++)
	    fkeys[i] = freqs[i];
    }
}

static char *
make_label(int ifreq)
{
    static char text[20],*l;

    if (NULL != (l = find_label(ifreq)))
	return l;
    sprintf(text, "%6.2f MHz", ifreq / 1e6);
    return text;
}

/* ---------------------------------------------------------------------- */
/* autoscan                                                               */

float *g, baseline;
int g_len, astation[MAX_STATIONS], max_astation = 0, current_astation = -1;
int write_config;

static void
foundone(int m)
{
    int i, freq;

    for (i = 0; i < MAX_STATIONS && astation[i]; i++) {
	/* Assume stations less then 5 steps apart are the same station */
	if (abs(astation[i] - m) < 5)
	    break;
    }
    if (g[m] > g[astation[i]]) {  //  select bigger signal
	astation[i] = m;
	max_astation = i;
	freq = FREQ_MIN + m * FREQ_STEP;
	fprintf(stderr, "Station %2d: %6.2f MHz - %.2f\n", i, freq/1e6, g[m]);
	if (write_config)
	    printf("%d=scan-%d\n", freq, i);
    }
}

static void
maxi(int m)
{
    int i, l, r;
    float freq, halbwert;

    if (debug) {
	freq = FREQ_MIN_MHZ + m * FREQ_STEP_MHZ;
	fprintf(stderr,"maxi i %d %f %f\n", m, freq, g[m]);
    }
    if (g[m] < baseline)
	return;
    halbwert = (g[m] - baseline) / 2 + baseline;

    for(i = m; i > 0; i--)
	if (g[i] < halbwert)
	    break;
    l = i;
    if (debug) {
	freq = FREQ_MIN_MHZ + i * FREQ_STEP_MHZ;
	fprintf(stderr, "Left   i %d %f %f\n", i, freq, g[i]);
    }

    for(i = m; i < g_len; i++)
	if (g[i] < halbwert)
	    break;
    if (debug) {
	freq = FREQ_MIN_MHZ + i * FREQ_STEP_MHZ;
	fprintf(stderr, "Right  i %d %f %f\n", i, freq, g[i]);
    }
    r = i;
    m = (l + r) / 2;
    if (debug) {
	freq = FREQ_MIN_MHZ + m * FREQ_STEP_MHZ;
	fprintf(stderr, "Middle m %d %f %f\n", m, freq, g[m]);
    }
    foundone(m);
}

static void
findmax(void)
{
    int i;

    for (i = 0; i < g_len - 1; i++) {
	if (g[i + 1] < g[i])
	    maxi(i);
    }
}

// find the baseline for this tuners signal strength
static float
get_baseline(float ming, float maxg)
{
    int unt, i;
    float nullinie = 0, u;

    if (debug)
	fprintf(stderr, "get_baseline:  min=%f max=%f\n", ming, maxg);
    for (u = ming; u < maxg; u += 0.1) {
	unt = 0;
	for (i = 0; i < g_len; i++)
	    if (g[i] < u) {
		unt++;
	    }
	if (unt > (g_len * 7 / 8)) {
	    fprintf(stderr, "baseline at %.2f\n", u);
	    nullinie = u;
	    break;
	}
	if (debug)
	    fprintf(stderr, "%f %d\n", u, unt);
    }
    return nullinie;
}

static void
findstations(void)
{
    float maxg = 0, ming = 65536;
    int i;

    for (i = 0; i < g_len; i++) {
	if (g[i] < ming) ming = g[i];
	if (g[i] > maxg) maxg = g[i];
    }

    if (write_config)
	printf("[Stations]\n");
    baseline = get_baseline(ming, maxg);
    findmax();
}

static void do_scan(int fd,int scan)
{
    FILE * fmap=NULL;
    int i, j, s, ifreq;

    if (scan > 1)
	fmap = fopen("radio.fmmap","w");

    g_len = (FREQ_MAX - FREQ_MIN) / FREQ_STEP + 1;
    g = malloc(g_len * sizeof(float));
    for (i = 0; i < g_len; i++) {
	ifreq = FREQ_MIN + i * FREQ_STEP;
	s = 0;
	radio_setfreq(fd, &ifreq);
	usleep(10000); /* give the tuner some time to settle */
	for(j = 0; j < 5; j++) {
	    s += radio_getsignal_n_stereo(fd);
	    usleep(1000);
	}
	g[i] = s / 5.0; // average
	if (scan > 1)
	    fprintf(fmap, "%f %d\n", ifreq / 1e6, s);
	fprintf(stderr, "scanning: %6.2f MHz - %6d\r", ifreq / 1e6, s);
    }
    fprintf(stderr, "%40s\r", "");
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
#if defined(HAVE_ALSA)
            "  -l 0/1	loopback digital audio from radio  [default: 1]\n"
	    "  -r dev   record/capture device for loopback  [default: auto]\n"
	    "  -p dev   playback device for loopback  [default: default]\n"
	    "  -L ms    latency for loopback in ms [default: %d]\n"
#endif
	    "  -q       quit.  Useful with other options to control the\n"
	    "           radio device without entering interactive mode,\n"
	    "           i.e. \"radio -qf 91.4\"\n"
	    "\n"
	    "(c) 1998-2001 Gerd Knorr <kraxel@bytesex.org>\n"
	    "interface by Juli Merino <jmmv@mail.com>\n"
	    "channel scan by Gunther Mayer <Gunther.Mayer@t-online.de>\n",
	    device
#if defined(HAVE_ALSA)
	    , DEFAULT_LATENCY
#endif
	    );
}

static void redraw(void)
{
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
}

int
main(int argc, char *argv[])
{
    /* JMMV: lastfreq set to 1 to start radio at 0.0 */
    int    fd,key=0,done,i,ifreq = 0,lastfreq = 1, mute=1;
    /* Variables set by JMMV */
    float  ffreq, newfreq = 0;
    int    stset = 0, c;
    int    quit=0, scan=0, arg_mute=0;

    setlocale(LC_ALL,"");

    /* parse args */
    for (;;) {
#if defined(HAVE_ALSA)
	c = getopt(argc, argv, "mhiqdsSf:c:l:r:p:L:");
#else
	c = getopt(argc, argv, "mhiqdsSf:c:");
#endif
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
	    if (1 == sscanf(optarg, "%f", &ffreq))
		ifreq = ffreq * 1e6;
	    break;
	case 'c':
	    device = optarg;
	    break;
	case 'h':
	    usage(stdout);
	    exit(0);
#if defined(HAVE_ALSA)
	case 'l':
	    alsa_loopback = atoi(optarg);
	    break;
	case 'r':
	    alsa_capture = optarg;
	    break;
	case 'p':
	    alsa_playback = optarg;
	    break;
	case 'L':
	    alsa_latency = atoi(optarg);
	    break;
#endif
	default:
	    usage(stderr);
	    exit(1);
	}
    }

    if (-1 == (fd = open(device, O_RDONLY))) {
	fprintf(stderr,"open %s: %s\n",device,strerror(errno));
	exit(1);
    }

#if defined(HAVE_ALSA)
    if (alsa_loopback && alsa_capture == NULL) {
	void *md = discover_media_devices();
	char *p = strrchr(device, '/');
	if (p)
	    p++;
	else
	    p = device;
	alsa_capture = strdup(get_associated_device(md, NULL,
						    MEDIA_SND_CAP, p,
						    MEDIA_V4L_RADIO));
	if (alsa_capture == NULL)
	    alsa_loopback = 0;

	free_media_devices(md);
    }
    
    if (alsa_playback == NULL)
	alsa_playback = "default";

    /* Don't bother starting the loopback thread if we're going to quit */
    if (quit)
        alsa_loopback = 0;

    if (alsa_loopback)
	fprintf(stderr, "Using alsa loopback: cap: %s (%s), out: %s\n",
		alsa_capture, device, alsa_playback);
#endif

    memset(&tuner, 0, sizeof(tuner));
    if (ioctl(fd, VIDIOC_G_TUNER, &tuner) != 0) {
	perror("G_TUNER");
	return 1;
    }

    if (tuner.capability & V4L2_TUNER_CAP_LOW)
	freqfact = 16000;

    /* non-interactive stuff */
    if (scan) {
	do_scan(fd, scan);
	if (!ifreq && max_astation) {
	    current_astation = 0;
	    ifreq = FREQ_MIN + astation[current_astation] * FREQ_STEP;
	}
    }
    if (ifreq) {
	if (!radio_setfreq(fd, &ifreq))
	    fprintf(stderr, "tuned %.2f MHz\n", ifreq / 1e6);
	radio_mute(fd, 0);
    }
    if (arg_mute) {
	fprintf(stderr, "muted radio\n");
	radio_mute(fd, 1);
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
    i = 1;
    mvwprintw(woptions, i++, 1, "Up/Down     - inc/dec frequency");
    if (tuner.capability & (V4L2_TUNER_CAP_HWSEEK_BOUNDED |
			    V4L2_TUNER_CAP_HWSEEK_WRAP))
	mvwprintw(woptions, i++, 1, "Right/Left  - seek up/down");
    if (max_astation || stations)
	mvwprintw(woptions, i++, 1, "PgUp/PgDown - next/prev preset");
    mvwprintw(woptions, i++, 1, "g           - go to frequency...");
    if (i <= 4)
	    mvwprintw(woptions, i++, 1, "ESC, q, e   - mute and exit");
    if (i <= 4)
	    mvwprintw(woptions, i++, 1, "x           - exit (no mute)");
    mvwprintw(woptions, i++, 1, "h, ?        - help (more options)");
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

    if (!ifreq)
	radio_getfreq(fd, &ifreq);

    radio_mute(fd, 0);
    for (done = 0; done == 0;) {
	if (ifreq != lastfreq) {
	    if (!radio_setfreq(fd, &ifreq)) {
		print_freq(ifreq);
		lastfreq = ifreq;
	    } else
		ifreq = lastfreq;
	}
	radio_getsignal_n_stereo(fd);
	wrefresh(wfreq);
	wrefresh(wcommand);

	if (0 == select_wait(1)) {
	    mvwprintw(wcommand,1,1,"%50.50s","");
	    wrefresh(wcommand);
	    continue;
	}
	key = getch();

	if (whelp) {
	    delwin(whelp);
	    whelp = NULL;
	    redraw();
	    continue;
	}

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
	    if (newfreq >= FREQ_MIN_MHZ && newfreq <= FREQ_MAX_MHZ)
		ifreq = newfreq * 1e6;
	    else
		mvwprintw(wcommand, 1, 2,
			  "Frequency out of range (%.2f-%.2f MHz)",
			  FREQ_MIN_MHZ, FREQ_MAX_MHZ);
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
	case KEY_LEFT:
	    if (!(tuner.capability & (V4L2_TUNER_CAP_HWSEEK_BOUNDED |
				      V4L2_TUNER_CAP_HWSEEK_WRAP)))
		break;
	    mvwprintw(wcommand, 1, 2, "Seek down");
	    if (!radio_seek(fd, 0, &ifreq)) {
		print_freq(ifreq);
		lastfreq = ifreq;
	    }
	    break;
	case KEY_RIGHT:
	    if (!(tuner.capability & (V4L2_TUNER_CAP_HWSEEK_BOUNDED |
				      V4L2_TUNER_CAP_HWSEEK_WRAP)))
		break;
	    mvwprintw(wcommand, 1, 2, "Seek up");
	    if (!radio_seek(fd, 1, &ifreq)) {
		print_freq(ifreq);
		lastfreq = ifreq;
	    }
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
		ifreq = FREQ_MIN + astation[current_astation] * FREQ_STEP;
	    } else {
		for (i = 0; i < stations; i++) {
		    if (ifreq == freqs[i])
			break;
		}
		if (i != stations) {
		    i += (key == KEY_NPAGE) ? -1 : 1;
		    if (i < 0)
			i = stations - 1;
		    if (i >= stations)
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
	    redraw();
	    break;
	case 'h':
	case 'H':
	case '?':
	    whelp = newwin(12, 40, (LINES-12)/2, (COLS-40)/2);
	    box(whelp, 0, 0);
	    i = 0;
	    mvwprintw(whelp, i++, 1, " Help ");
	    mvwprintw(whelp, i++, 1, "Up/Down     - inc/dec frequency");
	    if (tuner.capability & (V4L2_TUNER_CAP_HWSEEK_BOUNDED |
				    V4L2_TUNER_CAP_HWSEEK_WRAP))
		mvwprintw(whelp, i++, 1, "Right/Left  - seek up/down");
	    mvwprintw(whelp, i++, 1, "PgUp/PgDown - next/prev station");
	    mvwprintw(whelp, i++, 1, "F1-F8, 1-8  - select preset 1 - 8");
	    mvwprintw(whelp, i++, 1, "g           - go to frequency...");
	    mvwprintw(whelp, i++, 1, "ESC, q, e   - mute and exit");
	    mvwprintw(whelp, i++, 1, "x           - exit (no mute)");
	    mvwprintw(whelp, i++, 1, "h, ?        - this help screen");
	    wrefresh(whelp);
	    break;
	}
    }
    if (mute)
	radio_mute(fd, 1);
    close(fd);

    bkgd(0);
    clear();
    refresh();
    endwin();
    return 0;
}
