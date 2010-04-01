/*
 *   (c) 1997-2001 Gerd Knorr <kraxel@bytesex.org>
 *
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/wait.h>
#include <X11/Intrinsic.h>

#include "grab-ng.h"
#include "writefile.h"
#include "channel.h"
#include "sound.h"
#include "capture.h"
#include "commands.h"

/* ---------------------------------------------------------------------- */

static int       bufcount = 16;
static char*     tvnorm = NULL;
static char*     input  = NULL;
static char*     v4l_device = "/dev/video";
static char*     dsp_device = "/dev/dsp";
static char*     moviename = NULL;
static char*     audioname = NULL;
static char*     vfmt_name;
static char*     afmt_name;

static const struct ng_writer     *writer;
static const void                 *video_priv;
static const void                 *audio_priv;
static struct ng_video_fmt        video = {
    width: 320,
    height: 240,
};
static struct ng_audio_fmt        audio = {
    rate: 44100,
};
static void *movie_state;

static int  absframes = 1;
static int  quiet = 0, fps = 10;

static int  signaled = 0, wait_seconds = 0;

int debug = 0, have_dga = 0;

/* ---------------------------------------------------------------------- */

void
list_formats(FILE *out)
{
    const struct ng_writer *wr;
    int i,j;
    
    fprintf(out,"\nmovie writers:\n");
    for (i = 0; NULL != ng_writers[i]; i++) {
	wr = ng_writers[i];
	fprintf(out,"  %s - %s\n",wr->name,
		wr->desc ? wr->desc : "-");
	if (NULL != wr->video) {
	    fprintf(out,"    video formats:\n");
	    for (j = 0; NULL != wr->video[j].name; j++) {
		fprintf(out,"      %-7s %-28s [%s]\n",wr->video[j].name,
			wr->video[j].desc ? wr->video[j].desc :
			ng_vfmt_to_desc[wr->video[j].fmtid],
			wr->video[j].ext);
	    }
	}
	if (NULL != wr->audio) {
	    fprintf(out,"    audio formats:\n");
	    for (j = 0; NULL != wr->audio[j].name; j++) {
		fprintf(out,"      %-7s %-28s [%s]\n",wr->audio[j].name,
			wr->audio[j].desc ? wr->audio[j].desc :
			ng_afmt_to_desc[wr->audio[j].fmtid],
			wr->audio[j].ext ? wr->audio[j].ext : "-");
	    }
	}
	fprintf(out,"\n");
    }
}

void
usage(FILE *out, char *prog)
{
    char *h;

    if (NULL != (h = strrchr(prog,'/')))
	prog = h+1;
    
    fprintf(out,
	    "%s grabs image(s) and records movies\n"
	    "from a video4linux device\n"
	    "\n"
	    "usage: %s [ options ]\n"
	    "\n"
	    "general options:\n"
	    "  -h          print this help text\n"
	    "  -q          quiet operation\n"
	    "  -w seconds  wait before grabbing         [%d]\n"
	    "\n"
	    "video options:\n"
	    "  -o file     video/movie file name\n"
	    "  -f format   specify video format\n"
	    "  -c device   specify video4linux device   [%s]\n"
	    "  -r fps      frame rate                   [%d]\n"
	    "  -s size     specify size                 [%dx%d]\n"
	    "\n"
	    "  -t times    number of frames             [%d]\n"
	    "  -b buffers  specify # of buffers         [%d]\n"
	    "  -j quality  quality for mjpeg or jpeg    [%d]\n"
	    "  -n tvnorm   set pal/ntsc/secam\n"
	    "  -i input    set video source\n"
	    "\n"
	    "audio options:\n"
	    "  -O file     wav file name\n"
	    "  -F format   specify audio format\n"
	    "  -C device   specify dsp device           [%s]\n"
	    "  -R rate     sample rate                  [%d]\n"
	    "\n",
	    prog, prog,
	    
	    wait_seconds,
	    v4l_device, fps, video.width, video.height,
	    absframes, bufcount, jpeg_quality,
	    dsp_device, audio.rate
	);

    list_formats(out);
    fprintf(out,
	    "If you want to capture to multiple image files you should include some\n"
	    "digits into the movie filename (foo0000.jpeg for example), %s will\n"
	    "use the digit block to enumerate the image files.\n"
	    "For file formats which can hold *both* audio and video (like AVI and\n"
	    "QuickTime) the -O option has no effect.\n"
	    "\n"
	    "-- \n"
	    "(c) 1998-2001 Gerd Knorr <kraxel@bytesex.org>\n",
	    prog);
}

/* ---------------------------------------------------------------------- */

void
find_formats(void)
{
    const struct ng_writer *wr = NULL;
    char *mext = NULL;
    char *aext = NULL;
    int w,v=-1,a=-1;

    if (moviename) {
	mext = strrchr(moviename,'.');
	if (mext)
	    mext++;
    }
    if (audioname) {
	aext = strrchr(audioname,'.');
	if (aext)
	    aext++;
    }
    for (w = 0; NULL != ng_writers[w]; w++) {
	wr = ng_writers[w];
	if ((!wr->combined && mext) || NULL != vfmt_name) {
	    if (NULL == wr->video)
		continue;
	    for (v = 0; NULL != wr->video[v].name; v++) {
		if (mext && 0 != strcasecmp(wr->video[v].ext,mext))
		    continue;
		if (vfmt_name && 0 != strcasecmp(wr->video[v].name,vfmt_name))
		    continue;
		break;
	    }
	    if (NULL == wr->video[v].name)
		continue;
	}
	if ((!wr->combined && aext) || NULL != afmt_name) {
	    if (NULL == wr->audio)
		continue;
	    for (a = 0; NULL != wr->audio[a].name; a++) {
		if (!wr->combined &&
		    aext && 0 != strcasecmp(wr->audio[a].ext,aext))
		    continue;
		if (wr->combined &&
		    mext && 0 != strcasecmp(wr->audio[a].ext,mext))
		    continue;
		if (afmt_name && 0 != strcasecmp(wr->audio[a].name,afmt_name))
		    continue;
		break;
	    }
	    if (NULL == wr->audio[a].name)
		continue;
	}
	break;
    }
    if (NULL != ng_writers[w]) {
	writer = wr;
	if (-1 != v) {
	    video.fmtid = wr->video[v].fmtid;
	    video_priv  = wr->video[v].priv;
	}
	if (-1 != a) {
	    audio.fmtid = wr->audio[a].fmtid;
	    audio_priv  = wr->audio[a].priv;
	}
    }
}

/* ---------------------------------------------------------------------- */

void set_capture(int capture) {}

void
ctrlc(int signal)
{
    static char text[] = "^C - one moment please\n";
    write(2,text,strlen(text));
    signaled=1;
}

int
main(int argc, char **argv)
{
    int  c,queued=0;

    /* parse options */
    for (;;) {
	if (-1 == (c = getopt(argc, argv,
			      "hqdw:" "o:c:f:r:s:t:n:i:b:j:" "O:C:F:R:")))
	    break;
	switch (c) {
	    /* general options */
	case 'q':
	    quiet = 1;
	    break;
	case 'd':
	    debug++;
	    ng_debug++;
	    break;
	case 'w':
	    wait_seconds = atoi(optarg);
	    break;

	    /* video options */
	case 'o':
	    moviename = optarg;
	    break;
	case 'f':
	    vfmt_name = optarg;
	    break;
	case 'c':
	    v4l_device = optarg;
	    break;
	case 'r':
	    fps = atoi(optarg);
	    break;
	case 's':
	    if (2 != sscanf(optarg,"%dx%d",&video.width,&video.height))
		video.width = video.height = 0;
	    break;
	    
	case 't':
	    absframes = atoi(optarg);
	    break;
	case 'b':
	    bufcount = atoi(optarg);
	    break;
	case 'j':
	    jpeg_quality = ng_mjpeg_quality = atoi(optarg);
	    break;
	case 'n':
	    tvnorm = optarg;
	    break;
	case 'i':
	    input = optarg;
	    break;

	    /* audio options */
	case 'O':
	    audioname = optarg;
	    break;
	case 'F':
	    afmt_name = optarg;
	    break;
	case 'C':
	    dsp_device = optarg;
	    break;
	case 'R':
	    audio.rate = atoi(optarg);
	    break;

	    /* errors / help */
	case 'h':
	    usage(stdout,argv[0]);
	    exit(0);
	default:
	    usage(stderr,argv[0]);
	    exit(1);
	}
    }
    ng_init();
    find_formats();

    /* sanity checks */
    if (video.fmtid == VIDEO_NONE && audio.fmtid == AUDIO_NONE) {
	fprintf(stderr,"neither audio nor video format specified/found\n");
	exit(1);
    }
    if (NULL == writer) {
	fprintf(stderr,"no output driver found\n");
	exit(1);
    }
    if (audio.fmtid != AUDIO_NONE && !writer->combined && NULL == audioname) {
	fprintf(stderr,"no audio file name specified\n");
	exit(1);
    }

    /* open */
    if (writer && !quiet)
	fprintf(stderr,"%s / video: %s / audio: %s\n",writer->name,
		ng_vfmt_to_desc[video.fmtid],ng_afmt_to_desc[audio.fmtid]);

    if (video.fmtid != VIDEO_NONE) {
	drv = ng_grabber_open(v4l_device,NULL,0,&h_drv);
	if (NULL == drv) {
	    fprintf(stderr,"no grabber device available\n");
	    exit(1);
	}
	f_drv = drv->capabilities(h_drv);
	a_drv = drv->list_attrs(h_drv);
	if (!(f_drv & CAN_CAPTURE)) {
	    fprintf(stderr,"%s: capture not supported\n",drv->name);
	    exit(1);
	}
	audio_on();
	audio_init();

	/* modify settings */
	if (input != NULL)
	    do_va_cmd(2,"setinput",input);
	if (tvnorm != NULL)
	    do_va_cmd(2,"setnorm",tvnorm);
    }
	
    /* init movie writer */
    movie_state = movie_writer_init
	(moviename, audioname, writer,
	 &video, video_priv, fps,
	 &audio, audio_priv, bufcount);
    if (NULL == movie_state) {
	fprintf(stderr,"movie writer initialisation failed\n");
	if (video.fmtid != VIDEO_NONE) {
	    audio_off();
	    drv->close(h_drv);
	}
	exit(1);
    }

    /* catch ^C */
    signal(SIGINT,ctrlc);

    /* wait for some cameras to wake up and adjust light and all that */
    if (wait_seconds)
	sleep(wait_seconds);

    /* main loop */
    movie_writer_start(movie_state);
    for (;queued < absframes && !signaled;) {
	if (video.fmtid != VIDEO_NONE) {
	    /* video */
	    queued = movie_grab_put_video(movie_state);
	} else {
	    /* quick+dirty: use #frames as #seconds */
	    sleep(1);
	    queued++;
	}
    }
    movie_writer_stop(movie_state);

    /* done */
    if (video.fmtid != VIDEO_NONE) {
	audio_off();
	drv->close(h_drv);
    }
    return 0;
}
