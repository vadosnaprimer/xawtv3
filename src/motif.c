/*
 * playing with OpenMotif...
 *
 *   (c) 2000 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>

#include "config.h"

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/Xmu/Editres.h>
#include <Xm/Xm.h>
#include <Xm/Primitive.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/RowColumn.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/CascadeB.h>
#include <Xm/Separator.h>
#include <Xm/Scale.h>
#include <Xm/Protocols.h>
#ifdef HAVE_LIBXXF86VM
# include <X11/extensions/xf86vmode.h>
# include <X11/extensions/xf86vmstr.h>
#endif
#ifdef HAVE_LIBXV
# include <X11/extensions/Xv.h>
# include <X11/extensions/Xvlib.h>
#endif

#include "grab.h"
#include "channel.h"
#include "commands.h"
#include "frequencies.h"
#include "xt.h"
#include "x11.h"
#include "xv.h"

/*----------------------------------------------------------------------*/

int jpeg_quality, mjpeg_quality, debug;

/*----------------------------------------------------------------------*/

void PopupAction(Widget, XEvent*, String*, Cardinal*);

static XtActionsRec actionTable[] = {
    { "Command",     CommandAction },
    { "Popup",       PopupAction },
};

static Widget st_menu;

static Widget control_shell;

static struct MY_TOPLEVELS {
    char        *name;
    Widget      *shell;
} my_toplevels [] = {
    { "control",  &control_shell },
};
#define TOPLEVELS (sizeof(my_toplevels)/sizeof(struct MY_TOPLEVELS))

/*----------------------------------------------------------------------*/
/* debug/test code                                                      */

void
print_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char *msg = (char*) clientdata;
    fprintf(stderr,"%s\n",msg);
}

void
toggle_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmToggleButtonCallbackStruct *tb = call_data;

    if (tb->reason != XmCR_VALUE_CHANGED)
	return;
    fprintf(stderr,"toggle: set=%s\n",tb->set ? "on" : "off");
}

/*----------------------------------------------------------------------*/

void
PopupAction(Widget widget, XEvent *event,
	    String *params, Cardinal *num_params)
{
    int i;

    /* which window we are talking about ? */
    if (*num_params > 0) {
	for (i = 0; i < TOPLEVELS; i++) {
	    if (0 == strcasecmp(my_toplevels[i].name,params[0]))
		break;
	}
    } else {
	for (i = 0; i < TOPLEVELS; i++) {
	    if (*(my_toplevels[i].shell) == widget)
		break;
	}
    }
    if (i == TOPLEVELS) {
	fprintf(stderr,"PopupAction: oops: shell widget not found (%s)\n",
		(*num_params > 0) ? params[0] : "-");
	return;
    }

    /* popup/down window */
    if (!XtIsManaged(*(my_toplevels[i].shell))) {
	XtManageChild(*(my_toplevels[i].shell));
    } else {
	XtUnmanageChild(*(my_toplevels[i].shell));
    }
}

void
free_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    free(clientdata);
}

void
unmanage_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget *shell = (Widget*)clientdata;
    XtUnmanageChild(*shell);
}

/*----------------------------------------------------------------------*/

Widget
add_cmd_menuitem(char *n, int nr, Widget parent, const char *l,
		 char *k, char *a, char *c, const char *arg)
{
    char name[16];
    XmString label,accel;
    Widget w;
    struct DO_CMD *cmd;

    sprintf(name,"%.10s%d",n,nr);
    label = XmStringCreate((char*)l,XmSTRING_DEFAULT_CHARSET);
    if (k && a) {
	accel = XmStringCreate(k,XmSTRING_DEFAULT_CHARSET);
	w = XtVaCreateManagedWidget(name,xmPushButtonWidgetClass,parent,
				    XmNlabelString,label,
				    XmNacceleratorText,accel,
				    XmNaccelerator,a,
				    NULL);
    } else {
	w = XtVaCreateManagedWidget(name,xmPushButtonWidgetClass,parent,
				    XmNlabelString,label,
				    NULL);
    }
    if (c) {
	cmd = malloc(sizeof(*cmd));
	cmd->argc    = 1;
	cmd->argv[0] = c;
	if (arg) {
	    cmd->argc    = 2;
	    cmd->argv[1] = (char*)arg;
	}
	XtAddCallback(w,XmNactivateCallback,command_cb,cmd);
	XtAddCallback(w,XmNdestroyCallback,free_cb,cmd);
    }
    XmStringFree(label);
    return w;
}

void
create_control(void)
{
    Widget form,menubar,menu,submenu,push;
    Widget volume,color;
    int i;
    
    control_shell = XtVaCreateWidget("control",transientShellWidgetClass,
				     app_shell,
				     XtNclientLeader,app_shell,
				     XtNvisual,vinfo.visual,
				     XtNcolormap,colormap,
				     XtNdepth,vinfo.depth,
				     XmNdeleteResponse,XmDO_NOTHING,
				     NULL);
    XtAddEventHandler(control_shell, (EventMask)0, True,
		      _XEditResCheckMessages, NULL);
    XmAddWMProtocolCallback(control_shell,wm_delete_window,
			    unmanage_cb,&control_shell);
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, control_shell,
				   NULL);

    /* menus */
    menubar = XmCreateMenuBar(form,"menu",NULL,0);

    /* file */
    menu = XmCreatePulldownMenu(menubar,"fileM",NULL,0);
    XtVaCreateManagedWidget("file",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    XtVaCreateManagedWidget("quit",xmPushButtonWidgetClass,menu,NULL);

    /* tv stations */
    st_menu = XmCreatePulldownMenu(menubar,"stationsM",NULL,0);
    XtVaCreateManagedWidget("stations",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,st_menu,NULL);
    
    /* options */
    menu = XmCreatePulldownMenu(menubar,"optionsM",NULL,0);
    XtVaCreateManagedWidget("options",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    push = XtVaCreateManagedWidget("pointer",xmToggleButtonWidgetClass,menu,
				   NULL);
    XtAddCallback(push,XmNvalueChangedCallback,toggle_cb,NULL);
    push = XtVaCreateManagedWidget("ontop",xmToggleButtonWidgetClass,menu,
				   NULL);
    XtAddCallback(push,XmNvalueChangedCallback,toggle_cb,NULL);
    XtVaCreateManagedWidget("sep1",xmSeparatorWidgetClass,menu,
			    NULL);

    /* options / input */
    submenu = XmCreatePulldownMenu(menu,"inputM",NULL,0);
    XtVaCreateManagedWidget("input",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,submenu,NULL);
    for (i = 0; grabber->inputs[i].str != NULL; i++)
	add_cmd_menuitem("input", i, submenu,
			 grabber->inputs[i].str, NULL, NULL,
			 "setinput",grabber->inputs[i].str);

    /* options / norm */
    submenu = XmCreatePulldownMenu(menu,"normM",NULL,0);
    XtVaCreateManagedWidget("norm",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,submenu,NULL);
    for (i = 0; grabber->norms[i].str != NULL; i++)
	add_cmd_menuitem("norm", i, submenu,
			 grabber->norms[i].str, NULL, NULL,
			 "setnorm",grabber->norms[i].str);
    XtManageChild(menubar);

    /* scales */
    volume = XtVaCreateManagedWidget("volume", xmScaleWidgetClass, form,
				     XmNtopWidget,menubar,
				     NULL);
    color = XtVaCreateManagedWidget("color", xmScaleWidgetClass, form,
				    XmNtopWidget,volume,
				    NULL);

    /* done */
    XtRealizeWidget(control_shell);
}

void
channel_menu(void)
{
    char ctrl[16],key[32],accel[64];
    int  i;
    
    for (i = 0; i < count; i++) {
	if (channels[i]->key) {
	    if (2 == sscanf(channels[i]->key,
			    "%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
			    ctrl,key)) {
		sprintf(accel,"%s<Key>%s",ctrl,key);
	    } else {
		sprintf(accel,"<Key>%s",channels[i]->key);
	    }
	} else {
	    accel[0] = 0;
	}
	add_cmd_menuitem("station", i, st_menu,
			 channels[i]->name, channels[i]->key, accel,
			 "setstation",channels[i]->name);
    }
    calc_frequencies();
}

/*----------------------------------------------------------------------*/

void
do_capture(int from, int to)
{
    /* off */
    switch (from) {
    case CAPTURE_OVERLAY:
	video_overlay(NULL);
	break;
    }
    /* on */
    switch (to) {
    case CAPTURE_OVERLAY:
	video_overlay(grabber->grab_overlay);
	break;
    }
}

/*----------------------------------------------------------------------*/

int
main(int argc, char *argv[])
{
    Dimension      w;
    int            i;
    unsigned long  freq;

    app_shell = XtVaAppInitialize(&app_context,
				  "MoTV",
				  opt_desc, opt_count,
				  &argc, argv,
				  NULL /* fallback_res */,
				  NULL);
    XtAddEventHandler(app_shell, (EventMask)0, True,
		      _XEditResCheckMessages, NULL);
    dpy = XtDisplay(app_shell);

    /* handle command line args */
    XtGetApplicationResources(app_shell,&args,
			      args_desc,args_count,
			      NULL,0);
    debug = args.debug;
    snapbase = args.basename;
    
    /* look for a useful visual */
    visual_init("motv","MoTV");
    
    /* remote display? */
    do_overlay = !args.remote;
    if (do_overlay)
	x11_check_remote();
    v4lconf_init();

    /* x11 stuff */
    XtAppAddActions(app_context,actionTable,
		    sizeof(actionTable)/sizeof(XtActionsRec));
    x11_misc_init();
    xfree_dga_init();
    xv_init(args.xv_video,args.xv_scale,args.xv_port);

    /* set hooks (command.c) */
    set_capture_hook    = do_capture;

    tv = video_init(app_shell,&vinfo,xmPrimitiveWidgetClass);
    if (NULL == grabber)
	grabber_init();

    XtVaGetValues(tv,XtNwidth,&w,NULL);
    if (!w) {
	fprintf(stderr,"The app-defaults file is not correctly installed.\n");
	fprintf(stderr,"Your fault (core dumped)\n");
	exit(1);
    }

    /* create windows */
    XSetIOErrorHandler(x11_ctrl_alt_backspace);
    /* wm_detect(dpy); */
    create_control();

    /* read config file */
    if (args.readconfig)
	read_config();
    channel_menu();
    if (fs_width && fs_height && !args.vidmode) {
	if (debug)
	    fprintf(stderr,"fullscreen mode configured (%dx%d), "
		    "VidMode extention enabled\n",fs_width,fs_height);
	args.vidmode = 1;
    }
    xfree_vm_init();
    
    XtRealizeWidget(app_shell);

    /* init hw */
    attr_init();
    audio_on();
    audio_init();
    do_va_cmd(2,"setfreqtab",chanlist_names[chantab].str);

    cur_capture = 0;
    do_va_cmd(2,"capture","overlay");
    /* set_property(0,NULL,NULL); */
    if (optind+1 == argc) {
	do_va_cmd(2,"setstation",argv[optind]);
    } else {
	if (grabber->grab_tune && 0 != (freq = grabber->grab_tune(-1,-1))) {
	    for (i = 0; i < chancount; i++)
		if (chanlist[i].freq == freq*1000/16) {
		    do_va_cmd(2,"setchannel",chanlist[i].name);
		    break;
		}
	}
	if (-1 == cur_channel) {
	    if (count > 0)
		do_va_cmd(2,"setstation","0");
	    else
		set_defaults();
	}
    }

    XtAppMainLoop(app_context);
    return 0;
}
