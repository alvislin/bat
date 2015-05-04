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
#include <math.h>
#include <stdbool.h>

#include "common.h"

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
			bat->convert_float_to_sample(sinus_f, buf);
			buf += bat->sample_size;
		}
		i += 1;
		if (i == bat->rate)
			i = 0; /* Restart from 0 after one sine wave period */
	}
}
