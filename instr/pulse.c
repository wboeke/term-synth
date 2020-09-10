#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include "shared.h"

static int pulse_error;
static pa_simple *stream;
static pa_sample_spec ss;
static pa_buffer_attr ba;

int pulse_init(uint32_t sr,int nr_samples,uint8_t chs) {
  if (stream) return 0;
  ss.format= PA_SAMPLE_S16LE;
  ss.rate=sr;
  ss.channels=chs;
  ba.maxlength = -1;
  ba.tlength = nr_samples * 2 * ss.channels;
  ba.prebuf = -1;
  ba.minreq = -1;
  ba.fragsize = -1;
  stream = pa_simple_new(NULL, "pa-interface", PA_STREAM_PLAYBACK, NULL, "", &ss, NULL, &ba, &pulse_error);
  if (!stream) {
    LOG("pulse: %s", pa_strerror(pulse_error));
    return -1;
  }
  LOG("pulseaudio: sample_rate=%d nr_samples=%d nr_channels=%d",ss.rate,ba.tlength/2/ss.channels,ss.channels);
  return 0;
}      
  
void pulse_close() {
  if (stream) {
    pa_simple_flush(stream,&pulse_error);
    pa_simple_free(stream);
    stream=0;
  }
  LOG("pulseaudio: closed");
}

int pulse_write(short *buffer,int nr_s) {
  if (!stream) return -1;
  int ret=pa_simple_write(stream, buffer, ba.tlength, &pulse_error);
  if (ret<0)
    LOG("Write: ret=%d err=%d: %s",ret,pulse_error,pa_strerror(pulse_error));
  return ret;
}

#ifdef TEST
#include <math.h>
enum {
  sample_rate = 44100, nr_samples = 512, nr_channels=2,
};

void play() {
  short buffer[nr_samples*2];
  float pos=0;
  for (int n=0;n<50;++n) {
    for (int i=0; i<nr_samples*2; i+=2) {
      pos += 0.05;
      if (pos>2*M_PI) pos -= 2*M_PI;
      buffer[i]=buffer[i+1]=sinf(pos) * 10000;
    }
    int err=pulse_write(buffer, nr_samples);
    if (err<0) break;
  }    
}

int main() {
  if (pulse_init(sample_rate,nr_samples,2) < 0) {
    puts("pulse-audio init failed");
    return 1;
  }
  play();
  pulse_close();
  return 0;
}
#endif
