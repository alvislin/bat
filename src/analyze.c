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
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <math.h>
#include <fftw3.h>

#include "common.h"

/*
 * Convert from sample size to double
 */
static int convert(struct bat *bat, struct analyze *a)
{
	void *s = a->buf;
	int i;

	for (i = 0; i < bat->frames; i++) {
		switch (bat->sample_size) {
		case 1:
			a->in[i] = ((int8_t *) s)[i];
			break;
		case 2:
			a->in[i] = ((int16_t *) s)[i];
			break;
		case 4:
			a->in[i] = ((int32_t *) s)[i];
			break;
		default:
			fprintf(stderr, "Unsupported sample size!\n");
			return -1;
			break;
		}
	}
	return 0;
}
/**
 *
 * @return 0 if peak detected at right frequency, 1 if peak detected somewhere else
 *         2 if DC detected
 */
int check_peak(int end, int peak, float hz, float mean, float p, int channel,
		struct analyze *a, int start, struct bat *bat)
{
	int ret;

	fprintf(stdout, "Detected peak at %2.2f Hz of %2.2f dB\n",
		(peak + 1) * hz,
		10.0 * log10(a->mag[peak] / mean));
	fprintf(stdout, " Total %3.1f dB from %2.2f to %2.2f Hz\n",
		10.0 * log10(p / mean), (start + 1) * hz,
		(end + 1) * hz);

	if ((peak + 1) * hz < 7.01) {
		fprintf(stdout,
				" WARNING: Found low peak %2.2f Hz, very close to DC\n",
		(peak + 1) * hz);
		ret = 2;
	} else if ((peak + 1) * hz < bat->target_freq[channel] - 1.0) {
		fprintf(stdout, " FAIL: Peak freq too low %2.2f Hz\n",
		(peak + 1) * hz);
		ret = 1;
	} else if ((peak + 1) * hz > bat->target_freq[channel] + 1.0) {
		fprintf(stdout, " FAIL: Peak freq too high %2.2f Hz\n",
			(peak + 1) * hz);
		ret = 1;
	} else {
		fprintf(stdout,
			" PASS: Peak detected at target frequency\n");
		ret = 0;
	}

	return ret;
}

/*
 * Search for main frequencies in fft results and compare it to target
 */
static int check(struct bat *bat, struct analyze *a, int channel)
{
	float hz = 1.0 / ((float) bat->frames / (float) bat->rate);
	float mean = 0.0, t, sigma = 0.0, p = 0.0;
	int i, start = -1, end = -1, peak = 0, signals = 0;
	int ret = 0, N = bat->frames / 2;

	/* calculate mean */
	for (i = 0; i < N; i++)
		mean += a->mag[i];
	mean /= (float) N;

	/* calculate standard deviation */
	for (i = 0; i < N; i++) {
		t = a->mag[i] - mean;
		t *= t;
		sigma += t;
	}
	sigma /= (float) N;
	sigma = sqrtf(sigma);

	/* clip any data less than k sigma + mean */
	for (i = 0; i < N; i++) {
		if (a->mag[i] > mean + bat->sigma_k * sigma) {

			/* find peak start points */
			if (start == -1) {
				start = i;
				peak = i;
				end = i;
				p = 0;
				signals++;
			} else {
				if (a->mag[i] > a->mag[peak])
					peak = i;
				end = i;
			}
			p += a->mag[i];
		} else if (start != -1) {
			/* Check if peak is as expected */
			ret |= check_peak(end, peak, hz, mean, p, channel, a,
					start, bat);
			end = start = -1;
			if (signals == MAX_NB_OF_PEAK)
				break;
		}
	}
	if (signals == 0)
		ret = -EIO;		/* No peak detected */
	else if ((ret == 2) && (signals == 1))
		ret = -EIO;		/* Only DC detected */
	else if ((ret == 1) || (ret == 3))
		ret = -EIO;		/* Bad peak detected */
	else
		ret = 0;		/* Correct peak detected */


	fprintf(stdout, "Detected at least %d signal(s) in total\n", signals);

	return ret;
}

static void calc_magnitude(struct bat *bat, struct analyze *a, int N)
{
	double r2, i2;
	int i;

	for (i = 1; i < N / 2; i++) {
		r2 = a->out[i] * a->out[i];
		i2 = a->out[N - i - 1] * a->out[N - i - 1];

		a->mag[i] = sqrt(r2 + i2);
	}
	a->mag[0] = 0.0;
}

static int find_and_check_harmonics(struct bat *bat, struct analyze *a,
		int channel)
{
	fftw_plan p;
	int ret = -ENOMEM, N = bat->frames;

	/* Allocate FFT buffers */
	a->in = (double *) fftw_malloc(sizeof(double) * bat->frames);
	if (a->in == NULL)
		goto out1;

	a->out = (double *) fftw_malloc(sizeof(double) * bat->frames);
	if (a->out == NULL)
		goto out2;

	a->mag = (double *) fftw_malloc(sizeof(double) * bat->frames);
	if (a->mag == NULL)
		goto out3;

	/* create FFT plan */
	p = fftw_plan_r2r_1d(N, a->in, a->out, FFTW_R2HC,
			FFTW_MEASURE | FFTW_PRESERVE_INPUT);
	if (p == NULL)
		goto out4;

	/* convert source PCM to doubles */
	ret = convert(bat, a);
	if (ret != 0)
		goto out4;

	/* run FFT */
	fftw_execute(p);

	/* FFT out is real and imaginary numbers - calc magnitude for each */
	calc_magnitude(bat, a, N);

	/* check data */
	ret = check(bat, a, channel);

	fftw_destroy_plan(p);

out4:
	fftw_free(a->mag);
out3:
	fftw_free(a->out);
out2:
	fftw_free(a->in);
out1:
	return ret;
}

/*
 * Convert interleaved samples from channels in samples from a single channel
 */
static int reorder_data(struct bat *bat)
{
	char *p, *new_bat_buf;
	int ch, i, j;

	if (bat->channels == 1)
		return 0; /* No need for reordering */

	p = malloc(bat->frames * bat->frame_size);
	new_bat_buf = p;
	if (p == NULL)
		return -ENOMEM;

	for (ch = 0; ch < bat->channels; ch++) {
		for (j = 0; j < bat->frames; j += 1) {
			for (i = 0; i < bat->sample_size; i++) {
				*p++ = ((char *) (bat->buf))[j * bat->frame_size
						+ ch * bat->sample_size + i];
			}
		}
	}

	free(bat->buf);
	bat->buf = new_bat_buf;

	return 0;
}

int analyze_capture(struct bat *bat)
{
	int ret = -EINVAL;
	size_t items;
	int c;

	fprintf(stdout,
		"\nBAT analysed signal is %d frames at %d Hz, %d channels, "
		"%d bytes per sample\n",
		bat->frames, bat->rate, bat->channels, bat->sample_size);

	bat->fp = fopen(bat->capture_file, "rb");
	if (bat->fp == NULL) {
		fprintf(stderr, "failed to open %s!\n", bat->capture_file);
		return -ENOENT;
	}

	bat->buf = malloc(bat->frames * bat->frame_size);
	if (bat->buf == NULL)
		return -ENOMEM;

	/* Skip header */
	ret = skip_wav_header(bat);
	if (ret != 0)
		return ret;

	items = fread(bat->buf, bat->frame_size, bat->frames, bat->fp);
	if (items != bat->frames) {
		free(bat->buf);
		return -EIO;
	}

	ret = reorder_data(bat);
	if (ret != 0)
		return ret;

	for (c = 0; c < bat->channels; c++) {
		struct analyze a;

		printf("\nChannel %i - Checking for target frequency %2.2f Hz\n",
			c + 1, bat->target_freq[c]);
		a.buf = bat->buf +
			(c * bat->frames * bat->frame_size / bat->channels);
		ret = find_and_check_harmonics(bat, &a, c);
	}

	return ret;
}
