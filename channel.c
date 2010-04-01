/* 
    channel for Bt848 frame grabber driver

    Copyright (C) 1996,97 Marcus Metzler (mocm@thp.uni-koeln.de)

    many changes by Gerd Knorr <kraxel@cs.tu-berlin.de>
        [ hmm, think by now nearly nothing left from the original code ... ]

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
#include <ctype.h>
#include <math.h>

#include <X11/Intrinsic.h>

#include "grab.h"
#include "channels.h"
#include "channel.h"
#include "mixer.h"

/* ----------------------------------------------------------------------- */
/* misc common stuff, not only channel related                             */ 

struct CHANNEL  defaults   = { "defaults", NULL,
			       "5", 0, 0, 0,
			       CAPTURE_OVERLAY, 0, 0,
			       32768, 32768, 32768, 32768 };
struct CHANNEL  **channels = NULL;
int             count      = 0;
int             have_mixer = 0;

int    cur_sender = -1, cur_channel = 5, cur_fine = 0;
int    cur_norm = -1, cur_input = -1;

int    chan_tab = 4;

extern struct GRABBER *grabbers[];
extern int grabber;
extern int fs_width,fs_height,fs_xoff,fs_yoff,pix_width,pix_height;

/* ----------------------------------------------------------------------- */

int lookup_channel(char *channel)
{
    int    i,nr;
    char   tag;

    if (isdigit(channel[0])) {
	tag = 0;
	nr  = atoi(channel);
    } else {
	tag = channel[0];
	nr  = atoi(channel+1);
    }

    for (i = 0; i < CHAN_ENTRIES; i++) {
	if (tag && !isdigit(tvtuner[i].name[0]))
	    if (atoi(tvtuner[i].name+1) == nr && tvtuner[i].name[0] == tag)
		break;
	if (!tag && isdigit(tvtuner[i].name[0]))
	    if (atoi(tvtuner[i].name) == nr)
		break;
    }
    if (i == CHAN_ENTRIES)
	return -1;

    return i;
}

int  get_freq(int i)
{
    if (!tvtuner[i].freq[chan_tab])
	return -1;
    return tvtuner[i].freq[chan_tab]*16/1000;
}

int  cf2freq(char *name, int fine)
{
    int i;
    
    if (-1 == (i = lookup_channel(name)))
	return -1;
    return get_freq(i)+fine;
}

/* ----------------------------------------------------------------------- */

static struct STRTAB captab[] = {
    {  CAPTURE_OFF,         "off"         },
    {  CAPTURE_OFF,         "no"          },
    {  CAPTURE_OFF,         "false"       },
    {  CAPTURE_OVERLAY,     "on"          },
    {  CAPTURE_OVERLAY,     "yes"         },
    {  CAPTURE_OVERLAY,     "true"        },
    {  CAPTURE_OVERLAY,     "overlay"     },
    {  CAPTURE_GRABDISPLAY, "grab"        },
    {  CAPTURE_GRABDISPLAY, "grabdisplay" },
    {  -1, NULL,     },
};

void
read_config()
{
    FILE *fp;
    char filename[100],line[100], tag[32], val[100], *h;
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
	    fprintf(stderr,"%s:%d: parse error\n",filename,nr);
	    continue;
	}
	while (NULL != (h = strrchr(val,' ')))
	    *h = '\0';

	if (0 == strcmp(tag,"key"))
	    current->key = strdup(val);
	
	else if (0 == strcmp(tag,"capture")) {
	    current->capture = str_to_int(val,captab);
	    if (-1 == current->capture)
		fprintf(stderr,"%s:%d: invalid value for %s: %s\n",
			filename,nr,tag,val);
	} else if (0 == strcmp(tag,"source")) {
	    current->source = str_to_int(val,grabbers[grabber]->inputs);
	    if (-1 == current->source)
		fprintf(stderr,"%s:%d: invalid value for %s: %s\n",
			filename,nr,tag,val);
	} else if (0 == strcmp(tag,"norm")) {
	    current->norm = str_to_int(val,grabbers[grabber]->norms);
	    if (-1 == current->norm)
		fprintf(stderr,"%s:%d: invalid value for %s: %s\n",
			filename,nr,tag,val);
	} else if (0 == strcmp(tag,"channel")) {
	    current->cname   = strdup(val);
	    current->channel = lookup_channel(current->cname);
	    if (-1 == current->channel)
		fprintf(stderr,"%s:%d: invalid value for %s: %s\n",
			filename,nr,tag,val);
	}
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
	    else
		fprintf(stderr,"%s:%d: invalid value for %s: %s\n",
			filename,nr,tag,val);

	} else if (0 == count && 0 == strcmp(tag,"freqtab")) {
	    if (-1 != (i = str_to_int(val,chan_names)))
		chan_tab = i;
	    else
		fprintf(stderr,"%s:%d: invalid value for %s: %s\n",
			filename,nr,tag,val);

	} else if (0 == count && 0 == strcmp(tag,"fullscreen")) {
	    if (2 != sscanf(val,"%d x %d",&fs_width,&fs_height)) {
		fprintf(stderr,"%s:%d: invalid value for %s: %s\n",
			filename,nr,tag,val);
		fs_width = fs_height = 0;
	    }

	} else if (0 == count && 0 == strcmp(tag,"pixsize")) {
	    if (2 != sscanf(val,"%d x %d",&pix_width,&pix_height)) {
		fprintf(stderr,"%s:%d: invalid value for %s: %s\n",
			filename,nr,tag,val);
		pix_width = 128;
		pix_height = 96;
	    }

	} else if (0 == count && 0 == strcmp(tag,"wm-off-by")) {
	    if (2 != sscanf(val,"%d %d",&fs_xoff,&fs_yoff)) {
		fprintf(stderr,"%s:%d: invalid value for %s: %s\n",
			filename,nr,tag,val);
		fs_xoff = fs_yoff = 0;
	    }

	} else {
	    fprintf(stderr,"%s:%d: unknown tag %s\n",filename,nr,tag);

	}
    }
    fclose(fp);
}

/* ----------------------------------------------------------------------- */

struct STRTAB chan_names[] = {
    { 0, "ntsc-bcast"       },
    { 1, "ntsc-cable"       },
    { 2, "ntsc-bcast-jp"    },
    { 3, "ntsc-cable-jp"    },
    { 4, "pal-bcast-europe" },
    { 5, "pal-italy"	    },
    { 6, "pal-newzealand"   },
    { 7, "pal-australia"    },
    { 8, "pal-ireland"      },
    { 9, "pal-cable-europe" },
    { -1, NULL }
};

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

/* ------------------------------------------------------------------------- */
/* moved here from channels.h                                                */

/* NOTE : NTSC BROADCAST OVER 69 WERE RE-ALLOCATED CELLULAR, included anyway */

struct freqlist tvtuner[] = {
/* CH  US-TV  US-CATV JP-TV JP-CATV EUROPE  ITALY  NZ     AU   UHF_GHI BGCATV*/
{"S1",{     0,     0,     0,     0,     0,     0,     0,     0,     0, 69250}},
{"S2",{     0,     0,     0,     0,     0,     0,     0,     0,     0, 76250}},
{"S3",{     0,     0,     0,     0,     0,     0,     0,     0,     0, 83250}},

{"E2",{     0,     0,     0,     0,     0,     0,     0,     0,     0, 48250}},
{"E3",{     0,     0,     0,     0,     0,     0,     0,     0,     0, 55250}},
{"E4",{     0,     0,     0,     0,     0,     0,     0,     0,     0, 62250}},
{"E5",{     0,     0,     0,     0,     0,     0,     0,     0,     0,175250}},
{"E6",{     0,     0,     0,     0,     0,     0,     0,     0,     0,182250}},
{"E7",{     0,     0,     0,     0,     0,     0,     0,     0,     0,189250}},
{"E8",{     0,     0,     0,     0,     0,     0,     0,     0,     0,196250}},
{"E9",{     0,     0,     0,     0,     0,     0,     0,     0,     0,203250}},
{"E10",{     0,     0,     0,     0,     0,     0,     0,     0,     0,210250}},
{"E11",{     0,     0,     0,     0,     0,     0,     0,     0,     0,217250}},
{"E12",{     0,     0,     0,     0,     0,     0,     0,     0,     0,224250}},

{"0",{     0,     0,     0,     0,     0,     0,     0, 46250, 45750,     0}},
{"1",{     0, 73250, 91250,     0,     0,     0, 45250, 57250, 53750,105250}},
{"2",{ 55250, 55250, 97250,     0, 48250, 53750, 55250, 64250, 61750,112250}},
{"3",{ 61250, 61250,103250,     0, 55250, 62250, 62250, 86250,175250,119250}},
{"4",{ 67250, 67250,171250,     0, 62250, 82250,175250, 95250,183250,126250}},
{"5",{ 77250, 77250,177250,     0,175250,175250,182250,102250,191250,133250}},
{"5A",{     0,     0,     0,     0,     0,     0,     0,138250,     0,     0}},
{"6",{ 83250, 83250,183250,     0,182250,183750,189250,175250,199250,140250}},
{"7",{175250,175250,189250,     0,189250,192250,196250,182250,207250,147250}},
{"8",{181250,181250,193250,     0,196250,201250,203250,189250,215250,154250}},
{"9",{187250,187250,199250,     0,203250,210250,210250,196250,     0,161250}},
{"10",{193250,193250,205250,     0,210250,210250,217250,209250,     0,168250}},
{"11",{199250,199250,211250,     0,217250,217250,     0,216250,     0,231250}},
{"12",{205250,205250,217250,     0,224250,224250,     0,     0,     0,238250}},

{"13",{211250,211250,     0,109250,     0,     0,     0,     0,     0,245250}},
{"14",{471250,121250,     0,115250,     0,     0,     0,     0,     0,252250}},
{"15",{477250,127250,     0,121250,     0,     0,     0,     0,     0,259250}},
{"16",{483250,133250,     0,127250,     0,     0,     0,     0,     0,266250}},
{"17",{489250,139250,     0,133250,     0,     0,     0,     0,     0,273250}},
{"18",{495250,145250,     0,139250,     0,     0,     0,     0,     0,280250}},
{"19",{501250,151250,     0,145250,     0,     0,     0,     0,     0,287250}},
{"20",{507250,157250,     0,151250,     0,     0,     0,     0,     0,294250}},

{"21",{513250,163250,     0,157250,471250,     0,     0,     0,471250,303250}},
{"22",{519250,169250,     0,165250,479250,     0,     0,     0,479250,311250}},
{"23",{525250,217250,     0,223250,478250,     0,     0,     0,487250,319250}},
{"24",{531250,223250,     0,231250,495250,     0,     0,     0,495250,327250}},
{"25",{537250,229250,     0,237250,503250,     0,     0,     0,503250,335250}},
{"26",{543250,235250,     0,243250,511250,     0,     0,     0,511250,343250}},
{"27",{549250,241250,     0,249250,519250,     0,     0,     0,519250,351250}},
{"28",{555250,247250,     0,253250,527250,     0,     0,     0,527250,359250}},
{"29",{561250,253250,     0,259250,535250,     0,     0,     0,535250,367250}},
{"30",{567250,259250,     0,265250,543250,     0,     0,     0,543250,375250}},
{"31",{573250,265250,     0,271250,551250,     0,     0,     0,551250,383250}},
{"32",{579250,271250,     0,277250,559250,     0,     0,     0,559250,391250}},
{"33",{585250,277250,     0,283250,567250,     0,     0,     0,567250,399250}},
{"34",{591250,283250,     0,289250,575250,     0,     0,     0,575250,407250}},
{"35",{597250,289250,     0,295250,583250,     0,     0,     0,583250,415250}},
{"36",{603250,295250,     0,301250,591250,     0,     0,     0,591250,423250}},
{"37",{609250,301250,     0,307250,599250,     0,     0,     0,599250,431250}},
{"38",{615250,307250,     0,313250,607250,     0,     0,     0,607250,439250}},
{"39",{621250,313250,     0,319250,615250,     0,     0,     0,615250,447250}},
{"40",{627250,319250,     0,325250,623250,     0,     0,     0,623250,455250}},
{"41",{633250,325250,     0,331250,631250,     0,     0,     0,631250,463250}},
{"42",{639250,331250,     0,337250,639250,     0,     0,     0,639250,     0}},
{"43",{645250,337250,     0,343250,647250,     0,     0,     0,647250,     0}},
{"44",{651250,343250,     0,349250,655250,     0,     0,     0,655250,     0}},
{"45",{657250,349250,663250,355250,663250,     0,     0,     0,663250,     0}},
{"46",{663250,355250,669250,361250,671250,     0,     0,     0,671250,     0}},
{"47",{669250,361250,675250,367250,679250,     0,     0,     0,679250,     0}},
{"48",{675250,367250,681250,373250,687250,     0,     0,     0,687250,     0}},
{"49",{681250,373250,687250,379250,695250,     0,     0,     0,695250,     0}},
{"50",{687250,379250,693250,385250,703250,     0,     0,     0,703250,     0}},
{"51",{693250,385250,699250,391250,711250,     0,     0,     0,711250,     0}},
{"52",{699250,391250,705250,397250,719250,     0,     0,     0,719250,     0}},
{"53",{705250,397250,711250,403250,727250,     0,     0,     0,727250,     0}},
{"54",{711250,403250,717250,409250,735250,     0,     0,     0,735250,     0}},
{"55",{717250,409250,723250,415250,743250,     0,     0,     0,743250,     0}},
{"56",{723250,415250,729250,421250,751250,     0,     0,     0,751250,     0}},
{"57",{729250,421250,735250,427250,759250,     0,     0,     0,759250,     0}},
{"58",{735250,427250,741250,433250,767250,     0,     0,     0,767250,     0}},
{"59",{741250,433250,747250,439250,775250,     0,     0,     0,775250,     0}},
{"60",{747250,439250,753250,445250,783250,     0,     0,     0,783250,     0}},
{"61",{753250,445250,759250,451250,791250,     0,     0,     0,791250,     0}},
{"62",{759250,451250,765250,457250,799250,     0,     0,     0,799250,     0}},
{"63",{765250,457250,     0,463250,807250,     0,     0,     0,807250,     0}},
{"64",{771250,463250,     0,     0,815250,     0,     0,     0,815250,     0}},
{"65",{777250,469250,     0,     0,823250,     0,     0,     0,823250,     0}},
{"66",{783250,475250,     0,     0,831250,     0,     0,     0,831250,     0}},
{"67",{789250,481250,     0,     0,839250,     0,     0,     0,839250,     0}},
{"68",{795250,487250,     0,     0,847250,     0,     0,     0,847250,     0}},
{"69",{801250,493250,     0,     0,855250,     0,     0,     0,855250,     0}},

{"70",{807250,499250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"71",{813250,505250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"72",{819250,511250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"73",{825250,517250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"74",{831250,523250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"75",{837250,529250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"76",{843250,535250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"77",{849250,541250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"78",{855250,547250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"79",{861250,553250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"80",{867250,559250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"81",{873250,565250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"82",{879250,571250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"83",{885250,577250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"84",{     0,583250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"85",{     0,589250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"86",{     0,595250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"87",{     0,601250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"88",{     0,607250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"89",{     0,613250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"90",{     0,619250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"91",{     0,625250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"92",{     0,631250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"93",{     0,637250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"94",{     0,643250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"95",{     0, 91250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"96",{     0, 97250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"97",{     0,103250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"98",{     0,109250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"99",{     0,115250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"100",{     0,649250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"101",{     0,655250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"102",{     0,661250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"103",{     0,667250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"104",{     0,673250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"105",{     0,679250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"106",{     0,685250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"107",{     0,691250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"108",{     0,697250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"109",{     0,703250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"110",{     0,709250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"111",{     0,715250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"112",{     0,721250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"113",{     0,727250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"114",{     0,733250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"115",{     0,739250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"116",{     0,745250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"117",{     0,751250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"118",{     0,757250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"119",{     0,763250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"120",{     0,769250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"121",{     0,775250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"122",{     0,781250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"123",{     0,787250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"124",{     0,793250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"125",{     0,799250,     0,     0,     0,     0,     0,     0,     0,     0}},

{"T7",{     0,  8250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"T8",{     0, 14250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"T9",{     0, 20250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"T10",{     0, 26250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"T11",{     0, 32250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"T12",{     0, 38250,     0,     0,     0,     0,     0,     0,     0,     0}},
{"T13",{     0, 44250,     0,     0,     0,     0,     0,     0,     0,     0}}
};
