#define FORMAT_PCM 1

#define TEMP_RECORD_FILE_NAME "/tmp/test.wav"

#define COMPOSE(a,b,c,d) ((a) | ((b)<<8) | ((c)<<16) | ((d)<<24))
#define WAV_RIFF        COMPOSE('R','I','F','F')
#define WAV_WAVE        COMPOSE('W','A','V','E')
#define WAV_FMT         COMPOSE('f','m','t',' ')
#define WAV_DATA        COMPOSE('d','a','t','a')

#define MAX_NUMBER_OF_CHANNELS		2

typedef struct wavHeader {
	unsigned int magic; /* 'RIFF' */
	unsigned int length; /* file len */
	unsigned int type; /* 'WAVE' */
} wavHeader_t;

typedef struct wavChunkHeader {
	unsigned int type; /* 'data' */
	unsigned int length; /* sample count */
} wavChunkHeader_t;

typedef struct wavFmt {
	unsigned int magic; /* 'FMT '*/
	unsigned int fmt_size; /* 16 or 18 */
	unsigned short format; /* see WAV_FMT_* */
	unsigned short channels;
	unsigned int sample_rate; /* Frequency of sample */
	unsigned int bytes_p_second;
	unsigned short blocks_align; /* sample size; 1 or 2 bytes */
	unsigned short sample_length; /* 8, 12 or 16 bit */
} wavFmt_t;

typedef struct chunkFmt {
	unsigned short format; /* see WAV_FMT_* */
	unsigned short channels;
	unsigned int sample_rate; /* Frequency of sample */
	unsigned int bytes_p_second;
	unsigned short blocks_align; /* sample size; 1 or 2 bytes */
	unsigned short sample_length; /* 8, 12 or 16 bit */
} chunkFmt_t;

typedef struct WAVContainer {
	wavHeader_t header;
	wavFmt_t format;
	wavChunkHeader_t chunk;
} WAVContainer_t;

struct bat {
	unsigned int rate;
	int channels;
	int frames;
	int frame_size;
	int sample_size;

	float sigma_k;
	float target_freq[MAX_NUMBER_OF_CHANNELS];

	int sinus_duration;

	/* TODO: this can be consolidated into a struct for playback + capture */
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

	void *buf; 		/* PCM Buffer */

	void *(*playback)(void *);
	void *(*capture)(void *);

	bool local; 	/* TRUE for internal test, otherwise FALSE */
	bool tinyalsa;	/* TRUE if user want to use tinyalsa lib instead of alsa library */
};

struct analyze {
	void *buf;
	double *in;
	double *out;
	double *mag;
};

void close_file(void *);
void destroy_mem(void *);

void prepare_wav_info(WAVContainer_t *, struct bat *);
int read_wav_header(struct bat *);
int skip_wav_header(struct bat *);
void generate_sine_wave(struct bat *,int , void *, int );

