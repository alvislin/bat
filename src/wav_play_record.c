#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <pthread.h>

#include <alsa/asoundlib.h>

#include "common.h"
#include "wav_play_record.h"

struct SNDPCMContainer {
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
	if (NULL != hd) {
		snd_pcm_close(hd);
	}
}

static int setSNDPCMParams(struct bat *bat, struct SNDPCMContainer *sndpcm)
{
	snd_pcm_format_t format;
	snd_pcm_hw_params_t *params;
	unsigned int buffer_time = 0;
	unsigned int period_time = 0;

	/* Allocate a hardware parameters object. */
	snd_pcm_hw_params_alloca(&params);

	/* Fill it in with default values. */
	snd_pcm_hw_params_any(sndpcm->handle, params);

	/* Set access mode */
	snd_pcm_hw_params_set_access(sndpcm->handle, params,
		SND_PCM_ACCESS_RW_INTERLEAVED);

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
		fprintf(stderr, "Not supported format!\n");
		goto fail_exit;
	}
	snd_pcm_hw_params_set_format(sndpcm->handle, params, format);

	/* Set channels */
	snd_pcm_hw_params_set_channels(sndpcm->handle, params, bat->channels);

	/* Set sampling rate */
	snd_pcm_hw_params_set_rate_near(sndpcm->handle, params, &bat->rate, 0);

	if (snd_pcm_hw_params_get_buffer_time_max(params, &buffer_time, 0) < 0) {
		fprintf(stderr, "Error snd_pcm_hw_params_get_buffer_time_max!\n");
		goto fail_exit;
	}

	if (buffer_time > 500000)
		buffer_time = 500000;
	period_time = buffer_time / 8; /* Was 4, changed to 8 to remove reduce capture overrun */

	/* Set buffer time and period time */
	snd_pcm_hw_params_set_buffer_time_near(sndpcm->handle, params, &buffer_time, 0);
	snd_pcm_hw_params_set_period_time_near(sndpcm->handle, params, &period_time, 0);

	/* Write the parameters to the driver */
	if (snd_pcm_hw_params(sndpcm->handle, params) < 0) {
		fprintf(stderr, "Unable to set and pcm hw parameters!\n");
		goto fail_exit;
	}

	snd_pcm_hw_params_get_period_size(params, &sndpcm->period_size, 0);
	snd_pcm_hw_params_get_buffer_size(params, &sndpcm->buffer_size);
	if (sndpcm->period_size == sndpcm->buffer_size) {
		fprintf(stderr, "Can't use period equal to buffer size (%lu == %lu)!\n",
			sndpcm->period_size, sndpcm->buffer_size);
		goto fail_exit;
	}

	sndpcm->sample_bits = snd_pcm_format_physical_width(format);
	sndpcm->frame_bits = sndpcm->sample_bits * bat->channels;

	/* Calculate the period bytes */
	sndpcm->period_bytes = sndpcm->period_size * sndpcm->frame_bits / 8;
	sndpcm->buffer = (char *) malloc(sndpcm->period_bytes);
	if (sndpcm->buffer == NULL) {
		fprintf(stderr, "Memory buffer of snd pcm allocated fail!\n");
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
static int generate_input_data(struct SNDPCMContainer sndpcm, int count, struct bat *bat)
{
	int err;
	int load = 0;
	int i = 0;
	int k, l;

	if (bat->playback_file != NULL) {
		/* From input file */
		load = 0;

		while (1) {
			err = fread(sndpcm.buffer + load, 1, count - load, bat->fp);
			if (0 == err) {
				if (feof(bat->fp)) {
					fprintf(stdout, "End of playing.\n");
					return 1;
				}
			}
			if (err < count - load) {
				if (ferror(bat->fp)) {
					fprintf(stderr, "Error when reading input file!\n");
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

		switch (bat->sample_size) {
		case 1:
			buf = (int8_t *) sndpcm.buffer;
			max = INT8_MAX;
			break;
		case 2:
			buf = (int16_t *) sndpcm.buffer;
			max = INT16_MAX;
			break;
		case 4:
			buf = (int32_t *) sndpcm.buffer;
			max = INT32_MAX;
			break;
		default:
			fprintf(stderr, "Format not supported!\n");
			return -1;
		}

		float sin_val = (float) bat->target_freq / (float) bat->rate;
		for (k = 0; k < count * 8 / sndpcm.frame_bits; k++) {
			float sinus_f = sin(i++ * 2.0 * M_PI * sin_val) * max;
			if (i == bat->rate)
				i = 0;
			for (l = 0; l < bat->channels; l++) {
				switch (bat->sample_size) {
				case 1:
					*((int8_t *) buf) = (int8_t) (sinus_f);
					break;
				case 2:
					*((int16_t *) buf) = (int16_t) (sinus_f);
					break;
				case 4:
					*((int32_t *) buf) = (int32_t) (sinus_f);
					break;
				}
				buf += bat->sample_size;
			}
		}
		load += (count * 8 / sndpcm.frame_bits);
	}

	bat->periods_played++; 
	return 0;
}
/**
 * Play
 */
void *playback_alsa(void *bat_param)
{
	int err = 0;
	struct SNDPCMContainer sndpcm;
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

	err = setSNDPCMParams(bat, &sndpcm);
	if (err != 0) {
		goto fail_exit;
	}

	if (bat->playback_file == NULL) {
		fprintf(stdout, "Playing generated audio sine wave");
		bat->sinus_duration == 0 ? fprintf(stdout, " endlessly\n") : fprintf(stdout, "\n");
	} else {
		fprintf(stdout, "Playing input audio file: %s\n", bat->playback_file);
	}

	count = sndpcm.period_bytes;
	while (1) {
		offset = 0;
		size = count * 8 / sndpcm.frame_bits;

		ret = generate_input_data(sndpcm, count, bat);
		if (ret < 0)
			goto fail_exit;
		else if (ret > 0)
			break;

		if (bat->period_limit && bat->periods_played >= bat->periods_total)
			break;

		while (size > 0) {
			err = snd_pcm_writei(sndpcm.handle, sndpcm.buffer + offset, size);
			if (err == -EAGAIN || (err >= 0 && err < size)) {
				snd_pcm_wait(sndpcm.handle, 500);
			} else if (err == -EPIPE) {
				fprintf(stderr, "Playback: Underrun occurred!\n");
				snd_pcm_prepare(sndpcm.handle);
			} else if (err < 0) {
				fprintf(stderr, "Write to pcm device fail!\n");
				goto fail_exit;
			}

			if (err > 0) {
				size -= err;
				offset += err * sndpcm.frame_bits / 8;
			}
		}
	}

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
	struct SNDPCMContainer sndpcm;
	WAVContainer_t wav;
	int size, offset, count, frames;
	struct bat *bat = (struct bat *) bat_param;

	if (bat->sinus_duration == 0 && bat->playback_file == NULL)
		return 0; /* No capture when in mode: play sine wave endlessly */

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	fprintf(stdout, "Enter capture thread (ALSA).\n");
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

	err = setSNDPCMParams(bat, &sndpcm);
	if (err != 0) {
		goto fail_exit;
	}

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

	if (fwrite(&wav.header, 1, sizeof(wav.header), fp) != sizeof(wav.header)
		|| fwrite(&wav.format, 1, sizeof(wav.format), fp) != sizeof(wav.format)
		|| fwrite(&wav.chunk, 1, sizeof(wav.chunk), fp) != sizeof(wav.chunk)) {
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

		if (bat->period_limit && bat->periods_played >= bat->periods_total)
			break;
	}

	// Normally we will never reach this part of code (before fail_exit) as
	//  this thread will be cancelled by end of play thread.
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

