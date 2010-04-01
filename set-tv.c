/*
 * set-tv.c  --  (c) 1997 Gerd Knorr <kraxel@cs.tu-berlin.de>
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
#include "channels.h"
#include "grab.h"

/*--- drivers -------------------------------------------------------------*/

extern struct GRABBER grab_v4l;
struct GRABBER *grabbers[] = {
    &grab_v4l,
};

int grabber;

int debug = 0;
int have_dga = 0;
int cur_color;
int cur_bright;
int cur_hue;
int cur_contrast;
int fs_width,fs_height,fs_xoff,fs_yoff,pix_width,pix_height;

/*------------------------------------------------------------------------*/

void set_norm(int j)
{
    cur_norm = j;
    grabbers[grabber]->grab_input(-1,cur_norm);
}

void set_source(int j)
{
    cur_input = j;
    grabbers[grabber]->grab_input(cur_input,-1);
}

void
set_channel(struct CHANNEL *channel)
{
    /* image parameters */
    cur_color    = channel->color;
    cur_bright   = channel->bright;
    cur_hue      = channel->hue;
    cur_contrast = channel->contrast;
    grabbers[grabber]->grab_picture(cur_color,cur_bright,cur_hue,cur_contrast);

    /* input source */
    if (cur_input   != channel->source)
	set_source(channel->source);
    if (cur_norm    != channel->norm)
	set_norm(channel->source);

    /* station */
    cur_channel  = channel->channel;
    cur_fine     = channel->fine;
    grabbers[grabber]->grab_tune(channel->freq);

    printf("tuned in \"%s\": channel %s (%+d), freq %.3f, source %s\n",
	   channel->name, tvtuner[channel->channel].name,
	   channel->fine,
	   (float)channel->freq/16,
	   grabbers[grabber]->inputs[cur_input].str);
}

/*--- main ---------------------------------------------------------------*/

static void
grabber_init()
{
    for (grabber = 0; grabber < sizeof(grabbers)/sizeof(struct GRABBERS*);
	 grabber++) {
	if (-1 != grabbers[grabber]->grab_open
	    (NULL,0,0,0,0,NULL,0))
	    break;
    }
    if (grabber == sizeof(grabbers)/sizeof(struct GRABBERS*)) {
	fprintf(stderr,"no video grabber device available\n");
	exit(1);
    }
}


int main(int argc, char *argv[])
{
    int  c,i;

    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hv:")))
	    break;
	switch (c) {
	case 'v':
	    debug = atoi(optarg);
	    break;
	case 'h':
	default:
	    fprintf(stderr,"usage: %s [ -v debuglevel ] channel\n",argv[0]);
	    exit(1);
	}
    }
    if (optind+1 != argc) {
	fprintf(stderr,"usage: %s [ -v debuglevel ] channel\n",argv[0]);
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
    
    return 0;
}
