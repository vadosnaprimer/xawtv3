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

/* Pointers to either mmap or non-mmap functions */
static snd_pcm_sframes_t (*readi_func)(snd_pcm_t *handle, void *buffer, snd_pcm_uframes_t size);
static snd_pcm_sframes_t (*writei_func)(snd_pcm_t *handle, const void *buffer, snd_pcm_uframes_t size);

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
			    int mmap_flag,
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

    if (mmap_flag) {
	snd_pcm_access_mask_t *mask = alloca(snd_pcm_access_mask_sizeof());
	snd_pcm_access_mask_none(mask);
	snd_pcm_access_mask_set(mask, SND_PCM_ACCESS_MMAP_INTERLEAVED);
	snd_pcm_access_mask_set(mask, SND_PCM_ACCESS_MMAP_NONINTERLEAVED);
	snd_pcm_access_mask_set(mask, SND_PCM_ACCESS_MMAP_COMPLEX);
	err = snd_pcm_hw_params_set_access_mask(handle, params, mask);
    } else {
	err = snd_pcm_hw_params_set_access(handle, params,
					   SND_PCM_ACCESS_RW_INTERLEAVED);
    }

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

static int setparams_bufsize(snd_pcm_t *handle,
		      snd_pcm_hw_params_t *params,
		      snd_pcm_hw_params_t *tparams,
		      snd_pcm_uframes_t bufsize,
		      int period_size,
		      const char *id)
{
    int err;
    snd_pcm_uframes_t periodsize;

    snd_pcm_hw_params_copy(params, tparams);
    periodsize = bufsize * 2;
    err = snd_pcm_hw_params_set_buffer_size_near(handle, params, &periodsize);
    if (err < 0) {
	if (verbose)
	    fprintf(error_fp, "Unable to set buffer size %li for %s: %s\n",
		    bufsize * 2, id, snd_strerror(err));
	return err;
    }
    if (period_size > 0)
	periodsize = period_size;
    else
	periodsize /= 2;
    err = snd_pcm_hw_params_set_period_size_near(handle, params, &periodsize,
						 0);
    if (err < 0) {
	if (verbose)
	    fprintf(error_fp, "Unable to set period size %li for %s: %s\n",
		    periodsize, id, snd_strerror(err));
	return err;
    }
    return 0;
}

static int setparams_set(snd_pcm_t *handle,
			 snd_pcm_hw_params_t *params,
			 snd_pcm_sw_params_t *swparams,
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
    err = snd_pcm_sw_params_set_start_threshold(handle, swparams, 0x7fffffff);
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
		     snd_pcm_format_t format, int mmap_flag,
		     int allow_resample,
		     struct final_params *negotiated)
{
    int i;
    unsigned ratep, ratec;
    int latency_min = 600;		/* in frames / 2 */
    int channels = 2;
    int latency = latency_min - 4;
    int bufsize = latency;
    int err, last_bufsize = bufsize;
    snd_pcm_hw_params_t *pt_params, *ct_params;
    snd_pcm_hw_params_t *p_params, *c_params;
    snd_pcm_sw_params_t *p_swparams, *c_swparams;
    snd_pcm_uframes_t p_size, c_size, p_psize, c_psize;
    unsigned int p_time, c_time;

    snd_pcm_hw_params_alloca(&p_params);
    snd_pcm_hw_params_alloca(&c_params);
    snd_pcm_hw_params_alloca(&pt_params);
    snd_pcm_hw_params_alloca(&ct_params);
    snd_pcm_sw_params_alloca(&p_swparams);
    snd_pcm_sw_params_alloca(&c_swparams);

    if ((err = setparams_stream(phandle, pt_params, format, channels,
				    mmap_flag, "playback")) < 0) {
	fprintf(error_fp, "Unable to set parameters for playback stream: %s\n",
		snd_strerror(err));
	return 1;
    }
    if ((err = setparams_stream(chandle, ct_params, format, channels,
				mmap_flag, "capture")) < 0) {
	fprintf(error_fp, "Unable to set parameters for playback stream: %s\n",
		snd_strerror(err));
	return 1;
    }

    if (allow_resample) {
	err = snd_pcm_hw_params_set_rate_resample(chandle, ct_params, 1);
	if (err < 0) {
	    fprintf(error_fp, "Resample setup failed: %s\n", snd_strerror(err));
	    return 1;
	}
    }

    for (i = 0; i < NUM_RATES; i++) {
	ratec = seek_rates[i];
	err = snd_pcm_hw_params_set_rate_near(chandle, ct_params, &ratec, 0);
	if (err)
	    continue;
	ratep = ratec;
	err = snd_pcm_hw_params_set_rate_near(phandle, pt_params, &ratep, 0);
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
	if (verbose)
	    fprintf(error_fp, "Failed to set a supported rate: %s\n",
		    snd_strerror(err));
	return 1;
    }
    if (ratep != ratec) {
	if (verbose)
	    fprintf(error_fp,
		    "Couldn't find a rate that it is supported by both playback and capture\n");
	return 2;
    }

__again:
    if (last_bufsize == bufsize)
	bufsize += 4;
    last_bufsize = bufsize;

    if (bufsize > 10240)
	return 1;

    if ((err = setparams_bufsize(phandle, p_params, pt_params, bufsize, 0,
				 "playback")) < 0) {
	if (verbose)
	    fprintf(error_fp,
		    "Unable to set sw parameters for playback stream: %s\n",
		    snd_strerror(err));
	goto __again;
    }
    if ((err = setparams_bufsize(chandle, c_params, ct_params, bufsize, 0,
				 "capture")) < 0) {
	if (verbose)
	    fprintf(error_fp,
		    "Unable to set sw parameters for playback stream: %s\n",
		    snd_strerror(err));
	goto __again;
    }

    snd_pcm_hw_params_get_period_size(p_params, &p_psize, NULL);
    if (p_psize > (unsigned int)bufsize)
	bufsize = p_psize;

    snd_pcm_hw_params_get_period_size(c_params, &c_psize, NULL);
    if (c_psize > (unsigned int)bufsize)
	bufsize = c_psize;

    snd_pcm_hw_params_get_period_time(p_params, &p_time, NULL);
    snd_pcm_hw_params_get_period_time(c_params, &c_time, NULL);

    if (p_time != c_time)
	goto __again;

    snd_pcm_hw_params_get_buffer_size(p_params, &p_size);
    if (p_psize * 2 < p_size)
	goto __again;
    snd_pcm_hw_params_get_buffer_size(c_params, &c_size);
    if (c_psize * 2 < c_size)
	goto __again;

    if ((err = setparams_set(phandle, p_params, p_swparams, "playback")) < 0) {
	if (verbose)
	    fprintf(error_fp,
		    "Unable to set sw parameters for playback stream: %s\n",
		    snd_strerror(err));
	goto __again;
    }
    if ((err = setparams_set(chandle, c_params, c_swparams, "capture")) < 0) {
	if (verbose)
	    fprintf(error_fp,
		    "Unable to set sw parameters for playback stream: %s\n",
		    snd_strerror(err));
	goto __again;
    }

    if ((err = snd_pcm_prepare(phandle)) < 0) {
	fprintf(error_fp, "Prepare error: %s\n", snd_strerror(err));
	return 1;
    }

    if (verbose > 1) {
	fprintf(error_fp, "ALSA config:\n");
	snd_pcm_dump_setup(phandle, output);
	snd_pcm_dump_setup(chandle, output);
	fprintf(error_fp, "Parameters are %iHz, %s, %i channels\n",
		ratep, snd_pcm_format_name(format), channels);
	fflush(error_fp);
    }

    if (verbose)
	fprintf(error_fp, "Set bitrate to %u%s, buffer size is %u\n", ratec,
		allow_resample ? " with resample enabled at playback": "",
		bufsize * 2);

    negotiated->bufsize = bufsize;
    negotiated->rate = ratep;
    negotiated->channels = channels;
    negotiated->latency = bufsize;
    return 0;
}

static snd_pcm_sframes_t readbuf(snd_pcm_t *handle, char *buf, long len,
				 size_t *frames, size_t *max)
{
    snd_pcm_sframes_t r;

    r = readi_func(handle, buf, len);
    if (r < 0) {
	return r;
    }

    if (r > 0) {
	*frames += r;
	if ((long)*max < r)
	    *max = r;
    }

    return r;
}

static snd_pcm_sframes_t writebuf(snd_pcm_t *handle, char *buf, long len,
				  size_t *frames)
{
    snd_pcm_sframes_t r;

    while (len > 0) {
	r = writei_func(handle, buf, len);
	if (r == -EAGAIN || (r >= 0 && (size_t)r < len))
	    snd_pcm_wait(handle, 100);
	else if (r == -EPIPE) {
	    snd_pcm_status_t *status;

	    snd_pcm_status_alloca(&status);
	    if ((r = snd_pcm_status(handle, status)) < 0)
		return r;

	    if (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN) {
		if (verbose > 1)
		    fprintf(error_fp, "overrun\n");
		if ((r = snd_pcm_prepare(handle)) < 0)
		    return r;
		if ((r = snd_pcm_start(handle)) < 0)
		    return r;
	    }
	    r = 0;
	}
	if (r < 0) {
	    return r;
	}

	buf += r * 4;
	len -= r;
	*frames += r;
    }
    return 0;
}

static int startup_capture(snd_pcm_t *phandle, snd_pcm_t *chandle,
			   snd_pcm_format_t format, char *buffer, int latency,
			   int channels, int link_is_supported)
{
    size_t frames_out;
    int err;

    frames_out = 0;
    err = snd_pcm_format_set_silence(format, buffer, latency*channels);
    if (err < 0) {
	fprintf(error_fp, "silence error: %s\n", snd_strerror(err));
	return 1;
    }
    err = writebuf(phandle, buffer, latency, &frames_out);
    if (err < 0) {
	fprintf(error_fp, "write error: %s\n", snd_strerror(err));
	return 1;
    }
    err = writebuf(phandle, buffer, latency, &frames_out);
    if (err < 0) {
	fprintf(error_fp, "write error: %s\n", snd_strerror(err));
	return 1;
    }

    if ((err = snd_pcm_start(chandle)) < 0) {
	fprintf(error_fp, "Go error: %s\n", snd_strerror(err));
	return 1;
    }

    if (link_is_supported)
	return 0;

    if ((err = snd_pcm_start(phandle)) < 0) {
	fprintf(error_fp, "Go error: %s\n", snd_strerror(err));
	return 1;
    }

    return 0;
}

static int alsa_stream(const char *pdevice, const char *cdevice,
		       int enable_mmap)
{
    snd_pcm_t *phandle, *chandle;
    char *buffer;
    int err;
    ssize_t r;
    size_t frames_in, frames_out, in_max;
    struct final_params negotiated;
    int ret = 0, link_is_supported = 1;
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;

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
			    0)) < 0) {
	fprintf(error_fp, "Cannot open ALSA Capture device %s: %s\n",
		cdevice, snd_strerror(err));
	return 0;
    }

    if (enable_mmap) {
	writei_func = snd_pcm_mmap_writei;
	readi_func = snd_pcm_mmap_readi;
    } else {
	writei_func = snd_pcm_writei;
	readi_func = snd_pcm_readi;
    }

    frames_in = frames_out = 0;

    err = setparams(phandle, chandle, format, enable_mmap, 0, &negotiated);

    /* Try to use plughw instead, as it allows emulating speed */
    if (err == 2 && strncmp(pdevice, "hw", 2) == 0) {
        char pdevice_new[32];

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

	err = setparams(phandle, chandle, format, enable_mmap, 1, &negotiated);
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
    if ((err = snd_pcm_link(chandle, phandle)) < 0) {
	if (verbose)
	    fprintf(error_fp, "Streams link error: %d %s\n",
		    err, snd_strerror(err));
	link_is_supported = 0;
    }

    fprintf(error_fp,
	    "Alsa stream started, capturing from %s, playing back on %s at %i Hz%s\n",
	    cdevice, pdevice, negotiated.rate,
	    enable_mmap ? " with mmap enabled" : "");

    alsa_is_running = 1;
    startup_capture(phandle, chandle, format, buffer, negotiated.latency,
		    negotiated.channels, link_is_supported);

    while (!stop_alsa) {
	in_max = 0;

	/* use poll to wait for next event */
	ret = snd_pcm_wait(chandle, 1000);
	if (ret < 0) {
	    if ((err = snd_pcm_recover(chandle, ret, 0)) < 0) {
		if (verbose)
		    fprintf(error_fp, "xrun: recover error: %s",
			    snd_strerror(err));
		break;
	    }

	    /* Restart capture */
	    startup_capture(phandle, chandle, format, buffer,
			    negotiated.latency, negotiated.channels,
			    link_is_supported);
	    continue;
	} else if (ret == 0) {
	    /* Timed out */
	    continue;
	}

	if ((r = readbuf(chandle, buffer, negotiated.latency, &frames_in,
			 &in_max)) > 0) {
	    if (writebuf(phandle, buffer, r, &frames_out) < 0) {
		if (verbose)
		    fprintf(error_fp, "write error: %s\n", snd_strerror(err));
	    }
	} else if (r < 0) {
	    startup_capture(phandle, chandle, format, buffer,
			    negotiated.latency, negotiated.channels,
			    link_is_supported);
	}
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
    int mmap_enabled;
};

static void *alsa_thread_entry(void *whatever)
{
    struct input_params *inputs = (struct input_params *) whatever;

    if (verbose)
	fprintf(error_fp, "Starting copying alsa stream from %s to %s%s\n",
		inputs->cdevice, inputs->pdevice,
		(inputs->mmap_enabled ? "with mmap enabled" : ""));
    alsa_stream(inputs->pdevice, inputs->cdevice, inputs->mmap_enabled);
    fprintf(error_fp, "Alsa stream stopped\n");

    return whatever;
}

/*************************************************************************
 Public functions
 *************************************************************************/

int alsa_thread_startup(const char *pdevice, const char *cdevice,
			int mmap_enabled, FILE *__error_fp, int __verbose)
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
    inputs->mmap_enabled = mmap_enabled;

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
