#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <sndfile.h>

#include "shared.h"

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
