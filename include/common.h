#include <alsa/asoundlib.h>
#include <alsa/pcm.h>
#include <pthread.h>

struct bat {
        unsigned int rate;
        int channels;
        int frames;
        int frame_size;

        float sigma_k;
        float target_freq;

        int sinus_duration;

        char *playback_device;
	char *capture_device;
        char *input_file;
        void *buf;      /* PCM Buffer */
        double *in;
        double *out;
        double *mag;

        bool local;		/* TRUE for internal test, otherwise FALSE */
};
