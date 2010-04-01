/*
 * gl.c -- try using OpenGL to blit video frames to the screen
 *
 *   (c) 2002 Gerd Knorr <kraxel@bytesex.org>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/RowColumn.h>
#include <Xm/CascadeB.h>
#include <Xm/PushB.h>
#include <Xm/DrawingA.h>
#include <Xm/Protocols.h>

#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glx.h>

#include "RegEdit.h"
#include "grab-ng.h"

/* --------------------------------------------------------------------- */

XtAppContext  app_context;
Widget        app_shell,view;
Display       *dpy;

static String fallback_ressources[] = {
#include "gl.h"
    NULL
};

struct ARGS {
    char  *device;
    int   width;
    int   height;
    int   help;
    int   tex;
} args;

XtResource args_desc[] = {
    /* name, class, type, size, offset, default_type, default_addr */
    {
	/* Strings */
	"device",
	XtCString, XtRString, sizeof(char*),
	XtOffset(struct ARGS*,device),
	XtRString, NULL,
    },{
	/* Integer */
	"gw",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,width),
	XtRString, "0"
    },{
	"gh",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,height),
	XtRString, "0"
    },{
	"tex",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,tex),
	XtRString, "1"
    },{
	"help",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,help),
	XtRString, "0"
    }
};
const int args_count = XtNumber(args_desc);

XrmOptionDescRec opt_desc[] = {
    { "-c",          "device",      XrmoptionSepArg, NULL },
    { "-device",     "device",      XrmoptionSepArg, NULL },
    { "-x",          "gw",          XrmoptionSepArg, NULL },
    { "-width",      "gw",          XrmoptionSepArg, NULL },
    { "-y",          "gh",          XrmoptionSepArg, NULL },
    { "-height",     "gh",          XrmoptionSepArg, NULL },

    { "-img",        "tex",         XrmoptionNoArg,  "0" },
    { "-tex",        "tex",         XrmoptionNoArg,  "1" },

    { "-h",          "help",        XrmoptionNoArg,  "1" },
    { "-help",       "help",        XrmoptionNoArg,  "1" },
    { "--help",      "help",        XrmoptionNoArg,  "1" },
};
const int opt_count = (sizeof(opt_desc)/sizeof(XrmOptionDescRec));

/* --------------------------------------------------------------------- */

static inline int fix_width(int width)   { return width & ~0x03; }
static inline int fix_height(int height) { return height; }

/* --------------------------------------------------------------------- */

const struct ng_vid_driver  *drv;
void                        *h_drv;
struct ng_video_fmt         fmt,gfmt;
struct ng_video_conv        *conv;
void                        *hconv;
int                         cap_on;

static void
grab_init(char *dev)
{
    ng_debug=1;
    drv = ng_vid_open(dev ? dev : ng_dev.video,NULL,0,&h_drv);
    if (NULL == drv) {
	fprintf(stderr,"grab: no grabber device available\n");
	exit(1);
    }
    if (!(drv->capabilities(h_drv) & CAN_CAPTURE)) {
	fprintf(stderr,"grab: device does'nt support capture\n");
	exit(1);
    }
}

static int
grab_size(int format, int width, int height, int allow_convert)
{
    int i;
    
    /* cleanup */
    if (conv) {
	conv->fini(hconv);
	conv  = NULL;
	hconv = NULL;
    }
    if (cap_on) {
	drv->stopvideo(h_drv);
	cap_on = 0;
    }

    /* try native format */
    memset(&fmt,0,sizeof(fmt));
    fmt.fmtid  = format;
    fmt.width  = fix_width(width);
    fmt.height = fix_height(height);
    if (0 == drv->setformat(h_drv,&fmt)) {
	drv->startvideo(h_drv,-1,4);
	cap_on = 1;
	fprintf(stderr,"grab: native [%s]\n",ng_vfmt_to_desc[fmt.fmtid]);
	return 0;
    }
    if (!allow_convert)
	return -1;
    
    /* check all available conversion functions */
    fmt.bytesperline = fmt.width*ng_vfmt_to_depth[fmt.fmtid]/8;
    for (i = 0;;) {
	conv = ng_conv_find(fmt.fmtid, &i);
	if (NULL == conv)
	    break;
	gfmt = fmt;
	gfmt.fmtid = conv->fmtid_in;
	gfmt.bytesperline = 0;
	if (0 == drv->setformat(h_drv,&gfmt)) {
	    fmt.width  = gfmt.width;
	    fmt.height = gfmt.height;
	    hconv = conv->init(&fmt,conv->priv);
	    drv->startvideo(h_drv,-1,4);
	    cap_on = 1;
	    fprintf(stderr,"grab: convert [%s => %s]\n",
		    ng_vfmt_to_desc[gfmt.fmtid],
		    ng_vfmt_to_desc[fmt.fmtid]);
 	    return 0;
	}
    }
    return -1;
}

static struct ng_video_buf*
grab_one(void)
{
    struct ng_video_buf *cap,*buf;
    int size;

    if (NULL == (cap = drv->nextframe(h_drv))) {
	fprintf(stderr,"capturing image failed\n");
	exit(1);
    }

    if (NULL != conv) {
	size = (fmt.width * fmt.height * ng_vfmt_to_depth[fmt.fmtid]) >> 3;
        buf = ng_malloc_video_buf(&fmt,size);
	conv->frame(hconv,buf,cap);
	buf->info = cap->info;
	ng_release_video_buf(cap);
    } else {
	buf = cap;
    }
    return buf;
}

static void grab_mute(int state)
{
    struct ng_attribute *attr,*attrs;

    if (NULL == (attrs = drv->list_attrs(h_drv)))
	return;
    if (NULL == (attr = ng_attr_byid(attrs,ATTR_ID_MUTE)))
	return;
    attr->write(attr,state);
}

/* --------------------------------------------------------------------- */

static struct {
    int  fmtid;
    int  glfmt;
    int  gltype;
    char *glext;
} fmttab[] = {
    {
#ifdef GL_EXT_bgra
	fmtid:  VIDEO_BGR24,
	glfmt:  GL_BGR_EXT,
	gltype: GL_UNSIGNED_BYTE,
	glext:  "GL_EXT_bgra",
    },{
	fmtid:  VIDEO_BGR32,
	glfmt:  GL_BGRA_EXT,
	gltype: GL_UNSIGNED_BYTE,
	glext:  "GL_EXT_bgra",
    },{
#endif
	fmtid:  VIDEO_RGB24,
	glfmt:  GL_RGB,
	gltype: GL_UNSIGNED_BYTE,
    },{
	/* end of list */
    }
};

static int gl_attrib[] = { GLX_RGBA,
			   GLX_RED_SIZE, 1,
			   GLX_GREEN_SIZE, 1,
			   GLX_BLUE_SIZE, 1,
			   GLX_DOUBLEBUFFER,
			   None };
static Dimension wwidth,wheight;
static int wfmt;

static void gl_resize(Widget widget)
{
    int w,h,i;
    
    XtVaGetValues(widget,XmNwidth,&wwidth,XmNheight,&wheight,NULL);
    fprintf(stderr,"gl: resize %dx%d\n",wwidth,wheight);

    wwidth  = fix_width(wwidth);
    wheight = fix_height(wheight);
    
    glViewport(0, 0, wwidth, wheight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0.0, wwidth, 0.0, wheight);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    w = wwidth;
    h = wheight;
    if (args.width)
	w = args.width;
    if (args.height)
	h = args.height;

    if (args.tex) {
	fprintf(stderr,"gl: texture size in:  %dx%d\n",w,h);
	/* check against max size */
	glGetIntegerv(GL_MAX_TEXTURE_SIZE,&i);
	fprintf(stderr,"gl: texture size max: %d\n",i);
	if (w > i)
	    w = i;
	if (h > i)
	    h = i;
	/* textures have power-of-two x,y dimensions */
	for (i = 0; w >= (1 << i); i++)
	    ;
	w = (1 << (i-1));
	for (i = 0; h >= (1 << i); i++)
	    ;
	h = (1 << (i-1));
	fprintf(stderr,"gl: texture size out: %dx%d\n",w,h);
    }
    grab_size(fmttab[wfmt].fmtid,w,h,1);
}

static void gl_redraw(Widget widget)
{
    fprintf(stderr,"gl: redraw\n");
    glClear(GL_COLOR_BUFFER_BIT);
    glXSwapBuffers(XtDisplay(widget), XtWindow(widget));
}

static void gl_blit_img(Widget widget, struct ng_video_buf *buf)
{
    float xs = (float)wwidth  / buf->fmt.width;
    float ys = (float)wheight / buf->fmt.height;
    
    glRasterPos2i(0, wheight);
    glPixelZoom(xs,-ys);
    glDrawPixels(buf->fmt.width, buf->fmt.height,
		 fmttab[wfmt].glfmt,fmttab[wfmt].gltype,
		 buf->data);
    glXSwapBuffers(XtDisplay(widget), XtWindow(widget));
}

static void gl_blit_tex(Widget widget, struct ng_video_buf *buf)
{
    static GLint tex;
    static int width,height;

    if (0 == tex) {
	glGenTextures(1,&tex);
	glBindTexture(GL_TEXTURE_2D,tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    if (width != buf->fmt.width || height != buf->fmt.height) {
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
		     buf->fmt.width,buf->fmt.height,0,
		     fmttab[wfmt].glfmt,fmttab[wfmt].gltype,
		     buf->data);
	width  = buf->fmt.width;
	height = buf->fmt.height;
    } else {
	glTexSubImage2D(GL_TEXTURE_2D, 0,
			0,0,buf->fmt.width,buf->fmt.height,
			fmttab[wfmt].glfmt,fmttab[wfmt].gltype,
			buf->data);
    }

    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    glBegin(GL_QUADS);
    glTexCoord2f(0,1); glVertex3f(0,0,0);
    glTexCoord2f(0,0); glVertex3f(0,wheight,0);
    glTexCoord2f(1,0); glVertex3f(wwidth,wheight,0);
    glTexCoord2f(1,1); glVertex3f(wwidth,0,0);
    glEnd();
    glXSwapBuffers(XtDisplay(widget), XtWindow(widget));
    glDisable(GL_TEXTURE_2D);
}

static void gl_blit(Widget widget, struct ng_video_buf *buf)
{
    if (args.tex)
	gl_blit_tex(view,buf);
    else
	gl_blit_img(view,buf);
}

static int gl_ext(GLubyte *find)
{
    int len = strlen(find);
    const GLubyte *ext;
    GLubyte *pos;
    
    ext = glGetString(GL_EXTENSIONS);
    if (NULL == (pos = strstr(ext,find)))
	return 0;
    if (pos != ext && pos[-1] != ' ')
	return 0;
    if (pos[len] != ' ' && pos[len] != '\0')
	return 0;
    fprintf(stderr,"gl: extention %s available\n",find);
    return 1;
}

static void gl_init(Widget widget)
{
    XVisualInfo *visinfo;
    GLXContext ctx;
    int i;

    fprintf(stderr,"gl: init\n");
    visinfo = glXChooseVisual(XtDisplay(widget),
			      DefaultScreen(XtDisplay(widget)),
			      gl_attrib);
    if (!visinfo) {
	fprintf(stderr,"gl: can't get visual (rgb,db)\n");
	exit(1);
    }
    ctx = glXCreateContext( dpy, visinfo, NULL, True );
    glXMakeCurrent(XtDisplay(widget),XtWindow(widget),ctx);
    fprintf(stderr, "gl: DRI: %s\n", glXIsDirect(dpy, ctx) ? "Yes" : "No");
    
    glClearColor (0.0, 0.0, 0.0, 0.0);
    glShadeModel(GL_FLAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    /* try without converting first ... */
    for (i = 0; fmttab[i].fmtid != 0; i++) {
	if (fmttab[i].glext)
	    if (!gl_ext(fmttab[i].glext))
		continue;
	if (0 == grab_size(fmttab[i].fmtid,320,240,0)) {
	    wfmt = i;
	    return;
	}
    }
    /* ... failing that with converting */
    for (i = 0; fmttab[i].fmtid != 0; i++) {
	if (fmttab[i].glext)
	    if (!gl_ext(fmttab[i].glext))
		continue;
	if (0 == grab_size(fmttab[i].fmtid,320,240,1)) {
	    wfmt = i;
	    return;
	}
    }
    fprintf(stderr,"sorry, no way to display images\n");
    exit(1);
}

/* --------------------------------------------------------------------- */

static void event(Widget widget, XtPointer client_data,
		  XEvent *event, Boolean *d)
{
    switch (event->type) {
    case Expose:
	if (0 != event->xexpose.count)
	    return;
	fprintf(stderr,"ev: expose\n");
	gl_redraw(widget);
	break;
    case ConfigureNotify:
	fprintf(stderr,"ev: configure\n");
	XClearArea(XtDisplay(widget),XtWindow(widget),0,0,0,0,True);
	gl_resize(widget);
	break;
    case MapNotify:
	fprintf(stderr,"ev: map\n");
	gl_resize(widget);
	break;
    case UnmapNotify:
	fprintf(stderr,"ev: unmap\n");
	break;
    default:
	fprintf(stderr,"ev: unknown\n");
	break;
    }
}

static void
quit_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    grab_mute(1);
    exit(0);
}

static Boolean
idle(XtPointer client_data)
{
    static const char progress[] = "|/-\\";
    static int count,lastcount;
    static struct timeval t,lastt;
    struct ng_video_buf *buf;

    gettimeofday(&t,NULL);
    if (t.tv_sec != lastt.tv_sec) {
	fprintf(stderr,"%5d fps\r",count-lastcount);
	lastt     = t;
	lastcount = count;
    }

    buf = grab_one();
    gl_blit(view,buf);
    ng_release_video_buf(buf);
    fprintf(stderr,"%c\r",progress[(count++)%4]);
    return False;
}

static void create_widgets(Widget parent)
{
    Widget form,menubar,menu,push;

    /* form container */
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, parent,
				   NULL);

    /* menu bar */
    menubar = XmCreateMenuBar(form,"bar",NULL,0);
    XtManageChild(menubar);

    /* file menu */
    menu = XmCreatePulldownMenu(menubar,"fileM",NULL,0);
    XtVaCreateManagedWidget("file",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    push = XtVaCreateManagedWidget("quit",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,quit_cb,NULL);

    /* main view */
    view = XtVaCreateManagedWidget("view", xmDrawingAreaWidgetClass,form,
				   XmNwidth,320,
				   XmNheight,240,
				   NULL);
    XtAddEventHandler(view, StructureNotifyMask | ExposureMask,
		      True, event, NULL);
}

static void usage(void)
{
    fprintf(stderr,
	    "\n"
	    "gl - OpenGL TV test application\n"
	    "\n"
	    "use some other TV application to initialize the hardware first,\n"
	    "this is *really* a test utility only -- without controls\n"
	    "\n"
	    "known options:\n"
	    "  -h,-help        this text\n"
	    "  -c,-device dev  capture device [default: %s]\n"
	    "  -x,-width n     capture width  [default: window size]\n"
	    "  -y,-height n    capture height [default: window size]\n"
	    "     -tex         blit frames using texture mapping [default]\n"
	    "     -img         blit frames using glDrawPixels\n"
	    "\n",
	    ng_dev.video);
}

int
main(int argc, char *argv[])
{
    Atom del;

    ng_init();
    XtSetLanguageProc(NULL,NULL,NULL);
    app_shell = XtVaAppInitialize(&app_context, "gl",
				  opt_desc, opt_count,
				  &argc, argv,
				  fallback_ressources,
				  NULL);
    XtGetApplicationResources(app_shell,&args,
			      args_desc,args_count,
			      NULL,0);
    if (args.help) {
	usage();
	exit(1);
    }

    dpy = XtDisplay(app_shell);
    XmdRegisterEditres(app_shell);
    del = XInternAtom(dpy,"WM_DELETE_WINDOW",False);
    XmAddWMProtocolCallback(app_shell,del,quit_cb,NULL);
    create_widgets(app_shell);
    
    XtRealizeWidget(app_shell);
    grab_init(args.device);
    grab_mute(0);
    gl_init(view);

    XtAppAddWorkProc(app_context,idle,NULL);
    XtAppMainLoop(app_context);
    return 0;
}
