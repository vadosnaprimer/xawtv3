#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

#include <X11/Intrinsic.h>

#include "config.h"
#include "grab-ng.h"

#include "grab.h"
#include "commands.h"
#include "writefile.h"
#include "channel.h"
#include "webcam.h"
#include "frequencies.h"
#include "sound.h"

/* ----------------------------------------------------------------------- */

/* feedback for the user */
void (*update_title)(char *message);
void (*display_message)(char *message);
void (*vtx_message)(int argc, char **argv);

/* for updating GUI elements / whatever */
void (*attr_notify)(struct ng_attribute *attr, int val);
void (*volume_notify)(void);
void (*freqtab_notify)(void);
void (*setfreqtab_notify)(void);
void (*setstation_notify)(void);

/* gets called _before_ channel switches */
void (*channel_switch_hook)(void);

/* capture overlay/grab/off */
void (*set_capture_hook)(int old, int new);

/* toggle fullscreen */
void (*fullscreen_hook)(void);
void (*exit_hook)(void);
void (*capture_get_hook)(void);
void (*capture_rel_hook)(void);
void (*movie_hook)(int argc, char **argv);

int debug;
int do_overlay;
char *snapbase = "snap";

struct ng_video_fmt x11_fmt;
int cur_attrs[ATTR_ID_COUNT];

/* current hardware driver */
const struct ng_driver    *drv;
void                      *h_drv;
struct ng_attribute       *a_drv;
int                        f_drv;


/* ----------------------------------------------------------------------- */

static int setfreqtab_handler(char *name, int argc, char **argv);
static int setstation_handler(char *name, int argc, char **argv);
static int setchannel_handler(char *name, int argc, char **argv);

static int capture_handler(char *name, int argc, char **argv);
static int volume_handler(char *name, int argc, char **argv);
static int attr_handler(char *name, int argc, char **argv);
static int dattr_handler(char *name, int argc, char **argv);

static int snap_handler(char *name, int argc, char **argv);
static int webcam_handler(char *name, int argc, char **argv);
static int movie_handler(char *name, int argc, char **argv);
static int fullscreen_handler(char *name, int argc, char **argv);
static int msg_handler(char *name, int argc, char **argv);
static int showtime_handler(char *name, int argc, char **argv);
static int vtx_handler(char *name, int argc, char **argv);
static int exit_handler(char *name, int argc, char **argv);

static int keypad_handler(char *name, int argc, char **argv);

static struct COMMANDS {
    char  *name;
    int    min_args;
    int   (*handler)(char *name, int argc, char **argv);
} commands[] = {
    { "setstation", 0, setstation_handler },
    { "setchannel", 0, setchannel_handler },
    { "setfreqtab", 1, setfreqtab_handler },

    { "capture",    1, capture_handler    },

    { "setnorm",    1, attr_handler       },
    { "setinput",   1, attr_handler       },
    { "setattr",    1, attr_handler       },
    { "color",      0, attr_handler       },
    { "hue",        0, attr_handler       },
    { "bright",     0, attr_handler       },
    { "contrast",   0, attr_handler       },

    { "mute",       0, volume_handler     },
    { "volume",     0, volume_handler     },
    { "attr",       0, dattr_handler      },

    { "snap",       0, snap_handler       },
    { "webcam",     1, webcam_handler     },
    { "movie",      1, movie_handler      },
    { "fullscreen", 0, fullscreen_handler },
    { "msg",        1, msg_handler        },
    { "vtx",        0, vtx_handler        },
    { "message",    0, msg_handler        },
    { "exit",       0, exit_handler       },
    { "quit",       0, exit_handler       },
    { "bye",        0, exit_handler       },

    { "keypad",     1, keypad_handler     },
    { "showtime",   0, showtime_handler   },

    { NULL, 0, NULL }
};

#if 0
/* FIXME */
static int cur_dattr = 0;
static char *dattr[] = { "volume", "bright", "contrast", "color", "hue" };
#define NUM_DATTR (sizeof(dattr)/sizeof(char*))
#endif

static int keypad_state = -1;

/* ----------------------------------------------------------------------- */

int
do_va_cmd(int argc, ...)
{
    va_list ap;
    int  i;
    char *argv[32];
    
    va_start(ap,argc);
    for (i = 0; i < argc; i++)
	argv[i] = va_arg(ap,char*);
    argv[i] = NULL;
    va_end (ap);
    return do_command(argc,argv);
}

int
do_command(int argc, char **argv)
{
    int i;
    
    if (argc == 0) {
	fprintf(stderr,"do_command: no argument\n");
	return -1;
    }
    if (debug) {
	fprintf(stderr,"cmd:");
	for (i = 0; i < argc; i++) {
	    fprintf(stderr," \"%s\"",argv[i]);
	}
	fprintf(stderr,"\n");
    }

    for (i = 0; commands[i].name != NULL; i++)
	if (0 == strcasecmp(commands[i].name,argv[0]))
	    break;
    if (commands[i].name == NULL) {
	fprintf(stderr,"no handler for %s\n",argv[0]);
	return -1;
    }
    if (argc-1 < commands[i].min_args) {
	fprintf(stderr,"no enough args for %s\n",argv[0]);
	return -1;
    } else {
	return commands[i].handler(argv[0],argc-1,argv+1);
    }
}

char**
split_cmdline(char *line, int *count)
{
    static char cmdline[1024];
    static char *argv[32];
    int  argc,i;

    strcpy(cmdline,line);
    for (argc=0, i=0; argc<31;) {
	argv[argc++] = cmdline+i;
	while (cmdline[i] != ' ' &&
	       cmdline[i] != '\t' &&
	       cmdline[i] != '\0')
	    i++;
	if (cmdline[i] == '\0')
	    break;
	cmdline[i++] = '\0';
	while (cmdline[i] == ' ' ||
	       cmdline[i] == '\t')
	    i++;
	if (cmdline[i] == '\0')
	    break;
    }
    argv[argc] = NULL;

    *count = argc;
    return argv;
}

/* ----------------------------------------------------------------------- */

/* sharing code does'nt work well for this one ... */
static void
set_capture(int capture)
{
    static int last_on = 0;

    if (set_capture_hook) {
	if (capture == CAPTURE_ON)
	    capture = last_on;
	
	if (capture == CAPTURE_OVERLAY) {
	    /* can we do overlay ?? */
	    if (!(f_drv & CAN_OVERLAY))
		capture = CAPTURE_GRABDISPLAY;
	    if (!do_overlay)
		capture = CAPTURE_GRABDISPLAY;
	}

	if (cur_capture != capture) {
	    set_capture_hook(cur_capture,capture);
	    cur_capture = capture;
	}
	
	if (cur_capture != CAPTURE_OFF)
	    last_on = cur_capture;
    }
}

static void
set_attr(struct ng_attribute *attr, int val)
{
    if (NULL == attr)
	return;

    drv->write_attr(h_drv,attr,val);
    cur_attrs[attr->id] = val;
    if (attr_notify)
	attr_notify(attr,val);
}

static void
set_volume(void)
{
    struct ng_attribute *attr;
    int vol;
    
    if (have_mixer) {
	/* sound card */
	vol = cur_volume * 100 / 65536;
	mixer_set_volume(vol);
	cur_mute ? mixer_mute() : mixer_unmute();
    } else {
	/* v4l */
	if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_VOLUME)))
	    drv->write_attr(h_drv,attr,cur_volume);
	if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_MUTE)))
	    drv->write_attr(h_drv,attr,cur_mute);
    }

    if (volume_notify)
	volume_notify();
}

static void
set_freqtab(int j)
{
    chantab   = j;
    chanlist  = chanlists[chantab].list;
    chancount = chanlists[chantab].count;

    /* cur_channel might be invalid (>chancount) right now */
    cur_channel = -1;
    /* this is valid for (struct CHANNEL*)->channel too    */
    calc_frequencies();

    if (freqtab_notify)
	freqtab_notify();
}

static void
set_title(void)
{
    static char  title[256];
    const char *norm;

    keypad_state = -1;
    if (update_title) {
	if (-1 != cur_sender) {
	    sprintf(title,"%s",channels[cur_sender]->name);
	} else if (-1 != cur_channel) {
	    sprintf(title,"channel %s",chanlist[cur_channel].name);
	    if (cur_fine != 0)
		sprintf(title+strlen(title)," (%d)",cur_fine);
	    norm = ng_attr_getstr(ng_attr_byid(a_drv,ATTR_ID_NORM),cur_norm);
	    sprintf(title+strlen(title)," (%s/%s)",
		    norm ? norm : "???", chanlists[chantab].name);
	} else {
	    sprintf(title,"???");
	}
	update_title(title);
    }
}

static void
set_msg_int(const char *name, int val)
{
    static char  title[256];
    
    if (display_message) {
	sprintf(title,"%s: %d%%",name,val*100/65535);
	display_message(title);
    }
}

static void
set_msg_str(char *name, char *val)
{
    static char  title[256];
    
    if (display_message) {
	sprintf(title,"%s: %s",name,val);
	display_message(title);
    }
}

/* ----------------------------------------------------------------------- */

#define STEP (65536/100)

static int update_int(int old, char *new)
{
    int ret = old;
    
    if (0 == strcasecmp(new,"inc"))
        ret += STEP;
    else if (0 == strcasecmp(new,"dec"))
	ret -= STEP;
    else if (new[0] == '+')
	ret += attr_to_int(new+1);
    else if (new[0] == '-')
	ret -= attr_to_int(new+1);
    else if (isdigit(new[0]))
	ret = attr_to_int(new);
    else
	fprintf(stderr,"update_int: can't parse %s\n",new);

    if (ret < 0)     ret = 0;
    if (ret > 65535) ret = 65535;

    return ret;
}

/* ----------------------------------------------------------------------- */

void
attr_init()
{
    struct ng_attribute *attr;
    int val;

    for (attr = a_drv; attr->name != NULL; attr++) {
	if (attr->id == ATTR_ID_VOLUME ||
	    attr->id == ATTR_ID_MUTE)
	    continue;
	val = drv->read_attr(h_drv,attr);
	if (attr_notify)
	    attr_notify(attr,val);
    }
}

void
audio_init()
{
    struct ng_attribute *attr;

    if (have_mixer) {
	cur_volume = mixer_get_volume() * 65535/100;
	cur_mute   = 0;
    } else {
	if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_VOLUME)))
	    cur_volume = drv->read_attr(h_drv,attr);
	if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_MUTE)))
	    cur_mute = drv->read_attr(h_drv,attr);
    }
    if (volume_notify)
	volume_notify();
}

void
audio_on()
{
    struct ng_attribute *attr;

    if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_MUTE)))
	drv->write_attr(h_drv,attr,0);
}

void
audio_off()
{
    struct ng_attribute *attr;

    if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_MUTE)))
	drv->write_attr(h_drv,attr,1);
}

void
set_defaults()
{
    struct ng_attribute *attr;

    /* image parameters */
    if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_COLOR)))
	set_attr(attr,defaults.color);
    if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_BRIGHT)))
	set_attr(attr,defaults.bright);
    if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_HUE)))
	set_attr(attr,defaults.hue);
    if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_CONTRAST)))
	set_attr(attr,defaults.contrast);
    if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_INPUT)))
	set_attr(attr,defaults.input);
    if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_NORM)))
	set_attr(attr,defaults.norm);
    set_capture(defaults.capture);

    cur_channel  = defaults.channel;
    cur_fine     = defaults.fine;
    cur_freq     = defaults.freq;
    if (f_drv & CAN_TUNE)
	drv->setfreq(h_drv,defaults.freq);
}

/* ----------------------------------------------------------------------- */

static int setstation_handler(char *name, int argc, char **argv)
{
    struct ng_attribute *attr,*mute;
    int i;

    if (0 == argc) {
	set_title();
	return 0;
    }
    
    if (count && 0 == strcasecmp(argv[0],"next")) {
	i = (cur_sender+1) % count;
    } else if (count && 0 == strcasecmp(argv[0],"prev")) {
	i = (cur_sender+count-1) % count;
    } else if (count && 0 == strcasecmp(argv[0],"back")) {
	if (-1 == last_sender)
	    return -1;
	i = last_sender;
    } else {
	/* search the configured channels first... */
	for (i = 0; i < count; i++)
	    if (0 == strcasecmp(channels[i]->name,argv[0]))
		break;
	/* ... if it failes, take the argument as index */
	if (i == count)
	    i = atoi(argv[0]);
    }

    /* ok ?? */
    if (i < 0 || i >= count)
	return -1;
    
    /* switch ... */
    if (channel_switch_hook)
	channel_switch_hook();

    last_sender = cur_sender;
    cur_sender = i;

    mute = ng_attr_byid(a_drv,ATTR_ID_MUTE);
    if (!cur_mute) {
	if (have_mixer)
	    mixer_mute();
	else if (mute)
	    drv->write_attr(h_drv,mute,1);
    }

    /* image parameters */
    if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_COLOR)))
	set_attr(attr,channels[i]->color);
    if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_BRIGHT)))
	set_attr(attr,channels[i]->bright);
    if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_HUE)))
	set_attr(attr,channels[i]->hue);
    if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_CONTRAST)))
	set_attr(attr,channels[i]->contrast);
    set_capture(channels[i]->capture);
    
    /* input / norm */
    if (cur_input != channels[i]->input) {
	if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_INPUT)))
	    drv->write_attr(h_drv,attr,channels[i]->input);
	cur_input = channels[i]->input;
    }
    if (cur_norm != channels[i]->norm) {
	if (NULL != (attr = ng_attr_byid(a_drv,ATTR_ID_NORM)))
	    drv->write_attr(h_drv,attr,channels[i]->norm);
	cur_norm = channels[i]->norm;
    }
    
    /* station */
    cur_channel  = channels[i]->channel;
    cur_fine     = channels[i]->fine;
    cur_freq     = channels[i]->freq;
    if (f_drv & CAN_TUNE)
	drv->setfreq(h_drv,channels[i]->freq);
    
    set_title();
    if (setstation_notify)
	setstation_notify();

    if (!cur_mute) {
	usleep(20000);
	if (have_mixer)
	    mixer_unmute();
	else if (mute)
	    drv->write_attr(h_drv,mute,0);
    }
    return 0;
}

static int setchannel_handler(char *name, int argc, char **argv)
{
    struct ng_attribute *mute;
    int c,i;
    
    if (0 == argc) {
	set_title();
	return 0;
    }
    
    if (0 == strcasecmp(argv[0],"next")) {
	cur_channel = (cur_channel+1) % chancount;
	cur_fine = defaults.fine;
    } else if (0 == strcasecmp(argv[0],"prev")) {
	cur_channel = (cur_channel+chancount-1) % chancount;
	cur_fine = defaults.fine;
    } else if (0 == strcasecmp(argv[0],"fine_up")) {
	cur_fine++;
    } else if (0 == strcasecmp(argv[0],"fine_down")) {
	cur_fine--;
    } else {
	if (-1 != (c = lookup_channel(argv[0]))) {
	    cur_channel = c;
	    cur_fine = defaults.fine;
	}
    }

    if (0 != strncmp(argv[0],"fine",4)) {
	/* look if there is a known station on that channel */
	for (i = 0; i < count; i++) {
	    if (cur_channel == channels[i]->channel) {
		char *argv[2];
		argv[0] = channels[i]->name;
		argv[1] = NULL;
		return setstation_handler("", argc, argv);
	    }
	}
    }
    
    if (channel_switch_hook)
	channel_switch_hook();

    cur_sender  = -1;
    cur_freq = get_freq(cur_channel)+cur_fine;

    mute = ng_attr_byid(a_drv,ATTR_ID_MUTE);
    if (!cur_mute) {
	if (have_mixer)
	    mixer_mute();
	else if (mute)
	    drv->write_attr(h_drv,mute,1);
    }

    set_capture(defaults.capture);
    if (f_drv & CAN_TUNE)
	drv->setfreq(h_drv,cur_freq);

    set_title();
    if (setstation_notify)
	setstation_notify();

    if (!cur_mute) {
	usleep(20000);
	if (have_mixer)
	    mixer_unmute();
	else if (mute)
	    drv->write_attr(h_drv,mute,0);
    }
    return 0;
}

/* ----------------------------------------------------------------------- */

static void
print_choices(char *name, char *value, struct STRTAB *tab)
{
    int i;
    
    fprintf(stderr,"unknown %s: '%s' (available: ",name,value);
    for (i = 0; tab[i].str != NULL; i++)
	fprintf(stderr,"%s'%s'", (0 == i) ? "" : ", ", tab[i].str);
    fprintf(stderr,")\n");
}

static int setfreqtab_handler(char *name, int argc, char **argv)
{
    int i;

    i = str_to_int(argv[0],chanlist_names);
    if (i != -1)
	set_freqtab(i);
    else
	print_choices("freqtab",argv[0],chanlist_names);
    return 0;
}

static int capture_handler(char *name, int argc, char **argv)
{
    int i;

    if (0 == strcasecmp(argv[0],"toggle")) {
	i = (cur_capture == CAPTURE_OFF) ? CAPTURE_ON : CAPTURE_OFF;
    } else {
	i = str_to_int(argv[0],captab);
    }
    if (i != -1)
	set_capture(i);
    return 0;
}

/* ----------------------------------------------------------------------- */

static int volume_handler(char *name, int argc, char **argv)
{
    if (0 == argc)
	goto display;
    
    if (0 == strcasecmp(argv[0],"mute")) {
	/* mute on/off/toggle */
	if (argc > 1) {
	    switch (str_to_int(argv[1],booltab)) {
	    case 0:  cur_mute = 0; break;
	    case 1:  cur_mute = 1; break;
	    default: cur_mute = !cur_mute; break;
	    }
	} else {
	    cur_mute = !cur_mute;
	}
    } else {
	/* volume */
	cur_volume = update_int(cur_volume,argv[0]);
    }
    set_volume();

 display:
    if (cur_mute)
	set_msg_str("volume","muted");
    else
	set_msg_int("volume",cur_volume);
    return 0;
}

static int attr_handler(char *name, int argc, char **argv)
{
    struct ng_attribute *attr;
    int val,arg=0;

    if (0 == strcasecmp(name,"setnorm")) {
	attr = ng_attr_byname(a_drv,"norm");

    } else if (0 == strcasecmp(name,"setinput")) {
	attr = ng_attr_byname(a_drv,"input");

    } else if (0 == strcasecmp(name,"setattr") &&
	       argc > 0) {
	attr = ng_attr_byname(a_drv,argv[arg++]);

    } else {
	attr = ng_attr_byname(a_drv,name);
    }

    if (NULL == attr) {
	/* TODO: print error */
	return -1;
    }

    switch (attr->type) {
    case ATTR_TYPE_CHOICE:
	if (argc > arg) {
	    val = ng_attr_getint(attr, argv[arg]);
	    set_attr(attr,val);
	}
	break;
    case ATTR_TYPE_INTEGER:
	if (argc > arg) {
	    val = update_int(cur_attrs[attr->id],argv[arg]);
	    set_attr(attr,val);
	}
	set_msg_int(attr->name,cur_attrs[attr->id]);
	break;
    }
    return 0;
}

static int dattr_handler(char *name, int argc, char **argv)
{
#if 0
    int i;
    
    if (argc > 0 && 0 == strcasecmp(argv[0],"next")) {
	cur_dattr++;
	cur_dattr %= NUM_DATTR;
	argc = 0;
    }
    if (argc > 0) {
	for (i = 0; i < NUM_DATTR; i++)
	    if (0 == strcasecmp(argv[0],dattr[i]))
		break;
	if (i < NUM_DATTR) {
	    cur_dattr = i;
	    argc = 0;
	}
    }
    if (0 == cur_dattr)
	return volume_handler("volume",argc,argv);
    else
	return attr_handler(dattr[cur_dattr],argc,argv);
#else
    return 0;
#endif
}

/* ----------------------------------------------------------------------- */

static int snap_handler(char *hname, int argc, char **argv)
{
    char message[512];
    char *filename = NULL;
    char *name;
    int   jpeg = 0;
    int   ret = 0;
    struct ng_video_fmt fmt;
    struct ng_video_buf *buf = NULL;

    if (!(f_drv & CAN_CAPTURE)) {
	fprintf(stderr,"grabbing: not supported\n");
	return -1;
    }

    if (0 != cur_movie) {
	if (display_message)
	    display_message("grabber busy");
	return -1;
    }

    if (capture_get_hook)
	capture_get_hook();

    /* format */
    if (argc > 0) {
	if (0 == strcasecmp(argv[0],"jpeg"))
	    jpeg = 1;
	if (0 == strcasecmp(argv[0],"ppm"))
	    jpeg = 0;
    }

    /* size */
    memset(&fmt,0,sizeof(fmt));
    fmt.fmtid  = VIDEO_RGB24;
    fmt.width  = 2048;
    fmt.height = 1572;
    if (argc > 1) {
	if (0 == strcasecmp(argv[1],"full")) {
	    /* nothing */
	} else if (0 == strcasecmp(argv[1],"win")) {
	    fmt.width  = x11_fmt.width;
	    fmt.height = x11_fmt.height;
	} else if (2 == sscanf(argv[1],"%dx%d",&fmt.width,&fmt.height)) {
	    /* nothing */
	} else {
	    return -1;
	}
    }

    /* filename */
    if (argc > 2)
	filename = argv[2];
    
    if (0 != ng_grabber_setparams(&fmt,1) ||
	NULL == (buf = ng_grabber_capture(NULL,1))) {
	if (display_message)
	    display_message("grabbing failed");
	ret = -1;
	goto done;
    }

    if (NULL == filename) {
	if (-1 != cur_sender) {
	    name = channels[cur_sender]->name;
	} else if (-1 != cur_channel) {
	    name = chanlist[cur_channel].name;
	} else {
	    name = "???";
	}
	filename = snap_filename(snapbase, name, jpeg ? "jpeg" : "ppm");
    }

    if (jpeg) {
	if (-1 == write_jpeg(filename, buf, jpeg_quality, 0)) {
	    sprintf(message,"open %s: %s\n",filename,strerror(errno));
	} else {
	    sprintf(message,"saved jpeg: %s",filename);
	}
    } else {
	if (-1 == write_ppm(filename, buf)) {
	    sprintf(message,"open %s: %s\n",filename,strerror(errno));
	} else {
	    sprintf(message,"saved ppm: %s",filename);
	}
    }
    if (display_message)
	display_message(message);

done:
    if (NULL != buf)
	ng_release_video_buf(buf);
    if (capture_rel_hook)
	capture_rel_hook();
    return ret;
}

static int webcam_handler(char *hname, int argc, char **argv)
{
    struct ng_video_fmt fmt;
    struct ng_video_buf *buf;

    if (webcam)
	free(webcam);
    webcam = strdup(argv[0]);

    /* if either avi recording or grabdisplay is active, we do
       /not/ stop capture and switch the video format.  The next
       capture will send a copy of the frame to the webcam thread
       and it has to deal with it as-is */
    if (cur_movie)
	return 0;
    if (cur_capture == CAPTURE_GRABDISPLAY)
	return 0;

    /* if no capture is running we can switch to RGB first to make
       the webcam happy */
    if (capture_get_hook)
	capture_get_hook();
    fmt = x11_fmt;
    fmt.fmtid = VIDEO_RGB24;
    ng_grabber_setparams(&fmt,0);
    buf = ng_grabber_capture(NULL,1);
    ng_release_video_buf(buf);
    if (capture_rel_hook)
	capture_rel_hook();
    return 0;
}

static int movie_handler(char *name, int argc, char **argv)
{
    if (!movie_hook)
	return 0;
    movie_hook(argc,argv);
    return 0;
}

static int
fullscreen_handler(char *name, int argc, char **argv)
{
    if (fullscreen_hook)
	fullscreen_hook();
    return 0;
}

static int
msg_handler(char *name, int argc, char **argv)
{
    if (display_message)
	display_message(argv[0]);
    return 0;
}

static int
showtime_handler(char *name, int argc, char **argv)
{
    char timestr[6];
    struct tm *times;
    time_t timet;

    timet = time(NULL);
    times = localtime(&timet);
    strftime(timestr, 6, "%k:%M", times);
    if (display_message)
	display_message(timestr);
    return 0;
}

static int
vtx_handler(char *name, int argc, char **argv)
{
    if (vtx_message)
	vtx_message(argc,argv);
    return 0;
}

static int
exit_handler(char *name, int argc, char **argv)
{
    if (exit_hook)
	exit_hook();
    return 0;
}

/* ----------------------------------------------------------------------- */

static int
keypad_handler(char *name, int argc, char **argv)
{
    int n = atoi(argv[0])%10;
    char msg[8],ch[8];

    if (debug)
	fprintf(stderr,"keypad: key %d\n",n);
    if (-1 == keypad_state) {
	if (n > 0 && n <= (keypad_ntsc ? 99 : count)) {
	    if (keypad_ntsc) {
		sprintf(ch,"%d",n);
		do_va_cmd(2,"setchannel",ch,NULL);
	    } else
		do_va_cmd(2,"setstation",channels[n-1]->name,NULL);
	}
	if (n*10 <= (keypad_ntsc ? 99 : count)) {
	    if (debug)
		fprintf(stderr,"keypad: hang: %d\n",n);
	    keypad_state = n;
	    if (display_message) {
		sprintf(msg,"%d_",n);
		display_message(msg);
	    }
	}
    } else {
	n += keypad_state*10;
	keypad_state = -1;
	if (debug)
	    fprintf(stderr,"keypad: ok: %d\n",n);
	if (n > 0 && n <= (keypad_ntsc ? 99 : count)) {
	    if (keypad_ntsc) {
		sprintf(ch,"%d",n);
		do_va_cmd(2,"setchannel",ch,NULL);
	    } else
		do_va_cmd(2,"setstation",channels[n-1]->name,NULL);
	}
    }
    return 0;
}

void
keypad_timeout(void)
{
    if (debug)
	fprintf(stderr,"keypad: timeout\n");
    if (keypad_state == cur_sender+1)
	set_title();
    keypad_state = -1;
}
