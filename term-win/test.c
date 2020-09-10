// to try out things

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "term-win.h"

int xpos=1,
    ypos=4;

void mouse_event(Event *ev) {
  LOG("mouse: mode=%d x=%d y=%d but=%d",ev->mode,ev->x,ev->y,ev->button);
}

void ch_event(Event *ev) {
  LOG("ev->ch=%d (%c)",ev->ch,ev->ch);
  switch (ev->ch) {
    case 13: xpos=1; ++ypos;   // '\n'
      tw_set_cell(xpos-1,ypos,' ',0,0,0);
      break;
    case 127: --xpos;          // bs
      break;
    default:
      tw_set_cell(xpos++,ypos,ev->ch,0,0,0);
  }
}

void start_test_loop() {
  Event ev;
  while (true) {
    tw_get_event(&ev);
    LOG("tw_start_test_loop: ev.type=%d",ev.type);
    switch (ev.type) {
      case Ev_mouse:
        mouse_event(&ev);
        break;
      case Ev_key:
        if (ev.ch==k_esc) return;
        ch_event(&ev);
        break;
      default:
        LOG("main_loop: ev type=%d",ev.type);
    }
  }
}

int main() {
  tw_gui_init();
  LOG("screen %d x %d",tw_colums,tw_rows);
  tw_set_cell(10,2,'a',0,85,1);
  tw_set_cell(12,2,'b',0,0,1);
  tw_set_cell(10,2,'A',0,85,1);
  tw_set_cell(14,2,0x258f,0,85,1);
  start_test_loop();
  return 0;
}
