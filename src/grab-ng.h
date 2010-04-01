/*
 * next generation[tm] xawtv capture interfaces
 *
 * (c) 2001 Gerd Knorr <kraxel@bytesex.org>
 *
 */

/* old stuff -- to be removed once the new stuff is complete */
#include "grab.h"

/* --------------------------------------------------------------------- */
/* defines                                                               */

#define VIDEO_NONE           0
#define VIDEO_RGB08          1  /* bt848 dithered */
#define VIDEO_GRAY           2
#define VIDEO_RGB15_LE       3  /* 15 bpp little endian */
#define VIDEO_RGB16_LE       4  /* 16 bpp little endian */
#define VIDEO_RGB15_BE       5  /* 15 bpp big endian */
#define VIDEO_RGB16_BE       6  /* 16 bpp big endian */
#define VIDEO_BGR24          7  /* bgrbgrbgrbgr (LE) */
#define VIDEO_BGR32          8  /* bgr-bgr-bgr- (LE) */
#define VIDEO_RGB24          9  /* rgbrgbrgbrgb (BE) */
#define VIDEO_RGB32         10  /* -rgb-rgb-rgb (BE) */
#define VIDEO_LUT2          11  /* lookup-table 2 byte depth */
#define VIDEO_LUT4          12  /* lookup-table 4 byte depth */
#define VIDEO_YUV422	    13  /* YUV 4:2:2 */
#define VIDEO_YUV422P       14  /* YUV 4:2:2 (planar) */
#define VIDEO_YUV420P	    15  /* YUV 4:2:0 (planar) */
#define VIDEO_MJPEG	    16  /* MJPEG (AVI) */
#define VIDEO_JPEG	    17  /* JPEG (JFIF) */
#define VIDEO_FMT_MAX	    17

#define AUDIO_NONE           0
#define AUDIO_U8_MONO        1
#define AUDIO_U8_STEREO      2
#define AUDIO_S16_LE_MONO    3
#define AUDIO_S16_LE_STEREO  4
#define AUDIO_S16_BE_MONO    5
#define AUDIO_S16_BE_STEREO  6
#define AUDIO_FMT_MAX        6

#define ATTR_TYPE_INTEGER    1   /*  range 0 - 65535  */
#define ATTR_TYPE_CHOICE     2   /*  multiple choice  */
#define ATTR_TYPE_BOOL       3   /*  yes/no           */

#define ATTR_ID_NORM         1
#define ATTR_ID_INPUT        2
#define ATTR_ID_VOLUME       3
#define ATTR_ID_MUTE         4
#define ATTR_ID_AUDIO_MODE   5
#define ATTR_ID_COLOR        6
#define ATTR_ID_BRIGHT       7
#define ATTR_ID_HUE          8
#define ATTR_ID_CONTRAST     9
#define ATTR_ID_MAX          9

#define CAN_OVERLAY          1
#define CAN_CAPTURE          2
#define CAN_TUNE             4

/* --------------------------------------------------------------------- */

extern const unsigned int   ng_vfmt_to_depth[];
extern const char*          ng_vfmt_to_desc[];

extern const unsigned int   ng_afmt_to_channels[];
extern const unsigned int   ng_afmt_to_bits[];
extern const char*          ng_afmt_to_desc[];

extern const char*          ng_attr_to_desc[];

/* --------------------------------------------------------------------- */

struct STRTAB {
    long nr;
    const char *str;
};

struct OVERLAY_CLIP {
    int x1,x2,y1,y2;
};

/* --------------------------------------------------------------------- */
/* video data structures                                                 */

struct ng_video_fmt {
    int   fmtid;         /* VIDEO_* */
    int   width;
    int   height;
    int   bytesperline;  /* zero for compressed formats */
};

struct ng_video_buf {
    struct ng_video_fmt  fmt;
    int                  size;
    char                 *data;

    /* for planar formats */
    char                 *data2;
    char                 *data3;

    /* FIXME: time (struct timeval?) */

    /*
     * the lock is for the reference counter.
     * if the reference counter goes down to zero release()
     * should be called.  priv is for the owner of the
     * buffer (can be used by the release callback)
     */
    pthread_mutex_t      lock;
    int                  refcount;
    void                 (*release)(struct ng_video_buf *buf);
    void                 *priv;
};

void ng_release_video_buf(struct ng_video_buf *buf);
struct ng_video_buf* ng_malloc_video_buf(struct ng_video_fmt *fmt,
					 int size);

/* --------------------------------------------------------------------- */
/* audio data structures                                                 */

struct ng_audio_fmt {
    int   fmtid;         /* AUDIO_* */
    int   rate;
};

struct ng_audio_buf {
    struct ng_audio_fmt  fmt;
    int                  size;
    char                 *data;

    /* FIXME: time */
};


/* --------------------------------------------------------------------- */
/* someone who receives video and/or audio data (writeavi, ...)          */

struct ng_format_list {
    const char  *name;
    const char  *desc;  /* if standard fmtid description doesn't work
			   because it's converted somehow */
    const char  *ext;
    const int   fmtid;
    const void  *priv;
};

struct ng_writer {
    const char *name;
    const char *desc;
    const struct ng_format_list *video;
    const struct ng_format_list *audio;
    const int combined; /* both audio + video in one file */

    void* (*wr_open)(char *moviename, char *audioname,
		     struct ng_video_fmt *video, const void *priv_video, int fps,
		     struct ng_audio_fmt *audio, const void *priv_audio);
    int (*wr_video)(void *handle, struct ng_video_buf *buf);
    int (*wr_audio)(void *handle, struct ng_audio_buf *buf);
    int (*wr_close)(void *handle);
};

/* --------------------------------------------------------------------- */
/* attributes                                                            */

struct ng_attribute {
    int                  id;
    const char           *name;
    int                  type;
    int                  defval;
    struct STRTAB        *choices;
    const void           *priv;
};

struct ng_attribute* ng_attr_byid(struct ng_attribute *attrs, int id);
struct ng_attribute* ng_attr_byname(struct ng_attribute *attrs, char *name);
const char* ng_attr_getstr(struct ng_attribute *attr, int value);
int ng_attr_getint(struct ng_attribute *attr, char *value);


/* --------------------------------------------------------------------- */
/* capture/overlay interface driver                                      */

struct ng_driver {
    const char *name;

    /* open/close */
    void*  (*open)(char *device);
    int    (*close)(void *handle);

    /* attributes */
    int   (*capabilities)(void *handle);
    struct ng_attribute* (*list_attrs)(void *handle);
    int   (*read_attr)(void *handle, struct ng_attribute*);
    void  (*write_attr)(void *handle, struct ng_attribute*, int val);

    /* overlay */
    int   (*setupfb)(void *handle, struct ng_video_fmt *fmt, void *base);
    int   (*overlay)(void *handle, struct ng_video_fmt *fmt, int x, int y,
		     struct OVERLAY_CLIP *oc, int count);
    
    /* capture */
    int   (*setformat)(void *handle, struct ng_video_fmt *fmt);
    int   (*startvideo)(void *handle, int fps, int buffers);
    void  (*stopvideo)(void *handle);
    struct ng_video_buf* (*nextframe)(void *handle); /* video frame */
    struct ng_video_buf* (*getimage)(void *handle);  /* single image */


    /* tuner */
    unsigned long (*getfreq)(void *handle);
    void  (*setfreq)(void *handle, unsigned long freq);
    int   (*is_tuned)(void *handle);
};

const struct ng_driver*
ng_grabber_open(char *device, struct ng_video_fmt *screen,
		void *base, void **handle);

/* --------------------------------------------------------------------- */
/* TODO: color space conversion / compression                            */
/* maybe add filters for on-the-fly image processing later               */


/* --------------------------------------------------------------------- */

extern const struct ng_driver *ng_drivers[];
extern const struct ng_writer *ng_writers[];

/* --------------------------------------------------------------------- */
/* half rewritten -- still in grab.c                                     */

int ng_grabber_setparams(struct ng_video_fmt *fmt, int lut_valid,
			 int fix_ratio);
struct ng_video_buf* ng_grabber_capture(struct ng_video_buf *dest,
					int single);
