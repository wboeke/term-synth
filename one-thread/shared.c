#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <alsa/asoundlib.h>
#include <sndfile.h>

#include <term-win.h>
#include "shared.h"

snd_pcm_t
  *playback_handle;
int snd_init(uint32_t sr,int nr_samples,uint8_t chs) {
  LOG("snd_init: sr=%d nr_samples=%d chs=%d",sr,nr_samples,chs);
  int err,
      sr2=sr;
  snd_pcm_hw_params_t
      *hw_params;
  char pcm_name[10];
  for (int nr=0; nr<2; ++nr) {
    sprintf(pcm_name,"hw:%d",nr);  // no resampling
    if (snd_pcm_open (&playback_handle, pcm_name, SND_PCM_STREAM_PLAYBACK, 0) == 0)
      break;
  }
  if (playback_handle) { LOG("sound: %s",pcm_name); }
  else { LOG("cannot open audio device hw:"); return -1; }
  snd_pcm_hw_params_alloca(&hw_params);
  snd_pcm_hw_params_any(playback_handle, hw_params);
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

  //LOG("pcm_snd: sample_rate=%d nr_samples=%d nr_channels=%d",sr,nr_samples,chs);
  return 0;
}

int snd_write(short *buffer,int samples) {
  if (!playback_handle) return -1;
  int ret=snd_pcm_writei(playback_handle, buffer, samples);
  if (ret<0) {
    LOG("snd_write: %s",snd_strerror(ret)); // will recover
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

bool mouse_event(Event *ev, uint8_t *tty_buf) {
  if (tty_buf[0]==27 && tty_buf[1]=='[') { // escape sequence?
    int nr=tty_buf[3],
        x=tty_buf[4],
        y=tty_buf[5];
    //LOG("nr=%d x=%d y=%d",nr,x,y);
    ev->type=Ev_mouse;
    ev->mode= (nr & 64)/64 ? Mouse_move : nr==35 ? Mouse_up : Mouse_down;
    ev->button= ev->mode==Mouse_up ? 99 : nr & 3;
    ev->x=x-33;
    ev->y=y-33;
    return true;
  }
  return false;
}

static bool inited=false;
static SNDFILE *file;
static SF_INFO sfinfo;

bool init_dump_wav(const char *fname,int nr_chan,int sample_rate) {
  sfinfo.samplerate=sample_rate;
  sfinfo.channels=nr_chan;
  sfinfo.frames=0;
  sfinfo.format=SF_FORMAT_WAV | SF_FORMAT_PCM_16;

  if (!(file = sf_open (fname, SFM_WRITE, &sfinfo))) {
    LOG("Error : Not able to open output file %s",fname) ;
    return false;
  }
  inited=true;
  return true;
}

bool close_dump_wav(void) {
  if (!inited) { LOG("close_dump_wav: not inited"); return false; }
  inited=false;
  if (!file) { LOG("close_dump_wav: file?"); return false; }
  sf_close(file);
  return true;
}

bool dump_wav(short *buf, int sz) {
  if (!inited) // probably called too early
    return true;
  sz *= sfinfo.channels;
  if (sf_write_short(file,buf,sz) != sz) {
    LOG("dump_wav: write problem");
    sf_close(file);
    return false;
  }
  return true;
}

