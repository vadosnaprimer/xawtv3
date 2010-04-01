/*
 *   (c) 1997 Gerd Knorr <kraxel@goldbach.in-berlin.de>
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


#include <asm/types.h>		/* XXX glibc */
#include "videodev.h"

#include "colorspace.h"
#include "writefile.h"
#include "writeavi.h"
#include "sound.h"

#define DEVNAME "/dev/video"

#define POST_NOTHING     0
#define POST_RGB_SWAP    1
#define POST_UNPACK422   2
#define POST_UNPACK411   3
#define POST_RAW         4

struct GRAB_FORMAT {
    char *name;
    int  bt848;
    int  mul,div;
    int  post;
    int  can_stdout;
    int  can_singlefile;
    int  can_multiplefile;
};

static struct GRAB_FORMAT formats[] = {
    /* file formats */
    { "ppm",  VIDEO_PALETTE_RGB24,       3, 1,  POST_RGB_SWAP,  0,0,1 },
    { "pgm",  VIDEO_PALETTE_GREY,        1, 1,  POST_NOTHING,   0,0,1 },
    { "jpeg", VIDEO_PALETTE_RGB24,       3, 1,  POST_RGB_SWAP,  0,0,1 },

    /* video */
    { "avi15",VIDEO_PALETTE_RGB555,      2, 1,  POST_NOTHING,   0,1,0 },
    { "avi24",VIDEO_PALETTE_RGB24,       3, 1,  POST_NOTHING,   0,1,0 },

    /* raw data */
    { "rgb",  VIDEO_PALETTE_RGB24,       3, 1,  POST_RGB_SWAP,  1,1,1 },
    { "gray", VIDEO_PALETTE_GREY,        1, 1,  POST_NOTHING,   1,1,1 },
    { "411",  VIDEO_PALETTE_YUV422,      3, 2,  POST_UNPACK411, 1,1,1 },
    { "422",  VIDEO_PALETTE_YUV422,      2, 1,  POST_UNPACK422, 1,1,1 },
    { "raw",  VIDEO_PALETTE_RAW,         1, 1,  POST_NOTHING,   1,1,1 },

    /* end of table */
    { NULL,   0,                         0, 0,  0              },
};

/* ---------------------------------------------------------------------- */

static struct video_capability  capability;
static struct video_channel     channel;
#if 0
static struct video_audio       audio;
static struct video_tuner       tuner;
static struct video_picture     pict;
#endif

static unsigned char            *map = NULL;
static struct video_mmap        gb1,gb2;
static struct video_mbuf        gb_buffers = { 2*0x151000, 0, {0,0x151000 }};

static unsigned char            *buffers;
static int                      bufsize;
static int			bfirst   = 0;
static int			blast    = 0;
static int                      bufcount = 6;
static int                      wtalk,rtalk;
static int                      wsync,rsync,synctime = 500;
static struct timeval           start;
static int                      tvnorm = -1;
static int                      input  = -1;

static char *device   = DEVNAME;
static char *filename = NULL;

static int  single = 1, format = -1, absframes = 1, with_audio = 0;
static int  fd = -1, width = 320, height = 240, quiet = 0, fps = 30;

static int  signaled = 0;

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
	    "  raw data:      rgb, gray, 411, 422, raw\n"
	    "  file formats:  ppm, pgm, jpeg\n"
	    "  movie formats: avi15, avi24\n"
	    "\n"
	    "examples:\n"
	    "  %s -o image.ppm                             write single ppm file\n"
	    "  %s -f 411 | display -size 320x240 yuv:-     yuv raw data to stdout\n"
	    "  %s -s 320x240 -t 5 -o frame000.jpeg         write 5 jpeg files\n"
	    "  %s -s 320x240 -am -r 10 -t 600 -o movie.avi record movie with sound\n"
	    "\n"
	    "--\n"
	    "(c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>\n",
	    prog, prog, device,
	    (format == -1) ? "none" : formats[format].name,
	    width, height, bufcount, absframes, fps,
	    filename ? filename : "stdout",
	    prog, prog, prog, prog, prog, prog);
}

/* ---------------------------------------------------------------------- */

void
writer()
{
    struct MOVIE_PARAMS params;
    char buffer, *sound_data;
    int  fd = -1, sound = -1, sound_size=0, n=0;
    fd_set set;

    signal(SIGINT,SIG_IGN);
    if (format >=  3 && format <= 4) {
	memset(&params,0,sizeof(params));
#define VIDEO_RGB15          2  /* host byte order */
#define VIDEO_BGR24          4  /* bgrbgrbgrbgr */
#define VIDEO_RGB24          7  /* rgbrgbrgbrgb */
	switch (formats[format].bt848) {
	case VIDEO_PALETTE_RGB555:
	    params.video_format = VIDEO_RGB15;
	    break;
	case VIDEO_PALETTE_RGB24:
	    params.video_format = VIDEO_BGR24;
	    break;
	}
	params.width        = width;
	params.height       = height;
	params.fps          = fps;
	if (with_audio) {
	    params.bits         = 16;
	    params.channels     = with_audio;
	    params.rate         = 44100;
	    if (-1 != (sound = sound_open(&params))) {
		sound_size = sound_bufsize();
		n = (rtalk>sound) ? rtalk+1 : sound+1;
	    }
	}
	avi_open(filename,&params);
    } else if (single) {
	if (filename) {
	    if (-1 == (fd = open(filename,O_RDWR | O_CREAT,0666))) {
		fprintf(stderr,"writer: open %s: %s\n",filename,strerror(errno));
		exit(1);
	    }
	} else {
	    filename = "stdout";
	    fd = 1;
	}
    }

    for (;;) {
	if (sound != -1) {
	    FD_ZERO(&set);
	    FD_SET(rtalk,&set);
	    FD_SET(sound,&set);
	    select(n,&set,NULL,NULL,NULL);
	    if (FD_ISSET(sound,&set)) {
		sound_data = sound_read();
		avi_writesound(sound_data, sound_size);
	    }
	    if (!FD_ISSET(rtalk,&set))
		continue;
	}
	    
	/* wait for frame */
	switch (read(rtalk,&buffer,1)) {
	case -1:
	    perror("writer: read socket");
	    exit(1);
	case 0:
	    if (!quiet)
		fprintf(stderr,"writer: done\n");
	    if (format >=  3 && format <= 4)
		avi_close();
	    exit(0);
	    return;
	}

	/* write out */
	switch (format) {
	case 0:
	    write_ppm(filename, buffers + buffer*bufsize, width, height);
	    patch_up(filename);
	    break;
	case 1:
	    write_pgm(filename, buffers + buffer*bufsize, width, height);
	    patch_up(filename);
	    break;
	case 2:
#ifdef HAVE_LIBJPEG
	    write_jpeg(filename, buffers + buffer*bufsize, width, height,75,0);
	    patch_up(filename);
#else
	    fprintf(stderr,"sorry, no jpeg support compiled in\n");
	    exit(1);
#endif
	    break;
	case 3:
	case 4:
	    avi_writeframe(buffers + buffer*bufsize);
	    break;
	default:
	    if (!single) {
	        if (-1 == (fd = open(filename,O_RDWR | O_CREAT,0666))) {
		    fprintf(stderr,"writer: open %s: %s\n",filename,strerror(errno));
		    exit(1);
	        }
	    }
	    if (bufsize != write(fd,buffers + buffer*bufsize, bufsize)) {
		fprintf(stderr,"writer: write %s: %s\n",filename,strerror(errno));
		exit(1);
	    }
	    if (!single) {
		close(fd);
		patch_up(filename);
	    }
	    break;
	}

	/* free buffer */
	if (1 != write(wtalk,&buffer,1)) {
	    perror("writer: write socket");
	    exit(1);
	}
	if (!quiet)
	    fprintf(stderr,"-");
    }
}

/* ---------------------------------------------------------------------- */

void
syncer()
{
    char dummy;
    
    signal(SIGINT,SIG_IGN);
    for (;;) {
	switch (read(rsync,&dummy,1)) {
	case -1:
	    perror("syncer: read socket");
	    exit(1);
	case 0:
	    if (!quiet)
		fprintf(stderr,"syncer: done\n");
	    exit(0);
	default:
	    if (!quiet)
		fprintf(stderr,"s");
	    sync();
	}
    }
}

/* ---------------------------------------------------------------------- */

int
putbuffer(char *src)
{
    static int     lastsec,lastsync,secframes,secqueued,absqueued,timediff;
    char           buffer;
    unsigned char  *dest;
    struct timeval tv;

    gettimeofday(&tv,NULL);
    timediff  = (tv.tv_sec  - start.tv_sec)  * 1000;
    timediff += (tv.tv_usec - start.tv_usec) / 1000;
    if (absqueued * 1000 / fps > timediff)
	return absqueued;

    if (timediff > lastsync+synctime) {
	/* sync */
	lastsync = timediff - timediff % synctime;
	if (1 != write(wsync,&bfirst,1)) {
	    perror("grabber: write socket");
	    exit(1);
	}
    }
    
    if (timediff > lastsec+1000) {
        /* statistics */
	if (!quiet && secframes)
	    fprintf(stderr," %2d/%2d\n",secqueued,secframes);
	lastsec = timediff - timediff % 1000;
	secqueued = 0;
	secframes = 0;
    }

    /* check for free buffers */
    switch(read(rtalk,&buffer,1)) {
    case -1:
	if (errno != EAGAIN) {
	    perror("grabber: read socket");
	    exit(1);
	}
	break;
    case 0:
	/* nothing */
	break;
    default:
	blast++;
	if (blast == bufcount)
	    blast = 0;
	break;
    }

    secframes++;
    if ((bfirst+1) % bufcount == blast) {
	/* no buffer free */
	if (!quiet)
	    fprintf(stderr,"o");
	return absqueued;
    }

    /* copy / convert */
    dest = buffers + bufsize*bfirst;
    switch (formats[format].post) {
    case POST_RGB_SWAP:
	rgb24_to_bgr24(dest,src,width*height);
	break;
    case POST_UNPACK422:
	packed422_to_planar422(dest,src,width,height);
	break;
    case POST_UNPACK411:
	packed422_to_planar411(dest,src,width,height);
	break;
    default:
	memcpy(dest,src,bufsize);
	break;
    }

    /* queue buffer */
    if (1 != write(wtalk,&bfirst,1)) {
	perror("grabber: write socket");
	exit(1);
    }
    secqueued++;
    absqueued++;
    bfirst++;
    if (bfirst == bufcount)
	bfirst = 0;
    if (!quiet)
	fprintf(stderr,"+");
    return absqueued;
}

/* ---------------------------------------------------------------------- */

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
    int  shm_id,c,s,count=0,i,n,p1[2],p2[2], queued=0;

    /* parse options */
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hqa:f:s:c:o:b:t:r:n:i:y:")))
	    break;
	switch (c) {
	case 'q':
	    quiet = 1;
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
	    if (0 == strcasecmp(optarg,"pal"))
		tvnorm = VIDEO_MODE_PAL;
	    else if (0 == strcasecmp(optarg,"ntsc"))
		tvnorm = VIDEO_MODE_NTSC;
	    else if (0 == strcasecmp(optarg,"secam"))
		tvnorm = VIDEO_MODE_SECAM;
	    else {
		fprintf(stderr,"unknown tv norm %s\n",optarg);
		exit(1);
	    }
	    break;
	case 'i':
	    input = atoi(optarg);
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
    if (-1 == fd && -1 == (fd = open(device,O_RDWR))) {
	fprintf(stderr,"open %s: %s\n",device,strerror(errno));
	exit(1);
    }

    /* get settings */
    if (-1 == ioctl(fd,VIDIOCGCAP,&capability)) {
	perror("ioctl VIDIOCGCAP");
	exit(1);
    }
    if (-1 == ioctl(fd,VIDIOCGCHAN,&channel))
	perror("ioctl VIDIOCGCHAN");
#if 0
    if (-1 == ioctl(fd,VIDIOCGAUDIO,&audio))
	perror("ioctl VIDIOCGAUDIO");
    if (-1 == ioctl(fd,VIDIOCGTUNER,&tuner))
	perror("ioctl VIDIOCGTUNER");
    if (-1 == ioctl(fd,VIDIOCGPICT,&pict))
	perror("ioctl VIDIOCGPICT");
#endif

    /* modify settings */
    if (tvnorm != -1 || input != -1) {
	if (tvnorm != -1)
	    channel.norm = tvnorm;
	if (input != -1)
	    channel.channel = input;
	if (-1 == ioctl(fd,VIDIOCSCHAN,&channel))
	    perror("ioctl VIDIOCSCHAN");
    }

    /* mmap() buffer */
    if (-1 == ioctl(fd,VIDIOCGMBUF,&gb_buffers)) {
	perror("ioctl VIDIOCGMBUF");
    }
    map = mmap(0,gb_buffers.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if ((unsigned char*)-1 == map) {
	perror("mmap");
    } else {
	if (!quiet)
	    fprintf(stderr,"v4l: mmap()'ed buffer size = 0x%x\n",
		    gb_buffers.size);
    }

    /* buffer initialisation */
    if (absframes < bufcount)
	bufcount = absframes+1;
    bufsize = width*height*formats[format].mul/formats[format].div;
    if (-1 == (shm_id = shmget(IPC_PRIVATE, bufsize * bufcount, IPC_CREAT | 0700))) {
	perror("shmget");
	exit(1);
    }
    buffers = shmat(shm_id, 0, 0);
    if ((void *) -1 == buffers) {
	perror("shmat");
	exit(1);
    }
    shmctl(shm_id, IPC_RMID, 0);

    /* start up writer */
    if (-1 == pipe(p1) || -1 == pipe(p2)) {
	perror("pipe");
	exit(1);
    }
    switch(fork()) {
    case -1:
	perror("fork");
	exit(1);
    case 0:
	close(p1[0]);
	close(p2[1]);
	wtalk = p1[1];
	rtalk = p2[0];
	fcntl(rtalk,F_SETFL,0);
	nice(5);
	writer();
	exit(0);
    default:
	close(p2[0]);
	close(p1[1]);
	wtalk = p2[1];
	rtalk = p1[0];
	fcntl(rtalk,F_SETFL,O_NONBLOCK);
	break;
    }

    /* start up syncer */
    if (-1 == pipe(p1)) {
	perror("pipe");
	exit(1);
    }
    switch(fork()) {
    case -1:
	perror("fork");
	exit(1);
    case 0:
	close(p1[1]);
	rsync = p1[0];
	fcntl(rsync,F_SETFL,0);
	nice(10);
	syncer();
	exit(0);
    default:
	close(p1[0]);
	wsync = p1[1];
	break;
    }

    /* catch ^C */
    signal(SIGINT,ctrlc);

    /* prepare for grabbing */
    gb1.format = formats[format].bt848;
    gb1.frame  = 0;
    gb1.width  = width;
    gb1.height = height;

    gb2.format = formats[format].bt848;
    gb2.frame  = 1;
    gb2.width  = width;
    gb2.height = height;

    if (-1 == ioctl(fd,VIDIOCMCAPTURE,&gb1)) {
	if (errno == EAGAIN)
	    fprintf(stderr,"grabber chip can't sync (no station tuned in?)\n");
	else
	    perror("ioctl VIDIOCMCAPTURE");
	exit(1);
    }
    count++;

    /* main loop */
    gettimeofday(&start,NULL);
    for (;queued < absframes && !signaled; count++) {

	if (-1 == ioctl(fd,VIDIOCMCAPTURE,(count%2) ? &gb2 : &gb1)) {
	    if (errno == EAGAIN)
		fprintf(stderr,"grabber chip can't sync (no station tuned in?)\n");
	    else
		perror("ioctl VIDIOCMCAPTURE");
	    exit(1);
	}

	if (-1 == ioctl(fd,VIDIOCSYNC,(count%2) ? &gb1.frame : &gb2.frame)) {
	    perror("ioctl VIDIOCSYNC");
	    exit(1);
	}
	queued = putbuffer(map + gb_buffers.offsets[(count%2) ? 0 : 1]);
    }

    if (-1 == ioctl(fd,VIDIOCSYNC,(count%2) ? &gb1.frame : &gb2.frame)) {
	perror("ioctl VIDIOCSYNC");
	exit(1);
    }

    /* done */
    if (!quiet)
	fprintf(stderr,"\n");
    close(fd);
    close(wtalk);
    close(wsync);
    wait(&s);
    wait(&s);
    return 0;
}
