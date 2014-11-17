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

static void convert_alsa_device_string_to_tiny_card_and_device(char *alsa_device, unsigned int *tiny_card,
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
static void create_play_and_record_thread(struct bat* bat)
{
	int ret;
	pthread_t capture_id, playback_id;
	int *thread_playback_ret;

	ret = pthread_create(&playback_id, NULL, bat->playback, (void *) bat);
	if (0 != ret) {
		fprintf(stdout, "Create playback thread error!\n");
		exit(EXIT_FAILURE);
	}

	sleep(1); /* Let time for playing something before recording *//* FIXME should be either removed or reduced! */

	ret = pthread_create(&capture_id, NULL, bat->capture, (void *) bat);
	if (0 != ret) {
		fprintf(stdout, "Create capture thread error!\n");
		pthread_cancel(playback_id);
		exit(EXIT_FAILURE);
	}

	ret = pthread_join(playback_id, (void**) &thread_playback_ret);
	if (ret != 0) {
		fprintf(stdout, "Joining playback thread error!\n");
		pthread_cancel(playback_id);
		pthread_cancel(capture_id);
		exit(EXIT_FAILURE);
	}
	fprintf(stdout, "Play thread exit.\n");
	pthread_cancel(capture_id);
	if (*thread_playback_ret != 0) {
		fprintf(stderr, "Return value of playback thread is %x!\n", *thread_playback_ret);
		fprintf(stderr, "Sound play fail!\n");
		exit(EXIT_FAILURE);
	}
	ret = pthread_join(capture_id, NULL);
	if (ret != 0) {
		fprintf(stdout, "Joining capture thread error!\n");
		exit(EXIT_FAILURE);
	}
	fprintf(stdout, "Record thread exit.\n");
}

static void usage(char *argv[])
{
	fprintf(stdout, "Usage:%s [-D pcm device] [-P pcm playback device] [-C pcm capture device] [-f input file] "
			"[-s sample size] [-c number of channels] [-r sampling rate] "
			"[-n frames to capture] [-k sigma k] [-F Target Freq] "
			"[-l internal loop, bypass alsa] [-t use tinyalsa instead of alsa]\n", argv[0]);
	fprintf(stdout, "Usage:%s [-h]\n", argv[0]);
	exit(EXIT_FAILURE);
}

static void create_bat_struct(struct bat *bat)
{
	memset(bat, 0, sizeof(struct bat));

	/* Set default values */
	bat->rate = 44100;
	bat->channels = 1;
	bat->frame_size = 2;
	bat->sample_size = 2;
	bat->frames = bat->rate * 2;
	bat->target_freq = 997.0;
	bat->sigma_k = 3.0;
	bat->playback_device = NULL;
	bat->capture_device = NULL;
	bat->buf = NULL;
	bat->local = false;
	bat->playback = &playback_alsa;
	bat->capture = &record_alsa;
	bat->tinyalsa = false;
}

static void parse_arguments(struct bat *bat, int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "hf:s:n:c:F:r:D:P:C:k::l::t::")) != -1) {
		switch (opt) {
		case 'D':
			if (bat->playback_device == NULL) {
				bat->playback_device = optarg;
			}
			if (bat->capture_device == NULL) {
				bat->capture_device = optarg;
			}
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
			bat->target_freq = atof(optarg);
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
		case 't':
#ifdef HAVE_LIBTINYALSA
			bat->playback = &playback_tinyalsa;
			bat->capture = &record_tinyalsa;
			bat->tinyalsa = true;
#else
			fprintf(stdout,"You don't have tinyalsa installed!\n");
			exit(-EINVAL);
#endif
			break;
		case 'h':
		default: /* '?' */
			usage(argv);
		}
	}
}

static void check_bat_struct(struct bat *bat)
{
	if ((bat->local == true) && (bat->capture_file == NULL)) {
		fprintf(stderr, "You have to specify an input file for local testing!\n");
		exit(EXIT_FAILURE);
	}
	if (bat->channels > 2 || bat->channels < 1) {
		fprintf(stderr, "BAT only supports 1 or 2 channels for now!\n");
		exit(EXIT_FAILURE);
	}

}
static void set_bat_struct(struct bat *bat)
{
	int ret;

	/* Determine capture file */
	if (bat->local == true)
		bat->capture_file = bat->playback_file;
	else
		bat->capture_file = TEMP_RECORD_FILE_NAME;

	/* Determine tiny device if needed */
	if (bat->tinyalsa == true) {
		convert_alsa_device_string_to_tiny_card_and_device(bat->capture_device, &bat->capture_card_tiny,
				&bat->capture_device_tiny);
		convert_alsa_device_string_to_tiny_card_and_device(bat->playback_device, &bat->playback_card_tiny,
				&bat->playback_device_tiny);
	}

	if (bat->playback_file == NULL) {
		/* No input file so we will generate our own sine wave */
		if (bat->frames) {
			bat->sinus_duration = bat->rate; /* Nb of frames for 1 second */
			bat->sinus_duration += 2 * bat->frames; /* Play long enough to capture frame_size frames */
		} else {
			/* Special case where we want to generate a sine wave endlessly without capturing */
			bat->sinus_duration = 0;
		}
	} else {
		bat->fp = fopen(bat->playback_file, "rb");
		if (bat->fp == NULL) {
			fprintf(stderr, "Cannot access %s: No such file!\n", bat->playback_file);
			exit(EXIT_FAILURE);
		}
		ret = read_wav_header(bat);
		if (ret == -1) {
			exit(EXIT_FAILURE);
		}
	}

	bat->frame_size = bat->sample_size * bat->channels;

}

int main(int argc, char *argv[])
{
	struct bat bat;
	int ret;

	create_bat_struct(&bat);

	parse_arguments(&bat, argc, argv);

	set_bat_struct(&bat);

	check_bat_struct(&bat);

	if (bat.local == false) {
		create_play_and_record_thread(&bat);
	}

	ret = analyze_capture(&bat);

	fprintf(stdout, "\nReturn value is %d\n", ret);

	return ret;
}
