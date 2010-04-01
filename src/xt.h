
struct ARGS {
    /* char */
    char *device;
    char *basename;

    /* int */
    int  debug;
    int  bpp;
    int  shift;
    int  xv_port;

    /* boolean */
    int  remote;
    int  readconfig;
    int  fullscreen;
    int  fbdev;
    int  xv_video;
    int  xv_scale;
    int  vidmode;
    int  dga;
    int  help;
};

extern struct ARGS args;
extern XtResource args_desc[];
extern XrmOptionDescRec opt_desc[];

extern const int args_count;
extern const int opt_count;

/*----------------------------------------------------------------------*/

extern XtAppContext      app_context;
extern Widget            app_shell, tv;
extern Display           *dpy;
extern Atom              wm_protocols,wm_delete_window;
extern Atom              xawtv_remote,xawtv_station;

extern XVisualInfo       vinfo;
extern Colormap          colormap;

extern int               have_dga;
extern int               have_vm;

#ifdef HAVE_LIBXXF86VM
extern int               vm_count;
extern XF86VidModeModeInfo **vm_modelines;
#endif
#ifdef HAVE_LIBXINERAMA
extern XineramaScreenInfo *xinerama;
extern int                nxinerama;
#endif

extern char v4l_conf[128];

/*----------------------------------------------------------------------*/

struct DO_CMD {
    int  argc;
    char *argv[8];
};

void command_cb(Widget widget, XtPointer clientdata, XtPointer call_data);

/*----------------------------------------------------------------------*/

void CommandAction(Widget, XEvent*, String*, Cardinal*);
void set_property(int freq, char *channel, char *name);

/*----------------------------------------------------------------------*/

void x11_misc_init(void);
void xfree_dga_init(void);
void xfree_xinerama_init(void);
void xfree_vm_init(void);
void grabber_init(void);
void x11_check_remote(void);
void visual_init(char *n1, char *n2);
void v4lconf_init(void);
int x11_ctrl_alt_backspace(Display *dpy);

void mouse_event(Widget widget, XtPointer client_data,
		 XEvent *event, Boolean *d);

/*----------------------------------------------------------------------*/

void create_pointers(Widget);
void create_bitmaps(Widget);

extern Cursor  left_ptr;
extern Cursor  menu_ptr;
extern Cursor  qu_ptr;
extern Cursor  no_ptr;

extern Pixmap bm_yes;
extern Pixmap bm_no;

