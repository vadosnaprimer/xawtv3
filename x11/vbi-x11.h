#ifndef _VBI_X11_H
#define _VBI_X11_H 1

#ifdef HAVE_ZVBI
#include "list.h"

struct vbi_window {
    Widget            shell,tt,subbtn,submenu;
    Widget            savebox;
    GC                gc;
    XFontStruct       *font1,*font2;
    int               w,a,d,h;
    unsigned long     colors[8];

    struct vbi_state  *vbi;
    struct vbi_page   pg;
    int               pgno,subno;
    char              *charset;

    int               newpage;
    Time              down;
    struct vbi_rect   s;

    struct list_head  selections;
};

struct vbi_window* vbi_render_init(Widget shell, Widget tt,
				   struct vbi_state *vbi);
void vbi_render_line(struct vbi_window *vw, Drawable d, struct vbi_char *ch,
		     int y, int top, int left, int right);
Pixmap vbi_export_pixmap(struct vbi_window *vw,
			 struct vbi_page *pg, struct vbi_rect *rect);

#endif /* HAVE_ZVBI */
#endif /* _VBI_X11_H */
