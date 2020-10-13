/*
Karplus-Strong piano
Derived from an implementation by Erik Entrich, licence: GPL,
  github.com/50m30n3/SO-KL5
*/
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include <term-win.h>
#include "shared.h"

int
  cnt;  // for debug
static short
  stereo=1,
  alt;  // tone
static float
  lfo1_pos,
  lfo1[2],
  lfo1_val;
const float
  PI2 = M_PI*2;
static bool
  recording,
  mkb_connected;
static tw_Menu
  tone_mode,
  mono_stereo;
static tw_Checkbox
  record;

enum {
  sample_rate = 44100, nr_samples = 512, nr_channels=2,
};

pthread_t
  conn_thread,
  audio_thread;

static float
  buffer[2*nr_samples];

static short
  out_buf_s[2*nr_samples];

#define NUMNOTES 85 // 73
#define BASENOTE 12 // 24

enum { busy, decaying, off };

typedef struct {
  float *string,
        stringcutoff,
        z,
        tap0, tap1;
  int note,
      stringpos,
      stringlen;
  int8_t status;
} String;

String strings[NUMNOTES];

static int minmax(int a, int x, int b) { return x>=b ? b : x<=a ? a : x; }
static float fminmax(float a,float x,float b) { return x>b ? b : x<a ? a : x; }

void init(String *string,int _note) {
  string->note = _note;
  float freq = 440.0 * powf(2.0, (string->note + BASENOTE - 69) / 12.0);
  string->stringcutoff = fminmax(0.5,powf((float)string->note / NUMNOTES, 0.5),0.95);
  string->stringlen = lrint(sample_rate / freq);
  string->string=calloc(string->stringlen,sizeof(float));
  string->stringpos = 0;
  string->status = off;
}

float randval() { return (float)rand() / RAND_MAX - 0.5; }

void set_string(String *string,float vol) {
  //LOG("set: string=%p veloc=%.2f alt=%d",string,vol,alt);
  float rand_val=0,
        d=0,
        *s_buf=string->string;
  int stringlen=string->stringlen;
  if (alt==2 || alt==3) { // sines
    float mult=vol * 0.15;
    for (int i = 0; i < stringlen; i++) {
      float f=2 * M_PI * i / stringlen;
      string->string[i]=(sinf(f)+sinf(2*f)+sinf(3*f)+sinf(8*f)) * mult;
    }
    string->stringpos=0;
  }
  else {
    float mix = vol * 0.2;   // vol = 0 -> 1
    for (int i = 0; i < stringlen; i++) {
      if (i % 10 == 0)
        rand_val=randval();
      d=mix*rand_val+(1-mix)*d; // lowpass
      s_buf[i] = d;
    }
    float avg = 0.0;
    for (int i = 0; i < stringlen; i++)
      avg += s_buf[i];
    avg /= stringlen;
    string->stringpos=0;
    for (int i = 0; i < stringlen; i++)
      string->string[i] = (s_buf[i] - avg) * vol;
  }
}

float lowpass(String *s, float in) { // 6 db/oct
  const float a = fmax(0.1, 2 * s->stringcutoff - 1.2),  // cof = 0.5 -> a = -0.2, cof = 1 -> a = 0.8
              b = 1. - a;
  //const float a = 0.2, b = 1. - a;
  s->z = in * a + s->z * b;
  return s->z;
}
float highpass(String *s, float in) { // 6 db/oct
  float a = 0.98, b = 1. - a;
  s->z = in * b + s->z * a;
  return in - s->z;
}

float interpol(float *inbuf,float ind_f,int size) {
  const int ind=ind_f;
  const float mix=ind_f-ind;
  if (ind>=size-1) return inbuf[ind];
  return inbuf[ind] * (1 - mix) + inbuf[ind+1] * mix;
}

void get_sample(String *string, float *sample0, float *sample1) {
  int s_len = string->stringlen;
  float *p=string->string + string->stringpos;
  if (alt==4) *p = lowpass(string,*p) * 1.005;
  else if (alt==5) *p = highpass(string,*p) * 1.01;
  if (string->status==off) {
    if (fabs(*p) > 0.001) string->status=decaying;
    else {
      if (alt==4 || alt==5) *p=string->z=0; // to prevent noisy residu
      goto end;
    }
  }
  float damp= string->stringcutoff; //  0.5 -> 1.0
  if (string->stringpos!=0)
    *p = *p * damp + *(p - 1) * (1. - damp);
  else
    *p = *p * damp + string->string[s_len-1] * (1. - damp);
  if (string->status==busy) {
    if (alt == 1 || alt == 3) *p *= 1.005 - fabs(*p) * 0.05; // delimit
    else *p *= 0.995;
  }
  else *p *= 0.95;
  if (stereo) {
    *sample0 += interpol(string->string, string->tap0, s_len);
    *sample1 += interpol(string->string, string->tap1, s_len);
  // *sample0 += string->string[(int)(string->tap0)];
  // *sample1 += string->string[(int)(string->tap1)];
  }
  else {
    *sample0 += *p; *sample1 = *sample0;
  }
  end:
  if (++string->stringpos >= s_len) {
    string->stringpos = 0;
    if (string->status==decaying)
      string->status=off;
  }
  int midtap=string->stringpos - s_len * 0.25;
  float dif=lfo1_val * s_len * 0.2;
  string->tap0=midtap - dif; if (string->tap0<0) string->tap0 += s_len;
  string->tap1=midtap + dif; if (string->tap1<0) string->tap1 += s_len;
}

void keyb_noteOn(uint8_t note,uint8_t velocity) {
  //LOG("keyb_noteOn: mnr=%d",note);
  if (note >= BASENOTE && note < BASENOTE + NUMNOTES) {
    note -= BASENOTE;
    strings[note].status = busy;
    float vol = velocity / 128.;
    set_string(strings+note,vol);
  }
}

void keyb_noteOff(uint8_t note) {
  //LOG("keyb_noteOff: mnr=%d",note);
  if ((note >= BASENOTE) && (note < BASENOTE + NUMNOTES)) {
    note -= BASENOTE;
    strings[note].status = decaying;
  }
}

static void stop_conn_mk() {
  if (!mkb_connected) return;
  mkb_connected=false;
}

static void set_lfo() {   // called once per frame
  const float fr=0.3;
  lfo1_pos +=  PI2 * fr / sample_rate * nr_samples;
  if (lfo1_pos>PI2) lfo1_pos -= PI2;
  lfo1[0]=lfo1[1];
  lfo1[1]=sinf(lfo1_pos);
}

void* play(void* d) {
  while (mkb_connected) {
    bzero(buffer,sizeof(buffer));
    set_lfo();
    for (uint fnr = 0; fnr < nr_samples; ++fnr) {
      float mix=(float)fnr/nr_samples;
      lfo1_val=lfo1[0] * (1 - mix) + lfo1[1] * mix;
      float sample_0 = 0, sample_1 = 0;;
      for (int note = 0; note < NUMNOTES; note++)
        get_sample(strings+note, &sample_0, &sample_1);
      buffer[2*fnr] = sample_0;
      buffer[2*fnr+1] = sample_1;
    }

    if (mkb_connected) {
      for (int i=0;i<nr_samples*2;++i)
        out_buf_s[i]=minmax(-32000, buffer[i]*20000, 32000);
      if (snd_write(out_buf_s,nr_samples)<0) { mkb_connected=0; break; }
      if (mkb_connected && recording)
        dump_wav(out_buf_s,nr_samples);
    }
  }
  snd_close();
  return 0;
}

void eval_midi_mes(const uint8_t *midi_mes) {
  switch(midi_mes[0]) {
    case 0x80:
      keyb_noteOff(midi_mes[1]);
      break;
    case 0x90:
      if (midi_mes[2]==0) keyb_noteOff(midi_mes[1]);
      else keyb_noteOn(midi_mes[1],midi_mes[2]);
      break;
    default: break;
  }
}

static void* connect_mkeyb(void* d) {
  uint8_t mbuf[3];
  while (read_midi_bytes(mbuf)) {
    if (!mkb_connected) { close_midi_kb(); return 0; } // <-- if ctrl_C pressed
    eval_midi_mes(mbuf); 
  }
  mkb_connected=false; // <-- if keyboard was disconnected
  close_midi_kb();
  return 0;
}

int main(int argc, char *argv[]) {
  read_screen_dim(&tw_colums,&tw_rows);

  for (int note = 0; note < NUMNOTES; note++)
    init(strings+note, note);

  if (snd_init(sample_rate,nr_samples,2) < 0) {
    puts("Alsa init failed");
    return 1;
  }
  if (!open_midi_kb()) {
    puts("midi keyboard not connected");
    return 1;
  }
  mkb_connected=true;
  pthread_create(&conn_thread,0,connect_mkeyb,0);
  pthread_create(&audio_thread, 0, play, 0);

  tw_gui_init();
  do_exit= ^{
    mkb_connected=false;
    if (audio_thread) { pthread_join(audio_thread,0); audio_thread=0; }
  };

  tw_menu_init(
    &tone_mode,
    (Rect){1,1,10,7},
    &alt,
    "tone",
    (char*[]){ "random","random steady","sines","sines steady","random lowpass","random highpass", 0 },
    0
  );

  tw_menu_init(
    &mono_stereo,
    (Rect){22,1,8,3},
    &stereo,
    "output",
    (char*[]){ "mono","stereo",0 },
    0
  );
  tone_mode.style= mono_stereo.style= Compact;

  tw_checkbox_init(
    &record,
    (Rect){22,7,0,0},
    &recording,
    "record (to out.wav)",
    ^{ if (recording) {
         if (!init_dump_wav("out.wav",2,sample_rate)) {
           usleep(300000);
           checkbox_draw(&record);
         }
       }
       else
         close_dump_wav();
     }
  );
  record.on_col=col_red;

  tw_draw();
  tw_start_main_loop();
  return 0;
}
