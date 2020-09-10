#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

#include <term-win.h>

tw_Button button;
tw_Slider slider;
tw_HorSlider hor_slider;
tw_VertSlider vert_slider;
tw_Menu menu;
tw_Checkbox checkbox;

short
  slider_val=2,
  hor_slider_val,
  vert_slider_val,
  menu_val;
bool
  chb_val;

typedef struct {
  tw_Custom bwin;
  char *title;
  short eye; // left eye
} AsciArt;

AsciArt asci_art;

void (^cust_draw)(tw_Custom *)=^(tw_Custom *bw) {
  tw_box(bw->wb.area,col_lblue);
  AsciArt *art=(AsciArt*)bw;
  // AsciArt *art=(void*)bw - offsetof(AsciArt,bwin); // ot needed: offsetof() = 0
  tw_print(bw->wb.area.x, bw->wb.area.y-1, 0, art->title, 0);

  short arr[][7]= {
         { 6,6,0,0,0,6,6 },
         { 0,asci_art.eye,0,0,0,4,0 },
         { 0,0,0,5,0,0,0 },
         { 0,2,1,1,1,3,0 } };
  short x=bw->wb.area.x+2,
        y=bw->wb.area.y+1;
  for (int j=0;j<tw_alen(arr);++j) {
    for (int i=0;i<tw_alen(arr[0]);++i) {
      uint16_t ch;
      short X=i+x,
            Y=j+y;
      switch (arr[j][i]) {
        case 1: ch='_'; break;
        case 2: ch='\\'; break;
        case 3: ch='/'; break;
        case 4: ch='O'; break;
        case 5: ch='v'; break;
        case 6: ch='='; break;
        case 7: ch='_'; break;
      }
      if (arr[j][i]>0) tw_set_cell(X,Y,ch,0,col_lblue,1);
    }
  }
};

void (^cust_mouse)(tw_Custom*,short,short,short,short)=^(tw_Custom* cust,short x,short y,short mode,short button) {
  LOG("cust_mouse: x=%d, y=%d, mode=%d, but=%d",x,y,mode,button);
};

int main(int argc,char **argv) {
  tw_gui_init();
  
  tw_button_init(
    &button,
    (Rect){1,1,7,1},
    "button",
    ^{ LOG("button cmd: down=%d",button.is_down);
       asci_art.eye= button.is_down ? 7 : 4;
       asci_art.bwin.draw(&asci_art.bwin);
       // cust_draw(&asci_art.bwin);  // same, global cust_draw used
     }
  );
  tw_slider_init(
    &slider,
    (Rect){1,5,12,2},
    7,
    &slider_val,
    "slider",
    ^{ LOG("slider cmd, val=%d",slider_val); }
  );

  tw_hor_slider_init(
    &hor_slider,
    (Rect){1,8,0,0},
    7,
    &hor_slider_val,
    ^{ snprintf(hor_slider.title,amax,"sl2: %d",hor_slider_val);
     }
  );
  hor_slider.cmd();

  tw_menu_init(
    &menu,
    (Rect){18,1,16,5},
    &menu_val,
    "menu",
    (char *[]){ "aap","noot","mies","wim","zus","jet",0 },
    ^{ if (menu.prev_val==menu_val) menu_val=-1;
       LOG("menu: title=%s val=%d (%s)",menu.title,menu_val,menu.labels[menu_val]);
    }
  );
  menu.style=Compact;
  
  tw_checkbox_init(
    &checkbox,
    (Rect){1,3,12,1},
    &chb_val,
    "checkbox",
    ^{ LOG("checkbox cmd, title=%s",checkbox.title); }
  );

  tw_custom_init(
    &asci_art.bwin,
    (Rect){38,2,11,6},
    cust_draw,
    cust_mouse
  );
  asci_art.title="That's me";
  asci_art.eye=4;

  tw_vert_slider_init(
    &vert_slider,
    (Rect){55,1,0,0},
    4,
    &vert_slider_val,
    ^{ sprintf(vert_slider.title1,"sl3");
       sprintf(vert_slider.title2,"val=%d",vert_slider_val);
     }
  );
  vert_slider.cmd();
  tw_draw();

  tw_start_main_loop();

  exit(0);
}
