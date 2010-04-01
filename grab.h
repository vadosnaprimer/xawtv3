#define VIDEO_RGB08          1
#define VIDEO_RGB15          2
#define VIDEO_RGB16          4
#define VIDEO_RGB24          8
#define VIDEO_RGB32         16
#define VIDEO_GRAY          32

#define CAN_AUDIO_VOLUME     1

#define TRAP(txt) fprintf(stderr,"%s:%d:%s\n",__FILE__,__LINE__,txt);exit(1);

extern int debug;
#ifdef HAVE_LIBXXF86DGA
extern int have_dga;
#endif

/* ------------------------------------------------------------------------- */

struct STRTAB {
    int  nr;
    char *str;
};

struct OVERLAY_CLIP {
    int x1,x2,y1,y2;
};

struct GRABBER {
    char            *name;
    int             video_formats;
    int             flags;
    struct STRTAB   *norms;
    struct STRTAB   *inputs;

    /* open+close */
    int   (*grab_open)(char *opt, int sw, int sh,
		       int format, int pixmap, void *base, int width);
    int   (*grab_close)();

    int   (*grab_overlay)(int x, int y, int width, int height, int format,
			 struct OVERLAY_CLIP *oc, int count);
    void* (*grab_scr)(void *dest, int width, int height, int single); /* grab for screen display */
    void* (*grab_one)(int width, int height); /* RGB24 snap */
    
    int   (*grab_tune)(unsigned long freq);
    int   (*grab_input)(int input, int norm);
    int   (*grab_picture)(int color, int bright, int hue, int contrast);
    int   (*grab_audio)(int mute, int volume, int *mode);
};
