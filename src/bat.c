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
#include <string.h>
#include <errno.h>
#include <fftw3.h>
#include <math.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm.h>

#define BUFFER_LENGTH 48000

struct bat {
	int rate;
	int channels;
	int frames;
	int frame_size;

	float sigma_k;
	float target_freq;

	void *buf;	/* PCM Buffer */
	double *in;
	double *out;
	double *mag;
};

static void usage(char *argv[])
{
	fprintf(stdout, "Usage:%s [-f file] [-n frames] [-s frame size] [-k sigma k] [-F Target Freq]\n",
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

/* hard coded atm to only accept short to double conversion */
static void convert(struct bat *bat)
{
	short *s = bat->buf;
	int i;

	for (i = 0; i < bat->frames; i++)
		bat->in[i] = s[i];
}

/* hard coded for rate of 44100 Hz atm */
static int check(struct bat *bat)
{
	float Hz =  2.0 / ((float) bat->frames / (float) bat->rate);
	float mean = 0.0, t, sigma = 0.0, p = 0.0;
	int i, start = -1, end = -1, peak = 0, signals = 0;
	int ret = 0, N = bat->frames / 2;

	/* calculate mean */
	for (i = 0; i < N; i++)
		mean += bat->mag[i];
	mean /= (float) N;

	/* calculate standard deviation */
	for (i = 0; i < N;  i++) {
		t = bat->mag[i] - mean;
		t *= t;
		sigma += t;
	}
	sigma /= (float) N;
	sigma = sqrtf(sigma);

	/* clip any data less than k sigma */
	for (i = 0; i < N; i++) {
		if (bat->mag[i] > bat->sigma_k * sigma) {

			/* find peak start points */
			if (start == -1) {
				start = i;
				peak = i;
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
					10.0 * log10(p / mean),
					(start + 1) * Hz, (end + 1) * Hz);
				if ((peak + 1) * Hz < bat->target_freq - 1.0) {
					fprintf(stdout, " Peak too low %2.2f Hz\n",
						(peak + 1) * Hz);
					ret = -EINVAL;
				} else if ((peak + 1) * Hz > bat->target_freq + 1.0) {
					fprintf(stdout, " Peak too high %2.2f Hz\n",
						(peak + 1) * Hz);
					ret = -EINVAL;
				}
 
				end = -1;
				start = -1;
			}
		}	
	}
	fprintf(stdout, "Detected %d signal(s) in total\n", signals);

	return ret;
}

static int fft(struct bat *bat)
{
	fftw_plan p;
	int ret = -ENOMEM, N = bat->frames / 2;

	/* alocate FFT buffers */
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

int generate_sine(int frequency, int sampling_frequency)
{
	static char *device = "default";		//soundcard, to change into parameters?
	float buffer [BUFFER_LENGTH];

    int err;
    int k;

    snd_pcm_t *handle;

    // Error Handling
    if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0)
	{
            printf("Playback open error: %s\n", snd_strerror(err));
            exit(EXIT_FAILURE);
    }

	if ((err = snd_pcm_set_params(handle,
                                  SND_PCM_FORMAT_FLOAT,
                                  SND_PCM_ACCESS_RW_INTERLEAVED,
                                  1,
                                  48000,
                                  1,
                                  500000)) < 0) 
	{
			printf("Playback open error: %s\n", snd_strerror(err));
			exit(EXIT_FAILURE);
    }

	// Sine wave value generation
	for (k = 0 ; k < BUFFER_LENGTH ; k++)
	{
		buffer[k] = (sin(k * 2 * M_PI * frequency / sampling_frequency));
	}
	
	snd_pcm_writei(handle, buffer, BUFFER_LENGTH);						// Send values to sound driver
	snd_pcm_close(handle);
	return 0;
}

int main(int argc, char *argv[])
{
	struct bat bat;
	int ret, opt;
	char *file = NULL;

	memset(&bat, 0, sizeof(bat));

	/* Set default values */
	bat.rate = 44100;
	bat.channels = 1;
	bat.frame_size = 2;
	bat.frames = bat.rate;
	bat.target_freq = 997.0;
	bat.sigma_k = 3.0;

	/* Parse options */
	while ((opt = getopt(argc, argv, "hf:s:n:F:c:r:k:")) != -1) {
		switch (opt) {
		case 'f':
			file = optarg;
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
		case 'h':
		default: /* '?' */
			usage(argv);
		}
	}
	
	// Sine generation
    fprintf(stdout, "Sine tone at %2.2f Hz, sampling frequency is %i Hz\n",
		bat.target_freq, bat.rate);
	generate_sine(bat.target_freq, bat.rate);
	fprintf(stdout, "Sine generation ended\n");
	
	
	fprintf(stdout, "BAT input is %d frames at %d Hz, %d channels, frame size %d bytes\n",
		bat.frames, bat.rate, bat.channels, bat.frame_size);
	fprintf(stdout, "BAT Checking for target frequency %2.2f Hz\n",
		bat.target_freq);
 
	ret = file_load(&bat, file);
	return ret;
}	
