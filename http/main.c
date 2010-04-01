#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifdef __linux__
# include <linux/videodev.h>
#endif

#include "httpd.h"
#include "devices.h"

/* libvbi */
#include "vt.h"
#include "misc.h"
#include "fdset.h"
#include "vbi.h"
#include "lang.h"
#include "dllist.h"
#include "export.h"

/* ---------------------------------------------------------------------- */
/* public variables - server configuration                                */

char    *server_name   = "alevt/1.5.1";

int     debug          = 0;
int     dontdetach     = 0;
int     timeout        = 60;
int     keepalive_time = 5;
int     tcp_port       = 0;
char    *listen_ip     = NULL;
char    *listen_port   = "5654";
char    server_host[256];
char    user[17];
char    group[17];
char    *logfile       = NULL;
FILE    *log           = NULL;
int     flushlog       = 0;
int     usesyslog      = 0;
int     have_tty       = 1;
int     max_conn       = 32;
int     cachereset     = 0;

time_t  now,start;
int     slisten;

struct vbi *vbi;
struct export *fmt;
void vbi_handler(struct vbi *vbi, int fd);

static void
dummy_client(struct dl_head *reqs, struct vt_event *ev)
{
    struct vt_page *vtp;

    if (!debug)
	return;

    switch (ev->type) {
    case EV_PAGE:
	vtp = ev->p1;
	fprintf(stderr,"page %x.%02x  \r", vtp->pgno, vtp->subno);
	break;
    }
}

/* ---------------------------------------------------------------------- */

static int termsig,got_sighup,got_sigusr1;

static void catchsig(int sig)
{
    if (SIGTERM == sig || SIGINT == sig)
	termsig = sig;
    if (SIGHUP == sig)
	got_sighup = 1;
    if (SIGUSR1 == sig)
	got_sigusr1 = 1;
}

/* ---------------------------------------------------------------------- */

static void
usage(char *name)
{
    char           *h;
    struct passwd  *pw;
    struct group   *gr;

    h = strrchr(name,'/');
    fprintf(stderr,
	    "alevt http daemon.\n"
	    "\n"
	    "usage: %s [ options ]\n"
	    "\n"
	    "Options:\n"
	    "  -h       print this text\n"
	    "  -v dev   vbi device                          [%s]\n"
	    "  -6       16 vbi lines  (depending on the bttv version\n"
	    "  -9       19 vbi lines   one of these two should work)\n"
	    "  -d       enable debug output                 [%s]\n"
	    "  -F       do not fork into background         [%s]\n"
	    "  -s       enable syslog (start/stop/errors)   [%s]\n"
	    "  -t sec   set network timeout                 [%i]\n"
	    "  -c n     set max. allowed connections        [%i]\n"
	    "  -p port  use tcp-port >port<                 [%s]\n"
	    "  -n host  server hostname is >host<           [%s]\n"
	    "  -i ip    bind to IP-address >ip<             [%s]\n"
	    "  -l log   write access log to file >log<      [%s]\n"
	    "  -L log   same as above + flush every line\n"
            "  -r       poll tv frequency and clear cache\n"
	    "           on station changes                  [%s]\n",
	    h ? h+1 : name,
	    ng_dev.vbi,
 	    debug     ?  "on" : "off",
 	    dontdetach ?  "on" : "off",
	    usesyslog ?  "on" : "off",
	    timeout, max_conn,
	    listen_port,
	    server_host,
	    listen_ip ? listen_ip : "any",
	    logfile ? logfile : "none",
	    cachereset ?  "on" : "off");
    if (getuid() == 0) {
	pw = getpwuid(0);
	gr = getgrgid(getgid());
	fprintf(stderr,
		"  -u user  run as user >user<                  [%s]\n"
		"  -g group run as group >group<                [%s]\n",
		pw ? pw->pw_name : "???",
		gr ? gr->gr_name : "???");
    }
    exit(1);
}

static void
fix_ug(void)
{
    struct passwd  *pw = NULL;
    struct group   *gr = NULL;
    
    /* root is allowed to use any uid/gid,
     * others will get their real uid/gid */
    if (0 == getuid() && strlen(user) > 0) {
	if (NULL == (pw = getpwnam(user)))
	    pw = getpwuid(atoi(user));
    } else {
	pw = getpwuid(getuid());
    }
    if (0 == getuid() && strlen(group) > 0) {
	if (NULL == (gr = getgrnam(group)))
	    gr = getgrgid(atoi(group));
    } else {
	gr = getgrgid(getgid());
    }

    if (NULL == pw) {
	xerror(LOG_ERR,"user unknown",NULL);
	exit(1);
    }
    if (NULL == gr) {
	xerror(LOG_ERR,"group unknown",NULL);
	exit(1);
    }

    /* set group */
    if (getegid() != gr->gr_gid || getgid() != gr->gr_gid)
	setgid(gr->gr_gid);
    if (getegid() != gr->gr_gid || getgid() != gr->gr_gid) {
	xerror(LOG_ERR,"setgid failed",NULL);
	exit(1);
    }
    strncpy(group,gr->gr_name,16);

    /* set user */
    if (geteuid() != pw->pw_uid || getuid() != pw->pw_uid)
	setuid(pw->pw_uid);
    if (geteuid() != pw->pw_uid || getuid() != pw->pw_uid) {
	xerror(LOG_ERR,"setuid failed",NULL);
	exit(1);
    }
    strncpy(user,pw->pw_name,16);
}

/* ---------------------------------------------------------------------- */

static void
access_log(struct REQUEST *req, time_t now)
{
    char timestamp[32];

    /* common log format: host ident authuser date request status bytes */
    strftime(timestamp,31,"[%d/%b/%Y:%H:%M:%S +0000]",gmtime(&now));
    if (0 == req->status)
	req->status = 400; /* bad request */
    if (400 == req->status) {
	fprintf(log,"%s - - %s \"-\" 400 %d\n",
		req->peerhost,
		timestamp,
		req->bc);
    } else {
	fprintf(log,"%s - - %s \"%s %s HTTP/%d.%d\" %d %d\n",
		req->peerhost,
		timestamp,
		req->type,
		req->uri,
		req->major,
		req->minor,
		req->status,
		req->bc);
    }
    if (flushlog)
	fflush(log);
}

/*
 * loglevel usage
 *   ERR    : fatal errors (which are followed by exit(1))
 *   WARNING: this should'nt happen error (oom, ...)
 *   NOTICE : start/stop of the daemon
 *   INFO   : "normal" errors (canceled downloads, timeouts,
 *            stuff what happens all the time)
 */

static void
syslog_init(void)
{
    openlog("alevtd",LOG_PID, LOG_DAEMON);
}

static void
syslog_start(void)
{
    syslog(LOG_NOTICE,
	   "started (listen on %s:%d, user=%s, group=%s)\n",
	   listen_ip,tcp_port,user,group);
}

static void
syslog_stop(void)
{
    if (termsig)
	syslog(LOG_NOTICE,"stopped on signal %d\n",termsig);
    else
	syslog(LOG_NOTICE,"stopped\n");
    closelog();
}

void
xperror(int loglevel, char *txt, char *peerhost)
{
    if (LOG_INFO == loglevel && usesyslog < 2 && !debug)
	return;
    if (have_tty) {
	if (NULL == peerhost)
	    perror(txt);
	else
	    fprintf(stderr,"%s: %s (peer=%s)\n",txt,strerror(errno),
		    peerhost);
    }
    if (usesyslog) {
	if (NULL == peerhost)
	    syslog(loglevel,"%s: %s\n",txt,strerror(errno));
	else
	    syslog(loglevel,"%s: %s (peer=%s)\n",txt,strerror(errno),
		   peerhost);
    }
}

void
xerror(int loglevel, char *txt, char *peerhost)
{
    if (LOG_INFO == loglevel && usesyslog < 2 && !debug)
	return;
    if (have_tty) {
	if (NULL == peerhost)
	    fprintf(stderr,"%s\n",txt);
	else
	    fprintf(stderr,"%s (peer=%s)\n",txt,peerhost);
    }
    if (usesyslog) {
	if (NULL == peerhost)
	    syslog(loglevel,"%s\n",txt);
	else
	    syslog(loglevel,"%s (peer=%s)\n",txt,peerhost);
    }	
}

/* ---------------------------------------------------------------------- */
/* main loop                                                              */

static void*
mainloop(void)
{
    struct REQUEST *conns = NULL;
    int curr_conn = 0;

    struct REQUEST      *req,*prev,*tmp;
    struct timeval      tv;
    int                 max,length,freq,lastfreq = 0;
    fd_set              rd,wr;

    for (;!termsig;) {
	if (got_sighup) {
	    if (NULL != logfile && 0 != strcmp(logfile,"-")) {
		if (debug)
		    fprintf(stderr,"got SIGHUP, reopen logfile %s\n",logfile);
		if (log) fclose(log);
		if (NULL == (log = fopen(logfile,"a")))
		    xperror(LOG_WARNING,"reopen access log",NULL);
	    }
	    got_sighup = 0;
	}
	if (got_sigusr1) {
	    if (debug)
		fprintf(stderr,"got SIGUSR1, reset cached vbi pages\n");
	    vbi->cache->op->reset(vbi->cache);
	    got_sigusr1 = 0;
	}

	FD_ZERO(&rd);
	FD_ZERO(&wr);
	FD_SET(vbi->fd,&rd);
	max = vbi->fd;
	/* add listening socket */
	if (curr_conn < max_conn) {
	    FD_SET(slisten,&rd);
	    max = slisten;
	}
	/* add connection sockets */
	for (req = conns; req != NULL; req = req->next) {
	    switch (req->state) {
	    case STATE_KEEPALIVE:
	    case STATE_READ_HEADER:
		FD_SET(req->fd,&rd);
		if (req->fd > max)
		    max = req->fd;
		break;
	    case STATE_WRITE_HEADER:
	    case STATE_WRITE_BODY:
		FD_SET(req->fd,&wr);
		if (req->fd > max)
		    max = req->fd;
		break;
	    }
	}
	/* go! */
	tv.tv_sec  = keepalive_time;
	tv.tv_usec = 0;
	if (-1 == select(max+1,&rd,&wr,NULL,(curr_conn > 0) ? &tv : NULL)) {
	    if (debug)
		perror("select");
	    continue;
	}
	now = time(NULL);

	/* vbi data? */
	if (FD_ISSET(vbi->fd,&rd)) {
#ifdef __linux__
	    if (cachereset) {
		ioctl(vbi->fd, VIDIOCGFREQ, &freq);
		if (lastfreq != freq) {
		    lastfreq = freq;
		    vbi->cache->op->reset( vbi->cache) ;
		    if (debug)
			fprintf(stderr, "frequency change: cache cleared.\n");
		}
	    }
#endif
	    vbi_handler(vbi,vbi->fd);
	}

	/* new connection ? */
	if (FD_ISSET(slisten,&rd)) {
	    req = malloc(sizeof(struct REQUEST));
	    if (NULL == req) {
		/* oom: let the request sit in the listen queue */
		if (debug)
		    fprintf(stderr,"oom\n");
	    } else {
		memset(req,0,sizeof(struct REQUEST));
		if (-1 == (req->fd = accept(slisten,NULL,NULL))) {
		    if (EAGAIN != errno)
			xperror(LOG_WARNING,"accept",NULL);
		    free(req);
		} else {
		    fcntl(req->fd,F_SETFL,O_NONBLOCK);
		    req->state = STATE_READ_HEADER;
		    req->ping = now;
		    req->next = conns;
		    conns = req;
		    curr_conn++;
		    if (debug)
			fprintf(stderr,"%03d: new request (%d)\n",req->fd,curr_conn);
		    length = sizeof(req->peer);
		    if (-1 == getpeername(req->fd,(struct sockaddr*)&(req->peer),&length)) {
			xperror(LOG_WARNING,"getpeername",NULL);
			req->state = STATE_CLOSE;
		    }
		    getnameinfo((struct sockaddr*)&req->peer,length,
				req->peerhost,64,req->peerserv,8,
				NI_NUMERICHOST | NI_NUMERICSERV);
		    if (debug)
			fprintf(stderr,"%03d: connect from (%s)\n",
				req->fd,req->peerhost);
		}
	    }
	}

	/* check active connections */
	for (req = conns, prev = NULL; req != NULL;) {
	    /* I/O */
	    if (FD_ISSET(req->fd,&rd)) {
		if (req->state == STATE_KEEPALIVE)
		    req->state = STATE_READ_HEADER;
		read_request(req,0);
		req->ping = now;
	    }
	    if (FD_ISSET(req->fd,&wr)) {
		write_request(req);
		req->ping = now;
	    }

	    /* check timeouts */
	    if (req->state == STATE_KEEPALIVE) {
		if (now > req->ping + keepalive_time) {
		    if (debug)
			fprintf(stderr,"%03d: keepalive timeout\n",req->fd);
		    req->state = STATE_CLOSE;
		}
	    } else {
		if (now > req->ping + timeout) {
		    if (req->state == STATE_READ_HEADER) {
			mkerror(req,408,0);
		    } else {
			xerror(LOG_INFO,"network timeout",req->peerhost);
			req->state = STATE_CLOSE;
		    }
		}
	    }

	    /* header parsing */
header_parsing:
	    if (req->state == STATE_PARSE_HEADER) {
		parse_request(req);
		if (req->state == STATE_WRITE_HEADER)
		    write_request(req);
	    }

	    /* handle finished requests */
	    if (req->state == STATE_FINISHED && !req->keep_alive)
		req->state = STATE_CLOSE;
	    if (req->state == STATE_FINISHED) {
		if (log)
		    access_log(req,now);
		/* cleanup */
		if (req->free_the_mallocs)
		    free(req->body);
		req->free_the_mallocs = 0;
		req->body      = NULL;
		req->written   = 0;
		req->head_only = 0;

		if (req->hdata == req->lreq) {
		    /* ok, wait for the next one ... */
		    if (debug)
			fprintf(stderr,"%03d: keepalive wait\n",req->fd);
		    req->state = STATE_KEEPALIVE;
		    req->hdata = 0;
		    req->lreq  = 0;
		} else {
		    /* there is a pipelined request in the queue ... */
		    if (debug)
			fprintf(stderr,"%03d: keepalive pipeline\n",req->fd);
		    req->state = STATE_READ_HEADER;
		    memmove(req->hreq,req->hreq+req->lreq,
			    req->hdata-req->lreq);
		    req->hdata -= req->lreq;
		    req->lreq  =  0;
		    read_request(req,1);
		    goto header_parsing;
		}
	    }

	    /* connections to close */
	    if (req->state == STATE_CLOSE) {
		if (log)
		    access_log(req,now);
		/* cleanup */
		close(req->fd);
		curr_conn--;
		if (debug)
		    fprintf(stderr,"%03d: done (%d)\n",req->fd,curr_conn);
		/* unlink from list */
		tmp = req;
		if (prev == NULL) {
		    conns = req->next;
		    req = conns;
		} else {
		    prev->next = req->next;
		    req = req->next;
		}
		/* free memory  */
		if (tmp->free_the_mallocs)
		    free(tmp->body);
		free(tmp);
	    } else {
		prev = req;
		req = req->next;
	    }
	}
    }
    return NULL;
}

/* ---------------------------------------------------------------------- */

int
main(int argc, char *argv[])
{
    struct sigaction         act,old;
    struct addrinfo          ask,*res;
#ifdef HAVE_SOCKADDR_STORAGE
    struct sockaddr_storage ss;
#else
    struct sockaddr ss;
#endif
    int                      c, opt, rc, ss_len;
    int bttv = -1;
    char host[INET6_ADDRSTRLEN+1];
    char serv[16];

    ng_device_init();
    gethostname(server_host,255);
    memset(&ask,0,sizeof(ask));
    ask.ai_flags = AI_CANONNAME;
    if (0 == (rc = getaddrinfo(server_host, NULL, &ask, &res))) {
	if (res->ai_canonname)
	    strcpy(server_host,res->ai_canonname);
    }
    
    /* parse options */
    for (;;) {
	if (-1 == (c = getopt(argc,argv,"69hsdFrp:n:i:t:c:u:g:l:L:v:")))
	    break;
	switch (c) {
	case 'h':
	    usage(argv[0]);
	    break;
	case '6':
	    bttv = 0;
	    break;
	case '9':
	    bttv = 1;
	    break;
	case 's':
	    usesyslog++;
	    break;
	case 'd':
	    debug++;
	    break;
	case 'F':
	    dontdetach++;
	    break;
	case 'r':
	    cachereset++;
	    break;
	case 'n':
	    strcpy(server_host,optarg);
	    break;
	case 'i':
	    listen_ip = optarg;
	    break;
	case 'p':
	    listen_port = optarg;
	    break;
	case 't':
	    timeout = atoi(optarg);
	    break;
	case 'c':
	    max_conn = atoi(optarg);
	    break;
	case 'u':
	    strncpy(user,optarg,16);
	    break;
	case 'g':
	    strncpy(group,optarg,16);
	    break;
	case 'L':
	    flushlog = 1;
	    /* fall through */
	case 'l':
	    logfile = optarg;
	    break;
	case 'v':
	    ng_dev.vbi = optarg;
	    break;
	default:
	    exit(1);
	}
    }
    if (usesyslog)
	syslog_init();

    /* open vbi device */
    fdset_init(fds);
    vbi = vbi_open(ng_dev.vbi, cache_open(), 0, bttv);
    if (vbi == 0) {
	xperror(LOG_ERR,"cannot open vbi device",NULL);
	exit(1);
    }
    fmt = export_open("ascii");
    vbi_add_handler(vbi, dummy_client, NULL);
#if 0
    if (vbi->cache)
	vbi->cache->op->mode(vbi->cache, CACHE_MODE_ERC, erc);
#endif

    /* bind to socket */
    slisten = -1;
    memset(&ask,0,sizeof(ask));
    ask.ai_flags = AI_PASSIVE;
    if (listen_ip)
	ask.ai_flags |= AI_CANONNAME;
    ask.ai_socktype = SOCK_STREAM;

    /* try ipv6 first ... */
    ask.ai_family = PF_INET6;
    if (0 != (rc = getaddrinfo(listen_ip, listen_port, &ask, &res))) {
	if (debug)
	    fprintf(stderr,"getaddrinfo (ipv6): %s\n",gai_strerror(rc));
    } else {
	if (-1 == (slisten = socket(res->ai_family, res->ai_socktype,
				    res->ai_protocol)) && debug)
	    xperror(LOG_ERR,"socket (ipv6)",NULL);
    }

    /* ... failing that try ipv4 */
    if (-1 == slisten) {
	ask.ai_family = PF_INET;
	if (0 != (rc = getaddrinfo(listen_ip, listen_port, &ask, &res))) {
	    fprintf(stderr,"getaddrinfo (ipv4): %s\n",gai_strerror(rc));
	    exit(1);
	}
	if (-1 == (slisten = socket(res->ai_family, res->ai_socktype,
				    res->ai_protocol))) {
	    xperror(LOG_ERR,"socket (ipv4)",NULL);
	    exit(1);
	}
    }

    memcpy(&ss,res->ai_addr,res->ai_addrlen);
    ss_len = res->ai_addrlen;
    if (res->ai_canonname)
	strcpy(server_host,res->ai_canonname);
    if (0 != (rc = getnameinfo((struct sockaddr*)&ss,ss_len,
			       host,INET6_ADDRSTRLEN,serv,15,
			       NI_NUMERICHOST | NI_NUMERICSERV))) {
	fprintf(stderr,"getnameinfo: %s\n",gai_strerror(rc));
	exit(1);
    }

    tcp_port = atoi(serv);
    opt = 1;
    setsockopt(slisten,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    fcntl(slisten,F_SETFL,O_NONBLOCK);

    if (-1 == bind(slisten, (struct sockaddr*) &ss, ss_len)) {
	xperror(LOG_ERR,"bind",NULL);
        exit(1);
    }
    if (-1 == listen(slisten, 2*max_conn)) {
	xperror(LOG_ERR,"listen",NULL);
        exit(1);
    }

    /* change user/group - also does chroot */
    fix_ug();

    if (logfile) {
	if (0 == strcmp(logfile,"-")) {
	    log = stdout;
	} else {
	    if (NULL == (log = fopen(logfile,"a")))
		xperror(LOG_WARNING,"open access log",NULL);
	}
    }

    if (debug) {
	fprintf(stderr,
		"alevt http server started\n"
		"  ipv6  : %s\n"
		"  node  : %s\n"
		"  ipaddr: %s\n"
		"  port  : %d\n"
		"  user  : %s\n"
		"  group : %s\n",
		res->ai_family == PF_INET6 ? "yes" : "no",
		server_host,host,tcp_port,user,group);
    }

    /* run as daemon - detach from terminal */
    if ((!debug) && (!dontdetach)) {
        switch (fork()) {
        case -1:
	    xperror(LOG_ERR,"fork",NULL);
	    exit(1);
        case 0:
            close(0); close(1); close(2); setsid();
	    have_tty = 0;
            break;
        default:
            exit(0);
        }
    }
    if (usesyslog) {
	syslog_start();
	atexit(syslog_stop);
    }

    /* setup signal handler */
    memset(&act,0,sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_handler = SIG_IGN;
    sigaction(SIGPIPE,&act,&old);
    act.sa_handler = catchsig;
    sigaction(SIGHUP,&act,&old);
    sigaction(SIGUSR1,&act,&old);
    sigaction(SIGTERM,&act,&old);
    if (debug)
	sigaction(SIGINT,&act,&old);

    /* go! */
    start = time(NULL);
    mainloop();
    
    if (log)
	fclose(log);
    if (debug)
	fprintf(stderr,"bye...\n");
    exit(0);
}
