#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <alsa/asoundlib.h>

#include <term-win.h>
#include "shared.h"

enum {
  nr_samples=256,
  sample_rate=44100,
  nr_voices=4,
  rrate=8,
  nr_wforms=6,
  nr_dist=4,
  nr_d_level=6
};
enum { Off, On, Steady, Decay };
typedef struct {
  uint8_t cur_midi_nr;
  float
    freq,
    ampl, cur_ampl,
    pos1, pos2;
  int8_t
    state;
} Voice;
Voice voices[nr_voices];

static int
  fd_in = -1;
int cnt;  // for debug
static bool
  recording;
static float
  snd_buf[nr_samples],
  dist_lev=1,
  d_lev[nr_d_level]={ 0.3,0.5,0.7,1.,1.5,2. };
static short
  snd_buf2[nr_samples*2],
  waveform=2,
  decay=1,
  distortion,
  post,
  dist_level=2;
static const float
  PI2 = 2*M_PI,
  freq_scale = 2*M_PI / sample_rate;
static tw_Menu
  wform,
  piano,
  dist,
  pre_post;
static tw_Checkbox
  record;
static tw_HorSlider
  dist_amount;

bool open_midi_kb() {
  char *node[3]={ "/dev/midi", "/dev/midi1", "/dev/midi2" };
  for (int i=0; i<3; ++i) {
    fd_in = open(node[i], O_RDONLY | O_NONBLOCK, 0);
    if (fd_in>=0) {
      LOG("midi: %s (fd=%d)",node[i],fd_in);
      return true;
    }
  }
  LOG("midi: open failed");
  return false;
}

void close_midi_kb() {
  if (fd_in >= 0) {
    sprintf(quit_message,"midi: closing (fd=%d)",fd_in);
    close(fd_in); fd_in=-1;
  }
}

static float interpol(const float *arr, float fi, const int arr_len) {
  const int ind1=fi,
            ind2=tw_min(ind1+1,arr_len-1);
  float mix=fi - (float)ind1;
  return arr[ind1] * (1. - mix) + arr[ind2] * mix;
}

static void mod_wheel(uint8_t val) {   // modulation wheel
  float fval=val / 128. * 5.; 
  int ind=lrint(fval);
  if (ind!=dist_level) {
    dist_level=ind;
    dist_amount.cmd();
    hor_slider_draw(&dist_amount);
    fflush(stdout); // else no complete draw
  }
  dist_lev=interpol(d_lev, fval, 5);
}

void eval_midi(uint8_t *midi_buf) {
  //printf("read: %x,%2d,%2d\n", midi_buf[0],midi_buf[1],midi_buf[2]);
  int mess = midi_buf[0],
      mnr = midi_buf[1],
      param = midi_buf[2];
  static int v_nr;
  Voice *v;
  if (mess == 0x90 && param>0) {
    v_nr = (v_nr+1) % nr_voices;
    v = voices+v_nr;
    v->state=On;
    v->cur_midi_nr=mnr;
    v->freq=440.0 * powf(2.0, (mnr - 69) / 12.0);
    v->ampl = param/128.;
  }
  else if (mess == 0x80 || param==0) {
    for (v=voices; v-voices<nr_voices; ++v)
      if (mnr == v->cur_midi_nr) v->state=Off;
  }
  else if (mess == 0xb0 && mnr==1) {
    mod_wheel(param);
  }
  else {
    LOG("midi message %d %d %d",mess,mnr,param);
  }
}

void fill_buffer(float *buffer) {
  bzero(buffer,nr_samples*sizeof(float));
  for (Voice *v=voices; v-voices<nr_voices; ++v) {
    for (int i=0; i<nr_samples; ++i) {
      if (v->pos1>PI2) v->pos1-=PI2;
      if (v->pos2>PI2) v->pos2-=PI2;
      if (i%rrate==0) {
        switch (v->state) {
          case On:
            v->cur_ampl = (v->ampl + 0.1 - v->cur_ampl)*0.1 + v->cur_ampl;
            if (v->ampl - v->cur_ampl < 0) {
              if (decay) v->state=Decay;
              else v->state=Steady;
            }
            break;
          case Steady: break;
          case Decay: v->cur_ampl = v->cur_ampl * 0.9997; break;
          case Off: v->cur_ampl = v->cur_ampl * 0.999; break;
        }
      }
      v->pos1 += v->freq*freq_scale;
      float wf;
      switch (waveform) {
        case 0:
          wf = sinf(v->pos1); break;
        case 1:
          wf = sinf(v->pos1) + 0.5*sinf(2 * v->pos1); break;
        case 2:
          wf = sinf(v->pos1) + 0.5*sinf(4.*v->pos1); break;
        case 3:
          wf = sinf(v->pos1) + 0.4*sinf(2.*v->pos1) + 0.6*sinf(2.*v->pos2);
          v->pos2 += (v->freq*freq_scale + 0.0001) * 1.001;
          break;
        case 4:
          wf = sinf(v->pos1) + 0.5*sinf(4.*v->pos1) + 0.5*sinf(4.*v->pos2);
          v->pos2 += (v->freq*freq_scale + 0.0001) * 1.002;
          break;
        case 5:
          wf= (sinf(v->pos2*2) + sinf(v->pos1*3) + sinf(v->pos1*4)) * 0.5;
          v->pos2 += (v->freq*freq_scale + 0.0001) * 1.002;
          break;
      }
      if (distortion == 0) {
        buffer[i] += v->cur_ampl * wf;
        continue;
      }
      wf *= dist_lev; // distortion amount
      if (post) {
        switch (distortion) { // ampl: post dist
          case 1:
            buffer[i] += tw_fminmax(-0.5, wf*2, 0.5) * v->cur_ampl * 2; break;
          case 2:
            wf *= 1 - fabs(wf) * 0.5;
            buffer[i] += (wf * v->cur_ampl * 3) / dist_lev; break;
          case 3:
            wf *= 2 - fabs(wf);
            wf *= 2 - fabs(wf);
            buffer[i] += (wf * v->cur_ampl) / dist_lev; break;
        }
      }
      else {  // ampl: pre dist
        float out= wf * v->cur_ampl;
        switch (distortion) {
          case 1:
            buffer[i] += tw_fminmax(-0.5, out*2, 0.5) * 2; break;
          case 2:
            out *= 1 - fabs(out);
            buffer[i] += 2 * out / dist_lev; break;
          case 3:
            out *= 2 - fabs(out) / dist_lev;
            out *= 1 - fabs(out);
            buffer[i] += 2 * out / dist_lev; break;
        }
      }
    }
  }
}
void split(float *in_buf, short *out_buf) {
  static float lp;
  const float
    lpf=expf(-PI2 * 1000. / sample_rate),
    scale=10000;
  for (int i=0;i<nr_samples;++i) {
    lp = (1-lpf) * in_buf[i] + lpf * lp;
    out_buf[2*i]=tw_minmax(-32000,lp * scale,32000);
    out_buf[2*i+1]=tw_minmax(-32000, (in_buf[i] - lp) * scale, 32000);
  }
  lp *= 0.998;  // to avoid low-freq ringing at note start
}

void main_loop(int tty0) {
  uint8_t mbuf[3],
          tty_buf[6];
  while (true) {
    fill_buffer(snd_buf);
    split(snd_buf,snd_buf2);
    if (snd_write(snd_buf2,nr_samples) < 0)
      exit(1);
    if (recording)
      if (!dump_wav(snd_buf2,nr_samples))
        exit(1);
    errno=0;
    int n=read(fd_in, mbuf, 3);
    if (n<0) {
      if (errno!=EAGAIN) {
        sprintf(quit_message,"disconnected");
        return;
      }
    }
    if (n==3) { eval_midi(mbuf); }

    bzero(tty_buf,6);
    n=read(tty0, tty_buf, 6);
    if (n>=1) {
      //printf("tty_buf: n=%d, vals:",n);
      //for (int m=0; m<n && m<6; ++m) printf(" %d",tty_buf[m]);
      //putchar('\');
      Event ev;
      if (mouse_event(&ev,tty_buf))
        if (ev.type==Ev_mouse)
          handle_event(ev.mode,ev.x,ev.y,ev.button);  
    }
  }
}

int main() {
  if (!open_midi_kb()) return 1;
  do_exit= ^{ close_midi_kb(); snd_close(); };
  snd_init(44100,nr_samples,2);  
  tw_gui_init();

  tw_menu_init(
    &wform,
    (Rect){1,1,10,nr_wforms+1},
    &waveform,
    "waveform",
    (char*[nr_wforms+1]){ "h 1","h 1+2","h 1+4","h 1+2+2","h 1+4+4","h 2+3+4",0 },
    0
  );
  tw_menu_init(
    &dist,
    (Rect){15,1,10,nr_dist+1},
    &distortion,
    "distortion",
    (char*[nr_dist+1]){ "clean","clipped","x*(1-abs x)","x*(1-abs x)^2", 0 },
    0
  );
  tw_menu_init(
    &piano,
    (Rect){15,7,10,3},
    &decay,
    "ampl mode",
    (char*[nr_dist+1]){ "steady","decay", 0 },
    0
  );
  tw_menu_init(
    &pre_post,
    (Rect){32,1,12,2},
    &post,
    "ampl ctrl",
    (char*[nr_dist+1]){ "pre","post",0 },
    0
  );
  tw_hor_slider_init(
    &dist_amount,
    (Rect){32,4,0,0},
    nr_d_level-1,
    &dist_level,
    ^{ dist_lev=d_lev[dist_level];
       sprintf(dist_amount.title,"dist lev=%-1g",dist_lev);
     }
  );
  tw_checkbox_init(
    &record,
    (Rect){32,7,0,0},
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
  wform.style = dist.style = piano.style = Compact;
  dist_amount.cmd();

  tw_draw();
  char *t = ttyname(STDIN_FILENO);
  int tty0 = open(t, O_RDONLY | O_NONBLOCK);
  main_loop(tty0);
  return 0;
}
