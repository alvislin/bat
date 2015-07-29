/*
 * Copyright (C) 2013-2015 Intel Corporation
 *
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>
#include <math.h>
#include <errno.h>

#include <tinyalsa/asoundlib.h>

#include "common.h"
#include "wav_play_record.h"

static int capturing = 1;
static int closed;

/**
 * Called when thread is finished
 */
static void close_handle(void *handle)
{
	struct pcm *pcm = handle;
	if (NULL != pcm)
		pcm_close(pcm);
}
/**
 * Handling of Ctrl-C for capture
 */
static void sigint_handler(int sig)
{
	capturing = 0;
}

/**
 * Handling of Ctrl-C for playback
 */
static void stream_close(int sig)
{
	/* allow the stream to be closed gracefully */
	signal(sig, SIG_IGN);
	closed = 1;
}

/**
 * Check that a parameter is inside bounds
 */
static int check_param(struct pcm_params *params, unsigned int param,
		unsigned int value, char *param_name, char *param_unit)
{
	unsigned int min;
	unsigned int max;
	int ret = 0;

	min = pcm_params_get_min(params, param);
	if (value < min) {
		loge(E_PARAMS, "%s is %u%s, device only supports >= %u%s!",
			param_name, value, param_unit, min, param_unit);
		ret = -EINVAL;
	}

	max = pcm_params_get_max(params, param);
	if (value > max) {
		loge(E_PARAMS, "%s is %u%s, device only supports <= %u%s!",
			param_name, value, param_unit, max, param_unit);
		ret = -EINVAL;
	}

	return ret;
}

static int generate_input_data(char *buffer, int size, struct bat *bat)
{
	static int load;
	int num_read;

	if (bat->playback.file != NULL) {
		num_read = fread(buffer, 1, size, bat->fp);
	} else {
		if ((bat->sinus_duration) && (load > bat->sinus_duration))
			return 0;

		void *buf;
		int max;
		/* Due to float conversion later on we need to get some margin
		 * on the max value in order to avoid sign inversion */
		switch (bat->sample_size) {
		case 1:
			buf = (int8_t *) buffer;
			max = INT8_MAX-1;
			break;
		case 2:
			buf = (int16_t *) buffer;
			max = INT16_MAX-10;
			break;
		case 4:
			buf = (int32_t *) buffer;
			max = INT32_MAX-100;
			break;
		default:
			loge(E_PARAMS S_PCMFORMAT, "size=%d", bat->sample_size);
			return -1;
		}

		generate_sine_wave(bat,
			size / bat->channels / bat->sample_size, buf, max);

		load += (size / bat->channels / bat->sample_size);
		num_read = size;
	}

	bat->periods_played++;
	return num_read;

}

/**
 * Sample size to format
 */
static enum pcm_format sample_size_to_format(int sample_size)
{
	switch (sample_size) {
	case 1:
		return PCM_FORMAT_S8;
	case 2:
		return PCM_FORMAT_S16_LE;
	case 4:
		return PCM_FORMAT_S32_LE;
	default:
		loge(E_PARAMS S_PCMFORMAT, "size = %d", sample_size);
		return PCM_FORMAT_MAX;
	}
}

/**
 * Check all parameters
 */
static int check_playback_params(struct bat *bat,
		struct pcm_config *config)
{
	struct pcm_params *params;
	unsigned int card = bat->playback.card_tiny;
	unsigned int device = bat->playback.device_tiny;
	int ret = 0;

	params = pcm_params_get(card, device, PCM_OUT);
	if (params == NULL) {
		loge(E_GETDEV, "%u", device);
		return -EINVAL;
	}

	ret = check_param(params, PCM_PARAM_RATE,
			config->rate, "Sample rate", "Hz");
	if (ret < 0)
		goto exit;
	ret = check_param(params, PCM_PARAM_CHANNELS,
			config->channels, "Sample", " channels");
	if (ret < 0)
		goto exit;
	ret = check_param(params, PCM_PARAM_SAMPLE_BITS,
			bat->sample_size * 8, "Bitrate", " bits");
	if (ret < 0)
		goto exit;
	ret = check_param(params, PCM_PARAM_PERIOD_SIZE,
			config->period_size, "Period size", "Hz");
	if (ret < 0)
		goto exit;
	ret = check_param(params, PCM_PARAM_PERIODS,
			config->period_count, "Period count", "Hz");
	if (ret < 0)
		goto exit;

exit:
	pcm_params_free(params);

	return ret;
}
/**
 * Play sample
 */
static int play_sample(struct bat *bat, struct pcm_config *config)
{
	struct pcm *pcm = NULL;
	char *buffer = NULL;
	unsigned int card = bat->playback.card_tiny;
	unsigned int device = bat->playback.device_tiny;
	int size;
	int num_read;
	int ret = 0;

	ret = check_playback_params(bat, config);
	if (ret < 0)
		goto exit;

	pcm = pcm_open(card, device, PCM_OUT, config);
	if (!pcm || !pcm_is_ready(pcm)) {
		loge(E_OPENPCMP, "%u: %s", device, pcm_get_error(pcm));
		ret = -ENODEV;
		goto exit;
	}

	size = pcm_frames_to_bytes(pcm, pcm_get_buffer_size(pcm));
	buffer = malloc(size);
	if (!buffer) {
		loge(E_MALLOC, "%d bytes", size);
		ret = -ENOMEM;
		goto exit;
	}

	printf("Playing sample: %u ch, %u hz, %u bits\n", bat->channels,
			bat->rate, bat->sample_size * 8);

	/* catch ctrl-c to shutdown cleanly */
	signal(SIGINT, stream_close);

	do {
		num_read = generate_input_data(buffer, size, bat);
		if (num_read > 0) {
			if (pcm_write(pcm, buffer, num_read)) {
				loge(E_WRITEPCM, "%d bytes", num_read);
				break;
			}
		}
		if (bat->period_limit &&
				bat->periods_played >= bat->periods_total)
			break;
	} while (!closed && num_read > 0);

exit:
	if (buffer)
		free(buffer);
	if (pcm)
		pcm_close(pcm);
	return ret;
}

/**
 * Play
 */
void *playback_tinyalsa(struct bat *bat)
{
	struct pcm_config config;
	enum pcm_format format;
	int ret = 0;

	retval_play = 0;

	config.channels = bat->channels;
	config.rate = bat->rate;
	config.period_size = 1024;
	config.period_count = 4;
	format = sample_size_to_format(bat->sample_size);
	if (format == PCM_FORMAT_MAX) {
		retval_play = 1;
		goto exit;
	}
	config.format = format;
	config.start_threshold = 0;
	config.stop_threshold = 0;
	config.silence_threshold = 0;

	printf("Entering playback thread (tinyalsa).\n");

	if (bat->playback.file == NULL) {
		printf("Playing generated audio sine wave ");
		bat->sinus_duration == 0 ?
			printf("endlessly\n") : printf("\n");
	} else {
		printf("Playing input audio file: %s\n",
				bat->playback.file);
		bat->fp = fopen(bat->playback.file, "rb");
		if (bat->fp == NULL) {
			loge(E_OPENFILEC, "%s", bat->playback.file);
			goto exit;
		}
	}

	ret = play_sample(bat, &config);
	if (ret < 0)
		retval_play = 1;

exit:
	if (bat->fp)
		fclose(bat->fp);
	pthread_exit(&retval_play);
}

static unsigned int capture_sample(FILE *file, struct bat *bat,
		struct pcm_config *config)
{
	struct pcm *pcm;
	char *buffer;
	unsigned int size;
	unsigned int bytes_read = 0;

	printf("rate: %i, format: %i\n", config->rate, config->format);

	pcm = pcm_open(bat->capture.card_tiny, bat->capture.device_tiny,
			PCM_IN, config);
	if (!pcm || !pcm_is_ready(pcm)) {
		loge(E_OPENPCMC, "%s", pcm_get_error(pcm));
		return 0;
	}
	pthread_cleanup_push(close_handle, pcm);

	size = pcm_frames_to_bytes(pcm, pcm_get_buffer_size(pcm));
	printf("Capture: size = %i\n", size);
	buffer = malloc(size);
	if (!buffer) {
		loge(E_MALLOC, "%d bytes", size);
		pcm_close(pcm);
		return 0;
	}
	pthread_cleanup_push(destroy_mem, buffer);

	printf("Capturing sample: %u ch, %u hz, %u bit\n",
			config->channels, config->rate,
			pcm_format_to_bits(config->format));

	while (bytes_read < bat->frames*bat->frame_size && capturing &&
			!pcm_read(pcm, buffer, size)) {
		if (fwrite(buffer, 1, size, file) != size) {
			loge(E_WRITEPCM, "break");
			break;
		}
		bytes_read += size;

		bat->periods_played++;

		if (bat->period_limit &&
				bat->periods_played >= bat->periods_total)
			break;

	}

	/* Normally we will never reach this part of code (before fail_exit) as
	   this thread will be cancelled by end of play thread. Except in case
	   of single line mode (capture only) */
	pthread_cleanup_pop(0);
	pthread_cleanup_pop(0);

	free(buffer);
	pcm_close(pcm);

	return pcm_bytes_to_frames(pcm, bytes_read);
}
/**
 * Record
 */
void *record_tinyalsa(struct bat *bat)
{
	FILE *file = NULL;
	struct pcm_config config;
	enum pcm_format format;
	struct wav_container header;
	unsigned int frames_read = 0;

	/* init config */
	config.channels = bat->channels;
	config.rate = bat->rate;
	config.period_size = 1024;
	config.period_count = 4;
	format = sample_size_to_format(bat->sample_size);
	if (format == PCM_FORMAT_MAX)
		goto fail_exit;
	config.format = format;
	config.start_threshold = 0;
	config.stop_threshold = 0;
	config.silence_threshold = 0;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	printf("Entering capture thread (tinyalsa).\n");

	/* init wav header */
	remove(bat->capture.file);
	file = fopen(bat->capture.file, "wb");
	if (!file) {
		loge(E_OPENFILEC, "%s", bat->capture.file);
		goto fail_exit;
	}

	header.header.magic = WAV_RIFF;
	header.header.type = WAV_WAVE;
	header.format.magic = WAV_FMT;
	header.format.fmt_size = 16;
	header.format.format = WAV_FORMAT_PCM;
	header.chunk.type = WAV_DATA;
	header.format.channels = bat->channels;
	header.format.sample_rate = bat->rate;
	header.format.sample_length = bat->frame_size * 8;
	header.format.blocks_align = bat->channels *
			(header.format.sample_length / 8);
	header.format.bytes_p_second = header.format.blocks_align * bat->rate;
	header.chunk.length = 10 * header.format.bytes_p_second;
	header.header.length = header.chunk.length + sizeof(header) - 8;

	if (fwrite(&header.header, 1, sizeof(header.header),
			file) != sizeof(header.header)) {
		loge(E_WRITEFILE, "header header");
		goto fail_exit;
	}
	if (fwrite(&header.format, 1, sizeof(header.format),
			file) != sizeof(header.format)) {
		loge(E_WRITEFILE, "header format");
		goto fail_exit;

	}
	if (fwrite(&header.chunk, 1, sizeof(header.chunk),
			file) != sizeof(header.chunk)) {
		loge(E_WRITEFILE, "header chunk");
		goto fail_exit;
	}

	/* install signal handler and begin capturing Ctrl-C */
	signal(SIGINT, sigint_handler);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

	pthread_cleanup_push((void *)close_file, file);

	frames_read = capture_sample(file, bat, &config);

	/* Normally we will never reach this part of code (unless error in
	 * previous call) (before fail_exit) as this thread will be cancelled
	 *  by end of play thread. Except in single line mode. */
	if (frames_read == 0)
		goto fail_exit;

	/* Normally we will never reach this part of code (before fail_exit) as
	   this thread will be cancelled by end of play thread. */
	pthread_cleanup_pop(0);

	fclose(file);

	retval_record = 0;
	pthread_exit(&retval_record);

fail_exit:
	if (file)
		fclose(file);
	retval_record = 1;
	pthread_exit(&retval_record);
}

