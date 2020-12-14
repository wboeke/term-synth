#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <alsa/asoundlib.h>

#include "shared.h"

static snd_pcm_t *playback_handle;

int snd_init(uint32_t sr,int nr_samples,uint8_t chs) {
  //LOG("snd_init: sr=%d buf_len=%ld chs=%d",sr,buf_len,chs);
  int err,
      sr2=sr;
  snd_pcm_hw_params_t
      *hw_params;
  char pcm_name[10];
  for (int nr=0; nr<2; ++nr) {
    //sprintf(pcm_name,"plughw:0,0"); // <-- enable resampling, however: invalid pcm period_size
    sprintf(pcm_name,"hw:%d",nr);  // no resampling
    if (snd_pcm_open (&playback_handle, pcm_name, SND_PCM_STREAM_PLAYBACK, 0) == 0)
      break;
  }
  if (playback_handle) { LOG("sound: %s",pcm_name); }
  else { LOG("cannot open audio device hw:"); return -1; }
  snd_pcm_hw_params_alloca(&hw_params);
  snd_pcm_hw_params_any(playback_handle, hw_params);
  //err = snd_pcm_hw_params_set_rate_resample(playback_handle, hw_params, 1);
  //if (err<0) { LOG("pcm resampling failed: %s",snd_strerror(err)); return -1; }
  snd_pcm_hw_params_set_access(playback_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
  err=snd_pcm_hw_params_set_format(playback_handle, hw_params, SND_PCM_FORMAT_S16_LE);
  if (err<0) { LOG("pcm format: %s",snd_strerror(err)); return -1; }
  snd_pcm_hw_params_set_channels(playback_handle, hw_params, chs);
  err=snd_pcm_hw_params_set_rate_near(playback_handle, hw_params, &sr, 0);
  if (err<0) { LOG("pcm set rate failed: %s",snd_strerror(err)); return -1; }
  if (sr2!=sr) { LOG("sr: %d -> %d, better modify params",sr2,sr); } //return -1; }
  snd_pcm_hw_params_set_periods(playback_handle, hw_params, 2, 0);
  snd_pcm_hw_params_set_period_size(playback_handle, hw_params, nr_samples, 0);
  snd_pcm_hw_params(playback_handle, hw_params);

  LOG("pcm_snd: sample_rate=%d nr_samples=%d nr_channels=%d",sr,nr_samples,chs);
  return 0;
}

int snd_write(short *buffer,int nr_samples) {
  if (!playback_handle) return -1;
  int ret=snd_pcm_writei(playback_handle, buffer, nr_samples);
  if (ret<0) {
    LOG("snd_write: %s",snd_strerror(ret)); // xrun will recover
    if (ret == -EPIPE)    // xrun 
      ret = snd_pcm_prepare(playback_handle);
  }
  return ret;
}

void snd_close() {
  if (playback_handle) {
    snd_pcm_close (playback_handle);
    playback_handle=0;
  }
  LOG("pcm: closed");
}

#ifdef TEST
#include <math.h>
enum {
//  sample_rate = 48000, nr_samples = 512, nr_channels=2,
  sample_rate = 22050, nr_samples = 256, nr_channels=2,
//  sample_rate = 44100, nr_samples = 512, nr_channels=2,
};

void play() {
  short buffer[nr_samples*2];
  float pos=0;
  float mult=44100./sample_rate;
  for (int n=0;n<50;++n) {
    for (int i=0; i<nr_samples*2; i+=2) {
      pos += 0.05 * mult;
      if (pos>2*M_PI) pos -= 2*M_PI;
      buffer[i]=buffer[i+1]=sinf(pos) * 10000;
    }
    int err=snd_write(buffer, nr_samples);
    if (err<0) break;
  }    
}

int main() {
  if (snd_init(sample_rate,nr_samples,2) < 0) {
    puts("pcm_snd init failed");
    return 1;
  }
  play();
  snd_close();
  return 0;
}
#endif

