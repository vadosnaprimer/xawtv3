/*
 * next generation[tm] xawtv capture interfaces
 *
 * (c) 2000 Gerd Knorr <kraxel@bytesex.org>
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
#define VIDEO_MJPEG	    16  /* MJPEG */
#define VIDEO_FMT_MAX	    16

#define AUDIO_NONE           0
#define AUDIO_U8_MONO        1
#define AUDIO_U8_STEREO      2
#define AUDIO_S16_LE_MONO    3
#define AUDIO_S16_LE_STEREO  4
#define AUDIO_S16_BE_MONO    5
#define AUDIO_S16_BE_STEREO  6
#define AUDIO_FMT_MAX        6

/* --------------------------------------------------------------------- */

extern const unsigned int   ng_vfmt_to_depth[];
extern const char*          ng_vfmt_to_desc[];

extern const unsigned int   ng_afmt_to_channels[];
extern const unsigned int   ng_afmt_to_bits[];
extern const char*          ng_afmt_to_desc[];

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
/* TODO: color space conversion / compression, grabber                   */
/* maybe add filters for on-the-fly image processing later               */


/* --------------------------------------------------------------------- */

extern const struct ng_writer *ng_writers[];


/* --------------------------------------------------------------------- */
/* half rewritten -- still in grab.c                                     */

int ng_grabber_setparams(struct ng_video_fmt *fmt, int lut_valid,
			 int fix_ratio);
struct ng_video_buf* ng_grabber_capture(struct ng_video_buf *dest);
