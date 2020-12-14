extern snd_pcm_t
  *playback_handle;

int snd_write(short *buffer,int nr_samples);
void snd_close();
int snd_init(uint32_t sr,int nr_samples,uint8_t chs);
bool mouse_event(Event *ev, uint8_t *tty_buf);
bool init_dump_wav(const char *fname,int nr_chan,int sample_rate);
bool close_dump_wav(void);
bool dump_wav(short *buf, int sz);

#ifndef LOG
#define LOG(format, ...) fprintf(stderr, format "\n", ##__VA_ARGS__);
#endif

