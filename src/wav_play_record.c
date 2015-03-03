#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <pthread.h>

#include <alsa/asoundlib.h>

#include "common.h"
#include "wav_play_record.h"

/*#define DEBUG*/

struct snd_pcm_container {
	snd_pcm_t *handle;
	snd_pcm_uframes_t period_size;
	snd_pcm_uframes_t buffer_size;
	snd_pcm_format_t format;
	unsigned short channels;
	size_t period_bytes;
	size_t sample_bits;
	size_t frame_bits;
	char *buffer;
};

/**
 * Called when thread is finished
 */
static void close_handle(void *handle)
{
	snd_pcm_t *hd = handle;
	if (NULL != hd)
		snd_pcm_close(hd);
}

static int set_snd_pcm_params(struct bat *bat, struct snd_pcm_container *sndpcm)
{
	snd_pcm_format_t format;
	snd_pcm_hw_params_t *params;
	unsigned int buffer_time = 0;
	unsigned int period_time = 0;
	unsigned int rate;
	int err;
	const char *device_name = snd_pcm_name(sndpcm->handle);

	/* Allocate a hardware parameters object. */
	snd_pcm_hw_params_alloca(&params);

	/* Fill it in with default values. */
	err = snd_pcm_hw_params_any(sndpcm->handle, params);
	if (err < 0) {
		fprintf(stderr,
			"Broken configuration for %s PCM: no configurations available: %s\n",
			snd_strerror(err), device_name);
		goto fail_exit;
	}


	/* Set access mode */
	err = snd_pcm_hw_params_set_access(sndpcm->handle, params,
		SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		fprintf(stderr,
			"Access type not available for %s: %s\n", device_name, snd_strerror(err));
		goto fail_exit;
	}

	/* Set format */
	switch (bat->sample_size) {
	case 1:
		format = SND_PCM_FORMAT_S8;
		break;
	case 2:
		format = SND_PCM_FORMAT_S16_LE;
		break;
	case 4:
		format = SND_PCM_FORMAT_S32_LE;
		break;
	default:
		fprintf(stderr, "error: format not supported!\n");
		goto fail_exit;
	}
	err = snd_pcm_hw_params_set_format(sndpcm->handle, params, format);
	if (err < 0) {
		fprintf(stderr,
			"Sample format not available for %s: %s\n", device_name, snd_strerror(err));
		goto fail_exit;
	}

	/* Set channels */
	err = snd_pcm_hw_params_set_channels(sndpcm->handle, params, bat->channels);
	if (err < 0) {
		fprintf(stderr,
				"Channels count (%i) not available for %s: %s\n", bat->channels, device_name, snd_strerror(err));
		goto fail_exit;
	}

	/* Set sampling rate */
	rate = bat->rate;
	err = snd_pcm_hw_params_set_rate_near(sndpcm->handle, params, &bat->rate, 0);
	if (err < 0) {
		fprintf(stderr,
				"Rate %iHz not available for %s: %s\n", bat->rate, device_name, snd_strerror(err));
		goto fail_exit;
	}
	if ((float)rate * 1.05 < bat->rate || (float)rate * 0.95 > bat->rate) {
		fprintf(stderr, "Rate is not accurate (requested = %iHz, got = %iHz)\n", rate, bat->rate);
		goto fail_exit;
	}

	if (snd_pcm_hw_params_get_buffer_time_max(params, &buffer_time,
			0) < 0) {
		fprintf(stderr, "error: snd_pcm_hw_params_get_buffer_time_max returns %i\n", err);
		goto fail_exit;
	}

	if (buffer_time > 500000)
		buffer_time = 500000;
	/* Was 4, changed to 8 to remove reduce capture overrun */
	period_time = buffer_time / 8;

	/* Set buffer time and period time */
	err = snd_pcm_hw_params_set_buffer_time_near(sndpcm->handle, params,
			&buffer_time, 0);
	if (err < 0) {
		fprintf(stderr,
				"Unable to set buffer time %i: %s\n", buffer_time, snd_strerror(err));
		goto fail_exit;
	}

	err = snd_pcm_hw_params_set_period_time_near(sndpcm->handle, params,
			&period_time, 0);
	if (err < 0) {
		fprintf(stderr,
				"Unable to set period time %i: %s\n", period_time, snd_strerror(err));
		goto fail_exit;
	}

	/* Write the parameters to the driver */
	if (snd_pcm_hw_params(sndpcm->handle, params) < 0) {
		fprintf(stderr, "Unable to set hw params for %s: %s\n", device_name, snd_strerror(err));
		goto fail_exit;
	}

	err = snd_pcm_hw_params_get_period_size(params, &sndpcm->period_size, 0);
	if (err < 0) {
		fprintf(stderr,
				"Unable to get period size: %s\n", snd_strerror(err));
		goto fail_exit;
	}

	err = snd_pcm_hw_params_get_buffer_size(params, &sndpcm->buffer_size);
	if (err < 0) {
		fprintf(stderr,
				"Unable to get buffer size: %s\n", snd_strerror(err));
		goto fail_exit;
	}

	if (sndpcm->period_size == sndpcm->buffer_size) {
		fprintf(stderr,
			"error: can't use period equal to buffer size (%lu = %lu)!\n",
			sndpcm->period_size, sndpcm->buffer_size);
		goto fail_exit;
	}

	sndpcm->sample_bits = snd_pcm_format_physical_width(format);
	if (sndpcm->sample_bits < 0) {
		fprintf(stderr,
			"error: snd_pcm_format_physical_width returns %i\n", err);
		goto fail_exit;
	}

	sndpcm->frame_bits = sndpcm->sample_bits * bat->channels;

	/* Calculate the period bytes */
	sndpcm->period_bytes = sndpcm->period_size * sndpcm->frame_bits / 8;
	sndpcm->buffer = (char *) malloc(sndpcm->period_bytes);
	if (sndpcm->buffer == NULL) {
		fprintf(stderr, "error: allocation of memory buffer of snd pcm fails!\n");
		goto fail_exit;
	}

	return 0;

fail_exit:
	return -1;
}

/*
 * Generate buffer to be played either from input file or from generated data
 * Return value
 * <0 error
 * 0 ok
 * >0 break
 */
static int generate_input_data(struct snd_pcm_container sndpcm, int count,
		struct bat *bat)
{
	int err;
	static int load;

	if (bat->playback_file != NULL) {
		/* From input file */
		load = 0;

		while (1) {
			err = fread(sndpcm.buffer + load, 1, count - load,
					bat->fp);
			if (0 == err) {
				if (feof(bat->fp)) {
					fprintf(stdout,
						"End of playing.\n");
					return 1;
				}
			}
			if (err < count - load) {
				if (ferror(bat->fp)) {
					fprintf(stderr,
						"Error reading input file!\n");
					return -1;
				}
				load += err;
			} else {
				break;
			}
		}
	} else {
		/* Generate sine wave */
		if ((bat->sinus_duration) && (load > bat->sinus_duration))
			return 1;

		void *buf;
		int max;
		/* Due to float conversion later on we need to get some margin
		 * on max value in order to avoid sign inversion */
		switch (bat->sample_size) {
		case 1:
			buf = (int8_t *) sndpcm.buffer;
			max = INT8_MAX-1;
			break;
		case 2:
			buf = (int16_t *) sndpcm.buffer;
			max = INT16_MAX-10;
			break;
		case 4:
			buf = (int32_t *) sndpcm.buffer;
			max = INT32_MAX-100;
			break;
		default:
			fprintf(stderr, "Format not supported!\n");
			return -1;
		}

		generate_sine_wave(bat, count * 8 / sndpcm.frame_bits, buf,
				max);

		load += (count * 8 / sndpcm.frame_bits);
	}

	bat->periods_played++;
	return 0;
}

static int write_to_pcm(int size, int err,
		const struct snd_pcm_container *sndpcm, int offset)
{
	while (size > 0) {
		err = snd_pcm_writei(sndpcm->handle, sndpcm->buffer + offset,
				size);
		if (err == -EAGAIN || (err >= 0 && err < size)) {
			snd_pcm_wait(sndpcm->handle, 500);
		} else if (err == -EPIPE) {
			fprintf(stderr, "Playback: Underrun occurred!\n");
			snd_pcm_prepare(sndpcm->handle);
		} else if (err < 0) {
			fprintf(stderr, "Write to pcm device fail!\n");
			return -1;
		}

		if (err > 0) {
			size -= err;
			offset += err * sndpcm->frame_bits / 8;
		}
	}
	return 0;
}

/**
 * Play
 */
void *playback_alsa(void *bat_param)
{
	int err = 0;
	struct snd_pcm_container sndpcm;
	int size, offset, count;
	struct bat *bat = (struct bat *) bat_param;
	int ret;

	fprintf(stdout, "Entering playback thread (ALSA).\n");

	memset(&sndpcm, 0, sizeof(sndpcm));
	if (NULL != bat->playback_device) {
		err = snd_pcm_open(&sndpcm.handle, bat->playback_device,
			SND_PCM_STREAM_PLAYBACK, 0);
		if (err < 0) {
			fprintf(stderr, "Unable to open pcm device: %s!\n",
				snd_strerror(err));
			goto fail_exit;
		}
	} else {
		fprintf(stderr, "No audio device to open for playback!\n");
		goto fail_exit;
	}

	err = set_snd_pcm_params(bat, &sndpcm);
	if (err != 0)
		goto fail_exit;

	if (bat->playback_file == NULL) {
		fprintf(stdout, "Playing generated audio sine wave");
		bat->sinus_duration == 0 ?
			fprintf(stdout, " endlessly\n") : fprintf(stdout, "\n");
	} else {
		fprintf(stdout, "Playing input audio file: %s\n",
				bat->playback_file);
	}

	count = sndpcm.period_bytes;
#ifdef DEBUG
	FILE *sin_file;
	sin_file = fopen("/tmp/sin.wav", "wb");
#endif
	while (1) {
		offset = 0;
		size = count * 8 / sndpcm.frame_bits;

		ret = generate_input_data(sndpcm, count, bat);
		if (ret < 0)
			goto fail_exit;
		else if (ret > 0)
			break;
#ifdef DEBUG
		fwrite(sndpcm.buffer, count * 8 / sndpcm.frame_bits, 4,
			sin_file);
#endif
		if (bat->period_limit &&
				bat->periods_played >= bat->periods_total)
			break;

		ret = write_to_pcm(size, err, &sndpcm, offset);
		if (ret == -1)
			goto fail_exit;
	}
#ifdef DEBUG
	fclose(sin_file);
#endif
	snd_pcm_drain(sndpcm.handle);
	if (bat->fp)
		fclose(bat->fp);
	free(sndpcm.buffer);
	snd_pcm_close(sndpcm.handle);
	retval_play = 0;
	pthread_exit(&retval_play);

fail_exit:
	if (bat->fp)
		fclose(bat->fp);
	if (sndpcm.buffer)
		free(sndpcm.buffer);
	if (sndpcm.handle)
		snd_pcm_close(sndpcm.handle);
	retval_play = 1;
	pthread_exit(&retval_play);
}

/**
 * Record
 */
void *record_alsa(void *bat_param)
{
	int err = 0;
	FILE *fp = NULL;
	struct snd_pcm_container sndpcm;
	struct wav_container wav;
	int size, offset, count, frames;
	struct bat *bat = (struct bat *) bat_param;

	if (bat->sinus_duration == 0 && bat->playback_file == NULL)
		return 0; /* No capture when playing sine wave endlessly */

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	fprintf(stdout, "Entering capture thread (ALSA).\n");
	memset(&sndpcm, 0, sizeof(sndpcm));

	if (NULL != bat->capture_device) {
		err = snd_pcm_open(&sndpcm.handle, bat->capture_device,
			SND_PCM_STREAM_CAPTURE, 0);
		if (err < 0) {
			fprintf(stderr, "Unable to open pcm device: %s!\n",
				snd_strerror(err));
			goto fail_exit;
		}
	} else {
		fprintf(stderr, "No audio device to open for capture!\n");
		goto fail_exit;
	}

	err = set_snd_pcm_params(bat, &sndpcm);
	if (err != 0)
		goto fail_exit;

	prepare_wav_info(&wav, bat);

	remove(bat->capture_file);
	fp = fopen(bat->capture_file, "w+");
	if (NULL == fp) {
		fprintf(stderr, "Cannot create file: %s!\n", bat->capture_file);
		goto fail_exit;
	}

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_cleanup_push(close_handle, sndpcm.handle);
	pthread_cleanup_push(destroy_mem, sndpcm.buffer);
	pthread_cleanup_push(close_file, fp);

	if (fwrite(&wav.header, 1,
			sizeof(wav.header), fp) != sizeof(wav.header)) {
		fprintf(stderr, "Error write wav file header!\n");
		goto fail_exit;
	}
	if (fwrite(&wav.format, 1,
			sizeof(wav.format), fp) != sizeof(wav.format)) {
		fprintf(stderr, "Error write wav file header!\n");
		goto fail_exit;
	}
	if (fwrite(&wav.chunk, 1,
			sizeof(wav.chunk), fp) != sizeof(wav.chunk)) {
		fprintf(stderr, "Error write wav file header!\n");
		goto fail_exit;
	}

	count = wav.chunk.length;
	fprintf(stdout, "Recording ...\n");
	while (count > 0) {
		size = (count <= sndpcm.period_bytes) ?
				count : sndpcm.period_bytes;
		frames = size * 8 / sndpcm.frame_bits;
		offset = 0;
		while (frames > 0) {
			err = snd_pcm_readi(sndpcm.handle,
					sndpcm.buffer + offset, frames);
			if (err == -EAGAIN || (err >= 0 && err < frames)) {
				snd_pcm_wait(sndpcm.handle, 500);
			} else if (err == -EPIPE) {
				snd_pcm_prepare(sndpcm.handle);
				fprintf(stderr, "Capture: Overrun occurred!\n");
			} else if (err < 0) {
				fprintf(stderr, "Read from pcm device fail!\n");
				goto fail_exit;
			}

			if (err > 0) {
				frames -= err;
				offset += err * sndpcm.frame_bits / 8;
			}
		}

		if (fwrite(sndpcm.buffer, 1, size, fp) != size) {
			fprintf(stderr, "Write to wav file fail!\n");
			goto fail_exit;
		}
		count -= size;
		bat->periods_played++;

		if (bat->period_limit &&
			bat->periods_played >= bat->periods_total)
			break;
	}

	/* Normally we will never reach this part of code (before fail_exit) as
	   this thread will be cancelled by end of play thread. */
	pthread_cleanup_pop(0);
	pthread_cleanup_pop(0);
	pthread_cleanup_pop(0);

	snd_pcm_drain(sndpcm.handle);
	fclose(fp);
	free(sndpcm.buffer);
	snd_pcm_close(sndpcm.handle);
	retval_record = 0;
	pthread_exit(&retval_record);

fail_exit:
	if (fp)
		fclose(fp);
	if (sndpcm.buffer)
		free(sndpcm.buffer);
	if (sndpcm.handle)
		snd_pcm_close(sndpcm.handle);
	retval_record = 1;
	pthread_exit(&retval_record);
}

