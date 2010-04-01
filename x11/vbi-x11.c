/*
 * vbi-x11  --  render videotext into X11 drawables
 *
 *   (c) 2002 Gerd Knorr <kraxel@bytesex.org>
 */

#include "config.h"

#ifdef HAVE_ZVBI

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <iconv.h>
#include <langinfo.h>
#include <sys/types.h>
#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>

#include "atoms.h"
#include "list.h"
#include "vbi-data.h"
#include "vbi-x11.h"

/* --------------------------------------------------------------------- */

static struct {
    char *f1,*f2;
} vbi_try_fonts[] = {
    {
	f1: "-*-teletext-medium-r-normal--20-*-*-*-*-*-iso10646-1",
	f2: "-*-teletext-medium-r-normal--40-*-*-*-*-*-iso10646-1",
    },{
	f1: "-*-fixed-medium-r-normal--18-*-*-*-*-*-iso10646-1",
    },{
	f1: "fixed",
    },{
	/* end of list */
    }
};

/* --------------------------------------------------------------------- */
/* render teletext pages                                                 */

struct vbi_window*
vbi_render_init(Widget shell, Widget tt, struct vbi_state *vbi)
{
    struct vbi_window *vw;
    Colormap cmap;
    XColor color,dummy;
    int i;

    vw = malloc(sizeof(*vw));
    memset(vw,0,sizeof(*vw));
    
    vw->shell = shell;
    vw->tt    = tt;
    vw->vbi   = vbi;
    vw->gc    = XCreateGC(XtDisplay(shell),
			  RootWindowOfScreen(XtScreen(shell)),
			  0, NULL);
    
    for (i = 0; NULL == vw->font1 && NULL != vbi_try_fonts[i].f1; i++) {
	vw->font1 = XLoadQueryFont(XtDisplay(shell),vbi_try_fonts[i].f1);
	if (NULL == vbi_try_fonts[i].f2)
	    continue;
	vw->font2 = XLoadQueryFont(XtDisplay(shell),vbi_try_fonts[i].f2);
    }
    if (NULL == vw->font1) {
	fprintf(stderr,"can't load font\n");
	exit(1);
    }
    vw->a      = vw->font1->max_bounds.ascent;
    vw->d      = vw->font1->max_bounds.descent;
    vw->w      = vw->font1->max_bounds.width;
    vw->h      = vw->a + vw->d;

    XtVaGetValues(tt, XtNcolormap, &cmap, NULL);
    for (i = 0; i < 8; i++) {
	XAllocNamedColor(XtDisplay(shell), cmap, vbi_colors[i],
			 &color, &dummy);
	vw->colors[i] = color.pixel;
    }

    INIT_LIST_HEAD(&vw->selections);
    return vw;
}

void
vbi_render_line(struct vbi_window *vw, Drawable d, struct vbi_char *ch,
		int y, int top, int left, int right)
{
    XGCValues values;
    XChar2b line[40];
    XTextItem16 ti;
    int x1,x2,i,code,sy;

    for (x1 = left; x1 < right; x1 = x2) {
	for (x2 = x1; x2 < right; x2++) {
	    if (ch[x1].foreground != ch[x2].foreground)
		break;
	    if (ch[x1].background != ch[x2].background)
		break;
	    if (ch[x1].size != ch[x2].size)
		break;
	}
	sy = 1;
	if (vw->font2) {
	    if (ch[x1].size == VBI_DOUBLE_HEIGHT ||
		ch[x1].size == VBI_DOUBLE_SIZE)
		sy = 2;
	    if (ch[x1].size == VBI_DOUBLE_HEIGHT2 ||
		ch[x1].size == VBI_DOUBLE_SIZE2)
		continue;
	}

	for (i = x1; i < x2; i++) {
	    code = ch[i].unicode;
	    if (ch[i].conceal)
		code = ' ';
	    if (ch[i].size == VBI_OVER_TOP       ||
		ch[i].size == VBI_OVER_BOTTOM    ||
		ch[i].size == VBI_DOUBLE_HEIGHT2 ||
		ch[i].size == VBI_DOUBLE_SIZE2)
		code = ' ';
	    line[i-x1].byte1 = (code >> 8) & 0xff;
	    line[i-x1].byte2 =  code       & 0xff;
	}
	ti.chars  = line;
	ti.nchars = x2-x1;
	ti.delta  = 0;
	ti.font   = (1 == sy) ? vw->font1->fid : vw->font2->fid;
	
	values.function   = GXcopy;
	values.foreground = vw->colors[ch[x1].background & 7];
	XChangeGC(XtDisplay(vw->tt), vw->gc, GCForeground|GCFunction, &values);
	XFillRectangle(XtDisplay(vw->tt), d, 
		       vw->gc, (x1-left)*vw->w, (y-top)*vw->h,
		       vw->w * (x2-x1), vw->h * sy);
	
	values.foreground = vw->colors[ch[x1].foreground & 7];
	XChangeGC(XtDisplay(vw->tt), vw->gc, GCForeground, &values);
	XDrawText16(XtDisplay(vw->tt), d, vw->gc,
		    (x1-left)*vw->w, vw->a + (y-top+sy-1)*vw->h, &ti,1);
    }
}

Pixmap
vbi_export_pixmap(struct vbi_window *vw,
		  struct vbi_page *pg, struct vbi_rect *rect)
{
    Pixmap pix;
    vbi_char *ch;
    int y;

    pix = XCreatePixmap(XtDisplay(vw->tt), XtWindow(vw->tt),
			vw->w * (rect->x2 - rect->x1),
			vw->h * (rect->y2 - rect->y1),
			DefaultDepthOfScreen(XtScreen(vw->tt)));
    for (y = rect->y1; y < rect->y2; y++) {
	ch = vw->pg.text + 41*y;
	vbi_render_line(vw,pix,ch,y,rect->y1,rect->x1,rect->x2);
    }
    return pix;
}

#endif /* HAVE_ZVBI */
