// for midi.c
bool open_midi_kb();
bool read_midi_bytes(uint8_t *mbuf);
void close_midi_kb();

// for pulse.c
int pulse_write(short *buffer,int nr_samples);
void pulse_close();
int pulse_init(uint32_t sr,int nr_samples,uint8_t chs);

// for pcm_snd.c
int snd_write(short *buffer,int nr_samples);
void snd_close();
int snd_init(uint32_t sr,int nr_samples,uint8_t chs);

// for dump-wave.c  
bool init_dump_wav(const char *fname,int nr_chan,int sample_rate);
bool close_dump_wav(void);
bool dump_wav(short *buf, int sz);

#ifndef LOG
#define LOG(format, ...) fprintf(stderr, format "\n", ##__VA_ARGS__);
#endif


