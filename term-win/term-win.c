#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>

#include "term-win.h"

uint8_t
  tw_colums,
  tw_rows;

static bool raw_mode=false;
char quit_message[100];

static struct termios
  cur_termios,
  old_termios;

void read_screen_dim(uint8_t *co,uint8_t *ro) {
  struct winsize ws;
  bzero(&ws,sizeof(ws));
  ioctl(0,TIOCGWINSZ,&ws);
  *ro=ws.ws_row;
  *co=ws.ws_col;
}

void tw_exit(void) {
  if (raw_mode) {
    fputs("\033[38;5;0;48;5;0\033[m",stdout); // set default fg and bg, needed to reset xfce terminal
    fputs("\033[?25h",stdout);   // make cursor visible
    fputs("\033[?1049l",stdout); // restore screen
    fputs("\033[?1002l",stdout); // exit mouse
    tcsetattr(0, TCSAFLUSH, &old_termios);
  }
  if (do_exit) do_exit();
  if (quit_message[0]) {
    LOG("%s",quit_message);
    puts(quit_message); quit_message[0]=0;
  }
  else puts("Bye!");
}

void sig_handler(int sig) {
  switch (sig) {
    case SIGINT: exit(0);
    default: printf("signal: %d\n",sig);
  }
}

static void term_init() {
  tcgetattr(0,&cur_termios);
  old_termios=cur_termios;

  //cfmakeraw(&cur_termios);
  cur_termios.c_iflag |= IGNBRK;         // ignore breaks
  cur_termios.c_lflag &= ~(ICANON|ECHO); // canonical input, disable echo (not disable signals)

  // important to prevent underruns:
  cur_termios.c_cc[VTIME]=1;  // timeout 100ms
  cur_termios.c_cc[VMIN]=3;   // char buffer 3 chars (0: block if no byte)

  tcsetattr(0,TCSADRAIN,&cur_termios);

  fputs("\033[?1049h",stdout);    // preserve screen
  fputs("\033[?1002h",stdout);    // all mouse events enabled, if button pressed
  fputs("\033[?25l",stdout);      // cursor invisible (visible: "\033[?25h")
  fputs("\033[2J",stdout);        // clear screen

  raw_mode=true;
  signal(SIGINT,sig_handler);
  atexit(tw_exit); 
  if (!tw_colums)  // maybe in main()
    read_screen_dim(&tw_colums,&tw_rows);
}

char *unicode_to_char(uint32_t c) {
  int len = 0,
      first;
  if (c < 0x80) { first = 0; len = 1; }
  else if (c < 0x800) { first = 0xc0; len = 2; }
  else if (c < 0x10000) { first = 0xe0; len = 3; }
  else if (c < 0x200000) { first = 0xf0; len = 4; }
  else if (c < 0x4000000) { first = 0xf8; len = 5; }
  else { first = 0xfc; len = 6; }

  static char out[10];
  bzero(out,10);
  for (int i = len - 1; i > 0; --i) {
    out[i] = (c & 0x3f) | 0x80;
    c >>= 6;
  }
  out[0] = c | first;
  return out;
}

void set_color(uint8_t fg, uint8_t bg) {
  if (bg==0)      printf("\033[4;9;m\033[38;5;%dm",fg); // default bg color. NB! eerst [4
  else if (fg==0) printf("\033[3;9;m\033[48;5;%dm",bg); // default fg color. NB! eerst [3
  else            printf("\033[38;5;%d;48;5;%dm",fg,bg);
}

void tw_goto(short x,short y) {
  printf("\033[%d;%dH",y+1,x+1);
}

void tw_set_cell(uint8_t x, uint8_t y, uint16_t ch, uint8_t fg, uint8_t bg, uint16_t nr) {
  if (x>=tw_colums || y>=tw_rows)
    return;
  tw_goto(x,y);
  set_color(fg,bg);
  for (int n=0; n<nr; ++n)
    fputs(unicode_to_char(ch),stdout);
}

void tw_get_event(Event *ev) {
  uint8_t ch,
          buf[3];
  ch=getchar();
  //LOG("tw_get_event: ch=%d",ch);
  if (ch==k_esc) {
    ch=getchar();
    if (ch=='[') {
      ch=getchar();
      if (ch=='M') {
        uint8_t nr=getchar(),
                x=getchar(),
                y=getchar();
        ev->type=Ev_mouse;
        ev->mode= (nr & 64)/64 ? Mouse_move : nr==35 ? Mouse_up : Mouse_down;
        ev->button= ev->mode==Mouse_up ? 99 : nr & 3;
        ev->x=x-33;
        ev->y=y-33;
/* button constants:
*         but0  but1  but2
*   down   32    33    34
*   move   64    65    66
*   up     35    35    35
*/
      }
    }
    else {
      ev->type=Ev_key;
      ev->ch=k_esc;
    }
  }
  else {
    ev->type=Ev_key;
    ev->ch=ch;
  }
}

//#include "tw-gui.c"
Widgets widgets;

void tw_gui_init() {
  term_init();
  widgets.lst=0;
  widgets.len=max_wids;
  widgets.wid_p=malloc(max_wids * sizeof(WBase*));
}

enum {
  is_but,
  is_slider,
  is_hor_slider,
  is_vert_slider,
  is_menu,
  is_checkbox,
  is_custom
};

uint8_t
  col_wid_bg=0,
  col_black=8,
  col_white=15,
  col_grey=grey+20, col_dgrey=grey+12, // grey
  col_lblue=153, col_mblue=75, col_dblue=105, // blue
  col_red=196, col_lred=209,   // red
  col_yel=226,  // yellow
  col_lbrown=223;  // brown

void tw_hline(short x1,short len,short y,short col) {
  tw_set_cell(x1, y, ' ', 0, col, len);
}

void tw_thin_hline(short x1,short len,short y,short col,short bg) {
  tw_set_cell(x1, y, 0x25ac, col, bg, len);
}

void tw_vline(short x,short y1,short len,short col) {
  for (short y=y1;y<y1+len;y++)
    tw_set_cell(x, y, ' ', 0, col, 1);
}

void tw_thin_vline(int x,int y1,int len,uint8_t col,uint8_t bg) {
  for (int y=y1;y<y1+len;y++)
    tw_set_cell(x, y, 0x2590, col, bg, 1);
}

void tw_box(Rect rect,short col) {
  for (short y=rect.y;y<rect.y+rect.h;y++) {
    tw_set_cell(rect.x, y, ' ', 0, col, rect.w);
  }
}

void tw_print(short x,short y,short bg,char *msg,short spec) {
  if (!msg) return;
  const short len=strlen(msg);
  if (!len) return;
  if (spec & RightAlign) x-=len; // nog doen
  else if (spec & MidAlign) x-=len/2;
  tw_goto(x,y);
  if (bg==0) printf("\033[38;5;%dm\033[4;9;m",col_black); // default bg color
  else printf("\033[38;5;%d;48;5;%dm",col_black,bg);
  for (char *p=msg; *p; ++p) {
    if (x+(msg-p) >= tw_colums) break;
    if (*p=='\n') { ++y; tw_goto(x,y); }
    else putc(*p,stdout);
  }
/*
  // wil niet, bug van clang? NB! title=tit zou vervangen moeten worden door title=strdup(tit)
  for (char *p1=msg, *p2=p1; ; ++p2) {
    if (!*p2) { fputs(p1,stdout); break; }
    if (*p2=='\n') {
      *p2=0; fputs(p1,stdout); *p2='\n';
      p1=p2+1; if (!p1) break;
      ++y;
      tw_goto(x,y);
    }
  }
*/
}

void button_draw(tw_Button *but) {
  short col= but->is_down ? col_mblue : col_lblue;
  tw_box(but->wb.area,col);
  tw_print(but->wb.area.x,but->wb.area.y,col,but->title,0);
}

void add_to_list(void* widget,short type) {
  if (widgets.lst==max_wids-1) { sprintf(quit_message,"more then %d widgets",max_wids); exit(1); }
  widgets.wid_p[widgets.lst]=(WBase*)widget;
  widgets.wid_p[widgets.lst]->type=type;
  ++widgets.lst;
}

void tw_button_init(tw_Button *but,Rect rect,char *tit,void (^_cmd)()) {
  add_to_list(but,is_but);
  but->wb.area=rect;
  but->title=tit;
  but->is_down=false;
  but->cmd=_cmd;
}

short sl_val(tw_Slider *sl,short ev_x) {
  return (ev_x - sl->wb.area.x) / sl->xgrid;
}
static short hsl_val(tw_HorSlider *sl,short ev_x) {
  return (ev_x - sl->wb.area.x) / sl->xgrid;
}
static short vsl_val(tw_VertSlider *sl,short ev_y) {
  return (sl->wb.area.h - ev_y + sl->wb.area.y -1) / sl->ygrid;
}

void slider_draw(tw_Slider *sl) {
  //LOG("slider_draw: title=%s val=%d",sl->title,*sl->val);
  char buf[20];
  snprintf(buf,20,"%d",*sl->val);
  tw_box(sl->wb.area,col_wid_bg);
  tw_print(sl->wb.area.x,sl->wb.area.y,0,sl->title,0);
  tw_print(sl->wb.area.x+sl->wb.area.w,sl->wb.area.y + 1,0,buf,RightAlign);
  short pos_x = sl->wb.area.x + *sl->val * sl->xgrid,
        pos_y = sl->wb.area.y + 1;
  tw_thin_hline(sl->wb.area.x,sl->xgrid * (sl->maxv) + 2,pos_y,col_dgrey,0); // bar
  //tw_set_cell(pos_x,pos_y,0x2b55,col_lred,0,1);   // pointer (red circle)
  tw_set_cell(pos_x,pos_y,0x2586,col_lred,0,2);   // pointer (rectangle)
}

void hor_slider_draw(tw_HorSlider *sl) {
  Rect area=sl->wb.area;
  tw_box(area,col_wid_bg);
  tw_hline(area.x,sl->title_len,area.y,0); // clear old title
  sl->title_len=strlen(sl->title);
  tw_print(area.x,area.y,0,sl->title,0);
  short pos_y = area.y + 1,
        pos_x = area.x + *sl->val * sl->xgrid;
  tw_thin_hline(area.x,area.w,pos_y,col_dgrey,0); // bar
  tw_set_cell(pos_x, pos_y, 0x2586, col_lred, 0, 2); // pointer, about square
}

void vert_slider_draw(tw_VertSlider *sl) {
  Rect area=sl->wb.area;
  tw_box(area,col_wid_bg);
  tw_hline(area.x,sl->title_len1,area.y,0); // clear old title
  tw_hline(area.x,sl->title_len2,area.y+area.h,0); // clear old title
  sl->title_len1=strlen(sl->title1);
  sl->title_len2=strlen(sl->title2);
  tw_print(area.x,area.y,0,sl->title1,0);
  tw_print(area.x,area.y+area.h,0,sl->title2,0);
  short pos_x = area.x + 1,
        pos_y = area.y + area.h - *sl->val * sl->ygrid - 1;
  tw_thin_vline(pos_x,area.y+1,area.h-1,col_dgrey,0); // bar
  tw_set_cell(pos_x, pos_y, ' ', 0, col_lred, 1); // pointer
  tw_set_cell(pos_x+1, pos_y, 0x258e, col_lred, 0, 1);
}

void tw_slider_init(tw_Slider *sl,Rect rect,short mv,short *v,char *tit,void (^_cmd)()) {
  add_to_list(sl,is_slider);
  sl->wb.area=(Rect){ rect.x, rect.y, rect.w, 2 };
  sl->title=tit;
  sl->val=v;
  sl->maxv=mv;
  sl->xgrid= mv>3 ? 1 : 2;
  sl->cmd=_cmd;
}

void tw_hor_slider_init(tw_HorSlider *sl,Rect rect,short mv,short *v,void (^_cmd)()) {
  add_to_list(sl,is_hor_slider);
  sl->val=v;
  sl->maxv=mv;
  sl->xgrid= mv>3 ? 1 : 2;
  sl->wb.area=(Rect){ rect.x, rect.y, sl->xgrid * sl->maxv + 2, 2 };
  sl->cmd=_cmd;
}

void tw_vert_slider_init(tw_VertSlider *sl,Rect rect,short mv,short *v,void (^_cmd)()) {
  add_to_list(sl,is_vert_slider);
  sl->val=v;
  sl->maxv=mv;
  sl->ygrid=1;
  sl->wb.area=(Rect){ rect.x, rect.y, 3, sl->ygrid * sl->maxv + 2 };
  sl->cmd=_cmd;
}

int lab_len(char **lab) {
  for (int i=0;;++i) { if (!lab[i]) return i; }
}

void tw_menu_init(tw_Menu *men,Rect rect,short *v,char *tit,char **lab,void (^_cmd)()) {
  add_to_list(men,is_menu);
  men->wb.area=rect;
  men->nr_rows=rect.h-1;
  men->title=tit;
  men->val=v;
  men->labels=lab;
  men->nr_items=lab_len(lab);
  men->nr_cols=tw_divup(men->nr_items,men->nr_rows);
  men->wid=men->wb.area.w/men->nr_cols;
  men->style=Wide;
  men->cmd=_cmd;
}

void tw_checkbox_init(tw_Checkbox *chb,Rect area,bool *val,char *tit,void (^cmd)()) {
  add_to_list(chb,is_checkbox);
  chb->wb.area=(Rect){ area.x,area.y,2,1 };
  chb->val=val;
  chb->title=tit;
  chb->cmd=cmd;
}

void checkbox_draw(tw_Checkbox *chb) {
  tw_box(chb->wb.area,col_wid_bg);
  uint8_t col= *chb->val==1 ? col_lred : col_grey;
  tw_hline(chb->wb.area.x,2,chb->wb.area.y,col);
  tw_print(chb->wb.area.x+2,chb->wb.area.y,0,chb->title,0);
}

void menu_draw_item(tw_Menu *men,short ind) {
  if (ind<0) return; // no item selected
  if (ind>men->nr_items) { LOG("menu: %d > nr_items",ind); return; }
  short i=ind/men->nr_rows,
        j=ind%men->nr_rows,
        xx=i*men->wid+men->wb.area.x,
        yy = 1+j+men->wb.area.y;
  short col= *men->val==ind ? col_yel : col_grey;
  switch (men->style) {
    case Wide:
      tw_hline(xx, men->wid, yy,col);
      tw_print(xx+1,yy,col,men->labels[ind],0);
      break;
    case Compact:
      tw_set_cell(xx,yy,0x2587,col,0,1);
      tw_print(xx+2,yy,0,men->labels[ind],0);
      break;
  }
}

void menu_draw(tw_Menu *men) {
  Rect rect=men->wb.area;
  tw_hline(rect.x, rect.w, rect.y,col_lbrown);
  tw_print(rect.x+rect.w/2,rect.y,col_lbrown,men->title,MidAlign);
  for (short ind=0;ind<men->nr_items;++ind)
    menu_draw_item(men,ind);
}

void menu_mouse_press(tw_Menu *men,short ev_x,short ev_y) {
  if (ev_y<1) return; // in title area?
  short ind = ev_x / men->wid * men->nr_rows + ev_y - 1;
  if (ind>=men->nr_items) return;
  men->prev_val=*men->val;
  *men->val=ind;
  if (men->cmd) men->cmd();
  menu_draw_item(men,men->prev_val);
  menu_draw_item(men,ind);
}

void tw_custom_init(tw_Custom *cust,Rect area,void (^draw)(tw_Custom*),
    void (^mouse_event)(tw_Custom*,short x,short y,short mode,short button)) {
  add_to_list(cust,is_custom);
  cust->wb.area=area;
  cust->draw=draw;
  cust->mouse_event=mouse_event;
}

void tw_draw() {
  WBase **wb=widgets.wid_p;
  //LOG("wb=%p",wb); if (wb) LOG("wb[0]=%p",wb[0]);
  for (short i=0;i<widgets.lst;++i) {
    short type=wb[i]->type;
    switch (type) {
      case is_but:
        button_draw((tw_Button*)(wb[i]));
        break;
      case is_slider:
        slider_draw((tw_Slider*)(wb[i]));
        break;
      case is_hor_slider:
        hor_slider_draw((tw_HorSlider*)(wb[i]));
        break;
      case is_vert_slider:
        vert_slider_draw((tw_VertSlider*)(wb[i]));
        break;
      case is_menu:
        menu_draw((tw_Menu*)(wb[i]));
        break;
      case is_checkbox:
        checkbox_draw((tw_Checkbox*)(wb[i]));
        break;
      case is_custom: {
          tw_Custom *cust=(tw_Custom*)(wb[i]);
          if (cust->draw) cust->draw(cust);
        }
        break;
      default:
        LOG("tw_draw: type %d?",type);
    }
  }
}

WBase* in_a_win(short x, short y) {
  for (short i=0;i<widgets.lst;++i) {
    WBase *wb=widgets.wid_p[i];
    short xx=wb->area.x,
          yy=wb->area.y;
    if (x >= xx && x < xx + wb->area.w && y >= yy && y < yy + wb->area.h) return wb;
  }
  return 0;
}

typedef struct {
  tw_Button *but;
  tw_Slider *sl;
  tw_HorSlider *sl2;
  tw_VertSlider *sl3;
  tw_Menu *men;
  tw_Checkbox *chb;
  tw_Custom *cust;
} WidgetPtr; // pointers to widgets

static void handle_event(short ev_type, short ev_x, short ev_y, short button) {
  //LOG("handle_event: type=%d but=%d",ev_type,button);
  static WidgetPtr pt;
  WBase *wb;
  switch (ev_type) {
    case Mouse_down:
      wb=in_a_win(ev_x,ev_y);
      if (!wb) break;
      switch (wb->type) {
        case is_but:
          pt.but=(tw_Button*)wb;
          pt.but->is_down=true;
          pt.but->cmd();
          button_draw(pt.but);
          break;
        case is_slider:
          pt.sl=(tw_Slider*)wb;
          pt.sl->d_start= sl_val(pt.sl,ev_x) - *pt.sl->val;
          break;
        case is_hor_slider:
          pt.sl2=(tw_HorSlider*)wb;
          pt.sl2->touched=true;
          pt.sl2->d_start= hsl_val(pt.sl2,ev_x) - *pt.sl2->val;
          break;
        case is_vert_slider:
          pt.sl3=(tw_VertSlider*)wb;
          pt.sl3->touched=true;
          pt.sl3->d_start= vsl_val(pt.sl3,ev_y) - *pt.sl3->val;
          break;
        case is_menu:
          pt.men=(tw_Menu*)wb;
          menu_mouse_press(pt.men,ev_x - pt.men->wb.area.x, ev_y - pt.men->wb.area.y);
          break;
        case is_checkbox:
          pt.chb=(tw_Checkbox*)wb;
          *pt.chb->val=!*pt.chb->val;
          checkbox_draw(pt.chb);
          pt.chb->cmd();
          break;
        case is_custom:
          pt.cust=(tw_Custom*)wb;
          if (pt.cust->mouse_event)
            pt.cust->mouse_event(pt.cust,ev_x - pt.cust->wb.area.x, ev_y - pt.cust->wb.area.y,Mouse_down,button);
          break;
      }
      break;
    case Mouse_move:
      if (pt.sl) {
        short val= *pt.sl->val;
        *pt.sl->val= tw_minmax(0, sl_val(pt.sl,ev_x) - pt.sl->d_start, pt.sl->maxv);
        //LOG("move: val=%d *pt.sl=%d",val,*pt.sl->val);
        if (val!=*pt.sl->val) {
          pt.sl->cmd();
          slider_draw(pt.sl);
        }
      }
      else if (pt.sl2) {
        short val= *pt.sl2->val;
        *pt.sl2->val= tw_minmax(0, hsl_val(pt.sl2,ev_x) - pt.sl2->d_start, pt.sl2->maxv);
        //LOG("move: val=%d *pt.sl2=%d",val,*pt.sl2->val);
        if (val!=*pt.sl2->val) {
          pt.sl2->cmd();
          hor_slider_draw(pt.sl2);
        }
      }
      else if (pt.sl3) {
        short val= *pt.sl3->val;
        *pt.sl3->val= tw_minmax(0, vsl_val(pt.sl3,ev_y) - pt.sl3->d_start, pt.sl3->maxv);
        //LOG("move: val=%d *pt.sl3=%d",val,*pt.sl3->val);
        if (val!=*pt.sl3->val) {
          pt.sl3->cmd();
          vert_slider_draw(pt.sl3);
        }
      }
      else if (pt.cust) {
        if (pt.cust->mouse_event)
          pt.cust->mouse_event(pt.cust,ev_x - pt.cust->wb.area.x, ev_y - pt.cust->wb.area.y,Mouse_move,button);
      }
      break;
    case Mouse_up:
      if (pt.but) {
        pt.but->is_down=false;
        pt.but->cmd();
        button_draw(pt.but);
      }
      else if (pt.sl2) {
        pt.sl2->touched=false;
        if (pt.sl2->cmd_at_up && pt.sl2->cmd) pt.sl2->cmd();
      }
      else if (pt.sl3) {
        pt.sl3->touched=false;
        if (pt.sl3->cmd_at_up && pt.sl3->cmd) pt.sl3->cmd();
      }
      else if (pt.cust) {
        if (pt.cust->mouse_event)
          pt.cust->mouse_event(pt.cust,ev_x - pt.cust->wb.area.x, ev_y - pt.cust->wb.area.y,Mouse_up,button);
      }
      bzero(&pt,sizeof(WidgetPtr));
      break;
    default: LOG("handle_event: ev_type %d?",ev_type);
  }
}

void (^tw_key_event)(uint16_t ch);
void (^do_exit)();

void tw_start_main_loop() {
  Event ev;
  while (true) {
    tw_get_event(&ev);
    //LOG("tw_start_main_loop: ev.type=%d",ev.type);
    switch (ev.type) {
      case Ev_mouse:
        handle_event(ev.mode,ev.x,ev.y,ev.button);
        break;
      case Ev_key:
        if (tw_key_event) tw_key_event(ev.ch);
        break;
      default:
        LOG("unexpected event type: %d",ev.type);
        return;
    }
  }
}

