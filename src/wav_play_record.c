#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "common.h"
#include "wav_play_record.h"

#define COMPOSE(a,b,c,d) ((a) | ((b)<<8) | ((c)<<16) | ((d)<<24))
#define WAV_RIFF        COMPOSE('R','I','F','F')
#define WAV_WAVE        COMPOSE('W','A','V','E')
#define WAV_FMT         COMPOSE('f','m','t',' ')
#define WAV_DATA        COMPOSE('d','a','t','a')

static int retval_play = 0;
static int retval_record = 0;

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

typedef struct wavHeader {
	unsigned int magic; /* 'RIFF' */
	unsigned int length; /* filelen */
	unsigned int type; /* 'WAVE' */
} wavHeader_t;

typedef struct wavFmt {
	unsigned int magic; /* 'FMT '*/
	unsigned int fmt_size; /* 16 or 18 */
	unsigned short format; /* see WAV_FMT_* */
	unsigned short channels;
	unsigned int sample_rate; /* frequence of sample */
	unsigned int bytes_p_second;
	unsigned short blocks_align; /* samplesize; 1 or 2 bytes */
	unsigned short sample_length; /* 8, 12 or 16 bit */
} wavFmt_t;

typedef struct wavChunkHeader {
	unsigned int type; /* 'data' */
	unsigned int length; /* samplecount */
} wavChunkHeader_t;

typedef struct WAVContainer {
	wavHeader_t header;
	wavFmt_t format;
	wavChunkHeader_t chunk;
} WAVContainer_t;

int setSNDPCMParams(struct bat *pa_bat, struct SNDPCMContainer *sndpcm)
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
	switch (pa_bat->frame_size) {
	case 1:
		format = SND_PCM_FORMAT_S8;
		break;
	case 2:
		format = SND_PCM_FORMAT_S16_LE;
		break;
	case 3:
		format = SND_PCM_FORMAT_S24_LE;
		break;
	case 4:
		format = SND_PCM_FORMAT_S32_LE;
		break;
	default:
		fprintf(stderr, "Not support format.\n");
		goto fail_exit;
	}
	snd_pcm_hw_params_set_format(sndpcm->handle, params, format);
	/* Set channels */
	snd_pcm_hw_params_set_channels(sndpcm->handle, params, pa_bat->channels);
	/* Set sampling rate */
	snd_pcm_hw_params_set_rate_near(sndpcm->handle, params, &pa_bat->rate, 0);

	if (snd_pcm_hw_params_get_buffer_time_max(params, &buffer_time, 0) < 0) {
		fprintf(stderr, "Error snd_pcm_hw_params_get_buffer_time_max\n");
		goto fail_exit;
	}

	if (buffer_time > 500000)
		buffer_time = 500000;
	period_time = buffer_time / 4;

	/* Set buffer time and period time */
	snd_pcm_hw_params_set_buffer_time_near(sndpcm->handle, params, &buffer_time,
			0);
	snd_pcm_hw_params_set_period_time_near(sndpcm->handle, params, &period_time,
			0);

	/* Write the parameters to the driver */
	if (snd_pcm_hw_params(sndpcm->handle, params) < 0) {
		fprintf(stderr, "Uunable to set and pcm hw parameters.\n");
		goto fail_exit;
	}

	snd_pcm_hw_params_get_period_size(params, &sndpcm->period_size, 0);
	snd_pcm_hw_params_get_buffer_size(params, &sndpcm->buffer_size);
	if (sndpcm->period_size == sndpcm->buffer_size) {
		fprintf(stderr,
				("Can't use period equal to buffer size (%lu == %lu)\n"),
				sndpcm->period_size, sndpcm->buffer_size);
		goto fail_exit;
	}

	sndpcm->sample_bits = snd_pcm_format_physical_width(format);
	sndpcm->frame_bits = sndpcm->sample_bits * pa_bat->channels;

	/* Calculate the period bytes */
	sndpcm->period_bytes = sndpcm->period_size * sndpcm->frame_bits / 8;
	sndpcm->buffer = (char *) malloc(sndpcm->period_bytes);
	if (sndpcm->buffer == NULL) {
		fprintf(stderr, "Memory buffer of snd pcm allocated fail.\n");
		goto fail_exit;
	}

	return 0;
fail_exit:
	return -1;
}

void *play(void *bat_param)
{
	int err = 0;
	FILE *fp = NULL;
	struct SNDPCMContainer sndpcm;
	int size, offset, count, load;
	struct bat *pa_bat = (struct bat *) bat_param;

	fprintf(stdout, "Enter play thread!\n");
	memset(&sndpcm, 0, sizeof(sndpcm));
	if (NULL != pa_bat->device) {
		err = snd_pcm_open(&sndpcm.handle, pa_bat->device,
				SND_PCM_STREAM_PLAYBACK, 0);
		if (err < 0) {
			fprintf(stderr, "Unable to open pcm device: %s\n",
					snd_strerror(err));
			goto fail_exit;
		}
	} else {
		fprintf(stderr, "No audio device to open\n");
		goto fail_exit;
	}

	err = setSNDPCMParams(pa_bat, &sndpcm);
	if (err != 0) {
		goto fail_exit;
	}

	if (NULL != pa_bat->input_file) {
		fp = fopen(pa_bat->input_file, "rb+");
		if (NULL == fp) {
			fprintf(stderr, "Cannot access %s: No such file\n",
					pa_bat->input_file);
			goto fail_exit;
		}
	} else {
		fprintf(stderr, "No input file to open\n");
		goto fail_exit;
	}

	count = sndpcm.period_bytes;
	load = 0;
	fprintf(stdout, "Playing input audio file: %s\n", pa_bat->input_file);
	while (1) {
		err = fread(sndpcm.buffer + load, 1, count - load, fp);
		if (0 == err) {
			if (feof(fp)) {
				fprintf(stdout, "End of playing.\n");
				break;
			}
		}
		if (err < count - load) {
			if (ferror(fp)) {
				fprintf(stderr, "Error when reading %s\n", pa_bat->input_file);
				goto fail_exit;
			}
			load += err;
			continue;
		}

		offset = 0;
		size = count * 8 / sndpcm.frame_bits;
		while (size > 0) {
			err = snd_pcm_writei(sndpcm.handle, sndpcm.buffer + offset, size);
			if (err == -EAGAIN || (err >= 0 && err < size)) {
				snd_pcm_wait(sndpcm.handle, 500);
			} else if (err == -EPIPE) {
				fprintf(stderr, "Underrun occurred\n");
				snd_pcm_prepare(sndpcm.handle);
			} else if (err < 0) {
				fprintf(stderr, "Write to pcm device fail\n");
				goto fail_exit;
			}

			if (err > 0) {
				size -= err;
				offset += err * sndpcm.frame_bits / 8;
			}
		}
		load = 0;
	}

	snd_pcm_drain(sndpcm.handle);
	fclose(fp);
	free(sndpcm.buffer);
	snd_pcm_close(sndpcm.handle);
//	fprintf(stdout, "Exit thread play!\n");
	retval_play = 0;
	pthread_exit(&retval_play);

fail_exit:
	if (fp)
		fclose(fp);
	if (sndpcm.buffer)
		free(sndpcm.buffer);
	if (sndpcm.handle)
		snd_pcm_close(sndpcm.handle);
	retval_play = 1;
	pthread_exit(&retval_play);
}

static int prepare_wav_info(WAVContainer_t *wav, struct bat *pa_bat)
{
	wav->header.magic = WAV_RIFF;
	wav->header.type = WAV_WAVE;
	wav->format.magic = WAV_FMT;
	wav->format.fmt_size = 16;
	wav->format.format = 0x0001;
	wav->chunk.type = WAV_DATA;
	wav->format.channels = pa_bat->channels;
	wav->format.sample_rate = pa_bat->rate;
	wav->format.sample_length = pa_bat->frame_size * 8;
	wav->format.blocks_align = pa_bat->channels * wav->format.sample_length / 8;
	wav->format.bytes_p_second = wav->format.blocks_align * pa_bat->rate;
	/* Defauly set time length to 10 seconds */
	wav->chunk.length = 10 * wav->format.bytes_p_second;
	wav->header.length = (wav->chunk.length) + sizeof(wav->chunk)
			+ sizeof(wav->format) + sizeof(wav->header) - 8;

	return 0;
}

static void close_file(void *file)
{
	FILE *fp = file;
	if (NULL != fp) {
		fprintf(stdout, "Close audio file stream.\n");
		fclose(fp);
	}
}

static void destroy_mem(void *block)
{
	if (NULL != block) {
		fprintf(stdout, "Free buffer memory.\n");
		free(block);
	}
}

static void close_handle(void *handle)
{
	snd_pcm_t *hd = handle;
	if (NULL != hd) {
		fprintf(stdout, "Close snd pcm handle.\n");
		snd_pcm_close(hd);
	}
}

void *record(void *bat_param)
{
	int err = 0;
	char *test_file = TEMP_RECORD_FILE_NAME;
	FILE *fp = NULL;
	struct SNDPCMContainer sndpcm;
	WAVContainer_t wav;
	int size, offset, count, frames;
	struct bat *pa_bat = (struct bat *) bat_param;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	fprintf(stdout, "Enter record thread!\n");
	memset(&sndpcm, 0, sizeof(sndpcm));
	if (NULL != pa_bat->device) {
		err = snd_pcm_open(&sndpcm.handle, pa_bat->device,
				SND_PCM_STREAM_CAPTURE, 0);
		if (err < 0) {
			fprintf(stderr, "Unable to open pcm device: %s\n",
					snd_strerror(err));
			goto fail_exit;
		}
	} else {
		fprintf(stderr, "No audio device to open\n");
		goto fail_exit;
	}

	err = setSNDPCMParams(pa_bat, &sndpcm);
	if (err != 0) {
		goto fail_exit;
	}

	prepare_wav_info(&wav, pa_bat);

	remove(test_file);
	fp = fopen(test_file, "w+");
	if (NULL == fp) {
		fprintf(stderr, "Cannot create file: %s\n", test_file);
		goto fail_exit;
	}

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_cleanup_push(close_handle, sndpcm.handle);
	pthread_cleanup_push(destroy_mem, sndpcm.buffer);
	pthread_cleanup_push(close_file, fp);

	if (fwrite(&wav.header, 1, sizeof(wav.header), fp)
			!= sizeof(wav.header)
			|| fwrite(&wav.format, 1, sizeof(wav.format), fp)
					!= sizeof(wav.format)
			|| fwrite(&wav.chunk, 1, sizeof(wav.chunk), fp)
					!= sizeof(wav.chunk)) {
		fprintf(stderr, "Error write wav file header\n");
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
				fprintf(stderr, "Underrun occurred\n");
			} else if (err < 0) {
				fprintf(stderr, "Read from pcm device fail\n");
				goto fail_exit;
			}

			if (err > 0) {
				frames -= err;
				offset += err * sndpcm.frame_bits / 8;
			}
		}

		if (fwrite(sndpcm.buffer, 1, size, fp) != size) {
			fprintf(stderr, "Write to wav file fail\n");
			goto fail_exit;
		}
		count -= size;
	}

	pthread_cleanup_pop(0);
	pthread_cleanup_pop(0);
	pthread_cleanup_pop(0);

	snd_pcm_drain(sndpcm.handle);
	fclose(fp);
	free(sndpcm.buffer);
	snd_pcm_close(sndpcm.handle);
//	fprintf(stdout, "exit thread record!\n");
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

