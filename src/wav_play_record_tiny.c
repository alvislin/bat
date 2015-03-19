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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>
#include <math.h>

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
	int is_within_bounds = 1;

	min = pcm_params_get_min(params, param);
	if (value < min) {
		fprintf(stderr, "%s is %u%s, device only supports >= %u%s!\n",
			param_name, value, param_unit, min, param_unit);
		is_within_bounds = 0;
	}

	max = pcm_params_get_max(params, param);
	if (value > max) {
		fprintf(stderr, "%s is %u%s, device only supports <= %u%s!\n",
			param_name, value, param_unit, max, param_unit);
		is_within_bounds = 0;
	}

	return is_within_bounds;
}

static int generate_input_data(char *buffer, int size, struct bat *bat)
{
	static int load;
	int num_read;

	if (bat->playback_file != NULL) {
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
			fprintf(stderr, "Format not supported!\n");
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
 * Check all parameters
 */
static int sample_is_playable(unsigned int card, unsigned int device,
		unsigned int channels, unsigned int rate,
		unsigned int bits, unsigned int period_size,
		unsigned int period_count)
{
	struct pcm_params *params;
	int can_play;

	params = pcm_params_get(card, device, PCM_OUT);
	if (params == NULL) {
		fprintf(stderr, "Unable to open PCM device %u!\n", device);
		return 0;
	}

	can_play = check_param(params, PCM_PARAM_RATE, rate,
			"Sample rate", "Hz");
	can_play &= check_param(params, PCM_PARAM_CHANNELS, channels,
			"Sample", " channels");
	can_play &= check_param(params, PCM_PARAM_SAMPLE_BITS, bits,
			"Bitrate", " bits");
	can_play &= check_param(params, PCM_PARAM_PERIOD_SIZE, period_size,
			"Period size", "Hz");
	can_play &= check_param(params, PCM_PARAM_PERIODS, period_count,
			"Period count", "Hz");

	pcm_params_free(params);

	return can_play;
}
/**
 * Play sample
 */
static unsigned int play_sample(unsigned int card, unsigned int device,
		struct bat *bat, unsigned int period_size,
		unsigned int period_count)
{
	struct pcm_config config;
	struct pcm *pcm;
	char *buffer;
	int size;
	int num_read;

	config.channels = bat->channels;
	config.rate = bat->rate;
	config.period_size = period_size;
	config.period_count = period_count;
	switch (bat->sample_size) {
	case 1:
		config.format = PCM_FORMAT_S8;
		break;
	case 2:
		config.format = PCM_FORMAT_S16_LE;
		break;
	case 4:
		config.format = PCM_FORMAT_S32_LE;
		break;
	default:
		fprintf(stderr, "Not supported format!\n");
		return 0;
	}
	config.start_threshold = 0;
	config.stop_threshold = 0;
	config.silence_threshold = 0;

	if (!sample_is_playable(card, device, bat->channels, bat->rate,
			bat->sample_size * 8, period_size, period_count))
		return 0;


	pcm = pcm_open(card, device, PCM_OUT, &config);
	if (!pcm || !pcm_is_ready(pcm)) {
		fprintf(stderr, "Unable to open PCM device %u (%s)!\n",
				device, pcm_get_error(pcm));
		return 0;
	}

	size = pcm_frames_to_bytes(pcm, pcm_get_buffer_size(pcm));
	buffer = malloc(size);
	if (!buffer) {
		fprintf(stderr, "Unable to allocate %d bytes!\n", size);
		pcm_close(pcm);
		return 0;
	}

	printf("Playing sample: %u ch, %u hz, %u bits\n", bat->channels,
			bat->rate, bat->sample_size * 8);

	/* catch ctrl-c to shutdown cleanly */
	signal(SIGINT, stream_close);

	do {
		num_read = generate_input_data(buffer, size, bat);
		if (num_read > 0) {
			if (pcm_write(pcm, buffer, num_read)) {
				fprintf(stderr, "Error playing sample!\n");
				break;
			}
		}
		if (bat->period_limit &&
				bat->periods_played >= bat->periods_total)
			break;
	} while (!closed && num_read > 0);

	free(buffer);
	pcm_close(pcm);
	return 1;
}

/**
 * Play
 */
void *playback_tinyalsa(void *bat_param)
{
	unsigned int period_size = 1024;
	unsigned int period_count = 4;
	unsigned int ret;

	struct bat *bat = (struct bat *) bat_param;
	retval_play = 0;

	fprintf(stdout, "Entering playback thread (tinyalsa).\n");

	if (bat->playback_file == NULL) {
		fprintf(stdout, "Playing generated audio sine wave ");
		bat->sinus_duration == 0 ?
			fprintf(stdout, "endlessly\n") : fprintf(stdout, "\n");
	} else {
		fprintf(stdout, "Playing input audio file: %s\n",
				bat->playback_file);
	}

	ret = play_sample(bat->playback_card_tiny, bat->playback_device_tiny,
			bat, period_size, period_count);
	if (ret == 0)
		retval_play = 1;

	if (bat->fp)
		fclose(bat->fp);
	pthread_exit(&retval_play);
}

static unsigned int capture_sample(FILE *file, struct bat *bat,
		unsigned int channels, unsigned int rate,
		enum pcm_format format, unsigned int period_size,
		unsigned int period_count)
{
	struct pcm_config config;
	struct pcm *pcm;
	char *buffer;
	unsigned int size;
	unsigned int bytes_read = 0;

	config.channels = channels;
	config.rate = rate;
	config.period_size = period_size;
	config.period_count = period_count;
	config.format = format;
	config.start_threshold = 0;
	config.stop_threshold = 0;
	config.silence_threshold = 0;

	printf("rate: %i, format: %i\n", config.rate, config.format);

	pcm = pcm_open(bat->capture_card_tiny, bat->capture_device_tiny,
			PCM_IN, &config);
	if (!pcm || !pcm_is_ready(pcm)) {
		fprintf(stderr, "Unable to open PCM device (%s)!\n",
				pcm_get_error(pcm));
		return 0;
	}
	pthread_cleanup_push(close_handle, pcm);

	size = pcm_frames_to_bytes(pcm, pcm_get_buffer_size(pcm));
	printf("Capture: size = %i\n", size);
	buffer = malloc(size);
	if (!buffer) {
		fprintf(stderr, "Unable to allocate %d bytes!\n", size);
		pcm_close(pcm);
		return 0;
	}
	pthread_cleanup_push(destroy_mem, buffer);

	printf("Capturing sample: %u ch, %u hz, %u bit\n", channels, rate,
			pcm_format_to_bits(format));

	while (bytes_read < bat->frames*bat->frame_size && capturing &&
			!pcm_read(pcm, buffer, size)) {
		if (fwrite(buffer, 1, size, file) != size) {
			fprintf(stderr, "Error capturing sample!\n");
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
void *record_tinyalsa(void *bat_param)
{
	FILE *file = NULL;
	struct wav_container header;
	unsigned int period_size = 1024;
	unsigned int period_count = 4;
	enum pcm_format format = 0;
	unsigned int ret;

	struct bat *bat = (struct bat *) bat_param;
	switch (bat->sample_size) {
	case 1:
		format = PCM_FORMAT_S8;
		break;
	case 2:
		format = PCM_FORMAT_S16_LE;
		break;
	case 4:
		format = PCM_FORMAT_S32_LE;
		break;
	default:
		fprintf(stderr, "Unsupported format!\n");
		goto fail_exit;
	}

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	fprintf(stdout, "Entering capture thread (tinyalsa).\n");

	remove(bat->capture_file);
	file = fopen(bat->capture_file, "wb");
	if (!file) {
		fprintf(stderr, "Cannot create file: %s!\n",
				bat->capture_file);
		goto fail_exit;
	}

	header.header.magic = WAV_RIFF;
	header.header.type = WAV_WAVE;
	header.format.magic = WAV_FMT;
	header.format.fmt_size = 16;
	header.format.format = FORMAT_PCM;
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
		fprintf(stderr, "Error write wav file header!\n");
		goto fail_exit;
	}
	if (fwrite(&header.format, 1, sizeof(header.format),
			file) != sizeof(header.format)) {
		fprintf(stderr, "Error write wav file header!\n");
		goto fail_exit;

	}
	if (fwrite(&header.chunk, 1, sizeof(header.chunk),
			file) != sizeof(header.chunk)) {
		fprintf(stderr, "Error write wav file header!\n");
		goto fail_exit;
	}

	/* install signal handler and begin capturing Ctrl-C */
	signal(SIGINT, sigint_handler);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

	pthread_cleanup_push(close_file, file);

	ret = capture_sample(file, bat, header.format.channels,
			header.format.sample_rate, format, period_size,
			period_count);

	/* Normally we will never reach this part of code (unless error in
	 * previous call) (before fail_exit) as this thread will be cancelled
	 *  by end of play thread. Except in single line mode. */
	if (ret == 0)
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

