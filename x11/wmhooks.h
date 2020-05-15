void wm_detect(Display *dpy);
extern void (*wm_stay_on_top)(Display *dpy, Window win, int state);
extern void (*wm_fullscreen)(Display *dpy, Window win, int state);
