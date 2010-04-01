#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#endif
#include <pthread.h>

#include "config.h"

#include "grab-ng.h"
#include "commands.h"
#include "webcam.h"

/*-------------------------------------------------------------------------*/

int   fd_grab;

/*-------------------------------------------------------------------------*/
/* parameter negotation for capture / color space conversion / compression */

static struct ng_video_fmt gfmt;
static struct ng_video_fmt ofmt;
static int gsize;
static int osize;

static struct ng_video_conv *conv;
static void *chandle;

int
ng_grabber_setparams(struct ng_video_fmt *fmt, int fix_ratio)
{
    int i,rc;
    
    /* no capture support */
    if (!(f_drv & CAN_CAPTURE)) {
	gfmt.fmtid = -1;
	return -1;
    }
    if (conv) {
	/* warn + cleanup */
    }
    conv = NULL;

    /* try native format first */
    gfmt = *fmt;
    rc = drv->setformat(h_drv,&gfmt);
    if (debug)
	fprintf(stderr,"grab: %s (native): %s\n",
		ng_vfmt_to_desc[gfmt.fmtid],
		(0 == rc) ? "ok" : "failed");
    if (0 == rc)
	goto found;

    /* check all available conversion functions */
    for (i = 0;;) {
	conv = ng_conv_find(fmt->fmtid, &i);
	if (NULL == conv)
	    break;
	gfmt = *fmt;
	gfmt.fmtid = conv->fmtid_in;
	rc = drv->setformat(h_drv,&gfmt);
	if (debug)
	    fprintf(stderr,"grab: %s (convert): %s\n",
		    ng_vfmt_to_desc[gfmt.fmtid],
		    (0 == rc) ? "ok" : "failed");
	if (0 == rc)
	    goto found;
    }
    fprintf(stderr,"grab: no match for: %dx%d %s\n",
	    fmt->width,fmt->height,ng_vfmt_to_desc[fmt->fmtid]);
    gfmt.fmtid = -1;
    return -1;

 found:
    if (fix_ratio) {
	ng_ratio_fixup(&gfmt.width, &gfmt.height, NULL, NULL);
	gfmt.bytesperline = 0;
	if (0 != drv->setformat(h_drv,&gfmt)) {
	    fprintf(stderr,"Oops: ratio size renegotiation failed\n");
	    exit(1);
	}
    }
    fmt->width  = gfmt.width;
    fmt->height = gfmt.height;
    if (0 == fmt->bytesperline)
	fmt->bytesperline = fmt->width * ng_vfmt_to_depth[fmt->fmtid] / 8;
    ofmt = *fmt;

    osize = ofmt.height * ofmt.bytesperline;
    if (0 == osize)
	osize = ofmt.width * ofmt.height * 3;
    gsize = gfmt.height * gfmt.bytesperline;
    if (0 == gsize)
	gsize = gfmt.width * gfmt.height * 3;

    if (debug) {
	fprintf(stderr,"grab: use: %dx%d %s (size=%d)\n",
		gfmt.width,gfmt.height,ng_vfmt_to_desc[gfmt.fmtid],gsize);
	fprintf(stderr,"grab: req: %dx%d %s (size=%d)\n",
		fmt->width,fmt->height,ng_vfmt_to_desc[fmt->fmtid],osize);
    }
    if (conv)
	chandle = conv->init(&ofmt,conv->priv);
    return 0;
}

static void
ng_grabber_copy(struct ng_video_buf *dest,
		struct ng_video_buf *src)
{
    int i,sw,dw;
    unsigned char *sp,*dp;

    dw = dest->fmt.width * ng_vfmt_to_depth[dest->fmt.fmtid] / 8;
    sw = src->fmt.width * ng_vfmt_to_depth[src->fmt.fmtid] / 8;
    if (src->fmt.bytesperline == sw && dest->fmt.bytesperline == dw) {
	/* can copy in one go */
	memcpy(dest->data, src->data,
	       src->fmt.bytesperline * src->fmt.height);
    } else {
	/* copy line by line */
	dp = dest->data;
	sp = src->data;
	for (i = 0; i < src->fmt.height; i++) {
	    memcpy(dp,sp,dw);
	    dp += dest->fmt.bytesperline;
	    sp += src->fmt.bytesperline;
	}
    }
}

struct ng_video_buf*
ng_grabber_getimage(int single)
{
    if (-1 == gfmt.fmtid)
	return NULL;
    return single ? drv->getimage(h_drv) : drv->nextframe(h_drv);
}

struct ng_video_buf*
ng_grabber_convert(struct ng_video_buf *dest, struct ng_video_buf *buf)
{
    if (NULL == buf)
	return NULL;

    if (NULL == dest && NULL != conv)
        dest = ng_malloc_video_buf(&ofmt,osize);

    if (NULL != dest) {
	dest->fmt  = ofmt;
	dest->size = osize;
	if (NULL != conv) {
	    conv->frame(chandle,dest,buf);
	} else {
	    ng_grabber_copy(dest,buf);
	}
	dest->ts = buf->ts;
	ng_release_video_buf(buf);
	buf = dest;
    }

    if (NULL != webcam && 0 == webcam_put(webcam,buf)) {
	free(webcam);
	webcam = NULL;
    }
    return buf;
}

struct ng_video_buf*
ng_grabber_capture(struct ng_video_buf *dest, int single)
{
    struct ng_video_buf *buf;

    buf = ng_grabber_getimage(single);
    return ng_grabber_convert(dest,buf);
}
