#if 0
set -x
gcc -o propwatch -I/usr/X11R6/include -L/usr/X11R6/lib \
	-lXaw3d -lXmu -lSM -lICE -lXext -lXt -lX11 $0
exit
#endif
/*
 * propwatch.c -- (c) 1998,99 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 * A tool to monitor window properties of root and application windows.
 * Nice for debugging property-based IPC of X11 programs.
 *
 * usage:
 *    propwatch [ property-list ]
 *
 * environment:
 *    $DISPLAY    - which display propwatch should use for its window.
 *    $PROPWATCH  - which display propwatch should monitor.  $DISPLAY
 *                  will be used if unset.
 *
 * see also:
 *   xprop(1), xhost(1)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/cursorfont.h>
#include <X11/Xaw/XawInit.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xmu/WinUtil.h>

#ifndef TRUE
#define TRUE   1
#define FALSE  0
#endif

/*-------------------------------------------------------------------------*/

struct WATCHLIST {
    Window                 win;
    int                    watch;
    struct WATCHLIST       *next;
    char                   text[4096];
};

/* WM */
static Atom wm_del_win;
static Atom wm_class;

/*-------------------------------------------------------------------------*/

static Widget              bl,vp;
static struct WATCHLIST    *watchlist = NULL;
static char                **watch_name;
static Atom                *watch_atom;
static int                 watch_count;

static char *watch_default[] = {
    "CUT_BUFFER0", 
    "WM_CLASS", "WM_COMMAND",
    "_MOZILLA_URL", "_XAWTV_STATION" };

static String   *str_list;
static int      str_count;
static int      verbose = 0;

static char *drop[] = {
    "WM_",
    "CUT_BUFFER",
    "KWM_ACTIVE_WINDOW",
    "KWM_WIN_",
    NULL
};

void AddWatch(Display *dpy, Window win, int i);
void DeleteWatch(Window win);
void CheckWindow(Display *dpy, Window win);
void Update(Display *dpy, Window win, Atom prop);

/*-------------------------------------------------------------------------*/

XtAppContext      app_context;
Widget            app_shell;
Cursor            left_ptr;
Cursor            menu_ptr;

void QuitAction(Widget, XEvent*, String*, Cardinal*);

void ProcessPropertyChange(Display*,XEvent*);
void ProcessKeyPress(Display*,XEvent*);
void ProcessCreateWindow(Display*,XEvent*);

/* Actions */
static XtActionsRec actionTable[] = {
    { "Quit",          QuitAction }
};

/*-------------------------------------------------------------------------*/

static int
x11_error_dev_null(Display * dpy, XErrorEvent * event)
{
    return 0;
}

static void
spy_input(XtPointer client_data, int *src, XtInputId *id)
{
    Display *spy_dpy = client_data;
    Window   root    = DefaultRootWindow(spy_dpy);
    XEvent   event;

    while (True == XCheckMaskEvent(spy_dpy, 0xffffffff, &event)) {
	if (event.type == PropertyNotify)
	    ProcessPropertyChange(spy_dpy,&event);
	else if (event.type == CreateNotify &&
		 event.xcreatewindow.parent == root)
	    ProcessCreateWindow(spy_dpy,&event);
	else if (event.type == DestroyNotify) {
	    DeleteWatch(event.xdestroywindow.window);
	}
    }
}

int
main(int argc, char *argv[])
{
    Screen *scr;
    XColor white,red,dummy;
    int i,n;
    Window root,rroot,parent,*children,w;
    Display *dpy, *spy_dpy;
    char *spy_name,title[1024];
    XEvent  event;

    /* init X11 */
    app_shell = XtAppInitialize(&app_context,
                                "Propwatch",
                                NULL, 0,
                                &argc, argv,
                                NULL,
                                NULL, 0);
    XtAppAddActions(app_context,actionTable,
                    sizeof(actionTable)/sizeof(XtActionsRec));
    XtOverrideTranslations
	(app_shell,XtParseTranslationTable("<Message>WM_PROTOCOLS: Quit()\n"));
    dpy = XtDisplay(app_shell);
    if (NULL != (spy_name = getenv("PROPWATCH"))) {
	if (NULL == (spy_dpy = XOpenDisplay(spy_name)))
	    exit(1);
	sprintf(title,"watch on %s - ",spy_name);
    } else {
	spy_dpy = dpy;
	sprintf(title,"watch - ");
    }
    root = DefaultRootWindow(spy_dpy);

    XSetErrorHandler(x11_error_dev_null);

    /* args */
    if (argc > 1) {
	watch_count = argc-1;
	watch_name  = argv+1;
    } else {
	watch_count = sizeof(watch_default)/sizeof(char*);
	watch_name  = watch_default;
    }
    watch_atom  = malloc(sizeof(Atom)*watch_count);

    /* Atoms */
    wm_del_win   = XInternAtom(dpy,"WM_DELETE_WINDOW", FALSE);
    wm_class     = XInternAtom(dpy,"WM_CLASS",         FALSE);
    for (i = 0; i < watch_count; i++) {
	watch_atom[i] = XInternAtom(spy_dpy,watch_name[i],FALSE);
	strcat(title,watch_name[i]);
	if (i < watch_count-1)
	    strcat(title,", ");
    }
    XtVaSetValues(app_shell,XtNtitle,title,NULL);

    /* nice Cursors */
    left_ptr = XCreateFontCursor(dpy,XC_left_ptr);
    menu_ptr = XCreateFontCursor(dpy,XC_right_ptr);
    scr = DefaultScreenOfDisplay(dpy);
    if (DefaultDepthOfScreen(scr) > 1) {
        if (XAllocNamedColor(dpy,DefaultColormapOfScreen(scr),
                             "white",&white,&dummy) &&
            XAllocNamedColor(dpy,DefaultColormapOfScreen(scr),
                             "red",&red,&dummy)) {
            XRecolorCursor(dpy,left_ptr,&red,&white);
            XRecolorCursor(dpy,menu_ptr,&red,&white);
        } 
    }

    /* widgets*/
    vp = XtVaCreateManagedWidget("vp",viewportWidgetClass,app_shell,
				 XtNallowHoriz, False,
				 XtNallowVert,  True,
				 XtNwidth,      400,
				 XtNheight,     250,
				 NULL);
    bl = XtVaCreateManagedWidget("box",listWidgetClass,vp,
				 XtNdefaultColumns,1,
				 XtNforceColumns,True,
				 NULL);

    XSelectInput(spy_dpy,root, /* KeyPressMask | */
		 SubstructureNotifyMask | PropertyChangeMask);
    CheckWindow(spy_dpy,root);

    XQueryTree(spy_dpy, root, &rroot, &parent, &children, &n);
    for (i = 0; i < n; i++) {
	w = XmuClientWindow(spy_dpy, children[i]);
	XSelectInput(spy_dpy,w, /* KeyPressMask | */
		     StructureNotifyMask | PropertyChangeMask);
	CheckWindow(spy_dpy,w);
    }
    XFree((char *) children);

    /* display main window */
    XtRealizeWidget(app_shell);
    XDefineCursor(dpy,XtWindow(app_shell),left_ptr);
    XSetWMProtocols(dpy,XtWindow(app_shell),&wm_del_win,1);

    /* enter main loop */
    if (spy_dpy != dpy) {
	XtAppAddInput(app_context,ConnectionNumber(spy_dpy),
		      (XtPointer)XtInputReadMask,
		      spy_input,spy_dpy);
    }
    while (TRUE) {
	XtAppNextEvent(app_context,&event);
	if (XtDispatchEvent(&event))
	    continue;
	if (event.type == PropertyNotify) {
	    ProcessPropertyChange(spy_dpy,&event);
	} else if (event.type == CreateNotify &&
		   event.xcreatewindow.parent == root) {
	    ProcessCreateWindow(spy_dpy,&event);
	} else if (event.type == KeyPress) {
	    ProcessKeyPress(spy_dpy,&event);
	} else if (event.type == DestroyNotify) {
	    DeleteWatch(event.xdestroywindow.window);
	}
    }
    
    /* keep compiler happy */
    return 0;
}

/*-------------------------------------------------------------------------*/

static int
cmp(const void *a, const void *b)
{
    char **aa = (char**)a;
    char **bb = (char**)b;
    return strcmp(*aa,*bb);
}

static void
RebuildList(void)
{
    static char *empty = "empty";
    int i;
    struct WATCHLIST *this;

    if (str_list)
	free(str_list);
    str_list = malloc(str_count*sizeof(String));
    for (i=0, this=watchlist; this!=NULL; i++, this=this->next)
	str_list[i] = this->text;
    qsort(str_list,str_count,sizeof(char*),cmp);
    XawListChange(bl,str_count ? str_list : &empty,
		  str_count ? str_count : 1,1000,1);
}

void
AddWatch(Display *dpy, Window win, int i)
{
    struct WATCHLIST   *this;

    this = malloc(sizeof(struct WATCHLIST));
    memset(this,0,sizeof(struct WATCHLIST));
    if (watchlist)
	this->next = watchlist;
    watchlist = this;

    this->win   = win;
    this->watch = i;
    str_count++;
    Update(dpy,win,watch_atom[i]);
    RebuildList();
}

void
DeleteWatch(Window win)
{
    struct WATCHLIST *this,*prev = NULL;

    for (this = watchlist; this != NULL;) {
	if (this->win == win) {
	    if (prev)
		prev->next = this->next;
	    else
		watchlist = this->next;
	    this = this->next;
	    str_count--;
	} else {
	    prev = this;
	    this = this->next;
	}
    }
    RebuildList();
}

void
CheckWindow(Display *dpy, Window win)
{
    Atom               type;
    int                format,i;
    unsigned long      nitems,rest;
    unsigned char      *data;

    for (i = 0; i < watch_count; i++) {
	if (Success != XGetWindowProperty
	    (dpy,win,watch_atom[i],
	     0,64,False,AnyPropertyType,
	     &type,&format,&nitems,&rest,&data))
	    continue;
	if (None != type) {
	    AddWatch(dpy,win,i);
	    XFree(data);
	}
    }
}

/*-------------------------------------------------------------------------*/

static void
PropertyToString(Display *dpy, Window win, Atom prop, char *value)
{
    Atom               type;
    int                format,i,j;
    unsigned long      nitems,rest;
    unsigned char      *cdata,*name;
    unsigned long      *ldata;
    char               *typename;

    if (Success != XGetWindowProperty
	(dpy,win,prop,0,64,False,AnyPropertyType,
	 &type,&format,&nitems,&rest,&cdata))
	return;
    ldata = (unsigned long*)cdata;
    switch (type) {
    case XA_STRING:
	for (i = 0, j = 0; i < nitems; i += strlen(cdata+i)+1)
	    j += sprintf(value+j,"\"%s\", ",cdata+i);
	value[j-2]=0;
	break;
    case XA_ATOM:
	for (i = 0, j = 0; i < nitems; i++) {
	    name = XGetAtomName(dpy,ldata[i]);
	    j += sprintf(value+j,"%s, ",name);
	    XFree(name);
	}
	value[j-2]=0;
	break;
    case XA_WINDOW:
	for (i = 0, j = 0; i < nitems; i++) {
	    j += sprintf(value+j,"0x%x, ",(unsigned int)ldata[i]);
	}
	value[j-2]=0;
	break;
    default:
	typename = XGetAtomName(dpy,type);
	sprintf(value,"unknown type (%s)",typename);
	XFree(typename);
	break;
    }
    XFree(cdata);    
}

void
Update(Display *dpy, Window win, Atom prop)
{
    int                n;
    struct WATCHLIST   *this;

    for (this = watchlist; this != NULL; this = this->next)
	if (this->win == win && watch_atom[this->watch] == prop)
	    break;
    if (this) {
	n = sprintf(this->text,"%8x: %s: ", (unsigned int)this->win,
		    watch_name[this->watch]);
	PropertyToString(dpy,win,prop,this->text+n);
    }
}

void
ProcessPropertyChange(Display *dpy, XEvent* event)
{
    int                i;
    struct WATCHLIST   *this;
    char               *name;

    for (i = 0; i < watch_count; i++) {
	if (watch_atom[i] == event->xproperty.atom) {
	    for (this = watchlist; this != NULL; this = this->next)
		if (this->win == event->xproperty.window &&
		    watch_atom[this->watch] == event->xproperty.atom)
		    break;
	    if (!this)
		AddWatch(dpy,event->xproperty.window, i);
	    else {
		Update(dpy,event->xproperty.window,event->xproperty.atom);
		XawListChange(bl,str_list,str_count,1000,1);
	    }
	}
    }

    if (verbose) {
	/* get it */
	name = XGetAtomName(dpy,event->xproperty.atom);

	for (i = 0; drop[i] != NULL; i++)
	    if (0 == strncmp(name,drop[i],strlen(drop[i])))
		break;
	if (NULL == drop[i])
	    printf("%8x: %s\n", (int)event->xproperty.window,name);
	XFree(name);
    }
}

void
ProcessKeyPress(Display *dpy, XEvent* event)
{
    static int last_window;

    if (event->xkey.window != last_window) {
	last_window = event->xkey.window;
	fprintf(stderr,"\n%8x: ",last_window);	
    }
    fprintf(stderr,"%d ",event->xkey.keycode);
}

/*-------------------------------------------------------------------------*/

void
ProcessCreateWindow(Display *dpy, XEvent* event)
{
    Atom            type;
    int             format;
    unsigned long   nitems,rest;
    unsigned char   *class = NULL, *class2 = NULL;
    
    if (Success != XGetWindowProperty
	(dpy,event->xcreatewindow.window,wm_class,
	 0,64,False,AnyPropertyType,
	 &type,&format,&nitems,&rest,&class))
	return;
    if (class != NULL && strlen(class)+1 < nitems)
	class2 = class + strlen(class)+1;
    
    if (verbose) {
	printf("%8x: new: %s %s\n",
	       (unsigned int)event->xcreatewindow.window,
	       class?class:(unsigned char*)"-",
	       class2?class2:(unsigned char*)"-");
    }
    
    XSelectInput(dpy, event->xcreatewindow.window, /* KeyPressMask | */
		 StructureNotifyMask | PropertyChangeMask);
    CheckWindow(dpy, event->xcreatewindow.window);
    
    if (class != NULL)
	XFree(class);
}

/*-------------------------------------------------------------------------*/

void
QuitAction(Widget widget, XEvent* event, String* arg, Cardinal* arg_count)
{
    exit(0);
}

