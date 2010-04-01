/*
 *   (c) 1997-99 Gerd Knorr <kraxel@goldbach.in-berlin.de>
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

#include "colorspace.h"
#include "writefile.h"
#include "writeavi.h"
#include "channel.h"
#include "sound.h"
#include "grab.h"
#include "capture.h"
#include "commands.h"

#define DEVNAME "/dev/video"

struct GRAB_FORMAT {
    char *name;
    int  bt848;
    int  mul,div;
    int  can_stdout;
    int  can_singlefile;
    int  can_multiplefile;
};

static struct GRAB_FORMAT formats[] = {
    /* file formats */
    { "ppm",  VIDEO_RGB24,     3,1,  0,0,1 },
    { "pgm",  VIDEO_GRAY,      1,1,  0,0,1 },
    { "jpeg", VIDEO_RGB24,     3,1,  0,0,1 },

    /* video */
    { "avi15",VIDEO_RGB15_LE,  2,1,  0,1,0 },
    { "avi24",VIDEO_BGR24,     3,1,  0,1,0 },
    { "mjpeg",VIDEO_MJPEG,     0,1,  0,1,0 },

    /* raw data */
    { "rgb",  VIDEO_RGB24,     3,1,  1,1,1 },
    { "gray", VIDEO_GRAY,      1,1,  1,1,1 },
    { "422",  VIDEO_YUV422,    2,1,  1,1,1 },
    { "422p", VIDEO_YUV422P,   2,1,  1,1,1 },
    { "420p", VIDEO_YUV420P,   3,2,  1,1,1 },

    /* end of table */
    { NULL,   0,               0,0,  0 },
};

/* ---------------------------------------------------------------------- */

static unsigned char            *buffers;
static int                      bufsize;
static int                      bufcount = 6;
static int                      writer;
static int                      wsync;
static struct timeval           start;
static char*                    tvnorm = NULL;
static char*                    input  = NULL;
static char*                    device = "/dev/video";

static char *filename = NULL;

static int  single = 1, format = -1, absframes = 1, with_audio = 0, gray = 0;
static int  fd = -1, width = 320, height = 240, quiet = 0, fps = 30;

static int  signaled = 0, linelength = 0;

int debug = 0, have_dga = 0;
char v4l_conf[] = "";

/* ---------------------------------------------------------------------- */

void
usage(char *prog)
{
    char *h;

    if (NULL != (h = strrchr(prog,'/')))
	prog = h+1;
    
    fprintf(stderr,
	    "%s grabs image(s) from a bt848 card\n"
	    "\n"
	    "usage: %s [ options ]\n"
	    "\n"
	    "options:\n"
	    "  -q          quiet operation\n"
	    "  -a m|s      audio recording mono/stereo [off]\n"
	    "              (avi movies only).  Check the record source with\n"
	    "              some mixer tool if you get silence.\n"
	    "  -c device   specify device              [%s]\n"
	    "  -f format   specify output format       [%s]\n"
	    "  -s size     specify size                [%dx%d]\n"
	    "  -b buffers  specify # of buffers        [%d]\n"
	    "  -t times    number of frames            [%d]\n"
	    "  -r fps      frame rate                  [%d]\n"
	    "  -o file     output file name            [%s]\n"
	    "  -n tvnorm   set pal/ntsc/secam          [no change]\n"
	    "  -i input    set input source (int)      [no change]\n"
	    "  -j quality  quality for mjpeg or jpeg   [%d]\n"
	    "  -g          convert to grayscale (jpeg) [%s]\n"
	    "\n"
	    "If the filename has some digits, %s will write multiple files,\n"
	    "otherwise one huge file.  Writing to one file works with raw\n"
	    "data only.  %s will switch the default format depending on the\n"
	    "filename extention (ppm, pgm, jpg, jpeg, avi)\n"
	    "\n"
	    "funny chars:\n"
	    "  +      grabbed picture queued to fifo\n"
	    "  o      grabbed picture not queued (fifo full)\n"
	    "  -      picture written to disk and dequeued from fifo\n"
	    "  s      sync\n"
	    "  xx/yy  (at end of line) xx frames queued, yy frames grabbed\n" 
	    "\n"
	    "formats:\n"
	    "  raw data:      rgb, gray, 422, 422p, 420p\n"
	    "  file formats:  ppm, pgm, jpeg\n"
	    "  movie formats: avi15, avi24, mjpeg\n"
	    "\n"
	    "examples:\n"
	    "  %s -o image.ppm                             write single ppm file\n"
	    "  %s -f 411 | display -size 320x240 yuv:-     yuv raw data to stdout\n"
	    "  %s -s 320x240 -t 5 -o frame000.jpeg         write 5 jpeg files\n"
	    "  %s -s 320x240 -am -r 10 -t 600 -o movie.avi record movie with sound\n"
	    "\n"
	    "--\n"
	    "(c) 1998,99 Gerd Knorr <kraxel@goldbach.in-berlin.de>\n",
	    prog, prog, device,
	    (format == -1) ? "none" : formats[format].name,
	    width, height, bufcount, absframes, fps,
	    filename ? filename : "stdout",
	    jpeg_quality,
	    gray ? "yes" : "no",
	    prog, prog, prog, prog, prog, prog);
}

/* ---------------------------------------------------------------------- */

static int
writer_single(int *talk)
{
    int  args[2];
    int  fd = -1,ret;
    
    /* start up new proccess */
    if (0 != (ret = grab_writer_fork(talk)))
	return ret;
    
    if (filename) {
	if (-1 == (fd = open(filename,O_RDWR | O_CREAT,0666))) {
	    fprintf(stderr,"writer: open %s: %s\n",filename,strerror(errno));
	    exit(1);
	}
    } else {
	filename = "stdout";
	fd = 1;
    }
    
    for (;;) {
	/* wait for frame */
	switch (read(*talk,args,2*sizeof(int))) {
	case -1:
	    close(fd);
	    perror("writer: read socket");
	    exit(1);
	case 0:
	    close(fd);
	    if (!quiet)
		fprintf(stderr,"writer: done\n");
	    exit(0);
	}

	/* write out */
	if (bufsize != write(fd,buffers + args[0]*bufsize, bufsize)) {
	    fprintf(stderr,"writer: write %s: %s\n",filename,strerror(errno));
	    exit(1);
	}

	/* free buffer */
	if (sizeof(int) != write(*talk,args,sizeof(int))) {
	    perror("writer: write socket");
	    exit(1);
	}
	if (!quiet)
	    fprintf(stderr,"-");
    }
}

static int
writer_files(int *talk)
{
    int  args[2];
    int  fd = -1, ret;

    /* start up new proccess */
    if (0 != (ret = grab_writer_fork(talk)))
	return ret;

    for (;;) {
	/* wait for frame */
	switch (read(*talk,args,2*sizeof(int))) {
	case -1:
	    perror("writer: read socket");
	    exit(1);
	case 0:
	    if (!quiet)
		fprintf(stderr,"writer: done\n");
	    exit(0);
	}

	/* write out */
	switch (format) {
	case 0:
	    write_ppm(filename, buffers + args[0]*bufsize, width, height);
	    patch_up(filename);
	    break;
	case 1:
	    write_pgm(filename, buffers + args[0]*bufsize, width, height);
	    patch_up(filename);
	    break;
	case 2:
#ifdef HAVE_LIBJPEG
	    write_jpeg(filename, buffers + args[0]*bufsize, width, height, jpeg_quality, gray);
	    patch_up(filename);
#else
	    fprintf(stderr,"sorry, no jpeg support compiled in\n");
	    exit(1);
#endif
	    break;
	default:
	    if (-1 == (fd = open(filename,O_RDWR | O_CREAT,0666))) {
		fprintf(stderr,"writer: open %s: %s\n",filename,strerror(errno));
		exit(1);
	    }
	    if (bufsize != write(fd,buffers + args[0]*bufsize, bufsize)) {
		fprintf(stderr,"writer: write %s: %s\n",filename,strerror(errno));
		exit(1);
	    }
	    close(fd);
	    patch_up(filename);
	    break;
	}

	/* free buffer */
	if (sizeof(int) != write(*talk,args,sizeof(int))) {
	    perror("writer: write socket");
	    exit(1);
	}
	if (!quiet)
	    fprintf(stderr,"-");
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
    int  c,s,count=0,i,n,queued=0;

    /* parse options */
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hqda:f:s:c:o:b:t:r:n:i:y:j:g")))
	    break;
	switch (c) {
	case 'q':
	    quiet = 1;
	    break;
	case 'd':
	    debug = 1;
	    break;
	case 'a':
	    with_audio = (optarg[0] == 's') ? 2 : 1;
	    break;
	case 'f':
	    for (i = 0; formats[i].name != NULL; i++)
		if (0 == strcasecmp(formats[i].name,optarg)) {
		    format = i;
		    break;
		}
	    if (formats[i].name == NULL) {
		fprintf(stderr,"unknown format %s (available:",optarg);
		for (i = 0; formats[i].name != NULL; i++)
		    fprintf(stderr," %s",formats[i].name);
		fprintf(stderr,")\n");
		exit(1);
	    }
	    break;
	case 's':
	    if (2 != sscanf(optarg,"%dx%d",&width,&height))
		width = height = 0;
	    break;
	case 'c':
	    device = optarg;
	    break;
	case 'o':
	    filename = optarg;
	    for (i = 0, n = strlen(filename); i < n; i++) {
		if (isdigit(filename[i]))
		    single = 0;
	    }
	    if (format == -1) {
		if (strstr(filename,"ppm"))
		    format = 0;
		if (strstr(filename,"pgm"))
		    format = 1;
		if (strstr(filename,"jpeg"))
		    format = 2;
		if (strstr(filename,"jpg"))
		    format = 2;
		if (strstr(filename,"avi"))
		    format = 3;
	    }
	    break;
	case 'n':
	    tvnorm = optarg;
	    break;
	case 'i':
	    input = optarg;
	    break;
	case 'j':
	    jpeg_quality = mjpeg_quality = atoi(optarg);
	    break;
	case 't':
	    absframes = atoi(optarg);
	    break;
	case 'b':
	    bufcount = atoi(optarg);
	    break;
	case 'r':
	    fps = atoi(optarg);
	    break;
	case 'y':
	    fd = atoi(optarg);
	    break;
	case 'g':
	    gray = 1;
	    break;
	case 'h':
	default:
	    usage(argv[0]);
	    exit(1);
	}
    }
    if (format == -1) {
	fprintf(stderr,"no format specified\n");
	exit(1);
    }
    if (gray && (format == 2)) {
	formats[format].mul = 1;
	formats[format].div = 1;
	formats[format].bt848 = VIDEO_GRAY;
    }
    if (filename == NULL && !formats[format].can_stdout) {
	fprintf(stderr,"writing to stdout is not supported for %s\n",
		formats[format].name);
	exit(1);
    }
    if (absframes > 1 && !single && !formats[format].can_multiplefile) {
#if 0
	fprintf(stderr,"writing to multiple files is not supported for %s\n",
		formats[format].name);
	exit(1);
#else
	single = 1;
#endif
    }
    if (absframes > 1 && single && !formats[format].can_singlefile) {
	fprintf(stderr,"writing to a single file is not supported for %s\n",
		formats[format].name);
	exit(1);
    }

    /* open */
    fd = grabber_open(device,0,0,0,0,0);
    if (grabber->grab_setparams == NULL ||
	grabber->grab_capture   == NULL) {
	fprintf(stderr,"%s\ncapture not supported\n",grabber->name);
	exit(1);
    }
    audio_init();
    audio_on();

    /* modify settings */
    if (input != NULL)
	do_va_cmd(2,"setinput",input);
    if (tvnorm != NULL)
	do_va_cmd(2,"setnorm",tvnorm);

    /* set capture parameters */
    grabber_setparams(formats[format].bt848,&width,&height,
		      &linelength,0);

    /* TODO: Add a shortcut for single frame captures here.  Forking
     *       off twice is overkill unless we really do streaming
     *       capture.
     */

    /* buffer initialisation */
    if (absframes < bufcount)
	bufcount = absframes+2;
    bufsize = width*height*formats[format].mul/formats[format].div;
    if (0 == bufsize)
	bufsize = width*height*3; /* compressed - should be enouth */
    if ((buffers = grab_initbuffers(bufsize, bufcount)) == NULL)
	exit(1);

    /* start up writer */
    if (format >= 3 && format <= 5) {
	struct MOVIE_PARAMS params;
	memset(&params,0,sizeof(params));
	params.video_format = formats[format].bt848;
        params.width        = width;
        params.height       = height;
        params.fps          = fps;
        if (with_audio) {
            params.bits         = 16;
            params.channels     = with_audio;
            params.rate         = 44100;
        }
	grab_writer_avi(filename,&params,
			buffers,bufsize,quiet,&writer);
    } else if (single && formats[format].can_singlefile) {
	grab_set_fps(fps);
	writer_single(&writer);
    } else {
	grab_set_fps(fps);
	writer_files(&writer);
    }
    
    /* start up syncer */
    grab_syncer(&wsync, quiet);

    /* catch ^C */
    signal(SIGINT,ctrlc);

    /* main loop */
    gettimeofday(&start,NULL);
    for (;queued < absframes && !signaled; count++) {
	queued = grab_putbuffer(quiet,writer,wsync);
    }

    /* done */
    audio_off();
    grabber->grab_close();
    if (!quiet)
	fprintf(stderr,"\n");
    close(fd);
    shutdown(writer,1);
    close(wsync);
    wait(&s);
    wait(&s);
    close(writer);
    return 0;
}
