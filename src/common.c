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
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "wav_play_record.h"
#include "common.h"

int retval_play;
int retval_record;

void close_file(void *file)
{
	FILE *fp = file;
	if (NULL != fp)
		fclose(fp);
}

void destroy_mem(void *block)
{
	if (NULL != block)
		free(block);
}

int skip_wav_header(struct bat *bat)
{
	struct wav_header riff_wave_header;
	struct wav_chunk_header chunk_header;
	struct chunk_fmt chunk_fmt;
	int more_chunks = 1;
	size_t ret;

	ret = fread(&riff_wave_header, sizeof(riff_wave_header), 1, bat->fp);
	if (ret != 1) {
		fprintf(stderr, "Error reading header of file %s!\n",
				bat->capture_file);
		return -1;
	}
	if ((riff_wave_header.magic != WAV_RIFF)
			|| (riff_wave_header.type != WAV_WAVE)) {
		fprintf(stderr, "Error: '%s' is not a riff/wave file!\n",
				bat->capture_file);
		return -1;
	}

	do {
		ret = fread(&chunk_header, sizeof(chunk_header), 1, bat->fp);
		if (ret != 1) {
			fprintf(stderr, "Error reading chunk of file %s!\n",
					bat->capture_file);
			return -1;
		}

		switch (chunk_header.type) {
		case WAV_FMT:
			ret = fread(&chunk_fmt, sizeof(chunk_fmt), 1, bat->fp);
			if (ret != 1) {
				fprintf(stderr,
					"Error reading chunk fmt of file %s!\n"
					, bat->capture_file);
				return -1;
			}
			/* If the format header is larger, skip the rest */
			if (chunk_header.length > sizeof(chunk_fmt)) {
				ret = fseek(bat->fp,
					chunk_header.length - sizeof(chunk_fmt),
					SEEK_CUR);
				if (ret == -1) {
					fprintf(stderr,
						"Error skipping chunk fmt "
						"of file %s!\n",
						bat->capture_file);
					return -1;
				}
			}
			break;
		case WAV_DATA:
			/* Stop looking for chunks */
			more_chunks = 0;
			break;
		default:
			/* Unknown chunk, skip bytes */
			ret = fseek(bat->fp, chunk_header.length, SEEK_CUR);
			if (ret == -1) {
				fprintf(stderr,
					"Error skipping unknown chunk of file %s!\n",
					bat->capture_file);
				return -1;
			}
		}
	} while (more_chunks);

	return 0;
}

int read_wav_header(struct bat *bat)
{
	struct wav_header riff_wave_header;
	struct wav_chunk_header chunk_header;
	struct chunk_fmt chunk_fmt;
	int more_chunks = 1;
	size_t ret;

	ret = fread(&riff_wave_header, sizeof(riff_wave_header), 1, bat->fp);
	if (ret != 1) {
		fprintf(stderr, "Error reading header of file %s!\n",
				bat->playback_file);
		return -1;
	}
	if ((riff_wave_header.magic != WAV_RIFF)
			|| (riff_wave_header.type != WAV_WAVE)) {
		fprintf(stderr, "Error: '%s' is not a riff/wave file!\n",
				bat->playback_file);
		return -1;
	}

	do {
		ret = fread(&chunk_header, sizeof(chunk_header), 1, bat->fp);
		if (ret != 1) {
			fprintf(stderr, "Error reading chunk of file %s!\n",
					bat->playback_file);
			return -1;
		}

		switch (chunk_header.type) {
		case WAV_FMT:
			ret = fread(&chunk_fmt, sizeof(chunk_fmt), 1, bat->fp);
			if (ret != 1) {
				fprintf(stderr,
					"Error reading chunk fmt of file %s!\n",
					bat->playback_file);
				return -1;
			}
			/* If the format header is larger, skip the rest */
			if (chunk_header.length > sizeof(chunk_fmt)) {
				ret = fseek(bat->fp,
					chunk_header.length - sizeof(chunk_fmt)
					, SEEK_CUR);
				if (ret == -1) {
					fprintf(stderr,
						"Error skipping chunk fmt of file %s!\n",
						bat->playback_file);
					return -1;
				}
			}
			bat->channels = chunk_fmt.channels;
			bat->rate = chunk_fmt.sample_rate;
			bat->sample_size = chunk_fmt.sample_length / 8;
			bat->frame_size = chunk_fmt.blocks_align;

			break;
		case WAV_DATA:
			/* The number of analysed captured frames is
			 * arbitrarily set to half of the number of frames
			 * of the wav file or the number of frames of the
			 * wav file when doing direct analysis (-l) */
			bat->frames = chunk_header.length / bat->frame_size;
			if (!bat->local)
				bat->frames /= 2;

			/* Stop looking for chunks */
			more_chunks = 0;
			break;
		default:
			/* Unknown chunk, skip bytes */
			ret = fseek(bat->fp, chunk_header.length, SEEK_CUR);
			if (ret == -1) {
				fprintf(stderr,
					"Error skipping unknown chunk of file %s!\n",
					bat->playback_file);
				return -1;
			}
		}
	} while (more_chunks);

	return 0;
}

void prepare_wav_info(struct wav_container *wav, struct bat *bat)
{
	wav->header.magic = WAV_RIFF;
	wav->header.type = WAV_WAVE;
	wav->format.magic = WAV_FMT;
	wav->format.fmt_size = 16;
	wav->format.format = FORMAT_PCM;
	wav->format.channels = bat->channels;
	wav->format.sample_rate = bat->rate;
	wav->format.sample_length = bat->sample_size * 8;
	wav->format.blocks_align = bat->channels * bat->sample_size;
	wav->format.bytes_p_second = wav->format.blocks_align * bat->rate;
	/* Default set time length to 10 seconds */
	wav->chunk.length = bat->frames * bat->frame_size;
	wav->chunk.type = WAV_DATA;
	wav->header.length = (wav->chunk.length) + sizeof(wav->chunk)
			+ sizeof(wav->format) + sizeof(wav->header) - 8;

}

void generate_sine_wave(struct bat *bat, int length, void *buf, int max)
{
	static int i;
	int k, c;
	float sin_val[MAX_NUMBER_OF_CHANNELS];

	for (c = 0; c < bat->channels; c++)
		sin_val[c] = (float) bat->target_freq[c] / (float) bat->rate;
	for (k = 0; k < length; k++) {
		for (c = 0; c < bat->channels; c++) {
			float sinus_f = sin(i * 2.0 * M_PI * sin_val[c]) * max;
			int32_t sinus_f_i;
			switch (bat->sample_size) {
			case 1:
				*((int8_t *) buf) = (int8_t) (sinus_f);
				break;
			case 2:
				*((int16_t *) buf) = (int16_t) (sinus_f);
				break;
			case 3:
				/* FIXME is dependent of endianess */
				sinus_f_i = (int32_t)sinus_f;
				*((int8_t *) (buf+0)) = (int8_t) (sinus_f_i & 0xff);
				*((int8_t *) (buf+1)) = (int8_t) ((sinus_f_i>>8) & 0xff);
				*((int8_t *) (buf+2)) = (int8_t) ((sinus_f_i>>16) & 0xff);
				break;
			case 4:
				*((int32_t *) buf) = (int32_t) (sinus_f);
				break;
			}
			buf += bat->sample_size;
		}
		i += 1;
		if (i == bat->rate)
			i = 0;

	}

}
