/*
 * (c) 2000,01 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>

/* xawtv */
#include "channel.h"
#include "frequencies.h"
#include "grab-ng.h"
#include "commands.h"

/* libvbi */
#include "vt.h"
#include "misc.h"
#include "fdset.h"
#include "vbi.h"
#include "lang.h"
#include "dllist.h"

int debug = 1;

int timeout = 3;
char xpacket[64];
char header[64];
char scratch[1024*256];

char *dev = "/dev/vbi";

/*---------------------------------------------------------------------*/

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
	    fprintf(stderr,"vtx page %x.%02x\n", vtp->pgno, vtp->subno);
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

int
main(int argc, char **argv)
{
    struct vbi *vbi;
    int c;

    setprgname(argv[0]);

    /* parse options */
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "c:")))
	    break;
	switch (c) {
	case 'c':
	    dev = optarg;
	    break;
	default:
	    exit(1);
	}
    }

    /* vbi */
    fdset_init(fds);
    if (not(vbi = vbi_open(dev, 0, 0, -1)))
	fatal("cannot open %s", dev);

    vbi_add_handler(vbi, event, NULL);
    for (;;)
	fdset_select(fds, 1000 * timeout);

    vbi_del_handler(vbi, event, NULL);
    vbi_close(vbi);

    exit(0);
}

/*
 * Local variables:
 * compile-command: "(cd ..; make)"
 * End:
 */
