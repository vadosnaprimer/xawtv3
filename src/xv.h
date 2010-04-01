extern int have_xv;
extern int have_xv_scale;
extern int im_adaptor,im_port;
void xv_init(int,int);
void xv_video(Window win, int width, int height, int on);

#ifdef HAVE_LIBXV
XvImage* xv_create_ximage(Display *dpy, int width, int height, void **shm);
void xv_destroy_ximage(Display *dpy, XvImage * xvimage, void *shm);
#endif
