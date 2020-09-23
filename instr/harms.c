// This code is public domain

#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <Block.h> // for Block_copy()
#include <unistd.h>

#include <term-win.h>
#include "shared.h"

enum {  // const's
  ts_dim = 200,
  sample_rate = 44100, nr_samples = 512, nr_channels=2,
  num_pitch_max = 20,
  voices_max = 6, // max polyphony
  h_xgrid = 2,    // pitches distance
  h_amp_max = 7,  // max value of harmonics
  rrate = 16,
  col_hbg=159, // harm background
  subdiv=5,    // adsr
  s_len=2      // adsr
};

static short
  patch_nr,
  mnr2vnr[128];
static bool
  recording,
  mkb_connected;
int
  cnt;  // for debugging
static const float
  mid_C = 265,
  PI2 = M_PI*2,
  freq_scale= (float)ts_dim / sample_rate,
  outvol[7]={ 0.5,0.7,1,1.4,2,3 };
       
enum {
  eAttack,
  eDecay,
  eSustain,
  eRelease,
  eIdle
};
enum {
  eFM_id, // same order as FM menu
  eFM_half,
  eFM_1,
  eFM_12,
  eFM_14
};
enum {
  eEqual, // for semi_log()
  eUp,
  eDown,
  eKc_tone,
  eKc_noise
};

static struct {
  float
    output_ampl,
    fm_det,
    modwheel_val,
    pitwheel_val,
    pit2_det,
    su_dec_rate,
    kc_freq,
    kc_amp,
    trem_val,
    lfo2[2];
  bool
    trem_enabled;
  int8_t
    kc_type;
} vcom;

typedef struct {
  char *name;
  short
    pitch_arr[num_pitch_max],
    two_pitch[num_pitch_max],
    fm_modu,
    detune_fm,
    detune_2p,
    kc_mode,
    kc_dur, kc_amp,
    trem_val,
    non_lin,
    out_volume,
    adsr_x1, adsr_x2, adsr_y2, adsr_x4;
} Patch;

static Patch
  patch,
  bi_patches[]= {
  { .name="rich organ",
    .pitch_arr={0,5,2,3,0,2,0,0,2,0,0,1,0,0,0,0,0,0,0,0},
    .two_pitch={0,0,1,0,0,1,0,0,1,0,0,1,0,0,0,0,0,0,0,0},
    .fm_modu=0, .detune_fm=0, .detune_2p=3, .kc_mode=-1, .kc_dur=0, .kc_amp=0, .trem_val=4,
    .non_lin=-1, .out_volume=4, .adsr_x1=0, .adsr_x2=3, .adsr_y2=1, .adsr_x4=3
  },
  { .name="low organ",
    .pitch_arr={5,5,0,5,0,2,0,2,0,3,0,0,0,0,0,0,0,0,0,0},
    .two_pitch={0,0,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0},
    .fm_modu=0, .detune_fm=0, .detune_2p=2, .kc_mode=-1, .kc_dur=0, .kc_amp=0, .trem_val=4,
    .non_lin=3, .out_volume=3, .adsr_x1=0, .adsr_x2=3, .adsr_y2=1, .adsr_x4=3
  },
  { .name="weird organ",
    .pitch_arr={0,5,0,0,4,0,0,0,3,0,0,0,4,0,0,0,0,0,0,0},
    .two_pitch={0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0},
    .fm_modu=2, .detune_fm=0, .detune_2p=2, .kc_mode=-1, .kc_dur=0, .kc_amp=0, .trem_val=4,
    .non_lin=0, .out_volume=3, .adsr_x1=0, .adsr_x2=3, .adsr_y2=1, .adsr_x4=3
  },
  { .name="wonky organ",
    .pitch_arr={0,0,0,5,0,0,0,5,0,0,0,4,0,0,0,2,0,0,0,0},
    .two_pitch={0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0},
    .fm_modu=4, .detune_fm=3, .detune_2p=4, .kc_mode=-1, .kc_dur=0, .kc_amp=0, .trem_val=4,
    .non_lin=3, .out_volume=3, .adsr_x1=0, .adsr_x2=3, .adsr_y2=1, .adsr_x4=3
  },
  { "piano1", {0,0,0,5,0,5,0,0,0,0,0,3,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0},
    3,1,2,1,3,1,4,3,3, 0,5,0,3 },
  { .name="piano2",
    .pitch_arr={0,5,0,5,0,5,0,2,0,2,0,2,0,0,0,2,0,0,0,0},
    .two_pitch={0,1,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0},
    .fm_modu=0, .detune_fm=0, .detune_2p=1, .kc_mode=1, .kc_dur=3, .kc_amp=1, .trem_val=4,
    .non_lin=-1, .out_volume=3, .adsr_x1=0, .adsr_x2=5, .adsr_y2=0, .adsr_x4=3
  },
  { "el.piano1", {0,5,2,3,0,3,0,0,0,0,0,2,0,0,0,2,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    0,0,0,2,6,3,5,0,3, 0,5,0,3 },
  { "el.piano2", {0,0,0,5,0,0,0,5,0,0,0,3,0,0,0,2,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    4,2,0,2,4,2,4,3,4, 0,5,0,3 },
  { "el.piano3", {0,0,3,0,0,5,0,0,2,0,0,2,0,0,2,0,0,0,0,0}, {0,0,1,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0},
    3,0,1,1,3,1,4,2,4, 0,5,0,3 },
  { "hammond1", {6,3,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    0,0,0,3,2,2,6,-1,4, 0,5,1,3 },
  { "hammond2", {5,5,5,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    0,0,0,3,2,2,5,0,4, 0,5,1,3 },
  { .name="wurlizer",
    .pitch_arr={0,3,0,0,0,3,0,0,0,5,0,0,0,0,0,0,0,0,0,0},
    .two_pitch={0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    .fm_modu=2, .detune_fm=0, .detune_2p=2, .kc_mode=-1, .kc_dur=0, .kc_amp=0, .trem_val=4,
    .non_lin=0, .out_volume=4, .adsr_x1=0, .adsr_x2=5, .adsr_y2=0, .adsr_x4=3
  },
  { "bass1", {3,5,3,2,1,1,0,0,0,0,0,0,0,0,1,1,1,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    2,4,0,-1,0,0,4,2,4, 0,4,0,3 },
  { .name="bass2",
    .pitch_arr={5,4,3,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    .two_pitch={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    .fm_modu=3, .detune_fm=4, .detune_2p=0, .kc_mode=0, .kc_dur=4, .kc_amp=1, .trem_val=4,
    .non_lin=3, .out_volume=5, .adsr_x1=0, .adsr_x2=2, .adsr_y2=0, .adsr_x4=3
  },
  { .name="church bell",
    .pitch_arr={0,5,0,0,4,0,0,3,0,0,0,4,0,0,0,0,0,0,0,0},
    .two_pitch={0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0},
    .fm_modu=2, .detune_fm=0, .detune_2p=3, .kc_mode=-1, .kc_dur=0, .kc_amp=0, .trem_val=4,
    .non_lin=0, .out_volume=4, .adsr_x1=1, .adsr_x2=4, .adsr_y2=0, .adsr_x4=5
  },
  { .name="dreamy",
    .pitch_arr={0,2,0,5,0,2,0,2,0,5,0,0,0,0,0,0,0,0,0,2},
    .two_pitch={0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0},
    .fm_modu=0, .detune_fm=0, .detune_2p=3, .kc_mode=-1, .kc_dur=0, .kc_amp=0, .trem_val=2,
    .non_lin=-1, .out_volume=4, .adsr_x1=5, .adsr_x2=5, .adsr_y2=0, .adsr_x4=5
  },
  { .name="vibraphone",
    .pitch_arr={0,0,0,5,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0},
    .two_pitch={0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0},
    .fm_modu=1, .detune_fm=3, .detune_2p=4, .kc_mode=1, .kc_dur=4, .kc_amp=1, .trem_val=4,
    .non_lin=0, .out_volume=4, .adsr_x1=0, .adsr_x2=5, .adsr_y2=0, .adsr_x4=3
  },
  { .name="key click",
    .pitch_arr={0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    .two_pitch={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    .fm_modu=0, .detune_fm=0, .detune_2p=0, .kc_mode=1, .kc_dur=6, .kc_amp=3, .trem_val=4,
    .non_lin=-1, .out_volume=4, .adsr_x1=0, .adsr_x2=3, .adsr_y2=1, .adsr_x4=3
  },
  { .name="test",
    .pitch_arr={0,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    .two_pitch={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    .fm_modu=0, .detune_fm=0, .detune_2p=0, .kc_mode=-1, .kc_dur=0, .kc_amp=0, .trem_val=4,
    .non_lin=-1, .out_volume=4, .adsr_x1=0, .adsr_x2=2, .adsr_y2=1, .adsr_x4=3
  }
};

static void report_patch(FILE *out) {
  fprintf(out,"  { \"%s\",",patch.name);
  fprintf(out," {");
  for (int i=0;i<num_pitch_max;++i) {
    if (i==0) fprintf(out,"%d",patch.pitch_arr[i]);
    else fprintf(out,",%d",patch.pitch_arr[i]);
  }
  fprintf(out,"}, {");
  for (int i=0;i<num_pitch_max;++i) {
    if (patch.pitch_arr[i]==0) patch.two_pitch[i]=0;
    if (i==0) fprintf(out,"%d",patch.two_pitch[i]);
    else fprintf(out,",%d",patch.two_pitch[i]);
  }
  fprintf(out,"},\n    ");
  fprintf(out,"%d,%d,%d,%d,%d,%d,%d,%d,%d, %d,%d,%d,%d },\n",
    patch.fm_modu,patch.detune_fm,patch.detune_2p,patch.kc_mode,patch.kc_dur,patch.kc_amp,patch.trem_val,
    patch.non_lin,patch.out_volume,patch.adsr_x1,patch.adsr_x2,patch.adsr_y2,patch.adsr_x4);
}

typedef struct {
  float
    freq_val,
    key_velocity,
    act_ampl,
    act_startup, stup_amp,
    eg1_val,
    fm_pos,
    lfo2_tmp,
    stup_pos,
    fr_val1,
    mult_am,
    freq_track,
    fr_array[num_pitch_max],
    osc_pos1[num_pitch_max],
    osc_pos2[num_pitch_max];
  uint8_t
    eg1_phase,
    stup_phase,
    midi_nr;
  bool
    odd;
} Values;

static tw_HorSlider
  volume_sl,
  fm_modu,
  vib_sl,
  keycl_amp,
  keycl_dur,
  detune_fm,
  detune_2pitch;

static tw_Menu
  patch_mn,
  keycl_mn,
  fm_mode;

static tw_Checkbox
  connect_mkb,
  record;

static tw_Custom
  harmonics,
  monitor,
  adsr_win;


static Values
  vals[voices_max];

static struct {
  void (^draw_harm_line)(tw_Custom *harm,short h_nr);
  void (^harm_draw)(tw_Custom *h);
  void (^harm_mouse)(tw_Custom* harmon,short ev_x,short ev_y,short mode,short button);
} harm= {
  .draw_harm_line=^(tw_Custom *h,short h_nr) {
    Rect r = h->wb.area; ++r.y; --r.h;
    tw_vline(r.x + h_nr * h_xgrid, r.y , r.h, col_hbg); // erase old line
    short h_amp = patch.pitch_arr[h_nr-1];
    if (h_amp>0) {
      uint8_t col = patch.two_pitch[h_nr-1]==1 ? grey + 6 : grey + 16;
      tw_vline(r.x + h_nr * h_xgrid, r.y + r.h - h_amp, h_amp, col);
    }
  },
  .harm_draw=^(tw_Custom *h) {
    Rect r = h->wb.area; ++r.y; --r.h;
    tw_box(r,col_hbg);
    for (short i=0;i<num_pitch_max;++i) {
      short h_nr = i+1;
      harm.draw_harm_line(h,h_nr);
    }
  },
  .harm_mouse=^(tw_Custom* harmon,short ev_x,short ev_y,short mode,short button) {
    if (mode!=Mouse_down) return;
    short h_nr = tw_div(ev_x,h_xgrid);
    if (h_nr<1 || h_nr>num_pitch_max) return;
    short h_amp = h_amp_max - ev_y - 1;
    switch (button) {
      case But_left:
        patch.pitch_arr[h_nr-1]=h_amp;
        break;
      case But_right:
        patch.two_pitch[h_nr-1]=!patch.two_pitch[h_nr-1];
        break;
    }
    harm.draw_harm_line(harmon,h_nr);
  }
};

typedef struct {
  uint32_t
    count,
    nr;
  float maxv;
  bool enab;
  short xpos,ypos;
} MonData; 

static MonData mon_data;

static void mon_get(MonData *m,float in) {
  m=&mon_data;
  ++m->count;
  if (m->count==10000) { // wait 0.2sec
    m->count=0;
    m->nr=0;
    m->maxv=0;
    m->enab=true;
  }
  if (m->enab) {
    if (in>=0) {
      if (m->maxv<in) m->maxv=in;
    } else {
      if (m->maxv< -in) m->maxv=-in;
    }
    ++m->nr; 
    if (m->nr==1000) { // take sample
      //LOG("maxv=%.2f\n",m->maxv);
      m->enab=false;
      uint8_t ampl=tw_min(m->maxv*8,8);  // m->maxv <= 1., ampl <= 8
      tw_vline(m->xpos, m->ypos - 8, 8-ampl, col_grey); // background
      tw_vline(m->xpos,m->ypos - ampl, ampl, 82);
      if (ampl>7)
        tw_set_cell(m->xpos,m->ypos-8,' ',0,col_lred,1); // clipping!
      fflush(stdout);
    }
  }
}

static struct Lfo {
  bool dir;
  float lfo_pos;
} lfo1;

static float get(struct Lfo *lfo, float freq) {
  float dir= lfo->dir ? 1.0 : -1.0;
  lfo->lfo_pos += dir * 8 * freq / sample_rate * nr_samples;
  if (lfo->lfo_pos>2) lfo->dir=false;
  else if (lfo->lfo_pos<-2) lfo->dir=true;
  return tw_fminmax(-1,lfo->lfo_pos,1);
}

typedef struct {
  float sin_table[ts_dim];
  short mode;
} TableSin;

static TableSin t_sin1, t_sin2, t_sin3, t_sin4;

static void tsin_set(TableSin *ts,short mode) {
  for (int i=0;i<ts_dim;++i) {
    float ind=i*PI2/ts_dim;
    switch (mode) {
      case eFM_1: ts->sin_table[i] = 1.4 * sinf(ind); break;
      case eFM_half: ts->sin_table[i]= sinf(ind/2); break;
      case eFM_12: ts->sin_table[i]= sinf(ind)-sinf(ind*2); break;
      case eFM_14: ts->sin_table[i]= sinf(ind)-sinf(ind*4); break;
    }
  }
}

static struct {
  Point pt[5];
  short pt_ind,
        lst_x,
        lst_y;
  short *x1,*x2,*y2,*x4;
  void (^cmd)();
} adsr;

static void (^adsr_draw)(tw_Custom *bw)=^(tw_Custom *bw) {
  uint8_t(^xpos)(uint8_t)=^uint8_t(uint8_t valx){ return bw->wb.area.x + valx; };
  uint8_t(^ypos)(uint8_t)=^uint8_t(uint8_t valy){ return bw->wb.area.y + bw->wb.area.h - valy - 1; };

  tw_box(bw->wb.area,col_grey);
  tw_hline(bw->wb.area.x,bw->wb.area.w,bw->wb.area.y+bw->wb.area.h,0); // clear value numbers
  for (int i=0;i<=4;++i) {
    Point pnt={ xpos(adsr.pt[i].x),ypos(adsr.pt[i].y) };
    switch (i) {
      case 0: tw_set_cell(pnt.x,pnt.y,0x25fc,col_dblue,col_grey,1); break;
      case 1:
      case 2:
      case 3:
      case 4: tw_set_cell(pnt.x,pnt.y,0x25fc,col_red,col_grey,1); break;
    }
    switch (i) {
      case 1: case 2: case 4: // print values
        tw_set_cell(pnt.x,ypos(-1),'0'+adsr.pt[i].x-adsr.pt[i-1].x,0,0,1);
    }
  }
};

static struct {
  float attack, decay, sus, release;
} eg1;

static void (^adsr_mouse)(tw_Custom*,short,short,short,short)=
    ^(tw_Custom* cust,short x,short y,short mode,short button) {
  //LOG("adsr_mouse: x=%d, y=%d, mode=%d, but=%d",x,y,mode,button);
  switch (mode) {
    case Mouse_down: {
        adsr.pt_ind=0;
        for (int i=1;i<=4;++i) {
          if (x==adsr.pt[i].x) {
            adsr.pt_ind=i;
            adsr.lst_x=x; adsr.lst_y=y;
            break;
          }
        }
      }
      break;
    case Mouse_move: {
      int8_t difx=x-adsr.lst_x,
             dify=y-adsr.lst_y,
             prev_dif;
      if (y!=adsr.lst_y) {
        switch (adsr.pt_ind) {
          case 2:
          case 3:
            adsr.pt[2].y=tw_minmax(0,adsr.pt[2].y-dify,1);
            adsr.pt[3].y=adsr.pt[2].y;
            break;
        }
      }
      if (x!=adsr.lst_x) {
        switch (adsr.pt_ind) {
          case 1: 
            prev_dif=adsr.pt[2].x-adsr.pt[1].x;
            adsr.pt[1].x=tw_minmax(0,adsr.pt[1].x+difx,subdiv);
            adsr.pt[2].x=adsr.pt[1].x+prev_dif;
            break;
          case 2:
            adsr.pt[2].x=tw_minmax(adsr.pt[1].x+1,adsr.pt[2].x+difx,adsr.pt[1].x+subdiv);
            break;
          case 3:
            break;
          case 4:
            adsr.pt[4].x=tw_minmax(adsr.pt[3].x+1,adsr.pt[4].x+difx,adsr.pt[3].x+subdiv);
            break;
        }
      }
      if (x!=adsr.lst_x || y!=adsr.lst_y) {
        adsr.lst_x=x;
        adsr.lst_y=y;
        adsr_draw(&adsr_win);
      }
    }
    break;
  case Mouse_up:
    if (adsr.pt_ind==0) return;
    *adsr.x1=adsr.pt[1].x;
    *adsr.x2=adsr.pt[2].x - *adsr.x1;
    *adsr.y2=adsr.pt[2].y;
    *adsr.x4=adsr.pt[4].x-adsr.pt[3].x;
    adsr.pt_ind=0;
    adsr.cmd();
    break;
  } 
};

static const float freqs[13]={
   523.3, 554.4, 587.3, 622.3, 659.3, 698.5, 740.0, 784.0, 830.6, 440*2, 466.2*2, 493.9*2, 523.3*2
};

static float mnr2freq(uint8_t mnr) {
  float res=freqs[mnr%12] * ((1<<(mnr/12))/64.);
  return res;
}

static void set_pfreq(Values *v,uint8_t mnr,bool down,float key_veloc) {
  //LOG("set_pfreq: down=%d mnr=%d",down,mnr);
  if (down) {
    v->midi_nr=mnr;
    uint8_t mnr2=tw_max(30,mnr);
    v->freq_track=60./mnr2;
    v->freq_val=  mnr2freq(mnr);
    for (short pit=0;pit<num_pitch_max;++pit) {
      v->fr_array[pit]=freq_scale * v->freq_val * (pit+1) / 2.;  // 2: subdiv of octave in fr_array[]
    }
    v->eg1_val=0;
    v->act_startup=0.; v->stup_amp=1;
    bzero(v->osc_pos1,sizeof(float)*num_pitch_max);
    bzero(v->osc_pos2,sizeof(float)*num_pitch_max);
    v->stup_pos=0.;
    v->key_velocity=fmin(1.,key_veloc * key_veloc);
    v->eg1_phase=v->stup_phase = eAttack;
  }
  else {
    v->eg1_phase=eRelease;
  }
  //LOG("-> set_pfreq: down=%t freq_val=%.2f v.eg1_phase=%d\n",down,v.freq_val,v.eg1_phase)
}

static short find_free_voice(uint8_t midi_nr) {
  uint8_t v;
  for (v=0;v<voices_max;v++) {
    if (vals[v].eg1_phase==eIdle) return v;
  }
  for (v=0;v<voices_max;v++) {
    if (vals[v].eg1_phase==eRelease) return v;
  }
  mnr2vnr[midi_nr]=-1; // disable noteOff()
  return -1;
}

void keyb_noteOn(uint8_t midi_nr,uint8_t velocity) {
  short v=find_free_voice(midi_nr);
  //LOG("keyb_noteOn: mnr=%d vel=%d v=%d\n",midi_nr,velocity,v)
  if (v>=0) {
    mnr2vnr[midi_nr]=v;
    set_pfreq(&vals[v],midi_nr,true,velocity/100.);
  }
}

void keyb_noteOff(uint8_t midi_nr) {
  //LOG("keyb_noteOff: mnr=%d mnr2vnr[midi_nr]=%d\n",midi_nr,mnr2vnr[midi_nr])
  if (mnr2vnr[midi_nr] >= 0) {
    set_pfreq(&vals[mnr2vnr[midi_nr]],midi_nr,false,0);
  }
}

static float interpol(const float *arr, float fi, const int arr_len) {
  const int ind1=fi,
            ind2=tw_min(ind1+1,arr_len-1);
  float mix=fi - (float)ind1;
  return arr[ind1] * (1. - mix) + arr[ind2] * mix;
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
      if (midi_mes[1]==1) {
        float val=midi_mes[2]/128.;
        int nval=lrint(5*val);
        if (nval!=patch.fm_modu) {
          patch.fm_modu=nval;
          fm_modu.cmd();
          hor_slider_draw(&fm_modu);
          fflush(stdout); // <-- needed!
        }
        vcom.modwheel_val=val; // smooth control
      }
      else if (midi_mes[1]==7) {
        float fval=6 * midi_mes[2] / 128.;
        int nval=fval;
        if (nval!=patch.out_volume) {
          patch.out_volume=nval;
          volume_sl.cmd();
          hor_slider_draw(&volume_sl);
          fflush(stdout);
        }
        vcom.output_ampl=interpol(outvol, fval, 6);
      }
      break;
    case 0xe0:
      if (midi_mes[1]==0) {
        int nval=lrint(8*midi_mes[2] / 128.);
        if (nval!=patch.trem_val) {
          patch.trem_val=nval;
          vib_sl.cmd();
          hor_slider_draw(&vib_sl);
          fflush(stdout); // <-- needed!
        }
      }
      break;
    default: break;
  }
}

void stop_conn_mk() {
  mkb_connected=false;
  usleep(500000);
  checkbox_draw(&connect_mkb);
  monitor.draw(&monitor);
  fflush(stdout);
}

static pthread_t
  conn_thread,
  audio_thread;

static float buffer[nr_samples];
static short out_buf[2*nr_samples];

static void once_per_frame() {
  if (vcom.trem_enabled) {
    float freq=patch.trem_val > 4 ? 8 : 4;
    vcom.lfo2[0]=vcom.lfo2[1];
    vcom.lfo2[1]=get(&lfo1,freq); // 4 or 8Hz
  }
}

static void semi_log(float end, float incr, uint8_t direction, float *val, uint8_t *old_phase, uint8_t new_phase) {
  float mult = fabs(*val-end)+0.1;
  switch (direction) {
    case eEqual:
      *old_phase=new_phase;
      break;
    case eUp:
      *val += incr * mult;
      if (*val>end) { *val=end; *old_phase=new_phase; }
      break;
    case eDown:
      *val -= incr * mult;
      if (*val<end) { *val=end; *old_phase=new_phase; }
      break;
  }
}

static float calc_eg1(Values *v,float val,uint8_t *phase) {
  switch (*phase) {
    case eAttack:
      semi_log(1.,eg1.attack,eUp,&val,phase,eDecay);
      break;
    case eDecay:
      semi_log(eg1.sus,eg1.decay / v->freq_track,eDown,&val,phase,eSustain); // freq dependent decay
      break;
    case eRelease:
      semi_log(0,eg1.release,eDown,&val,phase,eIdle);
      break;
    case eSustain:
      break;
    default:
      LOG("calc_eg1: phase=%d?",*phase);
  }
  //if (cnt%2000==0) LOG("calc_eg:val=%.2f",val)
  return val;
}

static float calc_startup(Values *v,float act_stup,uint8_t *phase) {
  switch (*phase) {
    case eAttack:
      if (patch.kc_dur>0) {
        act_stup+=vcom.su_dec_rate;
        if (act_stup>1) { act_stup=1; *phase=eSustain; }
      } else {
        *phase=eSustain;
        act_stup=1;
      }
      break;
    case eSustain:
      break;
    default:
      LOG("calc_startup: phase %d?",*phase);
  }
  return act_stup;
}

void adsr_init() {
  adsr.pt_ind=0;
  adsr.x1=&patch.adsr_x1;
  adsr.x2=&patch.adsr_x2;
  adsr.y2=&patch.adsr_y2;
  adsr.x4=&patch.adsr_x4;

  adsr.pt[0]=(Point){ 0,0 };
  adsr.pt[1]=(Point){ *adsr.x1,1 };
  adsr.pt[2]=(Point){ *adsr.x1+*adsr.x2,*adsr.y2 };
  adsr.pt[3]=(Point){ 2 * subdiv + s_len,*adsr.y2 };
  adsr.pt[4]=(Point){ adsr.pt[3].x + *adsr.x4,0 };

  adsr.cmd=^{
    float sus_vol[2]={ 0.,1. },
          eg_diff[9]={ 1000,500,200,30,10,4,2,1,0.7 };
    eg1.sus=sus_vol[patch.adsr_y2];
    eg1.attack=eg_diff[patch.adsr_x1] * rrate/sample_rate;
    eg1.decay=eg_diff[patch.adsr_x2 + 3] * rrate/sample_rate;
    eg1.release=eg_diff[patch.adsr_x4 + 1] * rrate/sample_rate;
  };
}

static void set_lfo(Values *v,short i) {
  float mix=(float)i/nr_samples;
  v->lfo2_tmp=vcom.lfo2[0] * (1 - mix) + vcom.lfo2[1] * mix;
  if (vcom.trem_enabled) v->mult_am=v->lfo2_tmp * vcom.trem_val;
  else v->mult_am=0;
}

static float sine(TableSin *ts,float pos) {
  short ind = pos;
  if (ind>=0) return ts->sin_table[ind%ts_dim];
  return -ts->sin_table[(-ind)%ts_dim];
}

static float oscillator(Values *v,float fr1) {
  if (v->fm_pos > ts_dim) { v->fm_pos -= ts_dim; v->odd=!v->odd; }
  float out=0,
        nlin_val=0;
  switch (patch.non_lin) {
    case eFM_1: nlin_val=2 * sine(&t_sin1,v->fm_pos); break;
    case eFM_12: nlin_val=2 * sine(&t_sin2,v->fm_pos); break;
    case eFM_half:
      if (v->odd) nlin_val=2 * -sine(&t_sin4,v->fm_pos);
      else nlin_val=2 * sine(&t_sin4,v->fm_pos);
      break;
    default: nlin_val=0;
  }
  const float
    nlin_mult = vcom.modwheel_val * v->freq_track,
    pit2_det = vcom.pit2_det * v->freq_track;

  v->fm_pos += fr1 * (1 + vcom.fm_det);

  for (short pit=0;pit<num_pitch_max;++pit) {
    if (!patch.pitch_arr[pit]) continue;
    float add_nlin,
          tmp=0,
          p_freq = v->fr_array[pit],
          *pos1 = v->osc_pos1+pit;
    if (*pos1>ts_dim) *pos1 -= ts_dim;
    if (patch.non_lin>=0) {
      if (patch.non_lin==eFM_id) tmp=sine(&t_sin1,*pos1); else tmp=nlin_val;
      add_nlin=nlin_mult * tmp * ts_dim * 0.1;
    }
    else add_nlin=0;
    out += sine(&t_sin1,*pos1 + add_nlin) * patch.pitch_arr[pit];
    *pos1 += p_freq;
    if (patch.two_pitch[pit]==0) continue;

    float *pos2 = v->osc_pos2+pit;
    if (*pos2>ts_dim) *pos2 -= ts_dim;
    out += 0.7 * sine(&t_sin1,*pos2 + add_nlin) * patch.pitch_arr[pit]; // 0.7: no full canceling
    *pos2 += p_freq * (1 + pit2_det);
  }
  if (v->stup_phase==eAttack && patch.kc_mode>=0) { // key-click tone
    float mult=vcom.kc_amp * v->stup_amp;
    if (vcom.kc_type==eKc_tone) {
      if (v->stup_pos>ts_dim) v->stup_pos -= ts_dim;
      if (v->stup_pos<ts_dim/2) { // sine burst
        out += mult * sine(&t_sin1,2*v->stup_pos) * 10;
      }
      v->stup_pos += fr1 * vcom.kc_freq;
    }
    else {
      static int cntr;
      static float val;
      if (++cntr % 20==0) val=(float)(rand()) / RAND_MAX - 0.5;
      out += mult * val * 10;
    }
  }
  return out * (1+v->mult_am);
}

static void fill_buffer(Values *v,float *buffer) {
  for (int i=0;i<nr_samples;++i) {
    if (i%rrate==0) {
      if (v->eg1_phase!=eIdle) {
        v->eg1_val=calc_eg1(v,v->eg1_val,&v->eg1_phase);
        v->act_startup=calc_startup(v,v->act_startup,&v->stup_phase);
        v->stup_amp=1 - v->act_startup * v->act_startup;

        v->fr_val1=v->freq_val * freq_scale;
        v->act_ampl=v->eg1_val * v->key_velocity * 0.01; // ampl scaling
        set_lfo(v,i);
      }
    }
    if (v->eg1_phase==eIdle) continue;
    buffer[i] += oscillator(v,v->fr_val1) * v->act_ampl;
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
  while (mkb_connected) {
    once_per_frame();
    bzero(buffer,sizeof(buffer));
    for (short v=0; v<voices_max; ++v) {
      fill_buffer(vals+v,buffer);
    }
    for (short i=0;i<nr_samples;++i) {
      buffer[i] *= vcom.output_ampl;
      mon_get(&mon_data,buffer[i]);
    }
    split(buffer,out_buf);
    if (mkb_connected && recording) {
      dump_wav(out_buf,nr_samples);
    }
    if (mkb_connected) {
      if (snd_write(out_buf,nr_samples)<0) {
        mkb_connected=false; break;
      }
    }
  }
  snd_close();
  return 0;
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

int main(int argc,char **argv) {
  read_screen_dim(&tw_colums,&tw_rows);
  if (tw_colums<93 || tw_rows<15) {
    printf("window = %d x %d (must be 93 x 15)\n",tw_colums,tw_rows);
    return 1;
  }
  LOG("cols=%d rows=%d",tw_colums,tw_rows);
  if (snd_init(sample_rate,nr_samples,2) < 0) {
    puts("pulse-audio init failed");
    return 1;
  }
  if (!open_midi_kb()) {
    puts("midi keyboard not connected");
    return 1;
  }
  mkb_connected=true; // needed for play()
  pthread_create(&conn_thread,0,connect_mkeyb,0);
  pthread_create(&audio_thread, 0, play, 0);

  tw_gui_init();

  do_exit= ^{
    mkb_connected=false;
    if (audio_thread) pthread_join(audio_thread,0);
    // connect thread may block, so not joined
  };

  patch=bi_patches[0];
  tw_key_event=^(uint16_t ch) {
    LOG("key=%c",ch);
    if (ch=='p') report_patch(stderr);
  };

  for (int i=0;i<voices_max;++i) {
    vals[i].freq_val=mid_C;
    vals[i].eg1_phase=eIdle;
  }
  tsin_set(&t_sin1,eFM_1);
  tsin_set(&t_sin2,eFM_12);
  tsin_set(&t_sin3,eFM_14);
  tsin_set(&t_sin4,eFM_half);

  tw_custom_init(
    &harmonics,
    (Rect){1,0,(num_pitch_max+1)*h_xgrid,h_amp_max},
    harm.harm_draw,
    harm.harm_mouse
  );
  Rect r = harmonics.wb.area;
  for (short i=0;i<num_pitch_max;++i) {
    short h_nr = i+1;
    if (h_nr%2==0) {
      char buf[10]; snprintf(buf,10,"%d",h_nr/2);
      tw_print(r.x + h_nr * h_xgrid, r.y+r.h,0,buf,0);
    }
  }
  tw_hline(r.x, r.w, r.y, col_lbrown);
  tw_print(r.x + r.w/2, r.y, col_lbrown, "harmonics", MidAlign);

  short ypos=9;
  short xpos=1;

  tw_menu_init(
    &fm_mode,
    (Rect){xpos,ypos,8,6},
    &patch.non_lin,
    "FM",
    (char*[]){ "h: id","h: .5","h: 1","h: 1+2",0 },
    ^{ if (fm_mode.prev_val==patch.non_lin) patch.non_lin=-1; }
  );

  xpos += 11;
  tw_custom_init(
    &adsr_win,
    (Rect){ xpos,ypos+1,18,2 },
    adsr_draw,
    adsr_mouse
  );
  r = adsr_win.wb.area;
  tw_hline(r.x, r.w, r.y-1, col_lbrown);
  tw_print(r.x+r.w/2, r.y-1, col_lbrown, "ADSR", MidAlign);
  adsr_init();
  adsr.cmd();

  tw_checkbox_init(
    &record,
    (Rect){xpos,ypos+5,7,1},
    &recording,
    "record", ^{
        if (recording) {
          if (init_dump_wav("out.wav",2,sample_rate)) {
            LOG("recording");
          } 
          else {
            recording=false;
            usleep(200000);
            checkbox_draw(&record);
          }
        }
        else {
          close_dump_wav();
          LOG("stop recording");
        }
      }
  );

  xpos+=21;
  tw_menu_init(
    &keycl_mn,
    (Rect){xpos,ypos,8,6},
    &patch.kc_mode,
    "keyclick",
    (char *[]){ "f=0.5","f=1","f=2","chiff",0 }, // bingo!
    ^{ if (keycl_mn.prev_val==patch.kc_mode) patch.kc_mode=-1;
       if (patch.kc_mode>=0) {
         float kc_freq[4]={ 0.5,1,2 };
         if (patch.kc_mode<3) { vcom.kc_type=eKc_tone; vcom.kc_freq=kc_freq[patch.kc_mode]; }
         else vcom.kc_type=eKc_noise;
       }
     }
  );
  keycl_mn.cmd();

  xpos=45; ypos=0;
  tw_hor_slider_init(
    &detune_fm,
    (Rect){xpos,ypos,0,0},
    5,
    &patch.detune_fm,
    ^{ float detun[6]={ 0,0.001,0.002,0.003,0.005,0.01 };
       vcom.fm_det=detun[patch.detune_fm];
       sprintf(detune_fm.title,"det FM=%.1g",vcom.fm_det);
     }
  );
  detune_fm.cmd();

  tw_hor_slider_init(
    &detune_2pitch,
    (Rect){xpos,ypos+=2,0,0},
    5,
    &patch.detune_2p,
    ^{ float detun[6]={ 0,0.001,0.002,0.003,0.005,0.01 };
       vcom.pit2_det=detun[patch.detune_2p];
       sprintf(detune_2pitch.title,"det 2p=%.1g",vcom.pit2_det);
     }
  );
  detune_2pitch.cmd();

  tw_hor_slider_init(
    &fm_modu,
    (Rect){xpos,ypos+=2,0,0},
    5,
    &patch.fm_modu,
    ^{ vcom.modwheel_val=patch.fm_modu / 5.;
       sprintf(fm_modu.title,"FM amount=%.1g",vcom.modwheel_val);
     }
  );
  fm_modu.cmd();

  tw_hor_slider_init(
    &keycl_dur,
    (Rect){xpos,ypos+=2,0,0},
    6,
    &patch.kc_dur,
    ^{ float kc_dur[7]={ 0,20,10,6,3,2,1 };
       vcom.su_dec_rate=kc_dur[patch.kc_dur] * rrate / sample_rate * 5;
       if (patch.kc_dur)
         sprintf(keycl_dur.title,"kc dur=%.1g",0.01/vcom.su_dec_rate);
       else sprintf(keycl_dur.title,"kc dur=0");
     }
  );
  keycl_dur.cmd();

  tw_hor_slider_init(
    &keycl_amp,
    (Rect){xpos,ypos+=2,0,0},
    3,
    &patch.kc_amp,
    ^{ float kc_amp[4]={ 0.3,0.5,1,1.7 };
       vcom.kc_amp= kc_amp[patch.kc_amp];
       sprintf(keycl_amp.title,"kc ampl=%.1g",vcom.kc_amp);
     }
  );
  keycl_amp.cmd();

  tw_hor_slider_init(
    &vib_sl,
    (Rect){xpos,ypos+=2,0,0},
    8,
    &patch.trem_val,
    ^{ float fmod_arr[9]={ 0.7,0.4,0.2,0.1,0,0.1,0.2,0.4,0.7 };
       vcom.trem_val=fmod_arr[patch.trem_val];
       sprintf(vib_sl.title,"tremolo=%.1g",vcom.trem_val);
       vcom.trem_enabled= patch.trem_val != 4;
     }
  );
  vib_sl.cmd();
  tw_hor_slider_init(
    &volume_sl,
    (Rect){xpos,ypos+=3,0,0},
    5,
    &patch.out_volume,
    ^{ vcom.output_ampl=outvol[patch.out_volume];
       sprintf(volume_sl.title,"volume=%.1g",vcom.output_ampl);
    }
  );
  volume_sl.cmd();


  xpos+=13;
  tw_custom_init(
    &monitor,
    (Rect){xpos,6,1,8},
    ^(tw_Custom *mon) {
      tw_vline(mon->wb.area.x,mon->wb.area.y,mon->wb.area.h, col_grey);
    },
    0
  );
  tw_print(xpos+1,14,0,"output",MidAlign);

  r=monitor.wb.area;
  mon_data.xpos=r.x;
  mon_data.ypos=r.y+r.h;

  char* patch_labels[tw_alen(bi_patches)+1];
  for (int i=0;;++i) {
    if (i==tw_alen(bi_patches)) { patch_labels[i]=0; break; }
    patch_labels[i]=bi_patches[i].name;
    //LOG("name=%s",patch_labels[i]);
  }
  xpos+=6;
  tw_menu_init(
    &patch_mn,
    (Rect){xpos,0,26,15},
    &patch_nr,
    "patches",
    patch_labels,^{
        patch=bi_patches[patch_nr];
        keycl_dur.cmd(); hor_slider_draw(&keycl_dur);
        keycl_amp.cmd(); hor_slider_draw(&keycl_amp);
        volume_sl.cmd(); hor_slider_draw(&volume_sl);
        fm_modu.cmd(); hor_slider_draw(&fm_modu);
        detune_fm.cmd(); hor_slider_draw(&detune_fm);
        detune_2pitch.cmd(); hor_slider_draw(&detune_2pitch);
        vib_sl.cmd(); hor_slider_draw(&vib_sl);
        menu_draw(&keycl_mn); keycl_mn.cmd();
        menu_draw(&fm_mode);
        harmonics.draw(&harmonics); 
        adsr_init();
        adsr_draw(&adsr_win);
        adsr.cmd();
    }
  );

  tw_draw();

  tw_start_main_loop();

  return 0;
}
