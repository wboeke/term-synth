/* van: stackoverflow.com/questions/4842424/list-of-ansi-color-escape-sequences
  0x00-0x07:  standard colors (same as the 4-bit colours)
  0x08-0x0F:  high intensity colors
  0x10-0xE7:  6  6  6 cube (216 colors): 16 + 36  r + 6  g + b (0  r, g, b  5)
  0xE8-0xFF:  grayscale from black to white in 24 steps
*/
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include  <term-win.h>

int main() {
  tw_gui_init();
  char buf[10];
  for (int n=0;n<256;n++) {
    short x=7*(n/16),
          y=n%16;
    tw_hline(x,2,y,n);
    snprintf(buf,10,"%d",n);
    tw_print(x+2, y, 0, buf, 0);
  }
  tw_start_main_loop();
  exit(0);
}
