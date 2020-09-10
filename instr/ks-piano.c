#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <signal.h>

#include "shared.h"

int alt=-1;  // tone
enum {
  sample_rate = 44100, nr_samples = 512, nr_channels=2,
};

const float
  PI2 = M_PI*2;
int
  cnt;  // for debug
bool
  recording,
  mkb_connected;

pthread_t
  conn_thread,
  audio_thread;

float buffer[nr_samples],
      out_buf[2*nr_samples];
short out_buf_s[2*nr_samples];

#define NUMNOTES 73
#define BASENOTE 24

enum { busy, decaying, off };

typedef struct {
  float *string,
        stringcutoff;
  int note,
      stringpos,
      stringlen;
  int8_t status;
} String;

String strings[NUMNOTES];

static float
  *sbuf;

static int minmax(int a, int x, int b) { return x>=b ? b : x<=a ? a : x; }
static float fminmax(float a,float x,float b) { return x>b ? b : x<a ? a : x; }

void init(String *string,int _note) {
  string->note = _note;
  float freq = 440.0 * powf(2.0, (string->note + BASENOTE - 69) / 12.0);
  string->stringcutoff = fminmax(0.5,powf((float)string->note / NUMNOTES, 0.5),1.);
  string->stringlen = lrint(sample_rate / freq);
  string->string=malloc(string->stringlen*sizeof(float));
  string->stringpos = 0;
  string->status = off;
}

void set(String *string,float *sbuf,float vol) {
  //LOG("set: string=%p veloc=%.2f alt=%d",string,vol,alt);
  float rand_val=0;
  int stringlen=string->stringlen;
  if (alt==2) { // sines
    float mult=vol * 0.15;
    for (int i = 0; i < stringlen; i++) {
      float f=2 * M_PI * i / stringlen;
      string->string[i]=(sinf(f)+sinf(2*f)+sinf(3*f)+sinf(8*f)) * mult;
    }
    string->stringpos=0;
  }
  else {
    for (int i = 0; i < stringlen; i++) {
      if (i % 10 == 0)
        rand_val=(float)rand() / RAND_MAX - 0.5;
      sbuf[i] = rand_val;
    }
    float freq = vol * 0.8;   // vol = 0 -> 1
    for (int j = 0; j < 5; j++) { // lowpass effect at low volume
      sbuf[0] = sbuf[0] * freq + sbuf[stringlen - 1] * (1.0 - freq);
      for (int i = 1; i < stringlen; i++) {
        sbuf[i] = sbuf[i] * freq + sbuf[(i - 1) % stringlen] * (1.0 - freq);
      }
    }
    float avg = 0.0;
    for (int i = 0; i < stringlen; i++)
      avg += sbuf[i];
    avg /= stringlen;
    string->stringpos=0;
    for (int i = 0; i < stringlen; i++)
      string->string[i] = (sbuf[i] - avg) * vol;
  }
}

void get(String *string,float *sample) {
  float damp=string->stringcutoff; //  0.5 -> 1.0
  float *p=string->string + string->stringpos;
  if (string->status==off) {
    if (fabs(*p) > 0.001) string->status=decaying;
    else goto end;
  }
  //if (++cnt%5000==0) printf("coff=%.3f\n",string->stringcutoff);

  if (string->stringpos!=0)
    *p = *p * damp + *(p - 1) * (1. - damp);
  else
    *p = *p * damp + string->string[string->stringlen-1] * (1. - damp);

  //*p=*p*1.01/(1 + fabs(*p) * 0.05); // <-- no decay

  if (string->status==busy) {
    if (alt != 1) *p *= 0.995;
  }
  else *p *= 0.95;
  *sample += *p;
  end:
  if (++string->stringpos >= string->stringlen) {
    string->stringpos = 0;
    if (string->status==decaying) string->status=off;
  }
}

void keyb_noteOn(uint8_t note,uint8_t velocity) {
  //LOG("keyb_noteOn: mnr=%d",note);
  if (note >= BASENOTE && note < BASENOTE + NUMNOTES) {
    note -= BASENOTE;
    strings[note].status = busy;
    float vol = velocity / 128.;
    set(strings+note,sbuf,vol);
  }
}

void keyb_noteOff(uint8_t note) {
  //LOG("keyb_noteOff: mnr=%d",note);
  if ((note >= BASENOTE) && (note < BASENOTE + NUMNOTES)) {
    note -= BASENOTE;
    strings[note].status = decaying;
  }
}

void stop_conn_mk() {
  if (!mkb_connected) return;
  mkb_connected=false;
}

void split(float *in_buf,float *out_buf) {
  static float lp;
  float
    lpf=expf(-PI2 * 1000. / sample_rate);
  for (int i=0;i<nr_samples;++i) {
    lp = (1-lpf) * in_buf[i] + lpf * lp;
    out_buf[2*i]=lp;
    out_buf[2*i+1]=in_buf[i] - lp;
  }
  lp *= 0.999;  // to avoid low-freq ringing at note start
}

void* play(void* d) {
  while (mkb_connected) {
    bzero(buffer,sizeof(buffer));
    for (uint fnr = 0; fnr < nr_samples; ++fnr) {
      float sample = 0.0;
      for (int note = 0; note < NUMNOTES; note++)
        get(strings+note, &sample);
      buffer[fnr] = sample;
    }
    split(buffer,out_buf);

    if (mkb_connected) {
      for (int i=0;i<nr_samples*2;++i)
        out_buf_s[i]=minmax(-32000, out_buf[i]*50000, 32000);
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

void sig_handler(int sig) {
  switch (sig) {
    case SIGINT:
      putchar('\n');
      mkb_connected=false;
      if (audio_thread) pthread_join(audio_thread,0);
      puts("Bye!");
      exit(0);
    default: printf("signal: %d\n",sig);
  }
}

int main(int argc, char *argv[]) {
  for (int note = 0; note < NUMNOTES; note++)
    init(strings+note, note);
  float freq = 440.0 * powf(2.0, (BASENOTE - 69) / 12.0);
  int len = sample_rate / freq;
  sbuf = malloc(len * sizeof(float));

  if (snd_init(sample_rate,nr_samples,2) < 0) {
    puts("pulse-audio init failed");
    return 1;
  }
  if (!open_midi_kb()) {
    puts("midi keyboard not connected");
    return 1;
  }
  mkb_connected=true;
  pthread_create(&conn_thread,0,connect_mkeyb,0);
  pthread_create(&audio_thread, 0, play, 0);

  signal(SIGINT,sig_handler);
  puts("modify tone: <enter>\nstart/stop recording: r <enter>");

  for (char ch=0;;ch=getchar()) {
    if (ch=='r') {
      recording=!recording;
      if (recording) {
        fputs("recording ...\n",stdout);
        init_dump_wav("out.wav",2,sample_rate);
      }
      else {
        close_dump_wav();
        fputs("stop recording ",stdout);
      }
      getchar();
    }
    else {
      ++alt; alt%=3;
      printf("  tone = %s",alt==0 ? "piano " :
                           alt==1 ? "long " :
                           alt==2 ? "sines " : "??");
    }
  }
  return 0;
}
