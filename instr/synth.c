/*
This code is public domain. Inspiration sources:
Formantic synthesis - created by Doc Rochebois,
  https://www.musicdsp.org/en/latest/Synthesis/224-am-formantic-synthesis.html
Perlin noise - invented by Artem Popov: "Using Perlin noise in sound sythesis".
  https://lac.linuxaudio.org/2018/pdf/14-paper.pdf
Karplus-Strong - derived from an implementation by Erik Entrich, licence: GPL.
  https://github.com/50m30n3/SO-KL5
*/

#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

#include <term-win.h>
#include "shared.h"

enum {
  nr_samples=512,
  sample_rate=44100,
  //sample_rate=22050,
  voices_max=4,
  h_xgrid = 1,
  num_pitch_max=13,
  col_hbg=194,
  col_dgrey=grey+12,
  subdiv=5,    // adsr
  s_len=2,     // adsr
  modwheel_len=6,
  pitwheel_len=9,
  ampl_len=5,  // volume slider
  grad_max=0x100,
  ks_min_freq=50,
  stringlen_max=sample_rate/ks_min_freq  // karp-strong string 50Hz
};

int
  cnt; // for debug
static bool
  mkb_connected,
  recording;

static int8_t
  mnr2vnr[128];
static short
  patch_nr;
static const float
  PI2=2 * M_PI,
  coff[modwheel_len]={ 0.5,1,1.5,2,3,7 },
  outvol[ampl_len]={ 0.3,0.5,0.7,1,1.5 };

#define oload __attribute__((overloadable))

typedef struct {
  tw_Custom harms;
  short *pitch_arr;
} Harmonics;

typedef struct {
  tw_Custom adsr;
  Point pt[5];
  short pt_ind,
        lst_x,
        lst_y;
  short *x1,*x2,*y2,*x4;
  void (^cmd)();
} Adsr_amp;

typedef struct {
  tw_Custom adsr;
  Point pt[4];
  short pt_ind,
        lst_x,
        lst_y;
  short *y0,*x1,*y1,*x3,*y3;
  void (^cmd)();
  bool do_draw;
} Adsr_filt;

static tw_Menu
   wform_osc1, wform_osc2,
   filter_mode,
   instr_mode,
   mod_mode,
   patches;
static tw_HorSlider
   mix_1_2,
   amfm_2_1,
   detune,
   filt_coff,
   filt_q,
   eg2_mod_filt,
   trem_osc;
static tw_VertSlider
   volume;
static tw_Checkbox
   connect_mkb,
   record;
static Harmonics
   harm_win1,
   harm_win2;
static Adsr_amp
   adsr_amp;
static Adsr_filt
   adsr_filt;

enum { Attack, Decay, Sustain, Release, Idle };
enum { Sine, Sawtooth, Square, Harms, Perlin, Formant, KarpStrong, Off };
enum { eLP, eBP, eHP };  // same order as filter_mode
enum { Mode0, Mode1 };   // same order as instr_mode
enum { eAM, eFM };       // same order as mod_mode

typedef struct {
  float
    mix_1_2,//=0.5,
    detune,//=0.0001,
    qres,//=0.4,
    fixed_cutoff,//=2.0,
    eg2_mod_filt,//=1,
    start_lev_f,//=1,
    decay_f,//=0.005,
    sustain_f,//=0.5,
    release_f,//=0.005,
    end_lev_f,//=0,
    attack,//=0.01,
    decay,//=0.001,
    sustain,//=1.0,
    release,//=0.005,
    trem_freq,//=7,
    trem_osc,//=0,
    amfm_2_1,//=0,
    out_vol,//=1
    lfo1[2];//=[0,0];
  bool
    trem_enabled; 
} Vcom;
static Vcom vcom;

static pthread_t
  conn_thread,
  audio_thread;

typedef struct {
  short y0, x1, y1, x3, y3;
} Adsr_filt_data;

typedef struct {
  short x1, x2, y2, x4;
} Adsr_amp_data;

typedef struct {
  char *name;
  short
    pitch_arr1[num_pitch_max],
    pitch_arr2[num_pitch_max];
  short
    wform_osc1, wform_osc2, mix_1_2, detune,
    filt_mode, filt_coff, filt_q, eg2_mod_filt,
    trem_osc, amfm_2_1, instr_mode, mod_mode;
  Adsr_filt_data adsr_f_data;
  Adsr_amp_data adsr_a_data;
  short out_vol;
} Patch;
static Patch patch;

static void patch_report() {
  fprintf(stderr,"  { \"%s\", {", patch.name);
  if (patch.wform_osc1==Harms || patch.wform_osc1==Formant && patch.instr_mode==Mode1)
    for (int i=0;i<num_pitch_max;++i) {
      if (i==0) fprintf(stderr,"%d",patch.pitch_arr1[0]);
      else fprintf(stderr,",%d",patch.pitch_arr1[i]);
    }
  fprintf(stderr,"}, {");
  if (patch.wform_osc2==Harms)
    for (int i=0;i<num_pitch_max;++i) {
      if (i==0) fprintf(stderr,"%d",patch.pitch_arr2[0]);
      else fprintf(stderr,",%d",patch.pitch_arr2[i]);
    }
  fprintf(stderr,"},\n    %d,%d,%d,%d, %d,%d,%d,%d, %d,%d,%d,%d, { %d,%d,%d,%d,%d }, { %d,%d,%d,%d }, %d },\n",
    patch.wform_osc1, patch.wform_osc2, patch.mix_1_2, patch.detune,
    patch.filt_mode, patch.filt_coff, patch.filt_q, patch.eg2_mod_filt,
    patch.trem_osc, patch.amfm_2_1, patch.instr_mode, patch.mod_mode,
    patch.adsr_f_data.y0, patch.adsr_f_data.x1, patch.adsr_f_data.y1, patch.adsr_f_data.x3, patch.adsr_f_data.y3,
    patch.adsr_a_data.x1, patch.adsr_a_data.x2, patch.adsr_a_data.y2, patch.adsr_a_data.x4,
    patch.out_vol
  );
}

static Patch bi_patches[] = {
  { "piano", {}, {},
    1,1,2,1, 0,4,1,4, 4,0,-1,-1, { 2,3,1,3,1 }, { 0,4,0,3 }, 3 },
  { "glass harp", {0,0,0,3,3,0,0,0,0,0}, {0,0,0,3,3,0,0,0,0,0},
      3,3,2,1, 0,2,1,4, 4,0,-1,-1, { 2,3,1,3,1 }, { 0,5,0,3 }, 3 },
  { "hammond", {3,2,2,2,0,2,0,0,0,0,0,2,0}, {},
    3,-1,0,0, 0,4,0,4, 5,0,-1,-1, { 2,2,0,3,0 }, { 0,3,1,3 }, 3 },
  { "organ", {4,2,2,2,0,2,0,0,0,0,0,4,0}, {0,0,2,0,0,2,0,0,0,0,0,0,0},
    3,3,2,2, -1,0,0,0, 4,0,-1,-1, { 2,2,0,3,0 }, { 0,3,1,3 }, 3 },
  { "high organ", {4,2,0,0,0,0,0,0,0,0,0,2,0}, {4,2,0,0,0,0,0,0,0,0,0,0,0},
    3,3,2,2, 0,4,1,4, 4,0,-1,-1, { 2,4,0,3,0 }, { 0,3,1,3 }, 3 },
  { "el.piano", {}, {0,0,0,4,0,0,0,2,0,0,0,1,0},
    0,3,0,1, 0,4,1,4, 4,4,-1,1, { 2,5,0,3,0 }, { 0,5,0,3 }, 3 },
  { "bass",    {}, {3,0,0,0,0,0,3,0,0,0},
      1,3,0,1, 0,2,1,4, 4,3,-1,0, { 2,5,0,3,0 }, { 0,5,0,3 }, 3 },
  { "short bass", {}, {3,0,3,0,0,0,0,0,0,0,0,0,0},
    1,3,0,1, 0,2,2,4, 4,4,-1,0, { 1,3,1,3,1 }, { 0,3,0,3 }, 4 },
  { "simple", {0,0,3,3,0,3,0,0,0,0,0}, {},
    3,-1,0,0, 0,4,2,4, 4,0,-1,-1, { 2,2,1,3,1 }, { 0,5,0,3 }, 3 },
  { "wow",     {}, {0,0,0,3,3,0,0,0,0,0},
      1,3,2,1, 1,3,2,5, 6,0,-1,-1, { 2,5,0,3,0 }, { 3,5,0,4 }, 3 },
  { "wow2", {2,4,2,0,1,3,1,0,1,2,1}, {0,0,0,0,0,2,0,0,0,0},
    3,3,2,1, 0,3,3,3, 4,0,-1,-1, { 2,4,0,4,0 }, { 0,3,1,3 }, 2 },
  { "church bell", {0,1,0,3,0,1,0,0,2,0}, {0,0,0,0,0,0,0,0,3,0},
    3,3,2,1, 0,4,2,4, 4,0,-1,-1, { 2,1,1,5,1 }, { 0,5,0,4 }, 4 },
  { "perlin noise", {}, {},
      4,-1,0,0, 0,2,2,2, 4,0,-1,-1, { 2,4,1,3,1 }, { 0,3,1,3 }, 2 },
  { "r-formant", {}, {},
    5,-1,0,0, -1,1,0,0, 4,0,0,-1, { 2,2,1,3,0 }, { 0,3,1,3 }, 3 },
  { "c-formant", {0,4,0,0,0,0,0,0,0,4,0}, {},
    5,-1,0,0, -1,2,1,4, 4,0,1,-1, { 2,3,1,3,1 }, { 0,3,1,3 }, 3 },
  { "formant bass", {}, {},
    5,-1,0,0, 0,3,2,4, 4,0,0,-1, { 2,3,1,3,0 }, { 1,5,0,3 }, 2 },
  { "karp-s rand", {}, {},
    6,-1,0,0, -1,0,0,0, 4,0,0,-1, { 2,2,1,3,0 }, { 0,3,1,3 }, 3 },
  { "karp-s sines", {}, {},
    6,-1,0,0, -1,0,0,0, 4,0,1,-1, { 2,2,1,3,0 }, { 0,3,1,3 }, 3 },
  { "karp-s wow", {}, {},
    6,-1,0,0, 2,0,2,4, 4,0,0,-1, { 2,5,1,3,2 }, { 0,3,1,3 }, 3 },
  { "test",    {0,3,0,3,0,2,0,0,0,1}, {},
      3,0,2,2, 0,2,2,4, 4,0,-1,-1, { 2,2,1,3,0 }, { 0,3,1,3 }, 3 },
  { "test formant", {0,4,0,0,0,0,0,0,0,0,0}, {},
    5,-1,0,0, -1,0,0,0, 4,0,1,-1, { 2,3,1,3,1 }, { 0,3,1,3 }, 4 }
};

typedef struct {
  float
    eg1_val,
    eg2_val,
    freq,
    freq_scale,
    freq_track,
    detune_val,
    filt_cutoff,
    d1,d2,d3,d4,
    velocity,
    lfo1_tmp,
    mod_amp_1,
    osc1_pos, osc2_pos,
    f1,f2,f3,f4,
    a1,a2,a3,a4,
    ks_buffer[stringlen_max],
    stringcutoff;
  short
    eg1_phase,//=Idle,
    eg2_phase,//=Decay,
    midi_nr, //=60;
    stringpos,
    stringlen;
} Values;
static Values vals[voices_max];

typedef struct {
  bool dir;
  float lfo_pos;
} Lfo;
static Lfo lfo1;

static void set_lfo(Values *v,int i) {
  float mix=(float)i / nr_samples;
  v->lfo1_tmp=vcom.lfo1[0] * (1 - mix) + vcom.lfo1[1] * mix;
  v->mod_amp_1=v->lfo1_tmp * vcom.trem_osc;
}

static float filter_24db(Values *v, float input) {
// from: Virtual Analog (VA) Filter Implementation and Comparisons, by Will Pirkle
  if (patch.filt_mode==-1) return input;
  const int mode=patch.filt_mode;
  float
    g=v->filt_cutoff,
    H_P = (input - (vcom.qres + g) * v->d1 - v->d2)/(1 + vcom.qres * g + g * g),
    B_P = g * H_P + v->d1,
    L_P = g * B_P + v->d2;
  v->d1 = g * H_P + B_P;
  v->d2 = g * B_P + L_P;
  float out1= mode==eLP ? L_P : mode==eBP ? B_P : mode==eHP ? H_P : 0;
  H_P = (out1 - (vcom.qres + g) * v->d3 - v->d4)/(1 + vcom.qres * g + g * g),
  B_P = g * H_P + v->d3,
  L_P = g * B_P + v->d4;
  v->d3 = g * H_P + B_P;
  v->d3 /= (1 + v->d3 * v->d3 * 0.02); // soft clip
  v->d4 = g * B_P + L_P;
  return mode==eLP ? L_P : mode==eBP ? B_P : mode==eHP ? H_P : 0;
}

static const float freqs[13]= {
// C,    Cis,   D,     Dis,   E,     F,     Fis,   G,     Gis,   A*2,   Bes*2,   B*2,     C*2
  523.3, 554.4, 587.3, 622.3, 659.3, 698.5, 740.0, 784.0, 830.6, 440*2, 466.2*2, 493.9*2, 523.3*2
};

float
  F1[] = { 730, 200, 400, 250, 190, 350, 550, 550, 450 },
  A1[] = { 1.0, 0.5, 1.0, 1.0, 0.7, 1.0, 1.0, 0.3, 1.0 },
  F2[] = { 1090, 2100, 900, 1700, 800, 1900, 1600, 850, 1100 },
  A2[] = { 2.0, 0.5, 0.7, 0.7, 0.35, 0.3, 0.5, 1.0, 0.7 },
  F3[] = { 2440, 3100, 2300, 2100, 2000, 2500, 2250, 1900, 1500 },
  A3[] = { 0.3, 0.15, 0.2, 0.4, 0.1, 0.3, 0.7, 0.2, 0.2 },
  F4[] = { 3400, 4700, 3000, 3300, 3400, 3700, 3200, 3000, 3000 },
  A4[] = { 0.2, 0.1, 0.2, 0.3, 0.1, 0.1, 0.3, 0.2, 0.3 };

void init_formant(Values *v) {
  int F=rand() % 8;
  v->f1=F1[F]; v->f2=F2[F]; v->f3=F3[F]; v->f4=F4[F];
  v->a1=A1[F]; v->a2=A2[F]; v->a3=A3[F]; v->a4=A4[F];
  //LOG("F=%d (%.1f %.1f) (%.1f %.1f) (%.1f %.1f) (%.1f %.1f)",
  // F,v->f1,v->a1,v->f2,v->a2,v->f3,v->a3,v->f4,v->a4);
}

void init_custom_formant(Values *v) {
  short *form=harm_win1.pitch_arr;
  int nr=0;
  v->f1=v->f2=v->f3=v->f4=0;
  v->a1=v->a2=v->a3=v->a4=-1;
  for (int i=0; i<num_pitch_max && nr<4; ++i) {
    if (form[i]) {
      float freq=(i+1)*300.,
            ampl=form[i]/4.;
      switch (++nr) {
        case 1: v->f1=freq;  v->a1=ampl; break;
        case 2: v->f2=freq;  v->a2=ampl; break;
        case 3: v->f3=freq;  v->a3=ampl; break;
        case 4: v->f4=freq;  v->a4=ampl; break;
      }
    }
  }
  //LOG("(%.1f %.1f) (%.1f %.1f) (%.1f %.1f) (%.1f %.1f)",
  // v->f1,v->a1,v->f2,v->a2,v->f3,v->a3,v->f4,v->a4);
}

float rand_val() { return rand() / (float)RAND_MAX * 2.0 - 1.0; }

void init_string(Values *v) {
  v->stringlen=lrint(sample_rate / v->freq);
  v->stringpos=0;
  v->stringcutoff = tw_fminmax(0.5,v->freq_track/2,0.8);
  if (patch.instr_mode<0)
    patch.instr_mode=Mode0; // menu not updated
  switch (patch.instr_mode) {
    case Mode0: {
      float randval=0;
      for (int i = 0; i < v->stringlen; ++i) {
        if (i % 10 == 0)
          randval= rand_val();
        v->ks_buffer[i] = randval;
      }
    }
    break;
    case Mode1: {
      for (int i = 0; i < v->stringlen; ++i) {
        float f=PI2 * i / v->stringlen;
        v->ks_buffer[i]=(sinf(f)+sinf(2*f)+sinf(3*f)+sinf(8*f)) * 0.5;
      }
    }
  }
}

static float mnr2freq(uint8_t mnr) {
  return freqs[mnr%12] * ((1<<(mnr/12))/64.);
}

static void set_pfreq(Values *v, uint8_t mnr, bool down, float key_veloc) {
  //LOG("set_pfreq: down=",down," mnr=",mnr);
  if (down) {
    v->midi_nr=mnr;
    v->freq=mnr2freq(mnr);
    v->freq_scale=v->freq / sample_rate;
    v->freq_track=powf(0.5,(60-mnr)/20.); // mnr=60-24: 0.44, mnr=60+24: 2.3
    // LOG("mnr=%d freq=%.1f f_t=%.2f",mnr,v->freq,v->freq_track);
    v->eg1_val=0;
    v->velocity=key_veloc;
    v->eg1_phase=v->eg2_phase=Attack;
    v->filt_cutoff=v->freq_scale * PI2 * vcom.fixed_cutoff;
    if (patch.wform_osc1==Formant) {
      if (patch.instr_mode<0)
        patch.instr_mode=Mode0; // menu not updated
      switch (patch.instr_mode) {
        case Mode0: init_formant(v); break;
        case Mode1: init_custom_formant(v); break;
      }
    }
    else if (patch.wform_osc1==KarpStrong && v->freq>ks_min_freq) {
      init_string(v);
    }
  }
  else
    v->eg1_phase=v->eg2_phase=Release;
}

static int8_t find_free_voice(uint8_t midi_nr) {
  uint8_t v;
  for (v=0;v<voices_max;v++) {
    if (vals[v].eg1_phase==Idle) return v;
  }
  for (v=0;v<voices_max;v++) {
    if (vals[v].eg1_phase==Release) return v;
  }
  mnr2vnr[midi_nr]=-1; // disable noteOff()
  return -1;
}

void keyb_noteOn(uint8_t midi_nr,uint8_t velocity) {
  int8_t v=find_free_voice(midi_nr);
  //LOG("noteOn: mnr=",midi_nr," veloc=",velocity," v=",v);
  if (v>=0) {
    mnr2vnr[midi_nr]=v;
    set_pfreq(vals+v,midi_nr,true,velocity/128.);
  }
}

void keyb_noteOff(uint8_t midi_nr) {
  if (mnr2vnr[midi_nr] >= 0) {
    set_pfreq(vals+mnr2vnr[midi_nr],midi_nr,false,0);
  }
}

static float interpol(const float *arr, float fi, const int arr_len) {
    const int ind1=fi,
              ind2=tw_min(ind1+1,arr_len-1);
    float mix=fi - (float)ind1;
    return arr[ind1] * (1. - mix) + arr[ind2] * mix;
}

static void modWheel(uint8_t val) {   // modulation wheel
  float fval=val / 128. * modwheel_len;
  int ind=lrint(fval);
  if (ind!=patch.filt_coff) {
    patch.filt_coff=tw_minmax(0,ind,modwheel_len-1);
    filt_coff.cmd();
    hor_slider_draw(&filt_coff);
    fflush(stdout); // else no complete draw
  }
  vcom.fixed_cutoff=interpol(coff, fval, modwheel_len);
}

static void amplCtrl(uint8_t val) { // volume knob
  float fval=val * ampl_len / 128.;
  int ind=fval;
  if (ind!=patch.out_vol) {
    patch.out_vol=tw_minmax(0,ind,ampl_len-1);
    volume.cmd();
    vert_slider_draw(&volume);
    fflush(stdout); // else no complete draw
  }
  vcom.out_vol=interpol(outvol, fval, ampl_len);
}

static void pitchWheel(uint8_t val) {
  int ind=val * pitwheel_len / 128;
  if (ind!=patch.trem_osc) {
    patch.trem_osc=tw_minmax(0,ind,pitwheel_len-1);
    trem_osc.cmd();
    hor_slider_draw(&trem_osc);
    fflush(stdout);
  }
}

void stop_conn_mk() {
  mkb_connected=false;
  usleep(300000);
  if (mkb_connected) {
    checkbox_draw(&connect_mkb);
    fflush(stdout);
  }
}

static float get(Lfo *lfo, float freq) {
  float dir= lfo->dir ? 1.0 : -1.0;
  lfo->lfo_pos += dir * 8 * freq / sample_rate * nr_samples;
  if (lfo->lfo_pos>2) lfo->dir=false;
  else if (lfo->lfo_pos<-2) lfo->dir=true;
  return tw_fminmax(-1,lfo->lfo_pos,1);
}

static void slow_add(float *act_val,const float nom_val,const float add_val) {
  if (*act_val>nom_val+add_val) *act_val-=add_val;
  else if (*act_val<nom_val-add_val) *act_val+=add_val;
}

static void once_per_frame() {
  vcom.lfo1[0]=vcom.lfo1[1];
  vcom.lfo1[1]=get(&lfo1,vcom.trem_freq);
}

static float sawtooth(float pos) {
  return (sinf(pos)+sinf(pos*2)+sinf(pos*3)/1.5+sinf(pos*4)/2+sinf(pos*5)/2.5) * 0.5;
}
static float square(float pos) {
  return (sinf(pos)+0.3*sinf(pos*3)+0.2*sinf(pos*5)+0.15*sinf(pos*7)) * 0.5;
}

float gradients[grad_max];

float noise(float z) {
  float
    g1, g2, d2,
    result = 0;
  uint
    z1 = (uint)z % grad_max,
    z2 = (z1 + 1) % grad_max;
  float dz1 = z - z1,
        dz2 = dz1 - 1.0;
  static int count;
  if (++count%100==0)
    gradients[z1] = tw_fminmax(-1., gradients[z1] + rand_val() * 0.1, 1.);
  g1 = gradients[z1];
  g2 = gradients[z2];

  d2 = 1.0 - dz1 * dz1;
  d2 = d2 * d2 * d2 * d2;
  result += d2 * g1 * dz1;

  d2 = 1.0 - dz2 * dz2;
  result += d2 * d2 * d2 * d2 * g2 * dz2;

  return result;
}

static float fbm(Values *v, float pos) {
  float result = 0,
        multiplier = 1.0;
  for (int octave = 0; octave < 4; ++octave) {
    float value = noise(pos * (1 << octave)) * multiplier;
    result += value;
    multiplier *= 2; // persistence;
  }
  return result * 0.3;
}
static float harms(float pos,short *arr) { // arr: 0 -> 4
  static float ampl[]={0, 0.25, 0.5, 1, 1.5};
  float ret=0.;
  for (int pit=0;pit<num_pitch_max;++pit) {
    if (!arr[pit]) continue;
    ret += ampl[arr[pit]] * sinf(pos * (pit+1) * 0.5);
  }
  return ret * 0.5;
}

float fonc_formant(float p, const int I) { // p: -M_PI -> M_PI, I: >20 -> 2
  float a =1,
        phi = 0;
  const int hmax = tw_min(I, 20);
  //if (++cnt%100000==0) fprintf(stderr,"p=%.2f I=%d hmax=%d\n",p,I,hmax);
  for (int h = 1; h < hmax; h++) {
    phi += 0.5 * p;
    float hann = 1. + cosf(0.5 * M_PI * h / hmax);
    a += hann * cosf(phi);
    //a += hann * (expf(-h) + 0.3) * cosf(phi);
    //a += hann * (1. / h + 0.3) * cosf(phi);
  }
  return a;
}

float porteuse(const float h, const float p) {// if (++cnt%5000==0) LOG("h=%.2f p=%.2f",h,p);
  float h0 = floor(h),          //integer and
        hf = h - h0;            //decimal part of harmonic number.
  // two carriers.
  float Porteuse0 = cosf(p * h0),
        Porteuse1 = cosf(p * (h0+1));
  // crossfade between the two carriers.
  return Porteuse0 + hf * (Porteuse1 - Porteuse0);
}

float formant(Values *v, float pos) {
  const float freq=v->freq/1.5/v->freq_track; // warmer sound
  //const float freq=v->freq/2; // sharper sound
  float out=0;

  if (v->a1>0) out += v->a1 * freq / v->f1 * fonc_formant(pos, 500 / freq) * porteuse(v->f1 / freq, pos);
  if (v->a2>0) out += v->a2 * freq / v->f2 * fonc_formant(pos, 600 / freq) * porteuse(v->f2 / freq, pos);
  if (v->a3>0) out += v->a3 * freq / v->f3 * fonc_formant(pos, 800 / freq) * porteuse(v->f3 / freq, pos);
  if (v->a4>0) out += v->a4 * freq / v->f4 * fonc_formant(pos, 1500 / freq) * porteuse(v->f4 / freq, pos);
/*
  if (v->a1>0.01) out += v->a1 * freq / v->f1 * fonc_formant(pos, 500 / freq) * porteuse(v->f1 / freq, pos);
  if (v->a2>0.01) out += v->a2 * freq / v->f2 * fonc_formant(pos, 700 / freq) * porteuse(v->f2 / freq, pos);
  if (v->a3>0.01) out += v->a3 * freq / v->f3 * fonc_formant(pos, 1000 / freq) * porteuse(v->f3 / freq, pos);
  if (v->a4>0.01) out += v->a4 * freq / v->f4 * fonc_formant(pos, 1500 / freq) * porteuse(v->f4 / freq, pos);
*/
  return out;// * 0.5;
}

float ks(Values *v) {
  float *p=v->ks_buffer + v->stringpos;
  const float damp=v->stringcutoff;
  if (v->stringpos>0)
    *p = *p * damp + *(p - 1) * (1. - damp);
  else
    *p = *p * damp + v->ks_buffer[v->stringlen-1] * (1. - damp);
  if (++v->stringpos >= v->stringlen)
    v->stringpos = 0;
  return *p;
}

static float oscillator(Values  *v) {
  float pos1=v->osc1_pos,
        pos2=v->osc2_pos;
  pos1 += v->freq_scale;
  if (pos1 > 1) pos1 -= 2;
  pos2 += v->freq_scale + vcom.detune;
  if (pos2 > 1) pos2 -= 2;
  v->osc1_pos=pos1; v->osc2_pos=pos2;
  float val1=0., val2=0.,
        mix=vcom.mix_1_2;
  switch (patch.wform_osc1) {
    case Sine: val1=sinf(pos1*PI2); break;
    case Sawtooth: val1=sawtooth(pos1*PI2); break;
    case Square: val1=square(pos1*PI2); break;
    case Harms: val1=harms(pos1*PI2,patch.pitch_arr1); break;
    case Perlin: val1=fbm(v, pos1 * 0.5 + 0.5); break; // pos: 0 -> 1, half freq
    case Formant: val1=formant(v, pos1 * PI2 * 0.5); break;  // pos: -M_PI -> M_PI, half freq
    case KarpStrong:
      if (v->freq>ks_min_freq)
        val1=ks(v); // pos not used
      break;
  }
  switch (patch.wform_osc2) {
    case Sine: val2=sinf(pos2*PI2); break;
    case Sawtooth: val2=sawtooth(pos2*PI2); break;
    case Square: val2=square(pos2*PI2); break;
    case Harms: val2=harms(pos2*PI2,patch.pitch_arr2); break;
  }
  if (patch.mod_mode==eAM) val1 *= (1 - vcom.amfm_2_1/2) + vcom.amfm_2_1 * val2; // vcom.amfm_2_1: 0 -> 2
  else if (patch.mod_mode==eFM) v->osc1_pos += vcom.amfm_2_1 * val2 * 0.01;
  if (vcom.trem_enabled)
    return (val1 * (1. - mix) * (1 + v->mod_amp_1) + val2 * mix) * v->eg1_val;
  return (val1 * (1. - mix) + val2 * mix) * v->eg1_val;
}

static void fill_buffer(Values *v, float *buffer) {
  for (int i=0;i<nr_samples;++i) {
    if (i%4==0) {
      switch (v->eg1_phase) {
        case Idle: return;
        case Attack:
          v->eg1_val = (v->eg1_val-1.) * (1.-vcom.attack) + 1.;
          if (v->eg1_val>0.95) v->eg1_phase=Decay;
          break;
        case Decay:
          v->eg1_val = (v->eg1_val-vcom.sustain) * (1.-vcom.decay * v->freq_track) + vcom.sustain;
          break;
        case Release:
          if (v->eg1_val<0.02) v->eg1_val *= 0.99;
          else v->eg1_val *= 1. - vcom.release;
          if (v->eg1_val<0.001) v->eg1_phase=Idle;
          break;
        default: break;
      }
      set_lfo(v,i);
      start:
      switch (v->eg2_phase) {
        case Attack:
          v->eg2_val=vcom.start_lev_f;
          if (patch.adsr_f_data.y0 == patch.adsr_f_data.y1) { v->eg2_phase=Sustain; goto start; }
          else { v->eg2_phase=Decay; goto start; }
        case Decay: {
          float lev=vcom.sustain_f;
          v->eg2_val = (v->eg2_val-lev) * (1. - vcom.decay_f * v->freq_track) + lev;
          if (fabs(v->eg2_val-lev) < 0.01) v->eg2_phase=Sustain;
          }
          break;
        case Release: {
            float lev=vcom.end_lev_f;
            v->eg2_val = (v->eg2_val-lev) * (1. - vcom.release_f) + lev;
            if (fabs(v->eg2_val - lev) < 0.02) v->eg2_phase=Idle;
          }
          break;
        default: break;
      }
      float tmp=fmax(0.005,
        v->freq_scale * PI2 *
        (vcom.fixed_cutoff + (v->eg2_val - 1) * vcom.eg2_mod_filt)  // eg2_val = 0 -> 2
      );
      slow_add(&v->filt_cutoff,tmp,v->freq_scale*0.01);
    }
    if (v->eg1_phase==Idle) continue;
    buffer[i] += filter_24db(v,oscillator(v)) * v->velocity * vcom.out_vol * 0.3;
  }
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
    case 0xb0:
      switch (midi_mes[1]) {
        case 1:
          modWheel(midi_mes[2]); // 0 -> 128
          break;
        case 7:
          amplCtrl(midi_mes[2]);
          break;
      }
      break;
    case 0xe0:
      if (midi_mes[1]==0)
        pitchWheel(midi_mes[2]);
      break;
    default: break;
  }
}

static void split(float *in_buf,short *out_buf) {
  static float lp1,lp2;
  const float
    lpf1=expf(-PI2 * 20. / sample_rate),
    lpf2=expf(-PI2 * 1000. / sample_rate);
  for (int i=0;i<nr_samples;++i) {
    lp1 = (1.- lpf1) * in_buf[i] + lpf1 * lp1;
    float hp1=in_buf[i] - lp1; // highpass 20Hz
    lp2 = (1.- lpf2) * hp1 + lpf2 * lp2;
    out_buf[2*i]=tw_minmax(-32000, 32000 * lp2, 32000);
    out_buf[2*i+1]=tw_minmax(-32000,32000 * (hp1 - lp2),32000);
  }
}

static void* play(void* d) {
  once_per_frame();
  short out_buf[2*nr_samples];
  float buffer[nr_samples];
  while (true) {
    if (!mkb_connected) {
      snd_close();
      break;
    }
    once_per_frame();
    bzero(buffer,sizeof(buffer));
    for (int8_t v=0; v<voices_max; ++v)
      fill_buffer(vals+v,buffer);
    split(buffer,out_buf);
/*
    for (int i=0;i<nr_samples;++i) {
      short val=32000 * tw_fminmax(-1., buffer[i], 1.);
      out_buf[2*i]=out_buf[2*i+1]=val;
    }
*/
    if (mkb_connected) {
      if (snd_write(out_buf,nr_samples)<0) {
        mkb_connected=false; break;
      }
      if (mkb_connected && recording) {
        dump_wav(out_buf,nr_samples);
      }
    }
  }
  return 0;
}

static void draw_harm_line(tw_Custom *harm,short h_nr) {
  Harmonics *env=(Harmonics*)harm;
  Rect area=harm->wb.area;
  int xpos=area.x + h_nr * h_xgrid - 1;
  tw_vline(xpos, area.y+1 , area.h-1, col_hbg); // erase old line
  int h_amp = env->pitch_arr[h_nr-1];
  if (h_amp>0) {
    tw_thin_vline(xpos, area.y + area.h - h_amp, h_amp, col_dgrey,col_hbg);
  }
}
static void (^harm_draw)(tw_Custom*)=
  ^(tw_Custom *h) {
    for (int i=0;i<num_pitch_max;++i)
      draw_harm_line(h,i+1);
};

static void oload adsr_init(Adsr_amp* aa) {
  aa->pt_ind=0;
  Adsr_amp_data *ad=&patch.adsr_a_data;
  aa->x1=&ad->x1;
  aa->x2=&ad->x2;
  aa->y2=&ad->y2;
  aa->x4=&ad->x4;
  Point pts[]={
    {0,0},
    {*aa->x1,1},
    {*aa->x1+*aa->x2,*aa->y2},
    {2 * subdiv + s_len,*aa->y2},
    {pts[3].x + *aa->x4,0}
  };
  memcpy(aa->pt,pts,sizeof(aa->pt));
  aa->cmd=^{
    float eg1[6]={ 0.1, 0.01, 0.003, 0.001, 0.0003, 0.0001 };
    float sus[2]={ 0, 1. };
    vcom.attack=eg1[patch.adsr_a_data.x1];
    vcom.decay=eg1[patch.adsr_a_data.x2];
    vcom.sustain=sus[patch.adsr_a_data.y2];
    vcom.release=eg1[patch.adsr_a_data.x4];
  };
  aa->cmd();
}

static void oload adsr_init(Adsr_filt* af) {
  af->pt_ind=0;
  Adsr_filt_data *ad=&patch.adsr_f_data;
  af->y0=&ad->y0;
  af->x1=&ad->x1;
  af->y1=&ad->y1;
  af->x3=&ad->x3;
  af->y3=&ad->y3;
  Point pts[]={
    {0,*af->y0},
    {*af->x1,*af->y1},
    {subdiv + s_len,*af->y1},
    {subdiv + s_len + *af->x3, *af->y3}
  };
  memcpy(af->pt,pts,sizeof(af->pt));
  af->do_draw=true;
  af->cmd=^{
    float eg2[6]={ 0.03, 0.01, 0.003, 0.001, 0.0003, 0.0002 };
    vcom.start_lev_f=patch.adsr_f_data.y0;
    vcom.decay_f=eg2[patch.adsr_f_data.x1];
    vcom.sustain_f=patch.adsr_f_data.y1;
    vcom.release_f=eg2[patch.adsr_f_data.x3];
    vcom.end_lev_f=patch.adsr_f_data.y3;
  };
  af->cmd();
}

static void (^harm_mouse)(tw_Custom*,short,short,short,short)=
  ^(tw_Custom* harmon,short ev_x,short ev_y,short mode,short button) {
    if (mode!=Mouse_down) return;
    Harmonics *h=(Harmonics*)harmon;
    if (button==But_left) {
      int h_nr = tw_div(ev_x+1,h_xgrid);
      if (h_nr<1 || h_nr>num_pitch_max) return;
      h->pitch_arr[h_nr-1] = 4 - ev_y;
      draw_harm_line(harmon,h_nr);
    }
  };

static void (^adsr_amp_draw)(tw_Custom*)=^(tw_Custom *bw) {
  Rect area=bw->wb.area;
  uint8_t(^xpos)(uint8_t)=^uint8_t(uint8_t valx){ return area.x + valx; };
  uint8_t(^ypos)(uint8_t)=^uint8_t(uint8_t valy){ return area.y + area.h - valy - 1; };
  tw_box(area,col_grey);
  tw_hline(area.x,area.w,area.y+area.h,0); // clear value numbers
  Adsr_amp *env=(Adsr_amp*)bw;
  for (int i=0;i<5;++i) {
    Point pnt={ xpos(env->pt[i].x),ypos(env->pt[i].y) };
    switch (i) {
      case 0: tw_set_cell(pnt.x,pnt.y,0x25fc,col_dblue,col_grey,1); break;
      case 1: case 2: case 3: case 4:
        tw_set_cell(pnt.x,pnt.y,0x25fc,col_red,col_grey,1); break;
    }
    switch (i) {
      case 1: case 2: case 4: // print values
        tw_set_cell(pnt.x,ypos(-1),'0'+env->pt[i].x-env->pt[i-1].x,0,0,1);
    }
  }
  tw_hline(area.x, area.w, area.y-1, col_lbrown);  // title
  tw_print(area.x+area.w/2, area.y-1, col_lbrown, "ADSR (ampl)", MidAlign);
};

static void(^adsr_filt_draw)(tw_Custom*)=^(tw_Custom *bw) {
  Rect area=bw->wb.area;
  uint8_t(^xpos)(uint8_t)=^uint8_t(uint8_t valx){ return area.x + valx; };
  uint8_t(^ypos)(uint8_t)=^uint8_t(uint8_t valy){ return area.y + area.h - valy - 1; };
  tw_box(area,col_grey);
  tw_hline(area.x,area.w,area.y+area.h,0); // clear value numbers
  Adsr_filt *env=(Adsr_filt*)bw;  
  if (env->do_draw) {
    tw_hline(area.x,area.w,area.y+area.h,0); // clear value numbers
    for (int i=0;i<4;++i) {
      Point pnt={ xpos(env->pt[i].x),ypos(env->pt[i].y) };
      tw_set_cell(pnt.x,pnt.y,0x25fc,col_red,col_grey,1);
      switch (i) {
        case 0: case 1: case 3:
          tw_set_cell(pnt.x,ypos(-1),'0'+env->pt[i].x-env->pt[i-1].x,0,0,1);
      }
    }
    tw_hline(area.x, area.w, area.y-1, col_lbrown);  // title
    tw_print(area.x+area.w/2, area.y-1, col_lbrown, "ADSR (filter)", MidAlign);
  }
};

static void (^adsr_amp_mouse)(tw_Custom*,short,short,short,short)=
     ^(tw_Custom* cust,short x,short y,short mode,short button) {
  //LOG("adsr_mouse: x=%d, y=%d, mode=%d, but=%d",x,y,mode,button);
  Adsr_amp *env=(Adsr_amp*)cust;
  switch (mode) {
    case Mouse_down: {
        env->pt_ind=0;
        for (int i=1;i<5;++i) {
          if (x==env->pt[i].x) {
            env->pt_ind=i;
            env->lst_x=x; env->lst_y=y;
            break;
          }
        }
      }
      break;
    case Mouse_move: {
      int8_t difx=x-env->lst_x,
             dify=y-env->lst_y,
             prev_dif;
      if (y!=env->lst_y) {
        switch (env->pt_ind) {
          case 2:
          case 3:
            env->pt[2].y=tw_minmax(0,env->pt[2].y-dify,1);
            env->pt[3].y=env->pt[2].y;
            break;
        }
      }
      if (x!=env->lst_x) {
        switch (env->pt_ind) {
          case 1: 
            prev_dif=env->pt[2].x-env->pt[1].x;
            env->pt[1].x=tw_minmax(0,env->pt[1].x+difx,subdiv);
            env->pt[2].x=env->pt[1].x+prev_dif;
            break;
          case 2:
            env->pt[2].x=tw_minmax(env->pt[1].x+1,env->pt[2].x+difx,env->pt[1].x+subdiv);
            break;
          case 3:
            break;
          case 4:
            env->pt[4].x=tw_minmax(env->pt[3].x+1,env->pt[4].x+difx,env->pt[3].x+subdiv);
            break;
        }
      }
      if (x!=env->lst_x || y!=env->lst_y) {
        env->lst_x=x;
        env->lst_y=y;
        adsr_amp_draw(&env->adsr);
      }
    }
    break;
  case Mouse_up:
    if (env->pt_ind==0) return;
    *env->x1=env->pt[1].x;
    *env->x2=env->pt[2].x - *env->x1;
    *env->y2=env->pt[2].y;
    *env->x4=env->pt[4].x-env->pt[3].x;
    env->pt_ind=0;
    env->cmd();
    break;
  } 
};

static void (^adsr_filt_mouse)(tw_Custom*,short,short,short,short)=
      ^(tw_Custom* cust,short x,short y,short mode,short button) {
  Adsr_filt *env=(Adsr_filt*)cust;
  switch (mode) {
    case Mouse_down:
      env->pt_ind=-1;
      for (int i=0;i<4;++i) {
        if (x==env->pt[i].x) {
          env->pt_ind=i;
          env->lst_x=x; env->lst_y=y;
          break;
        }
      }
      break;
    case Mouse_move: {
        int difx=x-env->lst_x,
            dify=y-env->lst_y;
        if (y!=env->lst_y) {
          switch (env->pt_ind) {
            case 0:
              env->pt[0].y=tw_minmax(0,env->pt[0].y-dify,2);
              break;
            case 1: case 2:
              env->pt[1].y=env->pt[2].y=tw_minmax(0,env->pt[1].y-dify,2);
              break;
            case 3:
              env->pt[3].y=tw_minmax(0,env->pt[3].y-dify,2);
              break;
            default: break;
          }
        }
        if (x!=env->lst_x) {
          switch (env->pt_ind) {
            case 1: 
              env->pt[1].x=tw_minmax(1,env->pt[1].x+difx,subdiv);
              break;
            case 3:
              env->pt[3].x=tw_minmax(env->pt[2].x+1, env->pt[3].x+difx, env->pt[2].x+subdiv);
              break;
            default: break;
          }
        }
        if (x!=env->lst_x || y!=env->lst_y) {
          env->lst_x=x;
          env->lst_y=y;
          adsr_filt_draw(&env->adsr);
        }
      }
      break;
    case Mouse_up:
      if (env->pt_ind<0) return;
      *env->y0=env->pt[0].y;
      *env->x1=env->pt[1].x;
      *env->y1=env->pt[1].y;
      *env->x3=env->pt[3].x - env->pt[2].x;
      *env->y3=env->pt[3].y;
      env->pt_ind=-1;
      env->cmd();
      break;
  } 
};

static void harm_init(Harmonics *h,short pitch_arr[]) {
  h->pitch_arr=pitch_arr;
}

static void upd_titles() { // update sliders
  mix_1_2.cmd();
  detune.cmd();
  filt_coff.cmd();
  filt_q.cmd();
  eg2_mod_filt.cmd();
  trem_osc.cmd();
  amfm_2_1.cmd();
  volume.cmd();
  switch (patch.wform_osc1) {
    case Formant: instr_mode.labels[0]="rand"; instr_mode.labels[1]="cust"; break;
    case KarpStrong: instr_mode.labels[0]="noise"; instr_mode.labels[1]="sines"; break;
    default: instr_mode.labels[0]=instr_mode.labels[1]="";
  }
  harm_init(&harm_win1,patch.pitch_arr1);
  harm_init(&harm_win2,patch.pitch_arr2);
  adsr_init(&adsr_filt);
  adsr_init(&adsr_amp);
}
static void* connect_mkeyb(void* d) {
  uint8_t mbuf[3];
  //mkb_connected=true; <-- set allready
  while (read_midi_bytes(mbuf)) {
    if (!mkb_connected) { close_midi_kb(); return 0; } // <-- if ctrl_C pressed
    eval_midi_mes(mbuf); 
  }
  mkb_connected=false; // <-- if keyboard was disconnected
  //checkbox_draw(connect_mkb);
  close_midi_kb();
  return 0;
}

int main(int argc,char **argv) {
  read_screen_dim(&tw_colums,&tw_rows);
  if (tw_colums<95 || tw_rows<12) {
    printf("window = %d x %d (must be 95 x 12)\n",tw_colums,tw_rows);
    return 1;
  }
  if (snd_init(sample_rate,nr_samples,2) < 0) {
    puts("alsa init failed");
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
  patch=bi_patches[0];
  for (int z = 0; z < grad_max; ++z)
    gradients[z] = rand_val();

  tw_key_event=^(uint16_t ch) {
    LOG("key=%c",ch);
    if (ch=='p') patch_report();
  };

  for (int i=0;i<voices_max;++i) {
    Values *v=vals+i;
    v->eg1_phase=Idle;
    v->eg2_phase=Decay;
    v->midi_nr=60;
  }

  tw_menu_init(
    &wform_osc1,
    (Rect){1,1,4,8},
    &patch.wform_osc1,
    "osc1",
    (char*[]){ "","","","","perlin","formant","karp-strong", 0 },
    ^{ if (wform_osc1.prev_val==patch.wform_osc1) patch.wform_osc1=-1; }
  );

  tw_menu_init(
    &wform_osc2,
    (Rect){6,1,4,5},
    &patch.wform_osc2,
    "osc2",
    (char*[]){ "sine","sawtooth","square","harm's",0 },
    ^{ if (wform_osc2.prev_val==patch.wform_osc2) patch.wform_osc2=-1; }
  );
  wform_osc1.style=wform_osc2.style=Compact;

  tw_hor_slider_init(
    &mix_1_2,
    (Rect){1,11,0,0},
    4,
    &patch.mix_1_2,
    ^{ float mix[5]={0.0,0.25,0.5,0.75,1.0};
       vcom.mix_1_2=mix[patch.mix_1_2];
       sprintf(mix_1_2.title,"mix1/2=%-3g",vcom.mix_1_2);
     }
  );
  tw_checkbox_init(
    &record,
    (Rect){72,11,0,0},
    &recording,
    "record",
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

  harm_init(&harm_win1,patch.pitch_arr1);
  harm_init(&harm_win2,patch.pitch_arr2);

  tw_custom_init(
    &harm_win1.harms,
    (Rect){17,1,num_pitch_max,5},
    harm_draw,
    harm_mouse
  );
  tw_custom_init(
    &harm_win2.harms,
    (Rect){17,6,num_pitch_max,5},
    harm_draw,
    harm_mouse
  );
  Rect r1 = harm_win1.harms.wb.area,
       r2 = harm_win2.harms.wb.area;
  tw_print(r1.x,r1.y,col_lbrown,"harmonics    ",0);
  for (int i=1;i<=num_pitch_max;++i) {
    if (i%2==0) {
      char buf[2]; sprintf(buf,"%d",i/2);
      tw_print(r1.x - 1 + i * h_xgrid, r1.y+r1.h,0,buf,0);
      tw_print(r2.x - 1 + i * h_xgrid, r2.y+r2.h,0,buf,0);
    }
  }

  tw_custom_init(
    &adsr_amp.adsr,
    (Rect){62,2,18,2},
    adsr_amp_draw,
    adsr_amp_mouse
  ); 

  tw_menu_init(
    &instr_mode,
    (Rect){32,9,12,2},
    &patch.instr_mode,
    "mode",
    (char*[]){ "","",0 },
    ^{ if (instr_mode.prev_val==patch.instr_mode) patch.instr_mode=-1; }
  );

  tw_menu_init(
    &mod_mode,
    (Rect){32,12,8,2},
    &patch.mod_mode,
    "modul",
    (char*[]){ "AM","FM",0 },
    ^{ if (mod_mode.prev_val==patch.mod_mode) patch.mod_mode=-1; }
  );

  tw_menu_init(
    &filter_mode,
    (Rect){32,6,12,2},
    &patch.filt_mode,
    "filter",
    (char*[]){ "LP","BP","HP",0 },
    ^{ if (filter_mode.prev_val==patch.filt_mode) patch.filt_mode=-1;
       adsr_filt.do_draw= patch.filt_mode!=-1;
       adsr_filt_draw(&adsr_filt.adsr);
     }
  );

  tw_hor_slider_init(
    &detune,
    (Rect){49,1,0,0},
    2,
    &patch.detune,
    ^{ float det[3]={0.0,0.00002,0.00004};
       vcom.detune=det[patch.detune];
       sprintf(detune.title,"det=%g",vcom.detune*1000);
     }
  );

  tw_hor_slider_init(
    &filt_coff,
    (Rect){49,3,0,0},
    5,
    &patch.filt_coff,
    ^{ vcom.fixed_cutoff=coff[patch.filt_coff];
       sprintf(filt_coff.title,"cutoff=%g",vcom.fixed_cutoff);
     }
  );

  tw_hor_slider_init(
    &filt_q,
    (Rect){49,5,0,0},
    4,
    &patch.filt_q,
    ^{ float q[5]={1.0,0.6,0.3,0.15,0.1};
       vcom.qres=q[patch.filt_q];
       sprintf(filt_q.title,"Q=%.2g",1./vcom.qres);
    }
  );

  tw_hor_slider_init(
    &eg2_mod_filt,
    (Rect){49,7,0,0},
    5,
    &patch.eg2_mod_filt,
    ^{ float emf[6]={0,0.2,0.5,0.7,1,1.5};
       vcom.eg2_mod_filt=emf[patch.eg2_mod_filt];
       sprintf(eg2_mod_filt.title,"eg->coff=%.2g",vcom.eg2_mod_filt);
     }
  );

  tw_hor_slider_init(
    &amfm_2_1,
    (Rect){49,9,0,0},
    5,
    &patch.amfm_2_1,
    ^{ float afm[6]={0,0.3,0.5,0.8,1.2,2};
       vcom.amfm_2_1=afm[patch.amfm_2_1];
       sprintf(amfm_2_1.title,"AM/FM 2->1=%.2g",vcom.amfm_2_1);
     }
  );

  tw_custom_init(
    &adsr_filt.adsr,
    (Rect){32,2,13,3},
    adsr_filt_draw,
    adsr_filt_mouse
  ); 

  tw_hor_slider_init(
    &trem_osc,
    (Rect){49,11,0,0},
    8,
    &patch.trem_osc,
    ^{ float am[pitwheel_len]={ 0.7,0.5,0.2,0.1,0,0.1,0.2,0.5,0.7 };
       vcom.trem_osc=am[patch.trem_osc];
       sprintf(trem_osc.title,"tremolo=%g",vcom.trem_osc);
       if (patch.trem_osc == 4) {
         vcom.trem_enabled=false;
       } else {
         vcom.trem_enabled=true;
         vcom.trem_freq= patch.trem_osc > 4 ? 8 : 4;
       }
     }
  );

  tw_vert_slider_init(
    &volume,
    (Rect){65,6,0,0},
    4,
    &patch.out_vol,
    ^{ vcom.out_vol=outvol[patch.out_vol];
       strcpy(volume.title1,"volume");
       sprintf(volume.title2,"%.1f",vcom.out_vol);
     }
  );

  char* patch_names[tw_alen(bi_patches)+1];
  for (int i=0;;++i) {
    if (i==tw_alen(bi_patches)) { patch_names[i]=0; break; }
    patch_names[i]=bi_patches[i].name;
    //LOG("name=%s",patch_names[i]);
  }

  tw_menu_init(
    &patches,
    (Rect){82,1,28,12},
    &patch_nr,
    "patches",
    patch_names,
    ^{ patch=bi_patches[patch_nr];
       upd_titles();
       tw_draw();
     }
  );

  upd_titles();
  tw_draw();

  tw_start_main_loop();

  return 0;
}
