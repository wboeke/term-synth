#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

#include <term-win.h>

int base=0x2400;
tw_HorSlider set_base;
short base_val=18;
bool fgcol_val;

tw_Checkbox set_fgcol;

void draw_runes() {
  static char buf[20];
  for (int n=0;n<0x200;++n) {
    uint8_t x=7 * (n/32),
            y=n % 32;
    tw_set_cell(x+3,y,base+n,fgcol_val ? col_red : 0,col_grey,1);
    sprintf(buf,"%x",n);
    tw_print(x, y, 14, buf,0);
  }
}

int main(int argc,char **argv) {
  if (argc>1) base=strtol(argv[1],0,16);
  read_screen_dim(&tw_colums,&tw_rows);
  if (tw_colums<110 || tw_rows<35) {
    printf("window = %d x %d (must be 110 x 35)\n",tw_colums,tw_rows);
    return 1;
  }
  tw_gui_init();
  draw_runes();
  tw_hor_slider_init(
    &set_base,
    (Rect){20,33,0,0},
    25,
    &base_val,
    ^{ base=base_val * 0x200;
       sprintf(set_base.title,"base=%x",base);
       if (!set_base.touched) draw_runes();
     }
  );
  set_base.cmd_at_up=true;
  set_base.cmd();

  tw_checkbox_init(
    &set_fgcol,
    (Rect){55,34,15,1},
    &fgcol_val,
    "fg color: red",
    ^{ draw_runes(); } 
  );

  tw_draw();
  tw_start_main_loop();
  exit(0);
}
