/* 
    channel for Bt848 frame grabber driver

    Copyright (C) 1996,97 Marcus Metzler (mocm@thp.uni-koeln.de)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <X11/Intrinsic.h>

#include "grab.h"
#include "channel.h"
#include "mixer.h"
#include "chan.h"

/* ----------------------------------------------------------------------- */
/* misc common stuff, not only channel related                             */ 

struct CHANNEL  defaults   = { "defaults", NULL,
			       1, 0, 0, 5, 0, 0,
			       32768, 32768, 32768, 32768 };
struct CHANNEL  **channels = NULL;
int             count      = 0;
int             have_mixer = 0;

int    cur_sender = -1, cur_channel = 5, cur_fine = 0;
int    cur_norm = -1, cur_input = -1;

int    chan_tab = 0;

extern struct GRABBER *grabbers[];
extern int grabber;

/* ----------------------------------------------------------------------- */

int cf2freq(int channel, int fine)
{
    double freq = 0;
    int    i;
    
    for (i = 0; chan_tabs[chan_tab][i].chan_low != 0; i++) {
	if (chan_tabs[chan_tab][i].chan_low  <= channel &&
	    chan_tabs[chan_tab][i].chan_high >= channel) {
	    
	    freq = chan_tabs[chan_tab][i].freq_low +
		chan_tabs[chan_tab][i].factor *
		(channel - chan_tabs[chan_tab][i].chan_low);
	    break;
	}
    }

    return (int)(freq*16)+fine;
}

/* ----------------------------------------------------------------------- */

void
read_config()
{
    FILE *fp;
    char filename[100],line[100], tag[32], val[100];
    int i,nr = 0;
    struct CHANNEL *current = &defaults;

    sprintf(filename,"%s/%s",getenv("HOME"),".xawtv");
    fp = fopen(filename,"r");
    if (NULL == fp) {
	fprintf(stderr,"can't open config file %s\n",filename);
	return;
    }
    while (NULL != fgets(line,99,fp)) {
	nr++;
	if (line[0] == '\n' || line[0] == '#' || line[0] == '%')
	    continue;
	if (1 == sscanf(line,"[%99[^]]]",val)) {
	    if (0 == (count % 16))
		if (!count)
		    channels = malloc(sizeof(struct CHANNEL*)*16);
		else
		    channels = realloc(channels,sizeof(struct CHANNEL*)*(count+16));
	    current = channels[count] = malloc(sizeof(struct CHANNEL));
	    count++;
	    memcpy(current,&defaults,sizeof(struct CHANNEL));
	    current->name = strdup(val);
	    continue;
	}
	if (2 != sscanf(line," %31[^= ] = %99[^\n]",tag,val)) {
	    fprintf(stderr,"parse error line %d\n",nr);
	    exit(1);
	}

	if (0 == strcmp(tag,"key"))
	    current->key = strdup(val);
	
	else if (0 == strcmp(tag,"capture"))
	    current->capture = str_to_int(val,booltab);
	else if (0 == strcmp(tag,"source"))
	    current->source = str_to_int(val,grabbers[grabber]->inputs);
	else if (0 == strcmp(tag,"norm"))
	    current->norm = str_to_int(val,grabbers[grabber]->norms);
	else if (0 == strcmp(tag,"channel"))
	    current->channel = atoi(val);
	else if (0 == strcmp(tag,"fine"))
	    current->fine = atoi(val);

	else if (0 == strcmp(tag,"color"))
	    current->color = atoi(val);
	else if (0 == strcmp(tag,"bright"))
	    current->bright = atoi(val);
	else if (0 == strcmp(tag,"hue"))
	    current->hue = atoi(val);
	else if (0 == strcmp(tag,"contrast"))
	    current->contrast = atoi(val);
	
	else if (0 == count && 0 == strcmp(tag,"mixer")) {
	    if (-1 != mixer_open("/dev/mixer", val))
		have_mixer = 1;
	} else if (0 == count && 0 == strcmp(tag,"freqtab")) {
	    if (-1 != (i = str_to_int(val,chan_names)))
		chan_tab = i;

	} else {
	    fprintf(stderr,"parse error line %d: unknown tag %s\n",nr,tag);
	    exit(1);
	}
    }
    fclose(fp);
}

/* ----------------------------------------------------------------------- */

struct STRTAB booltab[] = {
    {  0, "no" },
    {  0, "false" },
    {  0, "off" },
    {  1, "yes" },
    {  1, "true" },
    {  1, "on" },
    { -1, NULL }
};

int
str_to_int(char *str, struct STRTAB *tab)
{
    int i;
    
    if (str[0] >= '0' && str[0] <= '9')
	return atoi(str);
    for (i = 0; tab[i].str != NULL; i++)
	if (0 == strcasecmp(str,tab[i].str))
	    return(tab[i].nr);
    return -1;
}
