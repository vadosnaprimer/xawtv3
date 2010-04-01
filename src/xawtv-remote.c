
/* shameless stolen from netscape :-) */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xmu/WinUtil.h>    /* for XmuClientWindow() */

int
x11_error_dev_null(Display * dpy, XErrorEvent * event)
{
    fprintf(stderr,"x11-error\n");
    return 0;
}

static Window
find_window(Display * dpy, Atom atom)
{
    int             i,n;
    Window          root = RootWindowOfScreen(DefaultScreenOfDisplay(dpy));
    Window          root2, parent, *kids;
    unsigned int    nkids;
    Window          result = 0;

    if (!XQueryTree(dpy, root, &root2, &parent, &kids, &nkids)) {
        fprintf(stderr, "XQueryTree failed on display %s\n",
                DisplayString(dpy));
        exit(2);
    }

    if (!(kids && nkids)) {
        fprintf(stderr, "root window has no children on display %s\n",
                DisplayString(dpy));
        exit(2);
    }
    for (n = nkids - 1; n >= 0; n--) {
        Atom            type;
        int             format;
        unsigned long   nitems, bytesafter;
        unsigned char  *args = NULL;

        Window          w = XmuClientWindow(dpy, kids[n]);

        XGetWindowProperty(dpy, w, atom,
                           0, (65536 / sizeof(long)),
                           False, XA_STRING,
                           &type, &format, &nitems, &bytesafter,
                           &args);

        if (!args)
            continue;
	printf("query 0x%08lx: ",w);
	for (i = 0; i < nitems; i += strlen(args + i) + 1)
	    printf("%s ", args + i);
	printf("\n");
        XFree(args);

        result = w;
#if 0 /* there might be more than window */
        break;
#endif
    }
    return result;
}

void
pass_cmd(Display *dpy, Atom atom, Window win, int argc, char **argv)
{
    int             i, len;
    char           *pass;

    printf("ctrl  0x%08lx: ",win);
    for (len = 0, i = 0; i < argc; i++) {
	printf("%s ",argv[i]);
        len += strlen(argv[i]) + 1;
    }
    printf("\n");
    pass = malloc(len);
    pass[0] = 0;
    for (len = 0, i = 0; i < argc; i++)
        strcpy(pass + len, argv[i]),
            len += strlen(argv[i]) + 1;
    XChangeProperty(dpy, win,
                    atom, XA_STRING,
                    8, PropModeReplace,
                    pass, len);
    free(pass);
}

void
usage(char *argv0)
{
    char *prog;

    if (NULL != (prog = strrchr(argv0,'/')))
	prog++;
    else
	prog = argv0;

    fprintf(stderr,
"This is a \"remote control\" for xawtv\n"
"usage: %s [ options ] [ command ]\n"
"\n"
"available options:\n"
"    -d display\n"
"        set X11 display\n"
"    -i window ID\n"
"        select xawtv window\n"
"\n"
"available commands:\n"
"    setstation <name> | <nr> | next | prev\n"
"        Tune in some station from $HOME/.xawtv. Takes either name or the\n"
"        number (0 = first entry) of the config file entry as argument.\n"
"    setchannel <name> | next | prev\n"
"        Tune in some channel.  Takes the channel number as argument\n"
"    capture on | off\n"
"        Turn on/off video\n"
"    volume mute | dec | inc | <number>\n"
"        number (range 0 - 65536) sets the volume, the other arguments\n"
"        (un)mute sound or increase/decrease volume\n"
"    grab [ <format> [ <size> [ <filename> ]]]\n"
"        capture a single image\n"
"        format     ppm | jpeg                     ppm is default\n"
"        size       full | win | <width>x<height>  full is default\n"
"        filename   default is snap*.jpeg (the same xawtv uses\n"
"                   if you press the G or J key to capture an image)\n"
"                   You should use absolute paths here...\n"
"    msg text\n"
"        display \"text\" in the window title (onscreen display if in\n"
"        fullscreen mode\n"
"\n"
	    ,prog);
}

int
main(int argc, char *argv[])
{
    Display  *dpy;
    char     *dpyname = NULL;
    Window   win,id = 0;
    Atom     station,remote;
    int      c;

    for (;;) {
	c = getopt(argc, argv, "hd:i:");
	if (c == -1)
	    break;
	switch (c) {
	case 'd':
	    dpyname = optarg;
	    break;
	case 'i':
	    id = atoi(optarg);
	    break;
	case 'h':
	default:
	    usage(argv[0]);
	    exit(1);
	}
    }

    if (NULL == (dpy = XOpenDisplay(dpyname))) {
	fprintf(stderr,"can't open display %s\n", dpyname?dpyname:"");
	exit(1);
    }
    XSetErrorHandler(x11_error_dev_null);
    station = XInternAtom(dpy, "_XAWTV_STATION", False);
    remote =  XInternAtom(dpy, "_XAWTV_REMOTE",  False);
    
    if (0 == (win = find_window(dpy,station)) &&
	0 == id) {
	fprintf(stderr,"xawtv not running\n");
	exit(2);
    }
    if (argc > optind)
	pass_cmd(dpy, remote, (id != 0) ? id : win, argc-optind, argv+optind);

    XCloseDisplay(dpy);
    return 0;
}
