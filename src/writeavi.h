#ifndef WRITEAVI_H
#define WRITEAVI_H 1

#include <endian.h>
#include "byteorder.h"

struct MOVIE_PARAMS {
    /* video */
    int video_format;
    int width, height;          /* size */
    int fps;                    /* frames per second */
    /* audio */
    int channels;               /* 1 = mono, 2 = stereo */
    int bits;                   /* 8/16 */
    int rate;                   /* sample rate (11025 etc) */
};

int avi_open(char *filename, struct MOVIE_PARAMS *par);
int avi_writeframe(void *data, int datasize);
int avi_writesound(void *data, int datasize);
int avi_close();

#if __BYTE_ORDER == __BIG_ENDIAN
#define AVI_SWAP2(a) SWAP2((a))
#define AVI_SWAP4(a) SWAP4((a))
#else
#define AVI_SWAP2(a) (a)
#define AVI_SWAP4(a) (a)
#endif

#endif
