/*
 *  tvtime ALSA device support
 *
 *  Copyright (c) by Devin Heitmueller <dheitmueller@kernellabs.com>
 *
 *  Derived from the alsa-driver test tool latency.c:
 *    Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *
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

snd_output_t *output = NULL;

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
			    int rate,
			    int mmap_flag,
			    const char *id)
{
    int err;
    unsigned int rrate;

    err = snd_pcm_hw_params_any(handle, params);
    if (err < 0) {
	printf("Broken configuration for %s PCM: no configurations available: %s\n", snd_strerror(err), id);
	return err;
    }
    err = snd_pcm_hw_params_set_rate_resample(handle, params, 1);
    if (err < 0) {
	printf("Resample setup failed for %s: %s\n", id, snd_strerror(err));
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
	printf("Access type not available for %s: %s\n", id,
	       snd_strerror(err));
	return err;
    }

    err = snd_pcm_hw_params_set_format(handle, params, format);
    if (err < 0) {
	printf("Sample format not available for %s: %s\n", id,
	       snd_strerror(err));
	return err;
    }
    err = snd_pcm_hw_params_set_channels(handle, params, channels);
    if (err < 0) {
	printf("Channels count (%i) not available for %s: %s\n", channels, id,
	       snd_strerror(err));
	return err;
    }
    rrate = rate;
    err = snd_pcm_hw_params_set_rate_near(handle, params, &rrate, 0);
    if (err < 0) {
	printf("Rate %iHz not available for %s: %s\n", rate, id,
	       snd_strerror(err));
	return err;
    }
    if ((int)rrate != rate) {
	printf("Rate doesn't match (requested %iHz, get %iHz)\n", rate, err);
	return -EINVAL;
    }

    return 0;
}

int setparams_bufsize(snd_pcm_t *handle,
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
	printf("Unable to set buffer size %li for %s: %s\n",
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
	printf("Unable to set period size %li for %s: %s\n", periodsize, id,
	       snd_strerror(err));
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
	printf("Unable to set hw params for %s: %s\n", id, snd_strerror(err));
	return err;
    }
    err = snd_pcm_sw_params_current(handle, swparams);
    if (err < 0) {
	printf("Unable to determine current swparams for %s: %s\n", id,
	       snd_strerror(err));
	return err;
    }
    err = snd_pcm_sw_params_set_start_threshold(handle, swparams, 0x7fffffff);
    if (err < 0) {
	printf("Unable to set start threshold mode for %s: %s\n", id,
	       snd_strerror(err));
	return err;
    }

    err = snd_pcm_sw_params_set_avail_min(handle, swparams, 4);
    if (err < 0) {
	printf("Unable to set avail min for %s: %s\n", id, snd_strerror(err));
	return err;
    }
    err = snd_pcm_sw_params(handle, swparams);
    if (err < 0) {
	printf("Unable to set sw params for %s: %s\n", id, snd_strerror(err));
	return err;
    }
    return 0;
}

int setparams(snd_pcm_t *phandle, snd_pcm_t *chandle, snd_pcm_format_t format,
	      int mmap_flag,
	      struct final_params *negotiated)
{
    int rate = 48000;
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
    if ((err = setparams_stream(phandle, pt_params, format, channels, rate,
				mmap_flag, "playback")) < 0) {
	printf("Unable to set parameters for playback stream: %s\n", snd_strerror(err));
	return 1;
    }
    if ((err = setparams_stream(chandle, ct_params, format, channels, rate,
				mmap_flag, "capture")) < 0) {
	printf("Unable to set parameters for playback stream: %s\n", snd_strerror(err));
	return 1;
    }

  __again:
    if (last_bufsize == bufsize)
	bufsize += 4;
    last_bufsize = bufsize;

    if ((err = setparams_bufsize(phandle, p_params, pt_params, bufsize, 0,
				 "playback")) < 0) {
	printf("Unable to set sw parameters for playback stream: %s\n",
	       snd_strerror(err));
	return -1;
    }
    if ((err = setparams_bufsize(chandle, c_params, ct_params, bufsize, 0,
				 "capture")) < 0) {
	printf("Unable to set sw parameters for playback stream: %s\n",
	       snd_strerror(err));
	return -1;
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
	printf("Unable to set sw parameters for playback stream: %s\n",
	       snd_strerror(err));
	return -1;
    }
    if ((err = setparams_set(chandle, c_params, c_swparams, "capture")) < 0) {
	printf("Unable to set sw parameters for playback stream: %s\n",
	       snd_strerror(err));
	return -1;
    }

    if ((err = snd_pcm_prepare(phandle)) < 0) {
	printf("Prepare error: %s\n", snd_strerror(err));
	return -1;
    }

#ifdef SHOW_ALSA_DEBUG
    printf("final config\n");
    snd_pcm_dump_setup(phandle, output);
    snd_pcm_dump_setup(chandle, output);
    printf("Parameters are %iHz, %s, %i channels\n", rate,
	   snd_pcm_format_name(format), channels);
    fflush(stdout);
#endif

    negotiated->bufsize = bufsize;
    negotiated->rate = rate;
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
	if (r < 0) {
	    return r;
	}

	buf += r * 4;
	len -= r;
	*frames += r;
    }
    return 0;
}

int startup_capture(snd_pcm_t *phandle, snd_pcm_t *chandle,
		    snd_pcm_format_t format, char *buffer, int latency,
		    int channels)
{
    size_t frames_out;
    int err;

    frames_out = 0;
    if (snd_pcm_format_set_silence(format, buffer, latency*channels) < 0) {
	fprintf(stderr, "silence error\n");
	return 1;
    }
    if (writebuf(phandle, buffer, latency, &frames_out) < 0) {
	fprintf(stderr, "write error\n");
	return 1;
    }
    if (writebuf(phandle, buffer, latency, &frames_out) < 0) {
	fprintf(stderr, "write error\n");
	return 1;
    }

    if ((err = snd_pcm_start(chandle)) < 0) {
	printf("Go error: %s\n", snd_strerror(err));
	return 1;
    }
    return 0;
}

static int alsa_stream(const char *pdevice, const char *cdevice,
		       int enable_mmap, FILE *errdev)
{
    snd_pcm_t *phandle, *chandle;
    char *buffer;
    int err;
    ssize_t r;
    size_t frames_in, frames_out, in_max;
    struct final_params negotiated;
    int ret = 0;
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;

    err = snd_output_stdio_attach(&output, errdev, 0);
    if (err < 0) {
	printf("Output failed: %s\n", snd_strerror(err));
	return 0;
    }

//    setscheduler();

    printf("Playback device is %s\n", pdevice);
    printf("Capture device is %s\n", cdevice);

    /* Open the devices */
    if ((err = snd_pcm_open(&phandle, pdevice, SND_PCM_STREAM_PLAYBACK,
			    0)) < 0) {
	printf("Cannot open ALSA Playback device %s: %s\n", pdevice,
	       snd_strerror(err));
	return 0;
    }
    if ((err = snd_pcm_open(&chandle, cdevice, SND_PCM_STREAM_CAPTURE,
			    0)) < 0) {
	printf("Cannot open ALSA Capture device %s: %s\n",
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
    if (setparams(phandle, chandle, format, enable_mmap, &negotiated) < 0) {
	printf("setparams failed\n");
	return 1;
    }

    buffer = malloc((negotiated.bufsize * snd_pcm_format_width(format) / 8)
		    * negotiated.channels);
    if (buffer == NULL) {
	printf("Failed allocating buffer for audio\n");
	return 0;

    }
    if ((err = snd_pcm_link(chandle, phandle)) < 0) {
	printf("Streams link error: %d %s\n", err, snd_strerror(err));
	return 1;
    }

    alsa_is_running = 1;
    startup_capture(phandle, chandle, format, buffer, negotiated.latency,
		    negotiated.channels);

    while (!stop_alsa) {
	in_max = 0;

	/* use poll to wait for next event */
	ret = snd_pcm_wait(chandle, 1000);
	if (ret < 0) {
	    if ((err = snd_pcm_recover(chandle, ret, 0)) < 0) {
		fprintf(stderr, "xrun: recover error: %s",
			snd_strerror(err));
		break;
	    }

	    /* Restart capture */
	    startup_capture(phandle, chandle, format, buffer,
			    negotiated.latency, negotiated.channels);
	    continue;
	} else if (ret == 0) {
	    /* Timed out */
	    continue;
	}

	if ((r = readbuf(chandle, buffer, negotiated.latency, &frames_in,
			 &in_max)) > 0) {
	    if (writebuf(phandle, buffer, r, &frames_out) < 0) {
		startup_capture(phandle, chandle, format, buffer,
				negotiated.latency, negotiated.channels);
	    }
	} else if (r < 0) {
	    startup_capture(phandle, chandle, format, buffer,
			    negotiated.latency, negotiated.channels);
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
};

static void *alsa_thread_entry(void *whatever)
{
    struct input_params *inputs = (struct input_params *) whatever;

    printf("Starting copying alsa stream from %s to %s\n", inputs->cdevice, inputs->pdevice);
    alsa_stream(inputs->pdevice, inputs->cdevice, 1, stderr);
    printf("Alsa stream stopped\n");

    return whatever;
}

/*************************************************************************
 Public functions
 *************************************************************************/

int alsa_thread_startup(const char *pdevice, const char *cdevice)
{
    int ret;
    pthread_t thread;
    struct input_params *inputs = malloc(sizeof(struct input_params));

    if (inputs == NULL) {
	printf("failed allocating memory for ALSA inputs\n");
	return 0;
    }

    if ((strcasecmp(pdevice, "disabled") == 0) ||
	(strcasecmp(cdevice, "disabled") == 0)) {
	free(inputs);
	return 0;
    }

    inputs->pdevice = strdup(pdevice);
    inputs->cdevice = strdup(cdevice);

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
