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

#include "common.h"

#include "wav_play_record.h"
#include "wav_play_record_tiny.h"
#include "convert.h"
#include "analyze.h"

static void get_duration(struct bat *bat)
{
	float duration_f;
	int duration_i;
	char *ptrf, *ptri;

	duration_f = strtod(bat->narg, &ptrf);
	duration_i = strtol(bat->narg, &ptri, 10);
	if (*ptrf == 's')
		bat->frames = duration_f * bat->rate;
	else if (*ptri == 0)
		bat->frames = duration_i;
	else {
		fprintf(stderr, "error: -n argument is not valid\n");
		exit(EXIT_FAILURE);
	}
}

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
	ret = pthread_create(&playback_id, NULL,
			(void *) bat->playback.fct, bat);
	if (ret != 0) {
		fprintf(stderr, "error: failed to create playback thread\n");
		exit(EXIT_FAILURE);
	}

	/* TODO: use a pipe to signal stream start etc - i.e. to sync threads */
	/* Let some time for playing something before capturing */
	usleep(PLAYBACK_TIME_BEFORE_CAPTURE * 1000);

	/* start capture */
	ret = pthread_create(&capture_id, NULL, (void *) bat->capture.fct, bat);
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
		fprintf(stderr, "error: playback failed %d\n",
				*thread_result_playback);
		pthread_cancel(capture_id);
		exit(EXIT_FAILURE);
	} else
		printf("Playback completed.\n");

	/* now stop and wait for capture to finish */
	pthread_cancel(capture_id);
	ret = thread_wait_completion(bat, capture_id, &thread_result_capture);
	if (ret != 0) {
		fprintf(stderr, "error: can't join capture thread\n");
		exit(EXIT_FAILURE);
	}

	/* check capture status */
	if (*thread_result_capture != 0) {
		fprintf(stderr, "error: capture failed %d\n",
				*thread_result_capture);
		exit(EXIT_FAILURE);
	} else
		printf("Capture completed.\n");
}

/* single ended playback only test */
static void test_playback(struct bat *bat)
{
	pthread_t playback_id;
	int ret;
	int *thread_result;

	/* start playback */
	ret = pthread_create(&playback_id, NULL,
			(void *) bat->playback.fct, bat);
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
		printf("Playback completed.\n");

}

/* single ended capture only test */
static void test_capture(struct bat *bat)
{
	pthread_t capture_id;
	int ret;
	int *thread_result;

	/* start capture */
	ret = pthread_create(&capture_id, NULL, (void *) bat->capture.fct, bat);
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
		printf("Capture completed.\n");

}

static void usage(char *argv[])
{
	fprintf(stdout,
			"Usage:%s [-D sound card] [-P playback pcm] [-C capture pcm] [-f input file]\n"
			"         [-s sample size] [-c number of channels] [-r sampling rate]\n"
			"         [-n frames to capture] [-k sigma k] [-F Target Freq]\n"
			"         [-l internal loop, bypass hardware]\n"
			"         [-t use tinyalsa instead of alsa]\n"
			"         [-a single ended capture (deprecated, use -C alone)]\n"
			"         [-b single ended playback (deprecated, use -P alone)]\n"
			"         [-p total number of periods to play/capture]\n",
			argv[0]);
	printf("Usage:%s [-h]\n", argv[0]);
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
	bat->convert_float_to_sample = convert_float_to_int16;
	bat->convert_sample_to_double = convert_int16_to_double;
	bat->frames = bat->rate * 2;
	bat->target_freq[0] = 997.0;
	bat->target_freq[1] = 997.0;
	bat->sigma_k = 3.0;
	bat->playback.device = NULL;
	bat->capture.device = NULL;
	bat->buf = NULL;
	bat->local = false;
	bat->playback.fct = &playback_alsa;
	bat->capture.fct = &record_alsa;
	bat->tinyalsa = false;
	bat->playback.single = false;
	bat->capture.single = false;
	bat->period_limit = false;
}

static void parse_arguments(struct bat *bat, int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv,
			"hf:s:n:c:F:r:D:P:C:k:p:ltab")) != -1) {
		switch (opt) {
		case 'D':
			if (bat->playback.device == NULL)
				bat->playback.device = optarg;
			if (bat->capture.device == NULL)
				bat->capture.device = optarg;
			break;
		case 'P':
			if (bat->capture.single == true)
				bat->capture.single = false;
			else
				bat->playback.single = true;
			bat->playback.device = optarg;
			break;
		case 'C':
			if (bat->playback.single == true)
				bat->playback.single = false;
			else
				bat->capture.single = true;
			bat->capture.device = optarg;
			break;
		case 'f':
			bat->playback.file = optarg;
			break;
		case 'n':
			bat->narg = optarg;
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
			bat->capture.single = true;
			break;
		case 'b':
			bat->playback.single = true;
			break;
		case 'p':
			bat->periods_total = atoi(optarg);
			bat->period_limit = true;
			break;
		case 't':
#ifdef HAVE_LIBTINYALSA
			bat->playback.fct = &playback_tinyalsa;
			bat->capture.fct = &record_tinyalsa;
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

static void validate_options(struct bat *bat)
{
	/* check we have an input file for local mode */
	if ((bat->local == true) && (bat->capture.file == NULL)) {
		fprintf(stderr, "error: no input file for local testing\n");
		exit(EXIT_FAILURE);
	}

	/* check supported channels */
	if (bat->channels > CHANNEL_MAX || bat->channels < CHANNEL_MIN) {
		fprintf(stderr, "error: %d channels not supported\n",
				bat->channels);
		exit(EXIT_FAILURE);
	}

	/* check single ended is in either playback or capture - not both */
	if (bat->playback.single && bat->capture.single) {
		fprintf(stderr, "error: single ended mode is simplex\n");
		exit(EXIT_FAILURE);
	}
}

static void bat_init(struct bat *bat)
{
	int ret;

	/* Determine n */
	if (bat->narg != 0)
		get_duration(bat);

	/* Determine capture file */
	if (bat->local == true)
		bat->capture.file = bat->playback.file;
	else
		bat->capture.file = TEMP_RECORD_FILE_NAME;

	/* Determine tiny device if needed */
	if (bat->tinyalsa == true) {
		if (bat->playback.single == false)
			get_tiny_format(bat->capture.device,
					&bat->capture.card_tiny,
					&bat->capture.device_tiny);
		if (bat->capture.single == false)
			get_tiny_format(bat->playback.device,
					&bat->playback.card_tiny,
					&bat->playback.device_tiny);
	}

	if (bat->playback.file == NULL) {
		/* No input file so we will generate our own sine wave */
		if (bat->frames) {
			if (bat->playback.single) {
				/* Play nb of frames given by -n argument */
				bat->sinus_duration = bat->frames;
			} else {
				/* Play PLAYBACK_TIME_BEFORE_CAPTURE msec +
				 * 150% of the nb of frames to be analysed */
				bat->sinus_duration = bat->rate *
						PLAYBACK_TIME_BEFORE_CAPTURE
						/ 1000;
				bat->sinus_duration +=
						(bat->frames + bat->frames / 2);
			}
		} else {
			/* Special case where we want to generate a sine wave
			 * endlessly without capturing */
			bat->sinus_duration = 0;
		}
	} else {
		bat->fp = fopen(bat->playback.file, "rb");
		if (bat->fp == NULL) {
			fprintf(stderr, "error: can't open %s %d\n",
					bat->playback.file, -errno);
			exit(EXIT_FAILURE);
		}
		ret = read_wav_header(bat, bat->playback.file, false);
		if (ret == -1)
			exit(EXIT_FAILURE);
	}

	bat->frame_size = bat->sample_size * bat->channels;

	/* Set convert function */
	switch (bat->sample_size) {
	case 1:
		bat->convert_float_to_sample = convert_float_to_int8;
		bat->convert_sample_to_double = convert_int8_to_double;
		break;
	case 2:
		bat->convert_float_to_sample = convert_float_to_int16;
		bat->convert_sample_to_double = convert_int16_to_double;
		break;
	case 3:
		bat->convert_float_to_sample = convert_float_to_int24;
		bat->convert_sample_to_double = convert_int24_to_double;
		break;
	case 4:
		bat->convert_float_to_sample = convert_float_to_int32;
		bat->convert_sample_to_double = convert_int32_to_double;
		break;
	default:
		fprintf(stderr, "error: sample size not supported!\n");
		exit(EXIT_FAILURE);
		break;
	}

}

int main(int argc, char *argv[])
{
	struct bat bat;
	int ret = 0;

	printf("%s version %s\n\n", PACKAGE_NAME, PACKAGE_VERSION);

	set_defaults(&bat);

	parse_arguments(&bat, argc, argv);

	bat_init(&bat);

	validate_options(&bat);

	if (bat.playback.single) {
		test_playback(&bat);
		goto out;
	}

	if (bat.capture.single) {
		test_capture(&bat);
		goto analyze;
	}

	if (bat.local == false)
		test_loopback(&bat);

analyze:
	ret = analyze_capture(&bat);
out:
	printf("\nReturn value is %d\n", ret);
	return ret;
}
