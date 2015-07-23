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
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <pthread.h>

#include <alsa/asoundlib.h>

#include "common.h"
#include "wav_play_record.h"

/*#define DEBUG		Uncomment if you want to save
					the input data in /tmp/sin.wav */

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
		loge(E_SETDEV S_DEFAULT, "%s: %s(%d)",
				device_name, snd_strerror(err), err);
		goto fail_exit;
	}

	/* Set access mode */
	err = snd_pcm_hw_params_set_access(sndpcm->handle, params,
			SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		loge(E_SETDEV S_ACCESS, "%s: %s(%d)",
				device_name, snd_strerror(err), err);
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
	case 3:
		format = SND_PCM_FORMAT_S24_3LE;
		break;
	case 4:
		format = SND_PCM_FORMAT_S32_LE;
		break;
	default:
		loge(E_PARAMS S_PCMFORMAT, "size=%d", bat->sample_size);
		goto fail_exit;
	}
	err = snd_pcm_hw_params_set_format(sndpcm->handle, params, format);
	if (err < 0) {
		loge(E_SETDEV S_PCMFORMAT, "%d %s: %s(%d)",
				format,
				device_name, snd_strerror(err), err);
		goto fail_exit;
	}

	/* Set channels */
	err = snd_pcm_hw_params_set_channels(sndpcm->handle,
			params, bat->channels);
	if (err < 0) {
		loge(E_SETDEV S_CHANNELS, "%d %s: %s(%d)",
				bat->channels,
				device_name, snd_strerror(err), err);
		goto fail_exit;
	}

	/* Set sampling rate */
	rate = bat->rate;
	err = snd_pcm_hw_params_set_rate_near(sndpcm->handle,
			params, &bat->rate,
			0);
	if (err < 0) {
		loge(E_SETDEV S_SAMPLERATE, "%d %s: %s(%d)",
				bat->rate,
				device_name, snd_strerror(err), err);
		goto fail_exit;
	}
	if ((float) rate * 1.05 < bat->rate
			|| (float) rate * 0.95 > bat->rate) {
		loge(E_PARAMS S_SAMPLERATE, "requested %dHz, got %dHz",
				rate, bat->rate);
		goto fail_exit;
	}

	if (snd_pcm_hw_params_get_buffer_time_max(params,
			&buffer_time, 0) < 0) {
		loge(E_GETDEV S_BUFFERTIME, "%d %s: %s(%d)",
				buffer_time,
				device_name, snd_strerror(err), err);
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
		loge(E_SETDEV S_BUFFERTIME, "%d %s: %s(%d)",
				buffer_time,
				device_name, snd_strerror(err), err);
		goto fail_exit;
	}

	err = snd_pcm_hw_params_set_period_time_near(sndpcm->handle, params,
			&period_time, 0);
	if (err < 0) {
		loge(E_SETDEV S_PERIODTIME, "%d %s: %s(%d)",
				period_time,
				device_name, snd_strerror(err), err);
		goto fail_exit;
	}

	/* Write the parameters to the driver */
	if (snd_pcm_hw_params(sndpcm->handle, params) < 0) {
		loge(E_SETDEV S_HWPARAMS, "%s: %s(%d)",
				device_name, snd_strerror(err), err);
		goto fail_exit;
	}

	err = snd_pcm_hw_params_get_period_size(params,
			&sndpcm->period_size, 0);
	if (err < 0) {
		loge(E_GETDEV S_PERIODSIZE, "%zd %s: %s(%d)",
				sndpcm->period_size,
				device_name, snd_strerror(err), err);
		goto fail_exit;
	}

	err = snd_pcm_hw_params_get_buffer_size(params, &sndpcm->buffer_size);
	if (err < 0) {
		loge(E_GETDEV S_BUFFERSIZE, "%zd %s: %s(%d)",
				sndpcm->buffer_size,
				device_name, snd_strerror(err), err);
		goto fail_exit;
	}

	if (sndpcm->period_size == sndpcm->buffer_size) {
		loge(E_PARAMS, "can't use period equal to buffer size (%zd)",
				sndpcm->period_size);
		goto fail_exit;
	}

	err = snd_pcm_format_physical_width(format);
	if (err < 0) {
		loge(E_PARAMS, "snd_pcm_format_physical_width: %d", err);
		goto fail_exit;
	}
	sndpcm->sample_bits = err;

	sndpcm->frame_bits = sndpcm->sample_bits * bat->channels;

	/* Calculate the period bytes */
	sndpcm->period_bytes = sndpcm->period_size * sndpcm->frame_bits / 8;
	sndpcm->buffer = (char *) malloc(sndpcm->period_bytes);
	if (sndpcm->buffer == NULL) {
		loge(E_MALLOC, "size=%zd", sndpcm->period_bytes);
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

	if (bat->playback.file != NULL) {
		/* From input file */
		load = 0;

		while (1) {
			err = fread(sndpcm.buffer + load, 1,
					count - load, bat->fp);
			if (0 == err) {
				if (feof(bat->fp)) {
					printf("End of playing.\n");
					return 1;
				}
			}
			if (err < count - load) {
				if (ferror(bat->fp)) {
					loge(E_READFILE, "%d", err);
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
			max = INT8_MAX - 1;
			break;
		case 2:
			buf = (int16_t *) sndpcm.buffer;
			max = INT16_MAX - 10;
			break;
		case 3:
			buf = (int8_t *) sndpcm.buffer;
			max = (1 << 23) - 50;
			break;
		case 4:
			buf = (int32_t *) sndpcm.buffer;
			max = INT32_MAX - 100;
			break;
		default:
			loge(E_PARAMS S_PCMFORMAT, "size=%d", bat->sample_size);
			return -1;
		}

		generate_sine_wave(bat, count * 8 / sndpcm.frame_bits,
				buf, max);

		load += (count * 8 / sndpcm.frame_bits);
	}

	bat->periods_played++;
	return 0;
}

static int write_to_pcm(int size, const struct snd_pcm_container *sndpcm,
		int offset)
{
	int err;

	while (size > 0) {
		err = snd_pcm_writei(sndpcm->handle, sndpcm->buffer + offset,
				size);
		if (err == -EAGAIN || (err >= 0 && err < size)) {
			snd_pcm_wait(sndpcm->handle, 500);
		} else if (err == -EPIPE) {
			loge(E_WRITEPCM S_UNDERRUN, "%s(%d)",
					snd_strerror(err), err);
			snd_pcm_prepare(sndpcm->handle);
		} else if (err < 0) {
			loge(E_WRITEPCM, "%s(%d)", snd_strerror(err), err);
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
void *playback_alsa(struct bat *bat)
{
	int err = 0;
	struct snd_pcm_container sndpcm;
	int size, offset, count;

	printf("Entering playback thread (ALSA).\n");

	memset(&sndpcm, 0, sizeof(sndpcm));
	if (NULL != bat->playback.device) {
		err = snd_pcm_open(&sndpcm.handle, bat->playback.device,
				SND_PCM_STREAM_PLAYBACK, 0);
		if (err < 0) {
			loge(E_OPENPCMP, "%s(%d)", snd_strerror(err), err);
			goto fail_exit;
		}
	} else {
		loge(E_NOPCMP, "exit");
		goto fail_exit;
	}

	err = set_snd_pcm_params(bat, &sndpcm);
	if (err != 0)
		goto fail_exit;

	if (bat->playback.file == NULL) {
		printf("Playing generated audio sine wave");
		bat->sinus_duration == 0 ?
				printf(" endlessly\n") : printf("\n");
	} else {
		printf("Playing input audio file: %s\n", bat->playback.file);
		bat->fp = fopen(bat->playback.file, "rb");
		if (bat->fp == NULL) {
			loge(E_OPENFILEC, "%s", bat->playback.file);
			goto fail_exit;
		}
	}

	count = sndpcm.period_bytes; /* playback buffer size */
#ifdef DEBUG
	FILE *sin_file;
	sin_file = fopen("/tmp/sin.wav", "wb");
#endif
	while (1) {
		offset = 0;
		size = count * 8 / sndpcm.frame_bits;

		err = generate_input_data(sndpcm, count, bat);
		if (err < 0)
			goto fail_exit;
		else if (err > 0)
			break;
#ifdef DEBUG
		fwrite(sndpcm.buffer, count * 8 / sndpcm.frame_bits, 4,
				sin_file);
#endif
		if (bat->period_limit
				&& bat->periods_played >= bat->periods_total)
			break;

		err = write_to_pcm(size, &sndpcm, offset);
		if (err == -1)
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
void *record_alsa(struct bat *bat)
{
	int err = 0;
	FILE *fp = NULL;
	struct snd_pcm_container sndpcm;
	struct wav_container wav;
	int size, offset, count, frames;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	printf("Entering capture thread (ALSA).\n");
	memset(&sndpcm, 0, sizeof(sndpcm));

	if (NULL != bat->capture.device) {
		err = snd_pcm_open(&sndpcm.handle, bat->capture.device,
				SND_PCM_STREAM_CAPTURE, 0);
		if (err < 0) {
			loge(E_OPENPCMC, "%s(%d)", snd_strerror(err), err);
			goto fail_exit;
		}
	} else {
		loge(E_OPENPCMC, "exit");
		goto fail_exit;
	}

	err = set_snd_pcm_params(bat, &sndpcm);
	if (err != 0)
		goto fail_exit;

	prepare_wav_info(&wav, bat);

	remove(bat->capture.file);
	fp = fopen(bat->capture.file, "w+");
	if (NULL == fp) {
		loge(E_OPENFILEC, "%s", bat->capture.file);
		goto fail_exit;
	}

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_cleanup_push(close_handle, sndpcm.handle);
	pthread_cleanup_push(destroy_mem, sndpcm.buffer);
	pthread_cleanup_push((void *)close_file, fp);

	if (fwrite(&wav.header, 1,
			sizeof(wav.header), fp)
			!= sizeof(wav.header)) {
		loge(E_WRITEFILE, "header %s(%d)", snd_strerror(err), err);
		goto fail_exit;
	}
	if (fwrite(&wav.format, 1,
			sizeof(wav.format), fp)
			!= sizeof(wav.format)) {
		loge(E_WRITEFILE, "format %s(%d)", snd_strerror(err), err);
		goto fail_exit;
	}
	if (fwrite(&wav.chunk, 1,
			sizeof(wav.chunk), fp)
			!= sizeof(wav.chunk)) {
		loge(E_WRITEFILE, "chunk %s(%d)", snd_strerror(err), err);
		goto fail_exit;
	}

	count = wav.chunk.length;
	printf("Recording ...\n");
	while (count > 0) {
		size = (count <= sndpcm.period_bytes) ?
				count : sndpcm.period_bytes;
		frames = size * 8
				/ sndpcm.frame_bits;
		offset = 0;
		while (frames > 0) {
			err = snd_pcm_readi(sndpcm.handle,
					sndpcm.buffer + offset, frames);
			if (err == -EAGAIN || (err >= 0 && err < frames)) {
				snd_pcm_wait(sndpcm.handle, 500);
			} else if (err == -EPIPE) {
				snd_pcm_prepare(sndpcm.handle);
				loge(E_READPCM S_OVERRUN, "%s(%d)",
						snd_strerror(err), err);
			} else if (err < 0) {
				loge(E_READPCM, "%s(%d)",
						snd_strerror(err), err);
				goto fail_exit;
			}

			if (err > 0) {
				frames -= err;
				offset += err * sndpcm.frame_bits / 8;
			}
		}

		if (fwrite(sndpcm.buffer, 1, size, fp) != size) {
			loge(E_WRITEFILE, "%s(%d)", snd_strerror(err), err);
			goto fail_exit;
		}
		count -= size;
		bat->periods_played++;

		if (bat->period_limit
				&& bat->periods_played >= bat->periods_total)
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

