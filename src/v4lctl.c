/*
 *  (c) 1999 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

#include "config.h"

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#ifdef HAVE_LIBXV
# include <X11/extensions/Xv.h>
# include <X11/extensions/Xvlib.h>
#endif

#include "grab-ng.h"
#include "channel.h"
#include "frequencies.h"
#include "commands.h"
#include "xv.h"

int debug = 0;
int have_dga = 0;
char v4l_conf[] = "";
char *device = "/dev/video";
Display *dpy;

/*--- main ---------------------------------------------------------------*/

static void
grabber_init(void)
{
    ng_grabber_open(device,NULL,0,NULL);
    f_drv = drv->capabilities(h_drv);
    a_drv = drv->list_attrs(h_drv);
}

void
usage(void)
{
    fprintf(stderr,
	    "\n"
	    "usage: v4lctl [ options ] command\n"
	    "options:\n"
	    "  -v, --debug=n      debug level n, n = [0..2]\n"
	    "  -c  --device=file  use <file> as video4linux device\n"
	    "  -h  --help         print this text\n"
	    "\n");
}

int main(int argc, char *argv[])
{
    int c;

    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hv:c:")))
	    break;
	switch (c) {
	case 'v':
	    debug = atoi(optarg);
	    break;
	case 'c':
	    device = optarg;
	    break;
	case 'h':
	default:
	    usage();
	    exit(1);
	}
    }
    if (optind == argc) {
	usage();
	exit(1);
    }

    if (NULL != getenv("DISPLAY"))
	dpy = XOpenDisplay(NULL);
    if (dpy)
	xv_init(1,0,0);
    if (NULL == drv)
	grabber_init();
    read_config();

    have_mixer = 0; /* don't use it */
    attr_init();
    audio_init();
    audio_init();

    do_command(argc-optind,argv+optind);
    drv->close(h_drv);
    if (dpy)
	XCloseDisplay(dpy);
    return 0;
}
