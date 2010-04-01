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

#include "channel.h"
#include "mixer.h"

int freq2chan(int f)
{
  double freq=(double)f/16;
  
  if (freq < 66.25  && freq >=48.25)  return (int)((freq-48.25)/7.0)+2;
  if (freq < 172.25 && freq >=112.25) return (int)((freq-112.25)/7.0+.5)+72;
  if (freq < 228.25 && freq >=175.25) return (int)((freq-175.25)/7.0+.5)+5;
  if (freq < 298.25 && freq >=231.25) return (int)((freq-231.25)/7.0+.5)+81;
  if (freq < 451.25 && freq >=303.25) return (int)((freq-303.25)/8.0+.5)+91;
  if (freq < 471.25 && freq >=455.25) return (int)((freq-455.25)/8.0+.5)+110;
  if (freq < 859.25 && freq >=471.25) return (int)((freq-471.25)/8.0+.5)+21;
  if (freq >= 859.25)                 return (int)((freq-455.25)/8.0+.5)+110;
  return 0;
}

int cf2freq(int chan, int fine)
{
  double freq;

  if(chan<5)
    freq =48.25+7.0*(chan-2);   /* E2-E4 */
  else if(chan<21)
    freq=175.25+7.0*(chan-5);   /* E5-E12 */
  else if(chan<70)
    freq=471.25+8.0*(chan-21);  /* E21-E69 */
  else if(chan<81)
    freq=112.25+7.0*(chan-72);  /* S02-S10 */
  else if(chan<91)
    freq=231.25+7.0*(chan-81);  /* S11-S20 */
  else if(chan<110)
    freq=303.25+8.0*(chan-91);  /* S21-S39 */
  else
    freq=455.25+8.0*(chan-110); /* S40-S.. */
  return (int)(freq*16)+fine;
}

int freq2fine(int f)
{
  int chan=freq2chan(f);
  int nf=cf2freq(chan,0);
  
  if (chan<200) return (f-nf);
  else return 0;
}

/* ----------------------------------------------------------------------- */
/* my stuff follows here - kraxel                                          */ 
/* misc common stuff, not only channel related                             */ 

struct CHANNEL  defaults   = { "defaults", NULL,
			       1, 0, 0, 5, 0, 0,
			       254, 0, 0, 216 };
struct CHANNEL  **channels = NULL;
int             count      = 0;
int             have_mixer = 0;


int    cur_sender = -1, cur_channel = 5, cur_fine = 0, cur_norm = 0;

/* ----------------------------------------------------------------------- */

void
read_config()
{
    FILE *fp;
    char filename[100],line[100], tag[32], val[100];
    int nr = 0;
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
	    current->source = str_to_int(val,srctab);
	else if (0 == strcmp(tag,"norm"))
	    current->norm = str_to_int(val,normtab);
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
	    if (-1 != mixer_open("/dev/mixer", val)) {
		have_mixer = 1;
	    }
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

struct STRTAB normtab[] = {
    {  0, "pal" },
    {  1, "ntsc" },
    {  2, "secam" },
    { -1, NULL }
};

struct STRTAB srctab[] = {
    {  0, "tuner" },
    {  1, "c1" },
    {  2, "c2" },
    {  3, "SVHS" },
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

void
VolumeAction(Widget widget, XEvent *event,
	     String *params, Cardinal *num_params)
{
    int vol; 

    if (!have_mixer)
	return;

    if (*num_params < 1)
	return;

    if (0 == strcasecmp(params[0],"mute")) {
	mixer_get_muted() ? mixer_unmute() : mixer_mute();
    } else if (0 == strcasecmp(params[0],"inc")) {
	vol = mixer_get_volume() + 3;
	if (vol > 100) vol = 100;
	mixer_set_volume(vol);
    } else if (0 == strcasecmp(params[0],"dec")) {
	vol = mixer_get_volume() - 3;
	if (vol < 0) vol = 0;
	mixer_set_volume(vol);
    }
}

