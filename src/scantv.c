/*
 *  (c) 2000 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>

#include "config.h"

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#ifdef HAVE_LIBXV
# include <X11/extensions/Xv.h>
# include <X11/extensions/Xvlib.h>
#endif

/* xawtv */
#include "channel.h"
#include "frequencies.h"
#include "commands.h"
#include "grab.h"
#include "xv.h"

/* libvbi */
#include "vt.h"
#include "misc.h"
#include "fdset.h"
#include "vbi.h"
#include "lang.h"
#include "dllist.h"

int debug = 0;
int have_dga = 0;
char v4l_conf[] = "";
char *device = "/dev/video";
Display *dpy;

int timeout = 3;
char xpacket[64];
char header[64];
char scratch[1024*256];

/*---------------------------------------------------------------------*/

static void
grabber_init(void)
{
    grabber_open(device,0,0,0,0,0);
}

static void
event(struct dl_head *reqs, struct vt_event *ev)
{
    unsigned char *p;
    struct vt_page *vtp;
    
    switch (ev->type) {
    case EV_HEADER:
	p = ev->p1;
	if (debug)
	    fprintf(stderr,"header %.32s\n", p+8);
	memcpy(header,p+8,32);
	header[32] = 0;
	break;
    case EV_PAGE:
	vtp = ev->p1;
	if (debug)
	    fprintf(stderr,"vtx page %x.%02x  \r", vtp->pgno, vtp->subno);
	break;
    case EV_XPACKET:
	p = ev->p1;
	if (debug)
	    fprintf(stderr,"xpacket %x %x %x %x - %.20s\n",
		    p[0],p[1],p[3],p[5],p+20);
	memcpy(xpacket,p+20,20);
	xpacket[20] = 0;
	break;
    }
}

char*
get_vbi_name(void)
{
    int start;
    char *name,*h;

    xpacket[0] = 0;
    header[0] = 0;
    start = time(NULL);
    for (;;) {
	if (fdset_select(fds, 1000 * timeout) == 0) {
	    break;
	}
	if (time(NULL) > start+timeout) {
	    break;
	}
	if (xpacket[0] && header[0]) {
	    break;
	}
    }
    if (xpacket[0]) {
	for (h = xpacket+19; h >= xpacket; h--) {
	    if (32 != *h)
		break;
	    *h = 0;
	}
	for (name = xpacket; *name == 32; name++)
	    ;
	return name;
    }
#if 0
    if (header[0]) {
	for (name = header; *name && name < header+20; name++)
	    if (isalpha(*name))
		break;
	for (h = name; *name && name < header+20; name++)
	    if (!isalpha(*h)) {
		*h = 0;
		break;
	    }
	if (*name)
	    return name;
    }
#endif
    return NULL;
}

int
menu(char *intro, struct STRTAB *tab, char *opt)
{
    int i,ret;
    char line[80];

    if (NULL != opt) {
	/* cmd line option -- non-interactive mode */
	for (i = 0; tab[i].str != NULL; i++)
	    if (0 == strcasecmp(tab[i].str,opt))
		return tab[i].nr;
	fprintf(stderr,"%s: not found\n",opt);
	exit(1);
    }

    fprintf(stderr,"\n%s\n",intro);
    for (i = 0; tab[i].str != NULL; i++)
	fprintf(stderr,"  %2ld: %s\n",tab[i].nr,tab[i].str);

    for (;;) {
	fprintf(stderr,"nr ? ");
	fgets(line,79,stdin);
	ret = atoi(line);
	for (i = 0; tab[i].str != NULL; i++)
	    if (ret == tab[i].nr)
		return ret;
	fprintf(stderr,"invalid choice\n");
    }
}

void
usage(FILE *out, char *prog)
{
    fprintf(out,
	    "This tool scan for tv stations and writes "
	    "a initial xawtv config file.\n"
	    "usage: %s outfile\n",
	    prog);
}

int
main(int argc, char **argv)
{
    char *vbi_name = "/dev/vbi";
    struct vbi *vbi;
    int c,i,j,unknown = 1, scan=1;
    char *name,dummy[32];
    char *tvnorm  = NULL;
    char *freqtab = NULL;
    char *outfile = NULL;
    FILE *conf = stdout;

    setprgname(argv[0]);

    /* parse options */
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hsdn:f:o:")))
	    break;
	switch (c) {
	case 'd':
	    debug++;
	    break;
	case 's':
	    scan=0;
	    break;
	case 'n':
	    tvnorm = optarg;
	    break;
	case 'f':
	    freqtab = optarg;
	    break;
	case 'o':
	    outfile = optarg;
	    break;
	case 'h':
	    usage(stdout,argv[0]);
	    exit(0);
	default:
	    usage(stderr,argv[0]);
	    exit(1);
	}
    }

    if (outfile) {
	if (NULL == (conf = fopen(outfile,"w"))) {
	    fprintf(stderr,"open %s: %s\n",argv[1],strerror(errno));
	    exit(1);
	}
    }

    /* video */
#if 0
    if (NULL != getenv("DISPLAY"))
	dpy = XOpenDisplay(NULL);
    if (dpy)
	xv_init(1,0);
#endif
    if (NULL == grabber)
	grabber_init();
    if (NULL == grabber) {
	fprintf(stderr,"can't open video device\n");
	exit(1);
    }

    have_mixer = 0; /* don't use it */
    attr_init();
    audio_init();

    /* ask the user some questions ... */
    i = menu("please select your TV norm",grabber->norms,tvnorm);
    j = menu("please select a frequency table",chanlist_names,freqtab);

    fprintf(conf,"[defaults]\n");
    fprintf(conf,"input = Television\n");
    fprintf(conf,"norm = %s\n",grabber->norms[i].str);
    fprintf(conf,"freqtab = %s\n",chanlist_names[j].str);
    fprintf(conf,"\n");
    if (!scan)
	exit(0);

    set_defaults();
    do_va_cmd(2,"setinput","television");
    do_va_cmd(2,"setnorm",grabber->norms[i].str);
    do_va_cmd(2,"setfreqtab",chanlist_names[j].str);

    /* vbi */
    fdset_init(fds);
    if (not(vbi = vbi_open(vbi_name, 0, 0, -1)))
	fatal("cannot open %s", vbi_name);

    /* scan channels */
    fprintf(stderr,"\nscanning...\n");
    vbi_add_handler(vbi, event, NULL);
    for (i = 0; i < chancount; i++) {
	fprintf(stderr,"%-4s (%6.2f MHz): ",chanlist[i].name,(float)chanlist[i].freq/1000);
	do_va_cmd(2,"setchannel",chanlist[i].name);
	usleep(200000); /* 0.2 sec */
	if (grabber->grab_tuned &&
	    0 == grabber->grab_tuned()) {
	    fprintf(stderr,"no station\n");
	    continue;
	}
	fdset_select(fds, 1000 * timeout);
	name = get_vbi_name();
	fprintf(stderr,"%s\n",name ? name : "???");
	if (NULL == name) {
	    sprintf(dummy,"unknown %d",unknown++);
	    name = dummy;
	}
	fprintf(conf,"[%s]\nchannel = %s\n\n",name,chanlist[i].name);
    }

    /* cleanup */
    audio_off();
    if (grabber->grab_close)
	grabber->grab_close();
    if (dpy)
	XCloseDisplay(dpy);

    vbi_del_handler(vbi, event, NULL);
    vbi_close(vbi);

    exit(0);
}
