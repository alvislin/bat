/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "config.h"

#include "wav_play_record.h"
#include "wav_play_record_tiny.h"

#include "common.h"
#include "analyze.h"

static void get_sine_frequencies(struct bat *bat, char *freq)
{
	char *tmp1;
	tmp1 = strchr(freq, ',');
	if (tmp1 == NULL) {
		bat->target_freq[1] = bat->target_freq[0] = atof(optarg);
	} else {
		*tmp1 = '\0';
		bat->target_freq[0] = atof(optarg);
		bat->target_freq[1] = atof(tmp1 + 1);
	}
}
static void get_tiny_format(char *alsa_device, unsigned int *tiny_card,
		unsigned int *tiny_device)
{
	char *tmp1, *tmp2, *tmp3;

	tmp1 = strchr(alsa_device, ':');
	tmp3 = tmp1 + 1;
	tmp2 = strchr(tmp3, ',');
	tmp1 = tmp2 + 1;
	*tiny_device = atoi(tmp1);
	*tmp2 = '\0';
	*tiny_card = atoi(tmp3);
	*tmp2 = ',';

}

static pthread_t thread_start(struct bat *bat, pthread_t *id, int playback)
{
	int ret;

	ret = pthread_create(id, NULL, playback ? bat->playback : bat->capture,
			(void *) bat);
	if (ret)
		fprintf(stderr, "error: can't create thread %d\n", ret);

	return ret;
}

static int thread_wait_completion(struct bat *bat, pthread_t id, int **val)
{
	int err;

	err = pthread_join(id, (void **) val);
	if (err) {
		fprintf(stderr, "error: can't join thread %d\n", err);
		pthread_cancel(id);
	}

	return err;
}

/* loopback test where we play sine wave and capture the same sine wave */
static void test_loopback(struct bat *bat)
{
	pthread_t capture_id, playback_id;
	int ret;
	int *thread_result_capture, *thread_result_playback;

	/* start playback */
	ret = thread_start(bat, &playback_id, 1);
	if (ret != 0) {
		fprintf(stderr, "error: failed to create playback thread\n");
		exit(EXIT_FAILURE);
	}

	/* TODO: use a pipe to signal stream start etc - i.e. to sync threads */
	sleep(1); /* Let time for playing something before recording */

	/* start capture */
	ret = thread_start(bat, &capture_id, 0);
	if (ret != 0) {
		fprintf(stderr, "error: failed to create capture thread\n");
		pthread_cancel(playback_id);
		exit(EXIT_FAILURE);
	}

	/* wait for playback to complete */
	ret = thread_wait_completion(bat, playback_id, &thread_result_playback);
	if (ret != 0) {
		fprintf(stderr, "error: can't join playback thread\n");
		pthread_cancel(capture_id);
		exit(EXIT_FAILURE);
	}

	/* check playback status */
	if (*thread_result_playback != 0) {
		fprintf(stderr, "error: playback failed %d\n", *thread_result_playback);
		pthread_cancel(capture_id);
		exit(EXIT_FAILURE);
	} else
		fprintf(stdout, "Playback completed.\n");

	/* now stop and wait for capture to finish */
	pthread_cancel(capture_id);
	ret = thread_wait_completion(bat, capture_id, &thread_result_capture);
	if (ret != 0) {
		fprintf(stderr, "error: can't join capture thread\n");
		exit(EXIT_FAILURE);
	}

	/* check capture status */
	if (*thread_result_capture != 0) {
		fprintf(stderr, "error: capture failed %d\n", *thread_result_capture);
		exit(EXIT_FAILURE);
	} else
		fprintf(stdout, "Capture completed.\n");
}

/* single ended playback only test */
static void test_playback(struct bat *bat)
{
	pthread_t playback_id;
	int ret;
	int *thread_result;

	/* start playback */
	ret = thread_start(bat, &playback_id, 1);
	if (ret != 0) {
		fprintf(stderr, "error: failed to create playback thread\n");
		exit(EXIT_FAILURE);
	}

	/* wait for playback to complete */
	ret = thread_wait_completion(bat, playback_id, &thread_result);
	if (ret != 0) {
		fprintf(stderr, "error: can't join playback thread\n");
		exit(EXIT_FAILURE);
	}

	/* check playback status */
	if (*thread_result != 0) {
		fprintf(stderr, "error: playback failed %d\n", *thread_result);
		exit(EXIT_FAILURE);
	} else
		fprintf(stdout, "Playback completed.\n");

}

/* single ended capture only test */
static void test_capture(struct bat *bat)
{
	pthread_t capture_id;
	int ret;
	int *thread_result;

	/* start capture */
	ret = thread_start(bat, &capture_id, 0);
	if (ret != 0) {
		fprintf(stderr, "error: failed to create capture thread\n");
		exit(EXIT_FAILURE);
	}

	/* TODO: stop capture */

	/* wait for capture to complete */
	ret = thread_wait_completion(bat, capture_id, &thread_result);
	if (ret != 0) {
		fprintf(stderr, "error: can't join capture thread\n");
		exit(EXIT_FAILURE);
	}

	/* check playback status */
	if (*thread_result != 0) {
		fprintf(stderr, "error: capture failed %d\n", *thread_result);
		exit(EXIT_FAILURE);
	} else
		fprintf(stdout, "Capture completed.\n");

}

static void usage(char *argv[])
{
	fprintf(stdout,
		"Usage:%s [-D sound card] [-P playback pcm] [-C capture pcm] [-f input file]\n"
		"         [-s sample size] [-c number of channels] [-r sampling rate]\n"
		"         [-n frames to capture] [-k sigma k] [-F Target Freq]\n"
		"         [-l internal loop, bypass hardware]\n"
		"         [-t use tinyalsa instead of alsa]\n"
		"         [-a single ended capture]\n"
		"         [-b single ended playback]\n"
		"         [-p total number of periods to play/capture]\n",
		argv[0]);
	fprintf(stdout, "Usage:%s [-h]\n", argv[0]);
	exit(EXIT_FAILURE);
}

static void set_defaults(struct bat *bat)
{
	memset(bat, 0, sizeof(struct bat));

	/* Set default values */
	bat->rate = 44100;
	bat->channels = 1;
	bat->frame_size = 2;
	bat->sample_size = 2;
	bat->frames = bat->rate * 2;
	bat->target_freq[0] = 997.0;
	bat->target_freq[1] = 997.0;
	bat->sigma_k = 3.0;
	bat->playback_device = NULL;
	bat->capture_device = NULL;
	bat->buf = NULL;
	bat->local = false;
	bat->playback = &playback_alsa;
	bat->capture = &record_alsa;
	bat->tinyalsa = false;
	bat->playback_single = false;
	bat->capture_single = false;
	bat->period_limit = false;
}

static void parse_arguments(struct bat *bat, int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "hf:s:n:c:F:r:D:P:C:k:p:ltab"))
			!= -1) {
		switch (opt) {
		case 'D':
			if (bat->playback_device == NULL)
				bat->playback_device = optarg;
			if (bat->capture_device == NULL)
				bat->capture_device = optarg;
			break;
		case 'P':
			bat->playback_device = optarg;
			break;
		case 'C':
			bat->capture_device = optarg;
			break;
		case 'f':
			bat->playback_file = optarg;
			break;
		case 'n':
			bat->frames = atoi(optarg);
			break;
		case 'F':
			get_sine_frequencies(bat, optarg);
			break;
		case 'c':
			bat->channels = atoi(optarg);
			break;
		case 'r':
			bat->rate = atoi(optarg);
			break;
		case 's':
			bat->sample_size = atoi(optarg);
			break;
		case 'k':
			bat->sigma_k = atof(optarg);
			break;
		case 'l':
			bat->local = true;
			break;
		case 'a':
			bat->capture_single = true;
			break;
		case 'b':
			bat->playback_single = true;
			break;
		case 'p':
			bat->periods_total = atoi(optarg);
			bat->period_limit = true;
			break;
		case 't':
#ifdef HAVE_LIBTINYALSA
			bat->playback = &playback_tinyalsa;
			bat->capture = &record_tinyalsa;
			bat->tinyalsa = true;
#else
			fprintf(stderr, "error: tinyalsa not installed\n");
			exit(-EINVAL);
#endif
			break;
		case 'h':
		default: /* '?' */
			usage(argv);
		}
	}
}

/* validate options */
static void validate_options(struct bat *bat)
{
	/* check we have an input file for local mode */
	if ((bat->local == true) && (bat->capture_file == NULL)) {
		fprintf(stderr, "error: no input file for local testing\n");
		exit(EXIT_FAILURE);
	}

	/* check supported channels */
	if (bat->channels > 2 || bat->channels < 1) {
		fprintf(stderr, "error: %d channels not supported\n",
			bat->channels);
		exit(EXIT_FAILURE);
	}

	/* check single ended is in either playback or capture - not both */
	if (bat->playback_single && bat->capture_single) {
		fprintf(stderr, "error: single ended mode is simplex\n");
		exit(EXIT_FAILURE);
	}
}

static void bat_init(struct bat *bat)
{
	int ret;

	/* Determine capture file */
	if (bat->local == true)
		bat->capture_file = bat->playback_file;
	else
		bat->capture_file = TEMP_RECORD_FILE_NAME;

	/* Determine tiny device if needed */
	if (bat->tinyalsa == true) {
		get_tiny_format(bat->capture_device, &bat->capture_card_tiny,
				&bat->capture_device_tiny);
		get_tiny_format(bat->playback_device, &bat->playback_card_tiny,
				&bat->playback_device_tiny);
	}

	if (bat->playback_file == NULL) {
		/* No input file so we will generate our own sine wave */
		if (bat->frames) {
			if (bat->playback_single) {
				/* Play nb of frames given by -n argument */
				bat->sinus_duration = bat->frames;
			} else {
				/* Play 1 sec + twice the nb of frames
				 * to be analysed */
				bat->sinus_duration = bat->rate;
				bat->sinus_duration += 2 * bat->frames;
			}
		} else {
			/* Special case where we want to generate a sine wave
			 * endlessly without capturing */
			bat->sinus_duration = 0;
		}
	} else {
		bat->fp = fopen(bat->playback_file, "rb");
		if (bat->fp == NULL) {
			fprintf(stderr, "error: can't open %s %d\n",
				bat->playback_file, -errno);
			exit(EXIT_FAILURE);
		}
		ret = read_wav_header(bat);
		if (ret == -1)
			exit(EXIT_FAILURE);
	}

	bat->frame_size = bat->sample_size * bat->channels;
}

int main(int argc, char *argv[])
{
	struct bat bat;
	int ret = 0;

	set_defaults(&bat);

	parse_arguments(&bat, argc, argv);

	bat_init(&bat);

	validate_options(&bat);

	if (bat.playback_single) {
		test_playback(&bat);
		goto out;
	}

	if (bat.capture_single) {
		test_capture(&bat);
		goto analyze;
	}

	if (bat.local == false)
		test_loopback(&bat);

analyze:
	ret = analyze_capture(&bat);
out:
	fprintf(stdout, "\nReturn value is %d\n", ret);
	return ret;
}
