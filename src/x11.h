#define XPUTIMAGE(dpy,dr,gc,xi,a,b,c,d,w,h)                          \
    if (have_shmem)                                                  \
	XShmPutImage(dpy,dr,gc,xi,a,b,c,d,w,h,True);                 \
    else                                                             \
	XPutImage(dpy,dr,gc,xi,a,b,c,d,w,h)

extern int  x11_native_format;
extern int  x11_byteswap;
extern int  swidth,sheight;

XImage *x11_create_ximage(Display *dpy,  XVisualInfo *vinfo,
			  int width, int height, void **shm);
void x11_destroy_ximage(Display *dpy, XImage * ximage, void *shm);
Pixmap x11_create_pixmap(Display *dpy, XVisualInfo *vinfo, Colormap colormap,
			 unsigned char *byte_data,
			 int width, int height, char *label);

Pixmap freeze_image(Display *dpy, Colormap colormap);

struct video_handle;
extern struct video_handle vh;
int video_gd_blitframe(struct video_handle *h, struct ng_video_buf *buf);
void video_gd_start(void);
void video_gd_stop(void);
void video_gd_suspend(void);
void video_gd_restart(void);
void video_gd_configure(int width, int height);


void video_new_size(void);
void video_overlay(int state);

Visual* x11_visual(Display *dpy);
Widget video_init(Widget parent, XVisualInfo *vinfo, WidgetClass class);
void video_close(void);
