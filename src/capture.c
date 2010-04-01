#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "grab.h"
#include "colorspace.h"
#include "sound.h"
#include "writeavi.h"
#include "capture.h"

/*-------------------------------------------------------------------------*/
/* for streaming capture                                                   */

/* buffer fifo */
static int             grab_bfirst;
static int             grab_blast;
static int             grab_bufcount;
static int             grab_bufsize;
static char*           grab_buffers;

/* timing */
static struct timeval  grab_start;
static int             grab_fps;
static int             grab_lastsec,grab_lastsync;
static int             grab_absqueued;
static int             grab_secframes,grab_secqueued;
static int             grab_missed;

/* forks of a new process, returns a file handle for a pipe
 *   The new process will sync if it reads some char from the pipe
 *   and exit on EOF
 */
int
grab_syncer(int *talk, int quiet)
{
    char dummy[16];
    int pid,rsync,p1[2];

    if (-1 == pipe(p1)) {
	perror("pipe");
	exit(1);
    }
    switch(pid = fork()) {
    case -1:
	perror("fork");
	exit(1);
    case 0:
	close(p1[1]);
	rsync = p1[0];
	fcntl(rsync,F_SETFL,0);
	signal(SIGINT,SIG_IGN);
	nice(10);
	for (;;) {
	    switch (read(rsync,dummy,16)) {
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
	break;
    default:
	close(p1[0]);
	*talk = p1[1];
	break;
    }
    return pid;
}


/* allocate shared memory for buffers, init variables for fifo/timing */
char*
grab_initbuffers(int size, int count)
{
    char *buffers;
    int  shmid;

    while (count > 0)
	if (-1 != (shmid = shmget(IPC_PRIVATE, size*count, IPC_CREAT | 0700)))
	    break;
    if (0 == count) {
	perror("shmget");
	return NULL;
    }
    buffers = shmat(shmid, 0, 0);
    if ((void *) -1 == buffers) {
	perror("shmat");
	return NULL;
    }
    if (-1 == shmctl(shmid, IPC_RMID, 0))
	perror("shmctl");

    gettimeofday(&grab_start,NULL);
    grab_lastsec   = 0;
    grab_lastsync  = 0;
    grab_absqueued = 0;
    grab_secframes = 0;
    grab_secqueued = 0;

    grab_bfirst    = 0;
    grab_blast     = 0;
    grab_bufcount  = count;
    grab_bufsize   = size;
    grab_buffers   = buffers;
    return buffers;
}

/* free buffers */
void
grab_freebuffers(char *buffers)
{
    if (-1 == shmdt(buffers))
	perror("shmdt");
}

/*-------------------------------------------------------------------------*/

int
grab_writer_fork(int *talk)
{
    int ret, s[2];
    
    /* start up new proccess */
    if (-1 == socketpair(AF_UNIX,SOCK_STREAM,PF_UNIX,s)) {
	perror("socketpair");
	return -1;
    }
    switch(ret = fork()) {
    case -1:
	perror("fork");
	close(s[0]);
	close(s[1]);
	break;
    case 0:
	close(s[0]);
	*talk = s[1];
	fcntl(*talk,F_SETFL,O_RDWR);
	signal(SIGINT,SIG_IGN);
	signal(SIGPIPE,SIG_IGN);
	nice(5);
	break;
    default:
	close(s[1]);
	*talk = s[0];
	fcntl(*talk,F_SETFL,O_RDWR|O_NONBLOCK);
	break;
    }
    return ret;
}

/* start up avi writer */
int
grab_writer_avi(char *filename, struct MOVIE_PARAMS *params,
		unsigned char *buffers, int bufsize, int quiet, int *talk)
{
    char *sound_data;
    int  args[2], sound = -1, sound_size=0, n=0, ret;
    fd_set set;

    grab_fps = params->fps;
    grab_missed = 0;

    /* start up new proccess */
    if (0 != (ret = grab_writer_fork(talk)))
	return ret;

    /* work */
    if (params->channels > 0) {
	if (-1 != (sound = sound_open(params))) {
	    sound_size = sound_bufsize();
	    n = (*talk>sound) ? *talk+1 : sound+1;
	}
    }
    avi_open(filename,params);
    for (;;) {
	if (sound != -1) {
	    FD_ZERO(&set);
	    FD_SET(*talk,&set);
	    FD_SET(sound,&set);
	    select(n,&set,NULL,NULL,NULL);
	    if (FD_ISSET(sound,&set)) {
		/* handle audio */
		sound_data = sound_read();
		avi_writesound(sound_data, sound_size);
	    }
	    if (!FD_ISSET(*talk,&set))
		continue;
	}

	/* wait for frame */
	switch (read(*talk,args,2*sizeof(int))) {
	case -1:
	    perror("writer: read socket");
	    exit(1);
	case 0:
	    avi_close();
	    if (!quiet)
		fprintf(stderr,"writer: done\n");
	    exit(0);
	}

	/* write video */
	avi_writeframe(buffers + args[0]*bufsize,args[1]);

	/* free buffer */
	if (sizeof(int) != write(*talk,args,sizeof(int))) {
	    perror("writer: write socket");
	    exit(1);
	}
	if (!quiet)
	    fprintf(stderr,"-");
    }
    
    /* done */
    exit(0);
}

void
grab_set_fps(int fps)
{
    grab_fps = fps;
    grab_missed = 0;
}

int
grab_putbuffer(int quiet, int writer, int wsync)
{
    static int     synctime = 500;
    int            timediff,args[2],size;
    struct timeval tv;

    /* get next frame */
    grabber_capture(grab_buffers + grab_bufsize*grab_bfirst,0,0,&size);

    /* check time */
    gettimeofday(&tv,NULL);
    timediff  = (tv.tv_sec  - grab_start.tv_sec)  * 1000;
    timediff += (tv.tv_usec - grab_start.tv_usec) / 1000;
    if (grab_absqueued * 1000 / grab_fps > timediff)
	return grab_absqueued;

    if (timediff > grab_lastsync+synctime) {
	/* sync */
	grab_lastsync = timediff - timediff % synctime;
	if (sizeof(grab_bfirst) != write(wsync,&grab_bfirst,sizeof(grab_bfirst))) {
	    perror("grabber: write socket");
	    exit(1);
	}
    }
    
    if (timediff > grab_lastsec+1000) {
        /* print statistics */
	grab_missed += (grab_fps - grab_secqueued);
	if (!quiet && grab_secframes)
	    fprintf(stderr," %2d/%2d (%d)\n",
		    grab_secqueued,grab_secframes,-grab_missed);
	grab_lastsec = timediff - timediff % 1000;
	grab_secqueued = 0;
	grab_secframes = 0;
    }

    /* check for free buffers */
    switch(read(writer,args,sizeof(int))) {
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
	grab_blast++;
	if (grab_blast == grab_bufcount)
	    grab_blast = 0;
	break;
    }

    grab_secframes++;
    if ((grab_bfirst+2) % grab_bufcount == grab_blast) {
	/* no buffer free */
	if (!quiet)
	    fprintf(stderr,"o");
	return grab_absqueued;
    }

    /* queue buffer */
    args[0] = grab_bfirst;
    args[1] = size;
    if (2*sizeof(int) != write(writer,args,2*sizeof(int))) {
	perror("grabber: write socket");
	exit(1);
    }
    grab_secqueued++;
    grab_absqueued++;
    grab_bfirst++;
    if (grab_bfirst == grab_bufcount)
	grab_bfirst = 0;
    if (!quiet)
	fprintf(stderr,"+");
    return grab_absqueued;
}
