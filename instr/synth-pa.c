// met pulse_audio (sr = 11025): geen crash, beetje storing
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
  nr_samples=128,
  sample_rate=11025,
  voices_max=4,
  h_xgrid = 1,
  num_pitch_max=11,
  col_hbg=194,
  col_dgrey=grey+12,
  subdiv=5,    // adsr
  s_len=2,     // adsr
  coff_len=6
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
  PI4=4 * M_PI,
  coff[coff_len]={ 0.5,1,1.5,2,3,7 };

#define oload __attribute__((overloadable))

typedef struct {
  tw_Custom harms;
  short *pitch_arr;
  bool do_draw;
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
   patches;
static tw_Slider2
   mix_1_2,
   am_2_1,
   detune,
   filt_coff,
   filt_q,
   eg2_mod_filt,
   vib_freq, vib_osc,
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
enum { Sine, Sawtooth, Square, Harms, Perlin, Formant, Off };
enum { eOff, eLP, eBP, eHP };  // same order as filter_mode

typedef struct {
  float
    mix_1_2,//=0.5,
    detune,//=0.0001,
    qres,//=0.4,
    fixed_cutoff,//=2.0,
    eg2_mod_filt,//=1,
    start_lev_f,//=1,
    decay_f,//=0.995,
    sustain_f,//=0.5,
    release_f,//=0.995,
    end_lev_f,//=0,
    attack,//=0.99,
    decay,//=0.999,
    sustain,//=1.0,
    release,//=0.995,
    vib_freq,//=7,
    vib_osc,//=0,
    am_2_1,//=0,
    out_vol,//=1;
    lfo1[2];//=[0,0];
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
    vib_freq, vib_osc, am_2_1;
  Adsr_filt_data adsr_f_data;
  Adsr_amp_data adsr_a_data;
  short out_vol;
} Patch;
static Patch patch;

static void patch_report() {
  fprintf(stderr,"  { \"%s\", {", patch.name);
  if (patch.wform_osc1==Harms)
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
  fprintf(stderr,"},\n    %d,%d,%d,%d, %d,%d,%d,%d, %d,%d,%d, { %d,%d,%d,%d,%d }, { %d,%d,%d,%d }, %d },\n",
    patch.wform_osc1, patch.wform_osc2, patch.mix_1_2, patch.detune,
    patch.filt_mode, patch.filt_coff, patch.filt_q, patch.eg2_mod_filt,
    patch.vib_freq, patch.vib_osc, patch.am_2_1,
    patch.adsr_f_data.y0, patch.adsr_f_data.x1, patch.adsr_f_data.y1, patch.adsr_f_data.x3, patch.adsr_f_data.y3,
    patch.adsr_a_data.x1, patch.adsr_a_data.x2, patch.adsr_a_data.y2, patch.adsr_a_data.x4,
    patch.out_vol
  );
}

static Patch bi_patches[] = {
  { "piano",   {}, {},
      1,1,2,1, 1,2,1,4, 2,0,0, { 2,3,1,3,1 }, { 0,4,0,3 }, 3 },
  { "glass harp", {0,0,0,3,3,0,0,0,0,0}, {0,0,0,3,3,0,0,0,0,0},
      3,3,2,1, 1,2,1,4, 2,0,0, { 2,3,1,3,1 }, { 0,5,0,3 }, 3 },
  { "hammond", {4,2,0,2,0,2,0,0,0,0}, {0,0,0,1,0,1,0,0,0,0},
    3,3,2,1, 1,3,1,3, 3,2,0, { 2,2,0,3,0 }, { 0,3,1,3 }, 4 },
  { "bass",    {}, {3,0,0,0,0,0,3,0,0,0},
      1,3,0,2, 1,2,1,4, 2,0,3, { 2,4,0,3,0 }, { 0,5,0,3 }, 3 },
  { "wow",     {}, {0,0,0,3,3,0,0,0,0,0},
      1,3,2,1, 2,3,2,5, 1,2,0, { 2,5,0,3,0 }, { 3,5,0,4 }, 3 },
  { "wow2", {2,4,2,0,1,3,1,0,1,2}, {0,0,0,0,0,2,0,0,0,0},
    3,3,2,1, 1,3,3,3, 2,0,0, { 2,4,0,4,0 }, { 0,3,1,3 }, 3 },
  { "church bell", {0,1,0,3,0,1,0,0,2,0}, {0,0,0,0,0,0,0,0,3,0},
    3,3,2,1, 1,4,2,4, 0,0,0, { 2,1,1,5,1 }, { 0,5,0,4 }, 4 },
  { "perlin noise", {}, {},
      4,6,0,0, 1,2,2,2, 2,0,0, { 2,4,1,3,0 }, { 0,3,1,3 }, 2 },
  { "formant",    {}, {},
      5,6,0,2, 0,2,2,4, 2,0,0, { 2,2,1,3,0 }, { 0,3,1,3 }, 3 },
  { "test",    {0,3,0,3,0,2,0,0,0,1}, {},
      3,0,2,2, 1,2,2,4, 2,0,0, { 2,2,1,3,0 }, { 0,3,1,3 }, 3 }
};

typedef struct {
  float
    eg1_val,
    eg2_val,
    freq,
    freq_scale,
    detune_val,
    filt_cutoff,
    d1,d2,d3,d4,
    velocity,
    lfo1_tmp,
    mod_amp_1,
    osc1_pos, osc2_pos,
    f1,f2,f3,f4,
    a1,a2,a3,a4;
  uint8_t
    eg1_phase,//=Idle,
    eg2_phase,//=Decay,
    midi_nr,//=60;
    F; // 0 -> 7 
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
  v->mod_amp_1=v->lfo1_tmp * vcom.vib_osc;
}

static float filter_24db(Values *v, float input) {
// from: Virtual Analog (VA) Filter Implementation and Comparisons, by Will Pirkle
  if (patch.filt_mode==eOff) return input;
  const int mode=patch.filt_mode;
  float
    g=v->filt_cutoff,
    H_P = (input - (vcom.qres + g) * v->d1 - v->d2)/(1 + vcom.qres * g + g * g),
    B_P = g * H_P + v->d1,
    L_P = g * B_P + v->d2;
  v->d1 = g * H_P + B_P;
  v->d2 = g * B_P + L_P;
  float out1= mode==eLP ? L_P :
              mode==eBP ? B_P :
              mode==eHP ? H_P : 0;
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
  int F=v->F=rand() % 8;
  v->f1=F1[F]; v->f2=F2[F]; v->f3=F3[F]; v->f4=F4[F];
  v->a1=A1[F]; v->a2=A2[F]; v->a3=A3[F]; v->a4=A4[F];
}

static float mnr2freq(uint8_t mnr) {
  return freqs[mnr%12] * ((1<<(mnr/12))/64.);
}

static void set_pfreq(Values *v, uint8_t mnr, bool down, float key_veloc) {
  //LOG("set_pfreq: down=",down," mnr=",mnr);
  if (down) {
    v->midi_nr=mnr;
    v->freq=mnr2freq(mnr);
    v->freq_scale=v->freq * PI2 / sample_rate;
    v->eg1_val=0;
    v->velocity=key_veloc;
    v->eg1_phase=v->eg2_phase=Attack;
    v->filt_cutoff=v->freq_scale * vcom.fixed_cutoff;
    if (patch.wform_osc1==Formant || patch.wform_osc2==Formant) init_formant(v);
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

static float interpol(float fi) {
    const int ind1=fi,
              ind2=tw_min(ind1+1,coff_len-1);
    float mix=fi - (float)ind1;
    return coff[ind1] * (1. - mix) + coff[ind2] * mix;
}

static void modCutoff(uint8_t val) { // modulation wheel
  int ind=val * coff_len / 128;
  if (ind!=patch.filt_coff) {
    patch.filt_coff=tw_minmax(0,ind,coff_len-1);
    filt_coff.cmd();
    slider2_draw(&filt_coff);
    fflush(stdout); // else no complete draw
  }
  vcom.fixed_cutoff=interpol((float)val * coff_len / 128.);
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
  vcom.lfo1[1]=get(&lfo1,vcom.vib_freq);
}

static float sawtooth(float pos) {
  return (sinf(pos)+sinf(pos*2)+sinf(pos*3)/1.5+sinf(pos*4)/2+sinf(pos*5)/2.5) * 0.5;
}
static float square(float pos) {
  return (sinf(pos)+0.3*sinf(pos*3)+0.2*sinf(pos*5)+0.15*sinf(pos*7)) * 0.5;
}

float gradients[0x10000];

float rand_val() { return rand() / (float)RAND_MAX * 2.0 - 1.0; }

void setSeed() {
  for (int z = 0; z < 0x10000; ++z)
    gradients[z] = rand_val();
}

float noise(bool modif, float z) {
  static float delta;
  static int interval;
  float
    g1, g2, d2,
    result = 0;
  uint
    z1 = (uint)z % 0x10000,
    z2 = (z1 + 1) % 0x10000;
  float dz1 = z - z1,
        dz2 = dz1 - 1.0;
  g1 = gradients[z1];
  g2 = gradients[z2];
  if (modif && ++interval % 500 == 0) {
    delta = tw_fminmax(-1,delta += 0.1 * rand_val(),1);
    float val= gradients[z1];
    val = tw_fminmax(-1,val + 0.05 * delta, 1);
    gradients[z1] = val;
  }
  if (g1 > 1.0) { g1 = 2.0 - g1; }
  else if (g1 < -1.0) { g1 = -2.0 - g1; }
  if (g2 > 1.0) { g2 = 2.0 - g2; }
  else if (g2 < -1.0) { g2 = -2.0 - g2; }
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
  bool modif=false; // modify gradient buffer?
  for (int i=0; i<voices_max ;++i) { // find first active voice
    if (vals[i].eg1_phase != Idle && vals+i == v) { modif=true; break; }
  }
  for (int octave = 0; octave < 4; ++octave) {
    float value = noise(modif, pos * (1 << octave)) * multiplier;
    result += value;
    multiplier *= 2; // persistence;
  }
  return result * 0.5;
}
static float harms(float pos,short *arr) { // arr: 0 -> 4
  static float ampl[]={0, 0.25, 0.5, 1, 1.5};
  float ret=0.;
  for (int pit=0;pit<num_pitch_max;++pit) {
    if (!arr[pit]) continue;
    ret += ampl[arr[pit]] * sinf(pos * (pit+1) * 0.5);
  }
  return ret * 0.3;
}

float fonc_formant(float p, const int I) { // p: -1->1
  float a = 0.5,
        phi = 0;
  const int hmax = tw_min(I, 20);
  //if (++cnt%100000==0) printf("p=%.2f I=%d hmax=%d\n",p,I,hmax);
  for (int h = 1; h < hmax; h++) {
    phi += 0.5 * M_PI * p;
    float hann = 1. + cosf(0.5 * M_PI * h / hmax);
    a += hann * cosf(phi);
  }
  return a;
}

float porteuse(const float h, const float p) {
  float h0 = floor(h),          //integer and
        hf = h - h0;            //decimal part of harmonic number.
  // two carriers.
  float Porteuse0 = cosf(M_PI * p * h0),
        Porteuse1 = cosf(M_PI * p * (h0+1));
  // crossfade between the two carriers.
  return Porteuse0 + hf * (Porteuse1 - Porteuse0);
}

float formant(Values *v, float pos) {
  float freq=v->freq/2;
  float out =
        v->a1 * (freq / v->f1) * fonc_formant(pos, 500 / freq) * porteuse(v->f1 / freq, pos)
      + v->a2 * (freq / v->f2) * fonc_formant(pos, 600 / freq) * porteuse(v->f2 / freq, pos) * 0.7f
      + v->a3 * (freq / v->f3) * fonc_formant(pos, 800 / freq) * porteuse(v->f3 / freq, pos)
      + v->a4 * (freq / v->f4) * fonc_formant(pos, 1500 / freq) * porteuse(v->f4 / freq, pos);
  return out * 0.5;
}

static float oscillator(Values  *v) {
  v->osc1_pos += v->freq_scale;
  if (v->osc1_pos > PI4) v->osc1_pos -= PI4;
  v->osc2_pos += v->freq_scale + vcom.detune;
  if (v->osc2_pos > PI4) v->osc2_pos -= PI4;
  float val1=0., val2=0.,
        pos1=v->osc1_pos,
        pos2=v->osc2_pos,
        mix=vcom.mix_1_2;
  switch (patch.wform_osc1) {
    case Sine: val1=sinf(pos1); break;
    case Sawtooth: val1=sawtooth(pos1); break;
    case Square: val1=square(pos1); break;
    case Harms: val1=harms(pos1,patch.pitch_arr1); break;
    case Perlin: val1=fbm(v, pos1 / PI4); break;          // pos: 0 -> 1
    case Formant: val1=formant(v, pos1 / PI2 - 1); break; // pos: -1 -> 1
  }
  switch (patch.wform_osc2) {
    case Sine: val2=sinf(pos2); break;
    case Sawtooth: val2=sawtooth(pos2); break;
    case Square: val2=square(pos2); break;
    case Harms: val2=harms(pos2,patch.pitch_arr2); break;
    case Perlin: val2=fbm(v, pos2 / PI4); break;
    case Formant: val2=formant(v, pos2 / PI2 - 1); break;
  }
  if (patch.am_2_1) val1 *= (1 - vcom.am_2_1/2) + vcom.am_2_1 * val2; // vcom.am_2_1: 0 -> 2
  return (val1 * (1. - mix) * (1 + v->mod_amp_1) + val2 * mix) * v->eg1_val;
}

static void fill_buffer(Values *v, float *buffer) {
  for (int i=0;i<nr_samples;++i) {
    if (i%4==0) {
      switch (v->eg1_phase) {
        case Idle: return;
        case Attack:
          v->eg1_val = (v->eg1_val-1.) * vcom.attack + 1.;
          if (v->eg1_val>0.95) v->eg1_phase=Decay;
          break;
        case Decay:
          v->eg1_val = (v->eg1_val-vcom.sustain) * vcom.decay + vcom.sustain;
          if (v->eg1_val<0.01) v->eg1_phase=Idle;
          break;
        case Release:
          if (v->eg1_val<0.05) v->eg1_val *= 0.99;
          else v->eg1_val *= vcom.release;
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
          v->eg2_val = (v->eg2_val-lev) * vcom.decay_f + lev;
          if (fabs(v->eg2_val-lev) < 0.01) v->eg2_phase=Sustain;
          }
          break;
        case Sustain:
        case Idle:
          break;
        case Release: {
          float lev=vcom.end_lev_f;
          v->eg2_val = (v->eg2_val-lev) * vcom.release_f + lev;
          if (fabs(v->eg2_val - lev) < 0.02) v->eg2_phase=Idle;
          }
          break;
        default: break;
      }
      float tmp=fmax(0.005,
        v->freq_scale *
        (vcom.fixed_cutoff + (v->eg2_val - 1) * vcom.eg2_mod_filt)  // eg2_val = 0 -> 2
      );
      slow_add(&v->filt_cutoff,tmp,v->freq_scale*0.001);
    }
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
      if (midi_mes[1]==1)
        modCutoff(midi_mes[2]); // 0 -> 128
      break;
    default: break;
  }
}

static void split(float *in_buf,short *out_buf) {
  static float lp;
  const float
    lpf=expf(-PI2 * 1000. / sample_rate),
    a0=1-lpf;
  for (int i=0;i<nr_samples;++i) {
    lp = a0 * in_buf[i] + lpf * lp;
    out_buf[2*i]=tw_minmax(-32000, 32000 * lp, 32000);
    out_buf[2*i+1]=tw_minmax(-32000,32000 * (in_buf[i] - lp),32000);
  }
  lp *= 0.998;  // to avoid low-freq ringing at note start
}

static void* play(void* d) {
  once_per_frame();
  short out_buf[2*nr_samples];
  float buffer[nr_samples];
  while (true) {
    if (!mkb_connected) {
      pulse_close();
      break;
    }
    once_per_frame();
    bzero(buffer,sizeof(buffer));
    for (int8_t v=0; v<voices_max; ++v)
      fill_buffer(vals+v,buffer);

    split(buffer,out_buf);

    if (mkb_connected) {
      if (pulse_write(out_buf,nr_samples)<0) {
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
    Harmonics *env=(Harmonics*)h; // h = first member of Harmonics
    if (env->do_draw)
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
  aa->cmd=^{  // d_major: eval_adsr()
    float eg1[6]={ 0.5, 0.05, 0.015, 0.005, 0.0015, 0.0005 };
    float sus[2]={ 0, 1. };
    vcom.attack=1. - eg1[patch.adsr_a_data.x1];
    vcom.decay=1. - eg1[patch.adsr_a_data.x2];
    vcom.sustain=sus[patch.adsr_a_data.y2];
    vcom.release=1. - eg1[patch.adsr_a_data.x4];
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
    float eg2[6]={ 0.15, 0.05, 0.015, 0.005, 0.0015, 0.001 };
    //float eg2[6]={ 0.03, 0.01, 0.003, 0.001, 0.0003, 0.0002 };
    vcom.start_lev_f=patch.adsr_f_data.y0;
    vcom.decay_f=1. - eg2[patch.adsr_f_data.x1];
    vcom.sustain_f=patch.adsr_f_data.y1;
    vcom.release_f=1. - eg2[patch.adsr_f_data.x3];
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

static void harm_init(Harmonics *h,short _pitch_arr[]) {
  h->pitch_arr=_pitch_arr;
  h->do_draw=true;
}

static void upd_titles() { // update sliders
  mix_1_2.cmd();
  detune.cmd();
  filt_coff.cmd();
  filt_q.cmd();
  eg2_mod_filt.cmd();
  vib_freq.cmd();
  vib_osc.cmd();
  am_2_1.cmd();
  volume.cmd();
  harm_init(&harm_win1,patch.pitch_arr1);
  harm_init(&harm_win2,patch.pitch_arr2);
  adsr_init(&adsr_filt);
  adsr_init(&adsr_amp);
  adsr_amp.cmd();
  adsr_filt.cmd();
}
static void* connect_mkeyb(void* d) {
  uint8_t mbuf[3];
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
  if (pulse_init(sample_rate,nr_samples,2) < 0) {
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

  tw_gui_init();
  do_exit= ^{
    mkb_connected=false;
    if (audio_thread) { pthread_join(audio_thread,0); audio_thread=0; }
  };
  patch=bi_patches[0];
  setSeed();

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
    (Rect){1,1,5,8},
    &patch.wform_osc1,
    "osc1",
    (char*[]){ "","","","","","","",0 },
    ^{ harm_init(&harm_win1,patch.pitch_arr1);
       harm_win1.do_draw= patch.wform_osc1==3;
       harm_draw(&harm_win1.harms);
     }
  );

  tw_menu_init(
    &wform_osc2,
    (Rect){7,1,5,8},
    &patch.wform_osc2,
    "osc2",
    (char*[]){ "sine","sawtooth","square","harm's","perlin","formant","off",0 },
    ^{ harm_init(&harm_win2,patch.pitch_arr2);
       harm_win2.do_draw= patch.wform_osc2==3;
       harm_draw(&harm_win2.harms);
     }
  );
  wform_osc1.style=wform_osc2.style=Compact;

  tw_slider2_init(
    &mix_1_2,
    (Rect){1,10,0,2},
    4,
    &patch.mix_1_2,
    ^{ float mix[5]={0.0,0.25,0.5,0.75,1.0};
       vcom.mix_1_2=mix[patch.mix_1_2];
       sprintf(mix_1_2.title,"mix1/2=%-3g",vcom.mix_1_2);
     }
  );
  tw_checkbox_init(
    &record,
    (Rect){72,10,0,0},
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

  harm_init(&harm_win1,patch.pitch_arr1);
  harm_init(&harm_win2,patch.pitch_arr2);

  tw_custom_init(
    &harm_win1.harms,
    (Rect){18,1,11,5},
    harm_draw,
    harm_mouse
  );
  tw_custom_init(
    &harm_win2.harms,
    (Rect){18,6,11,5},
    harm_draw,
    harm_mouse
  );
  Rect r1 = harm_win1.harms.wb.area,
       r2 = harm_win2.harms.wb.area;
  tw_print(r1.x,r1.y,col_lbrown,"harmonics  ",0);
  for (int i=1;i<=num_pitch_max;++i) {
    if (i%2==0) {
      char buf[2]; sprintf(buf,"%d",i/2);
      tw_print(r1.x - 1 + i * h_xgrid, r1.y+r1.h,0,buf,0);
      tw_print(r2.x - 1 + i * h_xgrid, r2.y+r2.h,0,buf,0);
    }
  }

  tw_custom_init(
    &adsr_amp.adsr,
    (Rect){60,2,18,2},
    adsr_amp_draw,
    adsr_amp_mouse
  ); 

  tw_menu_init(
    &filter_mode,
    (Rect){30,7,16,2},
    &patch.filt_mode,
    "filter",
    (char*[]){ "off","LP","BP","HP",0 },
    ^{ adsr_filt.do_draw= patch.filt_mode!=eOff;
       adsr_filt_draw(&adsr_filt.adsr);
     }
  );

  tw_slider2_init(
    &detune,
    (Rect){47,1,0,2},
    2,
    &patch.detune,
    ^{ float det[3]={0.0,0.0005,0.001};
       vcom.detune=det[patch.detune];
       sprintf(detune.title,"det=%g%%",vcom.detune*100);
     }
  );

  tw_slider2_init(
    &filt_coff,
    (Rect){47,3,0,2},
    5,
    &patch.filt_coff,
    ^{ vcom.fixed_cutoff=coff[patch.filt_coff];
       sprintf(filt_coff.title,"coff=%g",vcom.fixed_cutoff);
     }
  );

  tw_slider2_init(
    &filt_q,
    (Rect){47,5,0,2},
    4,
    &patch.filt_q,
    ^{ float q[5]={1.0,0.6,0.3,0.15,0.1};
       vcom.qres=q[patch.filt_q];
       sprintf(filt_q.title,"Q=%.2g",1./vcom.qres);
    }
  );

  tw_slider2_init(
    &eg2_mod_filt,
    (Rect){47,7,0,2},
    5,
    &patch.eg2_mod_filt,
    ^{ float emf[6]={0,0.2,0.5,0.7,1,1.5};
       vcom.eg2_mod_filt=emf[patch.eg2_mod_filt];
       sprintf(eg2_mod_filt.title,"eg->coff=%.2g",vcom.eg2_mod_filt);
     }
  );

  tw_slider2_init(
    &am_2_1,
    (Rect){47,9,0,2},
    4,
    &patch.am_2_1,
    ^{ float am[5]={0,0.3,0.6,1.2,2};
       vcom.am_2_1=am[patch.am_2_1];
       sprintf(am_2_1.title,"AM 2->1=%-3g",vcom.am_2_1);
     }
  );

  tw_custom_init(
    &adsr_filt.adsr,
    (Rect){31,2,13,3},
    adsr_filt_draw,
    adsr_filt_mouse
  ); 
  //adsr_init(&adsr_filt);

  tw_slider2_init(
    &vib_freq,
    (Rect){60,5,0,2},
    4,
    &patch.vib_freq,
    ^{ float freq[5]={ 5,6,7,8,10 };
      vcom.vib_freq=freq[patch.vib_freq];
      sprintf(vib_freq.title,"lfo=%gHz",vcom.vib_freq);
    }
  );

  tw_slider2_init(
    &vib_osc,
    (Rect){60,7,0,2},
    5,
    &patch.vib_osc,
    ^{ float am[6]={ 0,0.1,0.2,0.5,0.7,1 };
      vcom.vib_osc=am[patch.vib_osc];
      sprintf(vib_osc.title,"tremolo=%g",vcom.vib_osc);
     }
  );

  tw_slider2_init(
    &volume,
    (Rect){72,5,0,2},
    4,
    &patch.out_vol,
    ^{ float vol[5]={ 0.3,0.5,0.7,1,1.5 };
       vcom.out_vol=vol[patch.out_vol];
       sprintf(volume.title,"vol=%g",vcom.out_vol);
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
    (Rect){81,1,13,11},
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
