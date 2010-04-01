extern int have_xv;
extern int have_xv_scale;
extern int im_adaptor,im_port;
void xv_init(int,int,int port);
void xv_video(Window win, int width, int height, int on);

#ifdef HAVE_LIBXV
XvImage* xv_create_ximage(Display *dpy, int width, int height, void **shm);
void xv_destroy_ximage(Display *dpy, XvImage * xvimage, void *shm);

# ifdef HAVE_MITSHM
#  define XVPUTIMAGE(dpy,port,dr,gc,xi,a,b,c,d,x,y,w,h)		\
    if (have_shmem)						\
	XvShmPutImage(dpy,port,dr,gc,xi,a,b,c,d,x,y,w,h,True);	\
    else							\
	XvPutImage(dpy,port,dr,gc,xi,a,b,c,d,x,y,w,h)
# else
#  define XVPUTIMAGE(dpy,port,dr,gc,xi,a,b,c,d,x,y,w,h)		\
	XvPutImage(dpy,port,dr,gc,xi,a,b,c,d,x,y,w,h)
# endif
#endif
