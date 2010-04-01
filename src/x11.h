#ifdef HAVE_MITSHM
#define XPUTIMAGE(dpy,dr,gc,xi,a,b,c,d,w,h)                          \
    if (have_shmem)                                                  \
	XShmPutImage(dpy,dr,gc,xi,a,b,c,d,w,h,True);                 \
    else                                                             \
	XPutImage(dpy,dr,gc,xi,a,b,c,d,w,h)
#else
#define XPUTIMAGE(dpy,dr,gc,xi,a,b,c,d,w,h)                          \
	XPutImage(dpy,dr,gc,xi,a,b,c,d,w,h)
#endif

extern int  x11_native_format;
extern int  x11_byteswap;
extern int  swidth,sheight;

XImage *x11_create_ximage(Display *dpy,  XVisualInfo *vinfo,
			  int width, int height, void **shm);
void x11_destroy_ximage(Display *dpy, XImage * ximage, void *shm);
Pixmap x11_create_pixmap(Display *dpy, XVisualInfo *vinfo, Colormap colormap,
			 unsigned char *byte_data,
			 int width, int height, char *label);

typedef int   (*set_overlay)(int x, int y, int w, int h, int f,
			     struct OVERLAY_CLIP *oc, int count);

void video_new_size();
void video_overlay(set_overlay);

Visual* x11_visual(Display *dpy);
Widget video_init(Widget parent, XVisualInfo *vinfo);
void video_close(void);
