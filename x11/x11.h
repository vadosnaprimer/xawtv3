extern int  swidth,sheight;

void x11_label_pixmap(Display *dpy, Colormap colormap, Pixmap pixmap,
		      int height, char *label);
Pixmap x11_capture_pixmap(Display *dpy, XVisualInfo *vinfo, Colormap colormap,
			  int width, int height);

struct video_handle;
extern struct video_handle vh;
int video_gd_blitframe(struct video_handle *h, struct ng_video_buf *buf);
void video_gd_start(void);
void video_gd_stop(void);
void video_gd_suspend(void);
void video_gd_restart(void);
void video_gd_configure(int width, int height, int gl);

void video_new_size(void);
void video_overlay(int state);

Widget video_init(Widget parent, XVisualInfo *vinfo,
		  WidgetClass class, int bpp);
void video_close(void);
