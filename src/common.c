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

#include "common.h"
#include "wav_play_record.h"

int retval_play;
int retval_record;

void close_file(FILE *file)
{
	if (NULL != file)
		fclose(file);
}

void destroy_mem(void *block)
{
	free(block);
}

int read_wav_header(struct bat *bat, char *file, bool skip)
{
	struct wav_header riff_wave_header;
	struct wav_chunk_header chunk_header;
	struct chunk_fmt chunk_fmt;
	int more_chunks = 1;
	size_t ret;

	ret = fread(&riff_wave_header, sizeof(riff_wave_header), 1, bat->fp);
	if (ret != 1) {
		loge(E_READFILE, "header of %s:%zd", file, ret);
		return ret;
	}
	if ((riff_wave_header.magic != WAV_RIFF)
			|| (riff_wave_header.type != WAV_WAVE)) {
		loge(E_FILECONTENT, "%s is not a riff/wave file", file);
		return -1;
	}

	do {
		ret = fread(&chunk_header, sizeof(chunk_header), 1, bat->fp);
		if (ret != 1) {
			loge(E_READFILE, "chunk of %s:%zd", file, ret);
			return ret;
		}

		switch (chunk_header.type) {
		case WAV_FMT:
			ret = fread(&chunk_fmt, sizeof(chunk_fmt), 1, bat->fp);
			if (ret != 1) {
				loge(E_READFILE, "chunk fmt of %s:%zd",
						file, ret);
				return ret;
			}
			/* If the format header is larger, skip the rest */
			if (chunk_header.length > sizeof(chunk_fmt)) {
				ret = fseek(bat->fp,
					chunk_header.length - sizeof(chunk_fmt)
					, SEEK_CUR);
				if (ret == -1) {
					loge(E_SEEKFILE, "chunk fmt of %s:%zd",
							file, ret);
					return -1;
				}
			}
			if (skip == false) {
				bat->channels = chunk_fmt.channels;
				bat->rate = chunk_fmt.sample_rate;
				bat->sample_size = chunk_fmt.sample_length / 8;
				bat->frame_size = chunk_fmt.blocks_align;
			}

			break;
		case WAV_DATA:
			if (skip == false) {
				/*  The number of analysed captured frames is
					arbitrarily set to half of the number of
					frames of the wav file or the number of
					frames of the wav file when doing direct
					analysis (-l) */
				bat->frames =
						chunk_header.length
						/ bat->frame_size;
				if (!bat->local)
					bat->frames /= 2;
			}
			/* Stop looking for chunks */
			more_chunks = 0;
			break;
		default:
			/* Unknown chunk, skip bytes */
			ret = fseek(bat->fp, chunk_header.length, SEEK_CUR);
			if (ret == -1) {
				loge(E_SEEKFILE, "unknown chunk of %s:%zd",
						file, ret);
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
	wav->format.format = WAV_FORMAT_PCM;
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
