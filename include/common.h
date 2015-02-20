#define FORMAT_PCM 1

#define TEMP_RECORD_FILE_NAME "/tmp/test.wav"

#define COMPOSE(a, b, c, d) ((a) | ((b)<<8) | ((c)<<16) | ((d)<<24))
#define WAV_RIFF        COMPOSE('R', 'I', 'F', 'F')
#define WAV_WAVE        COMPOSE('W', 'A', 'V', 'E')
#define WAV_FMT         COMPOSE('f', 'm', 't', ' ')
#define WAV_DATA        COMPOSE('d', 'a', 't', 'a')

#define MAX_NUMBER_OF_CHANNELS		2
#define MAX_NB_OF_PEAK				10

struct wav_header {
	unsigned int magic; /* 'RIFF' */
	unsigned int length; /* file len */
	unsigned int type; /* 'WAVE' */
};

struct wav_chunk_header {
	unsigned int type; /* 'data' */
	unsigned int length; /* sample count */
};

struct wav_fmt {
	unsigned int magic; /* 'FMT '*/
	unsigned int fmt_size; /* 16 or 18 */
	unsigned short format; /* see WAV_FMT_* */
	unsigned short channels;
	unsigned int sample_rate; /* Frequency of sample */
	unsigned int bytes_p_second;
	unsigned short blocks_align; /* sample size; 1 or 2 bytes */
	unsigned short sample_length; /* 8, 12 or 16 bit */
};

struct chunk_fmt {
	unsigned short format; /* see WAV_FMT_* */
	unsigned short channels;
	unsigned int sample_rate; /* Frequency of sample */
	unsigned int bytes_p_second;
	unsigned short blocks_align; /* sample size; 1 or 2 bytes */
	unsigned short sample_length; /* 8, 12 or 16 bit */
};

struct wav_container {
	struct wav_header header;
	struct wav_fmt format;
	struct wav_chunk_header chunk;
};

struct bat {
	unsigned int rate;
	int channels;
	int frames;
	int frame_size;
	int sample_size;

	float sigma_k;
	float target_freq[MAX_NUMBER_OF_CHANNELS];

	int sinus_duration;

	/* TODO: this can be consolidated into a struct for
	 * playback + capture */
	char *playback_device;
	char *capture_device;
	unsigned int playback_card_tiny;
	unsigned int capture_card_tiny;
	unsigned int playback_device_tiny;
	unsigned int capture_device_tiny;
	unsigned int periods_played;
	unsigned int periods_total;
	bool period_limit;
	char *playback_file;
	char *capture_file;
	bool playback_single;
	bool capture_single;
	FILE *fp;

	void *buf;		/* PCM Buffer */

	void *(*playback)(void *);
	void *(*capture)(void *);

	bool local;	/* TRUE for internal test, otherwise FALSE */
	bool tinyalsa;	/* TRUE if user want to use tinyalsa instead of alsa */
};

struct analyze {
	void *buf;
	double *in;
	double *out;
	double *mag;
};

void close_file(void *);
void destroy_mem(void *);

void prepare_wav_info(struct wav_container *, struct bat *);
int read_wav_header(struct bat *);
int skip_wav_header(struct bat *);
void generate_sine_wave(struct bat *, int , void *, int);

