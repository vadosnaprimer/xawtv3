#define CAN_AUDIO_VOLUME     1

#define GRAB_ATTR_VOLUME     1
#define GRAB_ATTR_MUTE       2
#define GRAB_ATTR_MODE       3

#define GRAB_ATTR_COLOR     11
#define GRAB_ATTR_BRIGHT    12
#define GRAB_ATTR_HUE       13
#define GRAB_ATTR_CONTRAST  14

#define TRAP(txt) fprintf(stderr,"%s:%d:%s\n",__FILE__,__LINE__,txt);exit(1);

/* ------------------------------------------------------------------------- */

struct STRTAB {
    long nr;
    const char *str;
};

struct OVERLAY_CLIP {
    int x1,x2,y1,y2;
};

struct GRABBER {
    char            *name;
    int             flags;
    unsigned int    colorkey;
    struct STRTAB   *norms;
    struct STRTAB   *inputs;
    struct STRTAB   *audio_modes;

    int   (*grab_open)(char *opt);
    int   (*grab_close)(void);

    /* overlay */
    int   (*grab_setupfb)(int sw, int sh, int format, void *base, int bpl);
    int   (*grab_overlay)(int x, int y, int width, int height, int format,
			  struct OVERLAY_CLIP *oc, int count);
    int   (*grab_offscreen)(int y, int width, int height, int format);

    /* capture */
    int   (*grab_setparams)(int format, int *width, int *height, int *linelength);
    void  (*grab_start)(int fps, int buffers);
    void* (*grab_capture)(void);
    void  (*grab_stop)(void);

    /* configure device */
    unsigned long (*grab_tune)(unsigned long freq, int sat);
    int   (*grab_tuned)(void);
    int   (*grab_input)(int input, int norm);

    int   (*grab_hasattr)(int id);
    int   (*grab_getattr)(int id);
    int   (*grab_setattr)(int id, int val);
};

/* ------------------------------------------------------------------------- */

extern int debug;
extern int have_dga;

extern int fd_grab;
extern int grab_ratio_x;
extern int grab_ratio_y;
extern struct GRABBER *grabber;

/* ------------------------------------------------------------------------- */

void  grabber_run_v4l_conf(void);
int   grabber_open(char *device, int sw, int sh, void *base, int format, int width);
int   grabber_setparams(int format, int *width, int *height,
			int *linelength, int lut_valid, int fix_ratio);
void* grabber_capture(void *dest, int dest_linelength, int *size);
int   grabber_sw_rate(struct timeval *start, int fps, int count);
void  grabber_fix_ratio(int *width, int *height, int *xoff, int *yoff);
