/*
 * very simple libng filter -- does nothing.
 * main purpose is to have one bugfree[tm] plugin for debugging.
 *
 * If you looking for a template for your own plugin better have a
 * look at invert.c.  This one is very simple too, but it serves
 * better as template because it actually does something.
 *
 * (c) 2001 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "grab-ng.h"

/* ------------------------------------------------------------------- */

static void *init(struct ng_video_fmt *out)
{
    /* don't have to carry around status info */
    static int dummy;
    return &dummy;
}

static struct ng_video_buf*
frame(void *handle, struct ng_video_buf *in)
{
    /* do nothing -- just return the frame as-is */
    return in;
}

static void fini(void *handle)
{
    /* nothing to clean up */
}

/* ------------------------------------------------------------------- */

static struct ng_filter filter = {
    name:    "nop",
    fmts:
    (1 << VIDEO_RGB08)    |
    (1 << VIDEO_GRAY)     |
    (1 << VIDEO_RGB15_LE) |
    (1 << VIDEO_RGB16_LE) |
    (1 << VIDEO_RGB15_BE) |
    (1 << VIDEO_RGB16_BE) |
    (1 << VIDEO_BGR24)    |
    (1 << VIDEO_BGR32)    |
    (1 << VIDEO_RGB24)    |
    (1 << VIDEO_RGB32)    |
    (1 << VIDEO_YUV422)   |
    (1 << VIDEO_YUV422P)  |
    (1 << VIDEO_YUV420P),
    init:    init,
    frame:   frame,
    fini:    fini,
};

extern void ng_plugin_init(void);
void ng_plugin_init(void)
{
    ng_filter_register(&filter);
}
