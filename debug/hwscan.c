/*
 * just some test / debug code for now ...
 *
 * (c) 2002 Gerd Knorr
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <alsa/asoundlib.h>

#include "grab-ng.h"

#define ALSAERR(func,args...)						\
	if ((err = func(## args)) < 0) {       				\
		fprintf(stderr,"fixme: %s\n",snd_strerror(err));	\
		goto oops; }

static int alsa_mixer(char *id)
{
    int err;
    long min,max;
    snd_mixer_t *handle = NULL;
    snd_mixer_elem_t *elem;
    snd_mixer_selem_id_t *sid;
    snd_mixer_selem_id_alloca(&sid);

    ALSAERR(snd_mixer_open, &handle, 0);
    ALSAERR(snd_mixer_attach, handle, id);
    ALSAERR(snd_mixer_selem_register, handle, NULL, NULL);
    ALSAERR(snd_mixer_load, handle);

    for (elem = snd_mixer_first_elem(handle);
	 elem;
	 elem = snd_mixer_elem_next(elem)) {
	snd_mixer_selem_get_id(elem, sid);
	if (!snd_mixer_selem_has_playback_volume(elem))
	    continue;
	snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
	printf("    mixer ctl playback vol [%ld-%ld] \"%s\", #%i\n",
	       min,max,
	       snd_mixer_selem_id_get_name(sid),
	       snd_mixer_selem_id_get_index(sid));
    }

    snd_mixer_close(handle);
    return 0;

 oops:
    if (handle)
	snd_mixer_close(handle);
    return err;
}

static int alsa_card(char *id)
{
    int err;
    int device = -1;
    snd_ctl_t *handle = NULL;
    snd_ctl_card_info_t *card;
    snd_ctl_elem_list_t *elems;
    snd_ctl_card_info_alloca(&card);
    snd_ctl_elem_list_alloca(&elems);

    ALSAERR(snd_ctl_open, &handle, id, 0);
    ALSAERR(snd_ctl_card_info, handle, card);
    printf("alsa card %s id=\"%s\" name=\"%s\"\n", id,
	   snd_ctl_card_info_get_id(card),
	   snd_ctl_card_info_get_name(card));

    ALSAERR(snd_ctl_elem_list, handle, elems);
    printf("  %d controls\n",snd_ctl_elem_list_get_count(elems));

    for (;;) {
	ALSAERR(snd_ctl_pcm_next_device,handle,&device);
	if (-1 == device)
	    break;
	printf("  pcm%d\n",device);
    }

    snd_ctl_close(handle);
    handle = NULL;

 oops:
    if (handle)
	snd_ctl_close(handle);
    return err;
}

static int scan_alsa(void)
{
    char name[8];
    int card = -1;

    for (;;) {
	if (snd_card_next(&card) < 0) {
	    fprintf(stderr,"snd_card_next failed\n");
	    break;
	}
	if (-1 == card)
	    break;
	sprintf(name,"hw:%d",card);
	alsa_card(name);
	alsa_mixer(name);
	printf("\n");
    }
    return 0;
}

int main(int argc, char *argv[])
{
    struct ng_devinfo *info,*i2;
    int i,j,k;

    ng_init();
    for (i = 0; ng_mix_drivers && ng_mix_drivers[i]; i++) {
	info = ng_mix_drivers[i]->probe();
	if (NULL == info)
	    continue;
	fprintf(stderr,"%s mixers\n",ng_mix_drivers[i]->name);
	for (j = 0; strlen(info[j].device) > 0; j++) {
	    fprintf(stderr,"  %s: %s\n",info[j].device,info[j].name);
	    i2 = ng_mix_drivers[i]->channels(info[j].device);
	    if (NULL == i2)
		continue;
	    fprintf(stderr,"   ");
	    for (k = 0; strlen(i2[k].device) > 0; k++) {
		fprintf(stderr," %s",i2[k].device);
	    }
	    fprintf(stderr,"\n");
	}
	free(info);
    }
    
    scan_alsa();
    return 0;
}
