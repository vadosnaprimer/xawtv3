extern int have_xv;
extern int im_adaptor,im_port;
extern unsigned int im_formats[];
void xv_init(int,int,int port, int hwscan);

#ifdef HAVE_LIBXV

void xv_video(Window win, int width, int height, int on);
XvImage* xv_create_ximage(Display *dpy, int width, int height,
			  int format, void **shm);
void xv_destroy_ximage(Display *dpy, XvImage * xvimage, void *shm);

#define XVPUTIMAGE(dpy,port,dr,gc,xi,a,b,c,d,x,y,w,h)		\
    if (have_shmem)						\
	XvShmPutImage(dpy,port,dr,gc,xi,a,b,c,d,x,y,w,h,True);	\
    else							\
	XvPutImage(dpy,port,dr,gc,xi,a,b,c,d,x,y,w,h)

#endif
