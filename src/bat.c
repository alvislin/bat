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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fftw3.h>
#include <math.h>
#include "common.h"
#include "wav_play_record.h"

static void usage(char *argv[])
{
	fprintf(stdout,
			"Usage:%s [-D pcm device] [-Dp pcm playback device] [-Dc pcm capture device] [-f input file] [-n frames to capture] [-s frame size] [-k sigma k] [-F Target Freq] [-l internal loop, bypass alsa]\n",
			argv[0]);
	fprintf(stdout, "Usage:%s [-h]\n", argv[0]);
	exit(0);
}

static void calc_magnitude(struct bat *bat, int N)
{
	double r2, i2;
	int i;

	for (i = 1; i < N / 2; i++) {
		r2 = bat->out[i] * bat->out[i];
		i2 = bat->out[N - i - 1] * bat->out[N - i - 1];

		bat->mag[i] = sqrt(r2 + i2);
	}
	bat->mag[0] = 0.0;
}

static double hanning (int i, int nn)
{
  return ( 0.5 * (1.0 - cos (2.0*M_PI*(double)i/(double)(nn-1))) );
}

/* hard coded atm to only accept short to double conversion */
static void convert(struct bat *bat)
{
	short *s = bat->buf;
	int i;

	for (i = 0; i < bat->frames; i++)
		bat->in[i] = s[i] * hanning(i,bat->frames);
}

/* hard coded for rate of 44100 Hz atm */
static int check(struct bat *bat)
{
	float Hz = 2.0 / ((float) bat->frames / (float) bat->rate);
	float mean = 0.0, t, sigma = 0.0, p = 0.0;
	int i, start = -1, end = -1, peak = 0, signals = 0;
	int ret = 0, N = bat->frames / 2;

	/* calculate mean */
	for (i = 0; i < N; i++)
		mean += bat->mag[i];
	mean /= (float) N;

	/* calculate standard deviation */
	for (i = 0; i < N; i++) {
		t = bat->mag[i] - mean;
		t *= t;
		sigma += t;
	}
	sigma /= (float) N;
	sigma = sqrtf(sigma);

	/* clip any data less than k sigma + mean */
	for (i = 0; i < N; i++) {
		if (bat->mag[i] > mean + bat->sigma_k * sigma) {

			/* find peak start points */
			if (start == -1) {
				start = i;
				peak = i;
				end = i;
				p = 0;
				signals++;
			} else {
				if (bat->mag[i] > bat->mag[peak])
					peak = i;
				end = i;
			}

			//fprintf(stdout, "%5.1f Hz %2.2f dB\n",
			//	(i + 1) * Hz, 10.0 * log10(bat->mag[i] / mean));
			p += bat->mag[i];
		} else {

			/* find peak end point */
			if (end != -1) {
				fprintf(stdout, "Detected peak at %2.2f Hz of %2.2f dB\n",
						(peak + 1) * Hz, 10.0 * log10(bat->mag[peak] / mean));
				fprintf(stdout, " Total %3.1f dB from %2.2f to %2.2f Hz\n",
						10.0 * log10(p / mean), (start + 1) * Hz,
						(end + 1) * Hz);
				if ((peak + 1) * Hz > 1.99 && (peak + 1) * Hz < 7.01) {
					fprintf(stdout,
							"Warning: there is too low peak %2.2f Hz, very close to DC\n",
							(peak + 1) * Hz);
				} else if ((peak + 1) * Hz < bat->target_freq - 1.0) {
					fprintf(stdout, " FAIL: Peak freq too low %2.2f Hz\n",
							(peak + 1) * Hz);
					ret = -EINVAL;
				} else if ((peak + 1) * Hz > bat->target_freq + 1.0) {
					fprintf(stdout, " FAIL: Peak freq too high %2.2f Hz\n",
							(peak + 1) * Hz);
					ret = -EINVAL;
				} else {
					fprintf(stdout, " PASS: Peak detected at target frequency\n");
					ret = 0;
				}

				end = -1;
				start = -1;
			}
		}
	}
	fprintf(stdout, "Detected %d signal(s) in total\n", signals);
	fprintf(stdout, "\nReturn value is %d\n", ret);

	return ret;
}

static int fft(struct bat *bat)
{
	fftw_plan p;
	int ret = -ENOMEM, N = bat->frames / 2;

	/* Allocate FFT buffers */
	bat->in = (double*) fftw_malloc(sizeof(double) * bat->frames);
	if (bat->in == NULL)
		goto out;

	bat->out = (double*) fftw_malloc(sizeof(double) * bat->frames);
	if (bat->out == NULL)
		goto out;

	bat->mag = (double*) fftw_malloc(sizeof(double) * bat->frames);
	if (bat->mag == NULL)
		goto out;

	/* create FFT plan */
	p = fftw_plan_r2r_1d(N, bat->in, bat->out, FFTW_R2HC,
			FFTW_MEASURE | FFTW_PRESERVE_INPUT);
	if (p == NULL)
		goto out;

	/* convert source PCM to doubles */
	convert(bat);

	/* run FFT */
	fftw_execute(p);

	/* FFT out is real and imaginary numbers - calc magnitude for each */
	calc_magnitude(bat, N);

	/* check data */
	ret = check(bat);

	fftw_destroy_plan(p);

out:
	fftw_free(bat->in);
	fftw_free(bat->out);
	fftw_free(bat->mag);

	return ret;
}

static int file_load(struct bat *bat, char *file)
{
	FILE *in_file;
	int ret = -EINVAL;
	size_t items;

	in_file = fopen(file, "rb");
	if (in_file == NULL) {
		fprintf(stderr, "failed to open %s\n", file);
		return -ENOENT;
	}

	bat->buf = malloc(bat->frames * bat->frame_size);
	if (bat->buf == NULL) {
		fclose(in_file);
		return -ENOMEM;
	}

	items = fread(bat->buf, bat->frame_size, bat->frames, in_file);
	if (items != bat->frames) {
		ret = -EIO;
		goto out;
	}

	ret = fft(bat);

out:
	fclose(in_file);
	free(bat->buf);
	return ret;
}

static void play_and_record_alsa(struct bat* bat)
{
	int ret;
	pthread_t record_id, play_id;
	int* thread_ret;

	ret = pthread_create(&play_id, NULL, play, (void *) bat);

	if (0 != ret) {
		fprintf(stdout, "Create play thread error!\n");
		pthread_cancel(record_id);
		exit(1);
	}

	sleep(1);	/* Let time for playing something before recording */

	ret = pthread_create(&record_id, NULL, record, (void *) bat);
	if (0 != ret) {
		fprintf(stdout, "Create record thread error!\n");
		exit(1);
	}

	pthread_join(play_id, (void**) &thread_ret);
	fprintf(stdout, "Play thread exit!\n");
	pthread_cancel(record_id);
	if (0 != *thread_ret) {
		fprintf(stdout, "Return value of Play thread is %x!\n", *thread_ret);
		fprintf(stdout, "Sound played fail!\n");
		exit(1);
	}
	pthread_join(record_id, NULL);
	fprintf(stdout, "Record thread exit!\n");
}

int main(int argc, char *argv[])
{
	struct bat bat;
	int ret, opt;
	char *file;

	memset(&bat, 0, sizeof(bat));

	/* Set default values */
	bat.rate = 44100;
	bat.channels = 1;
	bat.frame_size = 2;
	bat.frames = bat.rate;
	bat.target_freq = 997.0;
	bat.sigma_k = 3.0;
	bat.playback_device = NULL;
	bat.capture_device = NULL;
	bat.buf = NULL;
	bat.local = false;

	/* Parse options */
	while ((opt = getopt(argc, argv, "hf:s:n:F:c:r:k:D:P:C:l::")) != -1) {
		switch (opt) {
		case 'D':
			if (bat.playback_device == NULL) {
				bat.playback_device = optarg;
			}
			if (bat.capture_device == NULL) {
				bat.capture_device = optarg;
			}
		case 'P':
			bat.playback_device = optarg;
			break;
		case 'C':
			bat.capture_device = optarg;
			break;
		case 'f':
			bat.input_file = optarg;
			break;
		case 'n':
			bat.frames = atoi(optarg);
			break;
		case 'F':
			bat.target_freq = atof(optarg);
			break;
		case 'c': /* ignored atm - to be implemented */
			bat.channels = atoi(optarg);
			break;
		case 'r':
			bat.rate = atoi(optarg);
			break;
		case 's':
			bat.frame_size = atoi(optarg);
			break;
		case 'k':
			bat.sigma_k = atof(optarg);
			break;
		case 'l':
			bat.local = true;
			break;
		case 'h':
		default: /* '?' */
			usage(argv);
		}
	}

	if (bat.input_file == NULL) {
		/* No input file so we will generate our own sinusoid */
		bat.frame_size=2;						/* SND_PCM_FORMAT_S16_LE */
		if (bat.frames) {
			bat.sinus_duration =  bat.rate;		/* Nb of frames for 1 second */
			bat.sinus_duration += 2*bat.frames;	/* Play long enough to record frame_size frames */
		} else {
			/* Special case where we want to generate a sine wave endlessly without capturing */
			bat.sinus_duration = 0;
		}
	}

	if (bat.local == false) {
		play_and_record_alsa(&bat);
		file = TEMP_RECORD_FILE_NAME;
	} else {
		file = bat.input_file;
		if (file == NULL) {
			fprintf(stdout,"You have to specify an input file for local testing!\n");
			exit(-1);
		}
	}

	fprintf(stdout,
			"\nBAT analysed signal is %d frames at %d Hz, %d channels, frame size %d bytes\n\n",
			bat.frames, bat.rate, bat.channels, bat.frame_size);
	fprintf(stdout, "BAT Checking for target frequency %2.2f Hz\n\n",
			bat.target_freq);

	ret = file_load(&bat, file);
	return ret;
}
