/*
 * set-tv.c
 *   tune in some TV station
 *
 *  (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <X11/Intrinsic.h>

#include "config.h"

#include "channel.h"
#include "frequencies.h"
#include "grab.h"

int debug = 0;
int have_dga = 0;
int x11_is_remote = 1;
char v4l_conf[] = "";

/*------------------------------------------------------------------------*/

void set_norm(int j)
{
    cur_norm = j;
    grabber->grab_input(-1,cur_norm);
}

void set_source(int j)
{
    cur_input = j;
    grabber->grab_input(cur_input,-1);
}

void
set_channel(struct CHANNEL *channel)
{
    /* image parameters */
    cur_color    = channel->color;
    cur_bright   = channel->bright;
    cur_hue      = channel->hue;
    cur_contrast = channel->contrast;
    grabber->grab_picture(cur_color,cur_bright,cur_hue,cur_contrast);

    /* input source */
    if (cur_input   != channel->input)
	set_source(channel->input);
    if (cur_norm    != channel->norm)
	set_norm(channel->norm);

    /* station */
    cur_channel  = channel->channel;
    cur_fine     = channel->fine;
    grabber->grab_tune(channel->freq);

    printf("tuned in \"%s\": channel %s (%+d), freq %.3f, source %s\n",
	   channel->name, chanlist[channel->channel].name,
	   channel->fine,
	   (float)channel->freq/16,
	   grabber->inputs[cur_input].str);
}

/*--- main ---------------------------------------------------------------*/

static void
grabber_init()
{
    grabber_open(0,0,0,0,0);
}


int main(int argc, char *argv[])
{
    int  c,i,audio=0;

    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hav:c:")))
	    break;
	switch (c) {
	case 'a':
	    audio = 1;
	    break;
	case 'v':
	    debug = atoi(optarg);
	    break;
	case 'c':
	    device = optarg;
	    break;
	case 'h':
	default:
	    fprintf(stderr,"usage: %s [ -v debuglevel ] [ -c device ] channel\n",argv[0]);
	    exit(1);
	}
    }
    if (optind+1 != argc) {
	fprintf(stderr,"usage: %s [ -v debuglevel ] [ -c device ] channel\n",argv[0]);
	exit(1);
    }

    grabber_init();
    read_config();

    cur_sender = -1;
    for (i = 0; i < count; i++)
	if (0 == strcasecmp(channels[i]->name,argv[optind]))
	    cur_sender = i;
    if (cur_sender == -1) {
	fprintf(stderr,"%s: channel \"%s\" not found in $HOME/.xawtv\n",
		argv[0],argv[optind]);
	exit(1);
    }
    channels[cur_sender]->freq =
	get_freq(channels[cur_sender]->channel) + channels[cur_sender]->fine;
    set_channel(channels[cur_sender]);
    if (!audio && grabber->grab_close)
	grabber->grab_close();
    
    return 0;
}
