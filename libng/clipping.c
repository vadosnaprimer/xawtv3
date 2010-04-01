/*
 * generic window clipping checking / optimization code
 *
 * (c) 2001 Gerd Knorr <kraxel@bytesex.org>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "config.h"
#include "grab-ng.h"

/* --------------------------------------------------------------------- */

static void dump(char *state, struct OVERLAY_CLIP *oc, int count)
{
    int i;

    fprintf(stderr,"clip: %s - %d clips\n",state,count);
    for (i = 0; i < count; i++)
	fprintf(stderr,"clip:   %d: %dx%d+%d+%d\n",i,
		oc[i].x2 - oc[i].x1,
		oc[i].y2 - oc[i].y1,
		oc[i].x1, oc[i].y1);
}

static void drop(struct OVERLAY_CLIP *oc, int n, int *count)
{
    (*count)--;
    memmove(oc+n, oc+n+1, sizeof(struct OVERLAY_CLIP) * (*count-n));
}

/* --------------------------------------------------------------------- */

void ng_check_clipping(int width, int height, int xadjust, int yadjust,
		       struct OVERLAY_CLIP *oc, int *count)
{
    int i,j;

    if (ng_debug > 1) {
	fprintf(stderr,"clip: win=%dx%d xa=%d ya=%d\n",
		width,height,xadjust,yadjust);
	dump("init",oc,*count);
    }
    for (i = 0; i < *count; i++) {
	/* fixup coordinates */
	oc[i].x1 += xadjust;
	oc[i].x2 += xadjust;
	oc[i].y1 += yadjust;
	oc[i].y2 += yadjust;
    }
    if (ng_debug > 1)
	dump("fixup adjust",oc,*count);

    for (i = 0; i < *count; i++) {
	/* fixup borders */
	if (oc[i].x1 < 0)
	    oc[i].x1 = 0;
	if (oc[i].x2 < 0)
	    oc[i].x2 = 0;
	if (oc[i].x1 > width)
	    oc[i].x1 = width;
	if (oc[i].x2 > width)
	    oc[i].x2 = width;
	if (oc[i].y1 < 0)
	    oc[i].y1 = 0;
	if (oc[i].y2 < 0)
	    oc[i].y2 = 0;
	if (oc[i].y1 > height)
	    oc[i].y1 = height;
	if (oc[i].y2 > height)
	    oc[i].y2 = height;
    }
    if (ng_debug > 1)
	dump("fixup range",oc,*count);

    /* drop zero-sized clips */
    for (i = 0; i < *count;) {
	if (oc[i].x1 == oc[i].x2 || oc[i].y1 == oc[i].y2) {
	    drop(oc,i,count);
	    continue;
	}
	i++;
    }
    if (ng_debug > 1)
	dump("zerosize done",oc,*count);

    /* try to merge clips */
 restart_merge:
    for (j = *count - 1; j >= 0; j--) {
	for (i = 0; i < *count; i++) {
	    if (i == j)
		continue;
	    if (oc[i].x1 == oc[j].x1 &&
		oc[i].x2 == oc[j].x2 &&
		oc[i].y1 <= oc[j].y1 &&
		oc[i].y2 >= oc[j].y1) {
		if (ng_debug > 1)
		    fprintf(stderr,"clip: merge y %d,%d\n",i,j);
		if (oc[i].y2 < oc[j].y2)
		    oc[i].y2 = oc[j].y2;
		drop(oc,j,count);
		if (ng_debug > 1)
		    dump("merge y done",oc,*count);
		goto restart_merge;
	    }
	    if (oc[i].y1 == oc[j].y1 &&
		oc[i].y2 == oc[j].y2 &&
		oc[i].x1 <= oc[j].x1 &&
		oc[i].x2 >= oc[j].x1) {
		if (ng_debug > 1)
		    fprintf(stderr,"clip: merge x %d,%d\n",i,j);
		if (oc[i].x2 < oc[j].x2)
		    oc[i].x2 = oc[j].x2;
		drop(oc,j,count);
		if (ng_debug > 1)
		    dump("merge x done",oc,*count);
		goto restart_merge;
	    }
	}
    }
    if (ng_debug)
	dump("final",oc,*count);
}

/* --------------------------------------------------------------------- */
/*
 * Local variables:
 * compile-command: "(cd ..; make)"
 * End:
 */
