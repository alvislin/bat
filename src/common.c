/*
 * common.c
 *
 *  Created on: 6 Oct 2014
 *      Author: gautier
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>

#include "wav_play_record.h"
#include "common.h"


int retval_play = 0;
int retval_record = 0;

void close_file(void *file)
{
	FILE *fp = file;
	if (NULL != fp) {
		fclose(fp);
	}
}

void destroy_mem(void *block)
{
	if (NULL != block) {
		free(block);
	}
}

int skip_wav_header(struct bat *bat)
{
	wavHeader_t riff_wave_header;
	wavChunkHeader_t chunk_header;
	chunkFmt_t chunk_fmt;
	int more_chunks = 1;
	size_t ret;

	ret = fread(&riff_wave_header, sizeof(riff_wave_header), 1, bat->fp);
	if (ret != 1) {
		fprintf(stderr, "Error reading header of file %s!\n", bat->capture_file);
		return -1;
	}
	if ((riff_wave_header.magic != WAV_RIFF) || (riff_wave_header.type != WAV_WAVE)) {
		fprintf(stderr, "Error: '%s' is not a riff/wave file!\n", bat->capture_file);
		return -1;
	}

	do {
		ret = fread(&chunk_header, sizeof(chunk_header), 1, bat->fp);
		if (ret != 1) {
			fprintf(stderr, "Error reading chunk of file %s!\n", bat->capture_file);
			return -1;
		}

		switch (chunk_header.type) {
		case WAV_FMT:
			ret = fread(&chunk_fmt, sizeof(chunk_fmt), 1, bat->fp);
			if (ret != 1) {
				fprintf(stderr, "Error reading chunk fmt of file %s!\n", bat->capture_file);
				return -1;
			}
			/* If the format header is larger, skip the rest */
			if (chunk_header.length > sizeof(chunk_fmt)) {
				ret = fseek(bat->fp, chunk_header.length - sizeof(chunk_fmt), SEEK_CUR);
				if (ret == -1) {
					fprintf(stderr, "Error skipping chunk fmt of file %s!\n", bat->capture_file);
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
				fprintf(stderr, "Error skipping unknown chunk of file %s!\n", bat->capture_file);
				return -1;
			}
		}
	} while (more_chunks);

	return 0;
}


int read_wav_header(struct bat *bat)
{
	wavHeader_t riff_wave_header;
	wavChunkHeader_t chunk_header;
	chunkFmt_t chunk_fmt;
	int more_chunks = 1;
	size_t ret;

	ret = fread(&riff_wave_header, sizeof(riff_wave_header), 1, bat->fp);
	if (ret != 1) {
		fprintf(stderr, "Error reading header of file %s!\n", bat->playback_file);
		return -1;
	}
	if ((riff_wave_header.magic != WAV_RIFF) || (riff_wave_header.type != WAV_WAVE)) {
		fprintf(stderr, "Error: '%s' is not a riff/wave file!\n", bat->playback_file);
		return -1;
	}

	do {
		ret = fread(&chunk_header, sizeof(chunk_header), 1, bat->fp);
		if (ret != 1) {
			fprintf(stderr, "Error reading chunk of file %s!\n", bat->playback_file);
			return -1;
		}

		switch (chunk_header.type) {
		case WAV_FMT:
			ret = fread(&chunk_fmt, sizeof(chunk_fmt), 1, bat->fp);
			if (ret != 1) {
				fprintf(stderr, "Error reading chunk fmt of file %s!\n", bat->playback_file);
				return -1;
			}
			/* If the format header is larger, skip the rest */
			if (chunk_header.length > sizeof(chunk_fmt)) {
				ret = fseek(bat->fp, chunk_header.length - sizeof(chunk_fmt), SEEK_CUR);
				if (ret == -1) {
					fprintf(stderr, "Error skipping chunk fmt of file %s!\n", bat->playback_file);
					return -1;
				}
			}
			bat->channels = chunk_fmt.channels;
			bat->rate = chunk_fmt.sample_rate;
			bat->sample_size = chunk_fmt.sample_length/8;
			bat->frame_size = chunk_fmt.blocks_align;

			break;
		case WAV_DATA:
			bat->frames = chunk_header.length / bat->frame_size /2 ; /* FIXME The number of analysed captured frames is arbitrarily set to half of the number of frames */
			/* Stop looking for chunks */
			more_chunks = 0;
			break;
		default:
			/* Unknown chunk, skip bytes */
			ret = fseek(bat->fp, chunk_header.length, SEEK_CUR);
			if (ret == -1) {
				fprintf(stderr, "Error skipping unknown chunk of file %s!\n", bat->playback_file);
				return -1;
			}
		}
	} while (more_chunks);


	return 0;
}


void prepare_wav_info(WAVContainer_t *wav, struct bat *bat)
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
	wav->chunk.length = 10 * wav->format.bytes_p_second;						/* FIXME could be set to number of frames ? given in cmd line */
	wav->chunk.type = WAV_DATA;
	wav->header.length = (wav->chunk.length) + sizeof(wav->chunk)
			+ sizeof(wav->format) + sizeof(wav->header) - 8;

}
