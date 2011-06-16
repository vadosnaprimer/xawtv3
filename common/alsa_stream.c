/*
 *  ALSA streaming support
 *
 *  Originally written by:
 *      Copyright (c) by Devin Heitmueller <dheitmueller@kernellabs.com>
 *	for usage at tvtime
 *  Derived from the alsa-driver test tool latency.c:
 *    Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *
 *  Copyright (c) 2011 - Mauro Carvalho Chehab <mchehab@redhat.com>
 *	Ported to xawtv, with bug fixes and improvements
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include "config.h"

#ifdef HAVE_ALSA_ASOUNDLIB_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <alsa/asoundlib.h>
#include <sys/time.h>
#include <math.h>
#include "alsa_stream.h"

/* Private vars to control alsa thread status */
static int alsa_is_running = 0;
static int stop_alsa = 0;

/* Error handlers */
snd_output_t *output = NULL;
FILE *error_fp;
int verbose = 0;

struct final_params {
    int bufsize;
    int rate;
    int latency;
    int channels;
};

static int setparams_stream(snd_pcm_t *handle,
			    snd_pcm_hw_params_t *params,
			    snd_pcm_format_t format,
			    int channels,
			    const char *id)
{
    int err;

    err = snd_pcm_hw_params_any(handle, params);
    if (err < 0) {
	fprintf(error_fp,
		"Broken configuration for %s PCM: no configurations available: %s\n",
		snd_strerror(err), id);
	return err;
    }

    err = snd_pcm_hw_params_set_access(handle, params,
				       SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
	fprintf(error_fp, "Access type not available for %s: %s\n", id,
		snd_strerror(err));
	return err;
    }

    err = snd_pcm_hw_params_set_format(handle, params, format);
    if (err < 0) {
	fprintf(error_fp, "Sample format not available for %s: %s\n", id,
	       snd_strerror(err));
	return err;
    }
    err = snd_pcm_hw_params_set_channels(handle, params, channels);
    if (err < 0) {
	fprintf(error_fp, "Channels count (%i) not available for %s: %s\n",
		channels, id, snd_strerror(err));
	return err;
    }

    return 0;
}

static int setparams_periods(snd_pcm_t *handle,
		      snd_pcm_hw_params_t *params,
		      unsigned int usecs,
		      unsigned int count,
		      const char *id)
{
    int err;

    err = snd_pcm_hw_params_set_period_time_near(handle, params, &usecs, 0);
    if (err < 0) {
	fprintf(error_fp, "Unable to set period time %u for %s: %s\n",
		usecs, id, snd_strerror(err));
	return err;
    }

    err = snd_pcm_hw_params_set_periods_near(handle, params, &count, 0);
    if (err < 0) {
	fprintf(error_fp, "Unable to set periods %u for %s: %s\n",
		count, id, snd_strerror(err));
	return err;
    }
    return 0;
}

static int setparams_set(snd_pcm_t *handle,
			 snd_pcm_hw_params_t *params,
			 snd_pcm_sw_params_t *swparams,
			 snd_pcm_uframes_t start_treshold,
			 const char *id)
{
    int err;

    err = snd_pcm_hw_params(handle, params);
    if (err < 0) {
	fprintf(error_fp, "Unable to set hw params for %s: %s\n",
		id, snd_strerror(err));
	return err;
    }
    err = snd_pcm_sw_params_current(handle, swparams);
    if (err < 0) {
	fprintf(error_fp, "Unable to determine current swparams for %s: %s\n",
		id, snd_strerror(err));
	return err;
    }
    err = snd_pcm_sw_params_set_start_threshold(handle, swparams,
						start_treshold);
    if (err < 0) {
	fprintf(error_fp, "Unable to set start threshold mode for %s: %s\n",
		id, snd_strerror(err));
	return err;
    }

    err = snd_pcm_sw_params_set_avail_min(handle, swparams, 4);
    if (err < 0) {
	fprintf(error_fp, "Unable to set avail min for %s: %s\n",
		id, snd_strerror(err));
	return err;
    }
    err = snd_pcm_sw_params(handle, swparams);
    if (err < 0) {
	fprintf(error_fp, "Unable to set sw params for %s: %s\n",
		id, snd_strerror(err));
	return err;
    }
    return 0;
}

static int seek_rates[] = {
    48000,
    44100,
    32000,
};
#define NUM_RATES (sizeof(seek_rates)/sizeof(seek_rates[0]))

static int setparams(snd_pcm_t *phandle, snd_pcm_t *chandle,
		     snd_pcm_format_t format,
		     int latency, int allow_resample,
		     struct final_params *negotiated)
{
    int i;
    unsigned ratep, ratec;
    int err, channels = 2;
    snd_pcm_hw_params_t *p_hwparams, *c_hwparams;
    snd_pcm_sw_params_t *p_swparams, *c_swparams;
    snd_pcm_uframes_t c_size, p_psize, c_psize;
    /* Our latency is 2 periods (in usecs) */
    unsigned int periodtime = latency * 1000 / 2;

    snd_pcm_hw_params_alloca(&p_hwparams);
    snd_pcm_hw_params_alloca(&c_hwparams);
    snd_pcm_sw_params_alloca(&p_swparams);
    snd_pcm_sw_params_alloca(&c_swparams);

    if (setparams_stream(phandle, p_hwparams, format, channels, "playback"))
	return 1;

    if (setparams_stream(chandle, c_hwparams, format, channels, "capture"))
	return 1;

    if (allow_resample) {
	err = snd_pcm_hw_params_set_rate_resample(chandle, c_hwparams, 1);
	if (err < 0) {
	    fprintf(error_fp, "Resample setup failed: %s\n", snd_strerror(err));
	    return 1;
	}
    }

    for (i = 0; i < NUM_RATES; i++) {
	ratec = seek_rates[i];
	err = snd_pcm_hw_params_set_rate_near(chandle, c_hwparams, &ratec, 0);
	if (err)
	    continue;
	ratep = ratec;
	err = snd_pcm_hw_params_set_rate_near(phandle, p_hwparams, &ratep, 0);
	if (err)
	    continue;
	if (ratep == ratec)
	    break;
	if (verbose)
	    fprintf(error_fp,
		    "Failed to set to %u: capture wanted %u, playback wanted %u%s\n",
		    seek_rates[i], ratec, ratep,
		    allow_resample ? " with resample enabled": "");
    }

    if (err < 0) {
	fprintf(error_fp, "Failed to set a supported rate: %s\n",
		snd_strerror(err));
	return 1;
    }
    if (ratep != ratec) {
	if (verbose || allow_resample)
	    fprintf(error_fp,
		    "Couldn't find a rate that it is supported by both playback and capture\n");
	return 2;
    }

    if (setparams_periods(phandle, c_hwparams, periodtime, 2, "capture"))
	return 1;

    /* Note we use twice as much periods for the playback buffer, since we
       will get a period size near the requested time and we don't want it to
       end up smaller then the capture buffer as then we could end up blocking
       on writing to it. Note we will configure the playback dev to start
       playing as soon as it has 2 capture periods worth of data, so this
       won't influence latency */
    if (setparams_periods(phandle, p_hwparams, periodtime, 4, "playback"))
	return 1;

    snd_pcm_hw_params_get_period_size(p_hwparams, &p_psize, NULL);
    snd_pcm_hw_params_get_period_size(c_hwparams, &c_psize, NULL);
    snd_pcm_hw_params_get_buffer_size(c_hwparams, &c_size);

    latency = 2 * c_psize;
    if (setparams_set(phandle, p_hwparams, p_swparams, latency, "playback"))
	return 1;

    if (setparams_set(chandle, c_hwparams, c_swparams, c_psize, "capture"))
	return 1;

    if ((err = snd_pcm_prepare(phandle)) < 0) {
	fprintf(error_fp, "Prepare error: %s\n", snd_strerror(err));
	return 1;
    }

    if (verbose) {
	fprintf(error_fp, "ALSA config:\n");
	snd_pcm_dump_setup(phandle, output);
	snd_pcm_dump_setup(chandle, output);
	fprintf(error_fp, "Parameters are %iHz, %s, %i channels\n",
		ratep, snd_pcm_format_name(format), channels);
	fprintf(error_fp, "Set bitrate to %u%s, buffer size is %u\n", ratec,
		allow_resample ? " with resample enabled at playback": "",
		(unsigned int)c_size);
    }

    negotiated->bufsize = c_size;
    negotiated->rate = ratep;
    negotiated->channels = channels;
    negotiated->latency = latency;
    return 0;
}

/* Read up to len frames */
static snd_pcm_sframes_t readbuf(snd_pcm_t *handle, char *buf, long len)
{
    snd_pcm_sframes_t r;

    r = snd_pcm_readi(handle, buf, len);
    if (r < 0 && r != -EAGAIN) {
	r = snd_pcm_recover(handle, r, 0);
	if (r < 0)
	    fprintf(error_fp, "overrun recover error: %s\n", snd_strerror(r));
    }
    return r;
}

/* Write len frames (note not up to len, but all of len!) */
static snd_pcm_sframes_t writebuf(snd_pcm_t *handle, char *buf, long len)
{
    snd_pcm_sframes_t r;

    while (1) {
	r = snd_pcm_writei(handle, buf, len);
	if (r == len)
	    return 0;
	if (r < 0) {
	    r = snd_pcm_recover(handle, r, 0);
	    if (r < 0) {
		fprintf(error_fp, "underrun recover error: %s\n",
			snd_strerror(r));
		return r;
	    }
	}
	buf += r * 4;
	len -= r;
	snd_pcm_wait(handle, 100);
    }
}

static int alsa_stream(const char *pdevice, const char *cdevice, int latency)
{
    snd_pcm_t *phandle, *chandle;
    char *buffer;
    int err;
    ssize_t r;
    struct final_params negotiated;
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
    char pdevice_new[32];

    err = snd_output_stdio_attach(&output, error_fp, 0);
    if (err < 0) {
	fprintf(error_fp, "Output failed: %s\n", snd_strerror(err));
	return 0;
    }

    /* Open the devices */
    if ((err = snd_pcm_open(&phandle, pdevice, SND_PCM_STREAM_PLAYBACK,
			    0)) < 0) {
	fprintf(error_fp, "Cannot open ALSA Playback device %s: %s\n",
		pdevice, snd_strerror(err));
	return 0;
    }
    if ((err = snd_pcm_open(&chandle, cdevice, SND_PCM_STREAM_CAPTURE,
			    SND_PCM_NONBLOCK)) < 0) {
	fprintf(error_fp, "Cannot open ALSA Capture device %s: %s\n",
		cdevice, snd_strerror(err));
	return 0;
    }

    err = setparams(phandle, chandle, format, latency, 0, &negotiated);

    /* Try to use plughw instead, as it allows emulating speed */
    if (err == 2 && strncmp(pdevice, "hw", 2) == 0) {

        snd_pcm_close(phandle);

        sprintf(pdevice_new, "plug%s", pdevice);
        pdevice = pdevice_new;
        if (verbose)
            fprintf(error_fp, "Trying %s for playback\n", pdevice);
        if ((err = snd_pcm_open(&phandle, pdevice, SND_PCM_STREAM_PLAYBACK,
                                0)) < 0) {
            fprintf(error_fp, "Cannot open ALSA Playback device %s: %s\n",
                    pdevice, snd_strerror(err));
        }

	err = setparams(phandle, chandle, format, latency, 1, &negotiated);
    }

    if (err != 0) {
        fprintf(error_fp, "setparams failed\n");
        return 1;
    }

    buffer = malloc((negotiated.bufsize * snd_pcm_format_width(format) / 8)
		    * negotiated.channels);
    if (buffer == NULL) {
	fprintf(error_fp, "Failed allocating buffer for audio\n");
	return 0;
    }

    /*
     * Buffering delay is due for capture and for playback, so we
     * need to multiply it by two.
     */
    fprintf(error_fp,
	    "Alsa stream started from %s to %s (%i Hz, buffer delay = %.2f ms)\n",
	    cdevice, pdevice, negotiated.rate,
	    negotiated.latency * 1000.0 / negotiated.rate);

    alsa_is_running = 1;

    while (!stop_alsa) {
	/* We start with a read and not a wait to auto(re)start the capture */
	r = readbuf(chandle, buffer, negotiated.bufsize);
	if (r == 0)   /* Succesfully recovered from an overrun? */
	    continue; /* Force restart of capture stream */
	if (r > 0)
	    writebuf(phandle, buffer, r);
	/* use poll to wait for next event */
	snd_pcm_wait(chandle, 1000);
    }

    snd_pcm_drop(chandle);
    snd_pcm_nonblock(phandle, 0);
    snd_pcm_drain(phandle);

    snd_pcm_unlink(chandle);
    snd_pcm_hw_free(phandle);
    snd_pcm_hw_free(chandle);

    snd_pcm_close(phandle);
    snd_pcm_close(chandle);

    alsa_is_running = 0;
    return 0;
}

struct input_params {
    const char *pdevice;
    const char *cdevice;
    int latency;
};

static void *alsa_thread_entry(void *whatever)
{
    struct input_params *inputs = (struct input_params *) whatever;

    if (verbose)
	fprintf(error_fp, "Starting copying alsa stream from %s to %s\n",
		inputs->cdevice, inputs->pdevice);
    alsa_stream(inputs->pdevice, inputs->cdevice, inputs->latency);
    fprintf(error_fp, "Alsa stream stopped\n");

    return whatever;
}

/*************************************************************************
 Public functions
 *************************************************************************/

int alsa_thread_startup(const char *pdevice, const char *cdevice, int latency,
			FILE *__error_fp, int __verbose)
{
    int ret;
    pthread_t thread;
    struct input_params *inputs = malloc(sizeof(struct input_params));

    if (__error_fp)
	error_fp = __error_fp;
    else
	error_fp = stderr;

    verbose = __verbose;

    if (inputs == NULL) {
	fprintf(error_fp, "failed allocating memory for ALSA inputs\n");
	return 0;
    }

    if ((strcasecmp(pdevice, "disabled") == 0) ||
	(strcasecmp(cdevice, "disabled") == 0)) {
	free(inputs);
	return 0;
    }

    inputs->pdevice = strdup(pdevice);
    inputs->cdevice = strdup(cdevice);
    inputs->latency = latency;

    if (alsa_is_running) {
       stop_alsa = 1;
       while ((volatile int)alsa_is_running)
	       usleep(10);
    }

    stop_alsa = 0;

    ret = pthread_create(&thread, NULL,
			 &alsa_thread_entry, (void *) inputs);
    return ret;
}

void alsa_thread_stop(void)
{
	stop_alsa = 1;
}

int alsa_thread_is_running(void)
{
	return alsa_is_running;
}

#endif
