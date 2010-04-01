/*
 * main.c - (c) 1997 Gerd Knorr <kraxel@cs.tu-berlin.de>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "config.h"

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xlib.h>
#include <X11/Shell.h>
#include <X11/Xaw/XawInit.h>

#include "toolbox.h"
#include "mixer.h"
#include "channel.h"
#include "TVscreen.h"

/*--- public variables ----------------------------------------------------*/

XtAppContext      app_context;
Widget            app_shell, tv;
Display           *dpy;

Atom              wm_delete_window = 0;
XtIntervalId      title_timer;

/*--- channels ------------------------------------------------------------*/

struct MENU     *cmenu    = NULL;

/*--- actions -------------------------------------------------------------*/

void CloseMainAction(Widget, XEvent*, String*, Cardinal*);
void SetResAction(Widget, XEvent*, String*, Cardinal*);
void SetChannelAction(Widget, XEvent*, String*, Cardinal*);
void TuneAction(Widget, XEvent*, String*, Cardinal*);
void MenuAction(Widget, XEvent*, String*, Cardinal*);
void ChannelAction(Widget, XEvent*, String*, Cardinal*);
void VolumeAction(Widget, XEvent*, String*, Cardinal*);
void PointerAction(Widget, XEvent*, String*, Cardinal*);

static XtActionsRec actionTable[] = {
    { "CloseMain",  CloseMainAction  },
    { "SetRes",     SetResAction },
    { "SetChannel", SetChannelAction },
    { "Tune",       TuneAction },
    { "Menu",       MenuAction },
    { "Channel",    ChannelAction },
    { "Volume",     VolumeAction },
    { "Pointer",    PointerAction },
};

/*--- exit ----------------------------------------------------------------*/

Boolean
ExitWP(XtPointer client_data)
{
    /* exit if the application is idle,
     * i.e. all the DestroyCallback's are called.
     */
    if (have_mixer)
	mixer_close();
    exit(0);
}

void
ExitCB(Widget widget, XtPointer client_data, XtPointer calldata)
{
    XtAppAddWorkProc (app_context,ExitWP, NULL);
    XtDestroyWidget(app_shell);
}

void
CloseMainAction(Widget widget, XEvent *event,
		String *params, Cardinal *num_params)
{
    ExitCB(widget,NULL,NULL);
}

/*--- tv -----------------------------------------------------------------*/

void
set_title()
{
    char title[32];

    if (-1 != cur_sender) {
	strcpy(title,channels[cur_sender]->name);
    } else {
	sprintf(title,"%s - kanal %d",normtab[cur_norm].str,cur_channel);
	if (cur_fine != 0)
	    sprintf(title+strlen(title)," (%d)",cur_fine);
    }
    XtVaSetValues(app_shell,XtNtitle,title,NULL);

    if (title_timer) {
	XtRemoveTimeOut(title_timer);
	title_timer = 0;
    }
}

void
set_channel(int i)
{
    XtVaSetValues(tv,
		  XtNcap,          channels[i]->capture ? TRUE : FALSE,
		  XtNinputSelect,  channels[i]->source,
		  XtNnorm,         channels[i]->norm,
		  XtNfreq,         channels[i]->freq,
		  
		  XtNcol,          channels[i]->color,
		  XtNbright,       channels[i]->bright,
		  XtNhue,          channels[i]->hue,
		  XtNcontrast,     channels[i]->contrast,

		  NULL);

    cur_channel = channels[i]->channel;
    cur_fine    = channels[i]->fine;
    cur_norm    = channels[i]->norm;
    cur_sender  = i;

    set_title();
}


void
set_title_timeout(XtPointer client_data, XtIntervalId *id)
{
    set_title();
}

void
SetResAction(Widget widget, XEvent *event,
	     String *params, Cardinal *num_params)
{
    static struct {
	char  *name;
	int   low;
	int   high;
    } limits[] = {
	{ "col",         0, 511 },
	{ "bright",   -128, 127 },
	{ "hue",      -128, 127 },
	{ "contrast",    0, 511 },
	{ NULL,0,0 }
    };
    
    int     i,n;
    Boolean bool;
    char    title[32];

#if 0
    fprintf(stderr,"SetRes: ");
    for (i = 0; i < *num_params; i++)
	fprintf(stderr,"%s ",params[i]);
    fprintf(stderr,"\n");
#endif
    if (*num_params != 3)
	fprintf(stderr,"SetRes: usage: SetRes(resource,type,value\n");

    if (0 == strcmp("string",params[1])) {
	XtVaSetValues(tv,params[0],params[2],NULL);
    } else if (0 == strcmp("int",params[1])) {
	switch (params[2][0]) {
	case '+':
	case '-':
	    n = 0;
	    XtVaGetValues(tv,params[0],&n,NULL);
	    n += atoi(params[2]);
	    for (i = 0; limits[i].name != NULL; i++)
		if (0 == strcmp(params[0],limits[i].name))
		    break;
	    if (limits[i].name != NULL) {
		if (n <= limits[i].low)  n = limits[i].low;
		if (n >= limits[i].high) n = limits[i].high;
		sprintf(title,"%s: %d / %d / %d",limits[i].name,
			limits[i].low,n,limits[i].high);
		XtVaSetValues(app_shell,XtNtitle,title,NULL);
		if (title_timer)
		    XtRemoveTimeOut(title_timer);
		title_timer = XtAppAddTimeOut(app_context, 5000,
					      set_title_timeout,NULL);

	    }
	    XtVaSetValues(tv,params[0],n,NULL);
	    break;
	default:
	    XtVaSetValues(tv,params[0],atoi(params[2]),NULL);
	}
    } else if (0 == strcmp("bool",params[1])) {
	bool = str_to_int(params[2],booltab);
	if (bool != -1)
	    XtVaSetValues(tv,params[0],bool,NULL);
	else {
	    if (0 == strcasecmp("toggle",params[2])) {
		XtVaGetValues(tv,params[0],&bool,NULL);
		XtVaSetValues(tv,params[0],!bool,NULL);
	    } else
		fprintf(stderr,"SetRes: %s is'nt bool\n",params[2]);
	}
    } else {
	fprintf(stderr,"SetRes: unknown type: %s\n",params[1]);
    }	
}

void
SetChannelAction(Widget widget, XEvent *event,
		 String *params, Cardinal *num_params)
{
    int i;
    
    if (*num_params != 1)
	return;
    i=atoi(params[0]);
    if (i >= 0 && i < count)
	set_channel(i);
}

void
TuneAction(Widget widget, XEvent *event,
		 String *params, Cardinal *num_params)
{
    int freq,norm;
    if (*num_params != 1)
	return;

    if (0 == strcasecmp(params[0],"next")) {
	cur_channel++;
	cur_fine = 0;
    } else if (0 == strcasecmp(params[0],"prev")) {
	cur_channel--;
	cur_fine = 0;
    } else if (0 == strcasecmp(params[0],"fine_up")) {
	cur_fine++;
    } else if (0 == strcasecmp(params[0],"fine_down")) {
	cur_fine--;
    } else if (-1 != (norm = str_to_int(params[0],normtab))) {
	cur_norm = norm;
    }
    cur_sender  = -1;

    freq = cf2freq(cur_channel,cur_fine);
    XtVaSetValues(tv,
		  XtNfreq,freq,
		  XtNnorm,cur_norm,
		  XtNinput,0,
		  XtNcap,TRUE,
		  NULL);
    set_title();
}

static struct MENU try[] = {
    {  1, "cap",     NULL, 0 },
    {  2, "mute",    NULL, 1 },
#ifdef XtNaudio
    { 10, "auto",    NULL, 0 },
    { 11, "mono",    NULL, 0 },
    { 12, "stereo",  NULL, 0 },
    { 13, "bilang1", NULL, 0 },
    { 14, "bilang2", NULL, 0 },
#endif
    { 99, "quit",    NULL, 0 },
    {  0, NULL,      NULL, 0 },
};

void
MenuAction(Widget widget, XEvent *event,
	   String *params, Cardinal *num_params)
{
    int i;
    Boolean bool;

#ifdef XtNaudio
    XtVaGetValues(tv,XtNaudio,&i,NULL);
    if (-1 == i) {
	for (i = 2; i < 7; i++) {
	    try[i].disabled = 1;
	}
    }
#endif
    switch(i=popup_menu(widget,try)) {
    case 1:
	XtVaGetValues(tv,XtNcap,&bool,NULL);
	XtVaSetValues(tv,XtNcap,!bool,NULL);
	break;
    case 2:
	if (have_mixer)
	    mixer_get_muted() ? mixer_unmute() : mixer_mute();
	break;
#ifdef XtNaudio
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
	XtVaSetValues(tv,XtNaudio,i-10,NULL);
	break;
#endif
    case 99:
	ExitCB(widget,NULL,NULL);
	break;
    default:
	/* nothing */
    }
}

void
ChannelAction(Widget widget, XEvent *event,
	      String *params, Cardinal *num_params)
{
    int i;

    if (0 == count)
	return;
    i = popup_menu(widget,cmenu);

    if (i != -1)
	set_channel(i-1);
}

void
PointerAction(Widget widget, XEvent *event,
	      String *params, Cardinal *num_params)
{
    static int is_on = 1;

    if (is_on)
	XDefineCursor(dpy, XtWindow(app_shell), no_ptr);
    else
	XDefineCursor(dpy, XtWindow(app_shell), left_ptr);
    is_on = !is_on;
}

void
channel_menu()
{
    int  i;
    char str[100];

    cmenu = malloc((count+1)*sizeof(struct MENU));
    memset(cmenu,0,(count+1)*sizeof(struct MENU));
    for (i = 0; i < count; i++) {
	channels[i]->freq = cf2freq(channels[i]->channel,channels[i]->fine);
	cmenu[i].val  = i+1;
	cmenu[i].name = channels[i]->name;
	if (channels[i]->key) {
	    sprintf(str,"<Key>%s: SetChannel(%d)",channels[i]->key,i);
	    XtOverrideTranslations(tv,XtParseTranslationTable(str));
	    sprintf(str,"%s: %s",channels[i]->key,channels[i]->name);
	    cmenu[i].title=strdup(str);
	}
    }
}

/*--- main ---------------------------------------------------------------*/

int main(int argc, char *argv[])
{
    read_config();
    if (have_mixer)
	try[1].disabled = 0;
    
    app_shell = XtAppInitialize(&app_context,
				"Xawtv",
				NULL, 0, /* opt_desc, 7, */
				&argc, argv,
				NULL /* fallback_res */,
				NULL, 0);
    XtAppAddActions(app_context,actionTable,
		    sizeof(actionTable)/sizeof(XtActionsRec));    
    wm_delete_window =
	XInternAtom(XtDisplay(app_shell), "WM_DELETE_WINDOW", False);
    XtOverrideTranslations(app_shell,XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: CloseMain()"));
    dpy = XtDisplay(app_shell);

    tv = XtVaCreateManagedWidget("tv",tvscreenWidgetClass,app_shell,
				 NULL);
    channel_menu();

    XtRealizeWidget(app_shell);
    create_pointers(app_shell);
    create_bitmaps(app_shell);
    XDefineCursor(dpy, XtWindow(app_shell), left_ptr);
    XSetWMProtocols(XtDisplay(app_shell), XtWindow(app_shell),
		    &wm_delete_window, 1);

    XtAppMainLoop(app_context);

    /* keep compiler happy */
    return 0;
}
