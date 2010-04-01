#if 0
set -x
gcc -o record $0 -lncurses
exit
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ncurses.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <sys/ioctl.h>
#include <linux/soundcard.h>

/* -------------------------------------------------------------------- */

void
tty_raw()
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr,1);
    refresh();
}

void
tty_restore()
{
    endwin();
}

/* -------------------------------------------------------------------- */

static int     sound_fd;
static int     sound_blksize;
static short  *sound_buffer;
static int     maxl,maxr;
static int     secl,secr;
static struct  timeval tl,tr;

int
sound_open()
{
    int frag,afmt,channels,rate,trigger;
    
    if (-1 == (sound_fd = open("/dev/dsp", O_RDONLY))) {
	perror("open /dev/dsp");
	exit(1);
    }

    frag = 0x7fff000d; /* 8k */
    if (-1 == ioctl(sound_fd, SNDCTL_DSP_SETFRAGMENT, &frag))
        perror("ioctl SNDCTL_DSP_SETFRAGMENT");

    /* format */
    afmt = AFMT_S16_LE;
    if (-1 == ioctl(sound_fd, SNDCTL_DSP_SETFMT, &afmt)) {
	perror("ioctl SNDCTL_DSP_SETFMT");
	exit(1);
    }
    if (afmt != AFMT_S16_LE) {
	fprintf(stderr,"can't set sound format to 16 bit (le)\n");
	exit(1);
    }

    /* channels */
    channels = 2;
    if (-1 == ioctl(sound_fd, SNDCTL_DSP_CHANNELS, &channels)) {
	perror("ioctl SNDCTL_DSP_CHANNELS");
	exit(1);
    }
    if (channels != 2) {
	fprintf(stderr,"can't record in stereo\n");
	exit(1);
    }

    /* rate */
    rate = 44100;
    if (-1 == ioctl(sound_fd, SNDCTL_DSP_SPEED, &rate)) {
	perror("ioctl SNDCTL_DSP_SPEED");
	exit(1);
    }
    rate += 50;
    rate -= rate % 100;
    if (rate != 44100) {
	fprintf(stderr,"can't set sample rate to 44100 (got %d)\n",rate);
	exit(1);
    }

    /* get block size */
    if (-1 == ioctl(sound_fd, SNDCTL_DSP_GETBLKSIZE,  &sound_blksize)) {
	perror("ioctl SNDCTL_DSP_GETBLKSIZE");
	exit(1);
    }
    sound_buffer = malloc(sound_blksize);

    /* trigger record */
    trigger = ~PCM_ENABLE_INPUT;
    ioctl(sound_fd,SNDCTL_DSP_SETTRIGGER,&trigger);
    trigger = PCM_ENABLE_INPUT;
    ioctl(sound_fd,SNDCTL_DSP_SETTRIGGER,&trigger);

    return sound_fd;
}

void
sound_read()
{
    struct  timeval now;
    int     i;
    short  *v;

    /* read */
    if (sound_blksize != read(sound_fd,sound_buffer,sound_blksize)) {
	perror("read /dev/dsp");
	exit(1);
    }

    /* look for peaks */
    maxl = 0;
    maxr = 0;
    for (i = sound_blksize>>2, v=sound_buffer; i > 0; i--) {
	if (abs(*v) > maxl)
	    maxl = abs(*v);
	v++;
	if (abs(*v) > maxr)
	    maxr = abs(*v);
	v++;	
    }

    /* max for the last second */
    gettimeofday(&now,NULL);
    if ((tl.tv_sec == now.tv_sec-1 && tl.tv_usec < now.tv_usec) ||
	(tl.tv_sec  < now.tv_sec-1)) {
	secl = 0;
    }
    if ((tr.tv_sec == now.tv_sec-1 && tr.tv_usec < now.tv_usec) ||
	(tr.tv_sec  < now.tv_sec-1)) {
	secr = 0;
    }
    if (secl < maxl) {
	secl = maxl;
	tl   = now;
    }
    if (secr < maxr) {
	secr = maxr;
	tr   = now;
    }
}

/* -------------------------------------------------------------------- */

char *names[SOUND_MIXER_NRDEVICES] = SOUND_DEVICE_NAMES;
char *config_names[SOUND_MIXER_NRDEVICES][4];

static int  mix;
static int  dev = -1;
static int  volume;

int
mixer_open(char *filename, char *device)
{
    int i, devmask;

    if (-1 == (mix = open(filename,O_RDONLY))) {
	perror("mixer open");
	exit(1);
    }
    if (-1 == ioctl(mix,MIXER_READ(SOUND_MIXER_DEVMASK),&devmask)) {
	perror("mixer read devmask");
	exit(1);
    }
    for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
	if ((1<<i) & devmask && strcasecmp(names[i],device) == 0) {
	    if (-1 == ioctl(mix,MIXER_READ(i),&volume)) {
		perror("mixer read volume");
		exit(1);
	    } else {
		dev = i;
	    }
	}
    }
    if (-1 == dev) {
	fprintf(stderr,"mixer: hav'nt found device '%s'\nmixer: available: ",device);
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
	    if ((1<<i) & devmask)
		fprintf(stderr," '%s'",names[i]);
	fprintf(stderr,"\n");
	exit(1);
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
    return 0;
}

/* ---------------------------------------------------------------------- */
/* *.wav I/O stolen from cdda2wav */

/* Copyright (C) by Heiko Eissfeldt */

typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned long  DWORD;
typedef unsigned long  FOURCC;	/* a four character code */

/* flags for 'wFormatTag' field of WAVEFORMAT */
#define WAVE_FORMAT_PCM 1

/* MMIO macros */
#define mmioFOURCC(ch0, ch1, ch2, ch3) \
  ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
  ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))

#define FOURCC_RIFF	mmioFOURCC ('R', 'I', 'F', 'F')
#define FOURCC_LIST	mmioFOURCC ('L', 'I', 'S', 'T')
#define FOURCC_WAVE	mmioFOURCC ('W', 'A', 'V', 'E')
#define FOURCC_FMT	mmioFOURCC ('f', 'm', 't', ' ')
#define FOURCC_DATA	mmioFOURCC ('d', 'a', 't', 'a')

typedef struct CHUNKHDR {
    FOURCC ckid;		/* chunk ID */
    DWORD dwSize; 	        /* chunk size */
} CHUNKHDR;

/* simplified Header for standard WAV files */
typedef struct WAVEHDR {
    CHUNKHDR chkRiff;
    FOURCC fccWave;
    CHUNKHDR chkFmt;
    WORD wFormatTag;	   /* format type */
    WORD nChannels;	   /* number of channels (i.e. mono, stereo, etc.) */
    DWORD nSamplesPerSec;  /* sample rate */
    DWORD nAvgBytesPerSec; /* for buffer estimation */
    WORD nBlockAlign;	   /* block size of data */
    WORD wBitsPerSample;
    CHUNKHDR chkData;
} WAVEHDR;

#define IS_STD_WAV_HEADER(waveHdr) ( \
  waveHdr.chkRiff.ckid == FOURCC_RIFF && \
  waveHdr.fccWave == FOURCC_WAVE && \
  waveHdr.chkFmt.ckid == FOURCC_FMT && \
  waveHdr.chkData.ckid == FOURCC_DATA && \
  waveHdr.wFormatTag == WAVE_FORMAT_PCM)

#define cpu_to_le32(x) (x)
#define cpu_to_le16(x) (x)
#define le32_to_cpu(x) (x)
#define le16_to_cpu(x) (x)

/* -------------------------------------------------------------------- */

static WAVEHDR  fileheader;
static int      wav_size;

void
wav_init_header()
{
    /* stolen from cdda2wav */
    int nBitsPerSample = 16;
    int channels = 2;
    int rate = 44100;

    unsigned long nBlockAlign = channels * ((nBitsPerSample + 7) / 8);
    unsigned long nAvgBytesPerSec = nBlockAlign * rate;
    unsigned long temp = /* data length */ 0 +
	sizeof(WAVEHDR) - sizeof(CHUNKHDR);

    fileheader.chkRiff.ckid    = cpu_to_le32(FOURCC_RIFF);
    fileheader.fccWave         = cpu_to_le32(FOURCC_WAVE);
    fileheader.chkFmt.ckid     = cpu_to_le32(FOURCC_FMT);
    fileheader.chkFmt.dwSize   = cpu_to_le32(16);
    fileheader.wFormatTag      = cpu_to_le16(WAVE_FORMAT_PCM);
    fileheader.nChannels       = cpu_to_le16(channels);
    fileheader.nSamplesPerSec  = cpu_to_le32(rate);
    fileheader.nAvgBytesPerSec = cpu_to_le32(nAvgBytesPerSec);
    fileheader.nBlockAlign     = cpu_to_le16(nBlockAlign);
    fileheader.wBitsPerSample  = cpu_to_le16(nBitsPerSample);
    fileheader.chkData.ckid    = cpu_to_le32(FOURCC_DATA);
    fileheader.chkRiff.dwSize  = cpu_to_le32(temp);
    fileheader.chkData.dwSize  = cpu_to_le32(0 /* data length */);
}

void
wav_start_write(int fd)
{
    wav_init_header();
    lseek(fd,0,SEEK_SET);
    write(fd,&fileheader,sizeof(WAVEHDR));
    wav_size = 0;
}

int
wav_write_audio(int fd, void *data, int len)
{
    int rc;

    rc = write(fd,data,len);
    if (len == rc) {
	wav_size += len;
	return 0;
    } else
	return -1;
}

void
wav_stop_write(int fd)
{
    unsigned long temp = wav_size + sizeof(WAVEHDR) - sizeof(CHUNKHDR);

    fileheader.chkRiff.dwSize = cpu_to_le32(temp);
    fileheader.chkData.dwSize = cpu_to_le32(wav_size);
    lseek(fd,0,SEEK_SET);
    write(fd,&fileheader,sizeof(WAVEHDR));
}

/* -------------------------------------------------------------------- */

static char full[] =
"##################################################"
"##################################################"
"##################################################"
"##################################################";

static char empty[] =
"--------------------------------------------------"
"--------------------------------------------------"
"--------------------------------------------------"
"--------------------------------------------------";

void
print_bar(int line, char *name, int val1, int val2, int max)
{
    int total,len;
    
    total = COLS-16;
    len   = val1*total/max;

    mvprintw(line,0,"%-6s: %5d  ",name,(val2 != -1) ? val2 : val1);
    printw("%*.*s",len,len,full);
    printw("%*.*s",total-len,total-len,empty);
    if (val2 != -1)
	mvprintw(line,14+val2*total/max,"|");
}

/* -------------------------------------------------------------------- */

char      *progname;
int       sig,verbose;
char      *input    = "line";
char      *filename = "new";

void
ctrlc(int signal)
{
    sig=1;
}

void
usage()
{
    fprintf(stderr,
	    "%s records sound in CD-Quality (44100/16bit/stereo).\n"
	    "It has a nice ascii-art input-level meter.  It is a\n"
	    "interactive curses application.  You'll need a fast\n"
	    "terminal, don't try this on a 9600 bps vt100...\n"
	    "\n"
	    "%s has three options:\n"
	    "  -h       this text\n"  
	    "  -i dev   mixer device [%s].  This should be the one\n"
	    "           where you can adjust the record level for\n"
	    "           your audio source.\n"
	    "  -o file  output file name [%s], a number and the .wav\n"
	    "           extention are added by %s.\n"
	    "\n",
	    progname,progname,input,filename,progname);
}

int
main(int argc, char *argv[])
{
    int             c,key,vol,delay,auto_adjust;
    int             record,nr,wav=0;
    char            *outfile;
    fd_set          s;

    progname = strrchr(argv[0],'/');
    progname = progname ? progname+1 : argv[0];

    /* parse options */
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "vhi:o:")))
	    break;
	switch (c) {
	case 'v':
	    verbose = 1;
	    break;
	case 'i':
	    input = optarg;
	    break;
	case 'o':
	    filename = optarg;
	    break;
	case 'h':
	default:
	    usage();
	    exit(1);
	}
    }

    mixer_open("/dev/mixer",input);
    sound_open();
    delay = 0;
    auto_adjust = 1;
    record = 0;
    nr = 0;
    outfile = malloc(strlen(filename)+16);
    
    tty_raw();
    atexit(tty_restore);

    signal(SIGINT,ctrlc);
    signal(SIGQUIT,ctrlc);
    signal(SIGTERM,ctrlc);
    signal(SIGHUP,ctrlc);

    mvprintw( 5,0,"record to   %s*.wav",filename);
    mvprintw( 7,0,"left/right  adjust mixer level for \"%s\"",input);
    mvprintw( 8,0,"space       starts/stops recording");
    /* line 9 is printed later */
    mvprintw(10,0,"            auto-adjust reduces the record level on overruns");
    mvprintw(11,0,"'Q'         quit");
    mvprintw(LINES-3,0,"--");
    mvprintw(LINES-2,0,"(c) 1999 Gerd Knorr <kraxel@goldbach.in-berlin.de>");
    
    for (;!sig;) {
	FD_ZERO(&s);
	FD_SET(0,&s);
	FD_SET(sound_fd,&s);
	select(sound_fd+1,&s,NULL,NULL,NULL);

	if (FD_ISSET(sound_fd,&s)) {
	    /* sound */
	    sound_read();
	    if (delay)
		delay--;
	    if (auto_adjust && (0 == delay) && (maxl >= 32767 || maxr >= 32767)) {
		/* auto-adjust */
		vol = mixer_get_volume();
		vol--;
		if (vol < 0)
		    vol = 0;
		mixer_set_volume(vol);
		delay = 3;
	    }
	    print_bar(0,input,mixer_get_volume(),-1,100);
	    print_bar(1,"left",maxl,secl,32768);
	    print_bar(2,"right",maxr,secr,32768);
	    mvprintw(9,0,"'A'         toggle auto-adjust [%s] ",auto_adjust ? "on" : "off");
	    if (record) {
		int sec = wav_size / (44100*4);
		mvprintw(3,0,"%s: %3d:%02d (%d MB) ",outfile,sec/60,sec%60,wav_size/(1024*1024));
		wav_write_audio(wav,sound_buffer,sound_blksize);
	    } else {
		mvprintw(3,0,"");
	    }
	    refresh();
	}

	if (FD_ISSET(0,&s)) {
	    /* tty in */
	    switch (key = getch()) {
	    case 'Q':
	    case 'q':
		sig=1;
		break;
	    case 'A':
	    case 'a':
		auto_adjust = !auto_adjust;
		break;
	    case ' ':
		if (!filename)
		    break;
		if (!record) {
		    /* start */
		    sprintf(outfile,"%s%02d.wav",filename,nr++);
		    wav = open(outfile,O_WRONLY | O_TRUNC | O_CREAT, 0666);
		    if (-1 == wav) {
			perror("open");
			exit(1);
		    }
		    wav_start_write(wav);
		    record=1;
		    auto_adjust=0;
		} else {
		    /* stop */
		    wav_stop_write(wav);
		    close(wav);
		    record=0;
		    mvprintw(3,0,"                                        ");
		}
		break;
	    case KEY_RIGHT:
		vol = mixer_get_volume();
		vol++;
		if (vol > 100)
		    vol = 100;
		mixer_set_volume(vol);
		break;
	    case KEY_LEFT:
		vol = mixer_get_volume();
		vol--;
		if (vol < 0)
		    vol = 0;
		mixer_set_volume(vol);
		break;
	    }
	}
    }
    if (record) {
	wav_stop_write(wav);
	close(wav);
    }
    exit(0);
}
