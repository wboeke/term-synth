typedef struct {
  uint8_t
    type, // Ev_mouse, Ev_key
    mode, // Ev_down, Ev_move, Ev_up
    x,y,
    button;
  uint16_t
    ch;
} Event;

enum {
  Mouse_down,
  Mouse_move,
  Mouse_up
};

enum {
  But_left,
  But_mid,
  But_right
};

enum {
  Ev_mouse,
  Ev_key
};

extern uint8_t
  tw_colums,
  tw_rows;

extern char quit_message[100];

#define LOG(format, ...) fprintf(stderr, format "\n", ##__VA_ARGS__);

enum {  // constants
  k_esc=033,
  max_wids=40, // max widgets
  amax=40, // max array len
  grey=233
};

enum {
  LefAlign, RightAlign, MidAlign, // text
  Wide=0, Compact  // menu style
};

typedef struct {
  short x,y,w,h;
} Rect;

typedef struct {
  short x,y;
} Point;

typedef struct {
  short type;
  Rect area;
} WBase;

typedef struct {
  WBase** wid_p;
  short lst,
        len;
} Widgets;

typedef struct {
  WBase wb;
  char *title;
  bool is_down;
  void (^cmd)();
} tw_Button;

typedef struct {
  WBase wb;
  short *val,
        maxv,
        xgrid;
  short d_start;
  char *title;
  void (^cmd)();
} tw_Slider;

typedef struct {
  WBase wb;
  short *val,
        maxv,
        xgrid,
        d_start,
        title_len;
  bool cmd_at_up, // call cmd() at mouse up?
       touched;
  char title[amax];
  void (^cmd)();
} tw_HorSlider;

typedef struct {
  WBase wb;
  short *val,
        maxv,
        ygrid,
        d_start,
        title_len1, title_len2;
  bool cmd_at_up, // call cmd() at mouse up?
       touched;
  char title1[amax], title2[amax];
  void (^cmd)();
} tw_VertSlider;

typedef struct {
  WBase wb;
  short *val, prev_val,
        nr_items, nr_rows, nr_cols, wid;
  char *title,
       **labels;
  uint8_t style;
  void (^cmd)();
} tw_Menu;
  
typedef struct {
  WBase wb;
  bool *val;
  int8_t on_col;
  char *title;
  void (^cmd)();
} tw_Checkbox;

typedef struct tw_Custom {
  WBase wb;
  void (^draw)(struct tw_Custom*);
  void (^mouse_event)(struct tw_Custom*,short x,short y,short mode,short button);
} tw_Custom;

extern uint8_t
  col_grey,
  col_black,
  col_lbrown,
  col_lblue, col_mblue, col_dblue,
  col_lred, col_red;

extern Widgets widgets;
void tw_gui_init();
void read_screen_dim(uint8_t *co,uint8_t *ro);
void tw_set_cell(uint8_t x, uint8_t y, uint16_t ch, uint8_t fg, uint8_t bg, uint16_t nr);
void tw_flush();
void tw_start_test_loop();
void tw_get_event(Event*);
void tw_vline(short x,short y1,short len,short col);
void tw_thin_vline(int x,int y1,int len,uint8_t col,uint8_t bg);
void tw_hline(short x1,short len,short y,short col);
void tw_thin_hline(short x1,short len,short y,short col,short bg);
void tw_box(Rect rect,short col);
void slider_draw(tw_Slider *sl);
void hor_slider_draw(tw_HorSlider *sl);
void vert_slider_draw(tw_VertSlider *sl);
void checkbox_draw(tw_Checkbox *chb);
void menu_draw(tw_Menu *men);
void tw_draw();
void tw_checkbox_init(tw_Checkbox *chb,Rect area,bool *val,char *tit,void (^cmd)());
void tw_button_init(tw_Button *but,Rect rect,char *tit,void (^_cmd)());
void tw_menu_init(tw_Menu *men,Rect rect,short *v,char *tit,char **lab,void (^_cmd)());
void tw_slider_init(tw_Slider *sl,Rect rect,short mv,short *v,char *tit,void (^_cmd)());
void tw_hor_slider_init(tw_HorSlider *sl,Rect rect,short mv,short *v,void (^_cmd)());
void tw_vert_slider_init(tw_VertSlider *sl,Rect rect,short mv,short *v,void (^_cmd)());
void tw_custom_init(tw_Custom *cust,Rect area,void (^draw)(tw_Custom*),
    void (^mouse_event)(tw_Custom*,short x,short y,short mode,short button));
void tw_start_main_loop();
void tw_print(short x,short y,short bg,char *msg,short spec);
void (^tw_key_event)(uint16_t ch);
void handle_event(short ev_type, short ev_x, short ev_y, short button);
void (^do_exit)();

static short tw_div(short a,short b) { // okay if a<0 or b<0 
  return (a*2 + (a>0 && b>0 || a<0 && b<0 ? b : -b))/(b*2);
}
static int tw_min(int a, int b) { return a<=b ? a : b; }
static int tw_max(int a, int b) { return a>=b ? a : b; }
static int tw_minmax(int a, int x, int b) { return x>=b ? b : x<=a ? a : x; }
static int tw_divup(int a,int b) { return (a+b-1)/b; }
static float tw_fminmax(float a,float x,float b) { return x>b ? b : x<a ? a : x; }

#define tw_alen(ar) sizeof(ar)/sizeof(ar[0])
