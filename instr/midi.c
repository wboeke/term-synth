#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>

#include "shared.h"

static int fd_in = -1;

bool open_midi_kb() {
  char *node[]={ "/dev/midi", "/dev/midi1", "/dev/mid2" };
  for (int i=0; i<3; ++i) {
    fd_in = open(node[i], O_RDONLY, 0);
    if (fd_in>=0) {
      LOG("midi: %s",node[i]);
      return true;
    }
  }
  LOG("midi: open failed");
  return false;
}

bool read_midi_bytes(uint8_t *mbuf) {
  while (true) {
    int n=read(fd_in, mbuf, 3);
    if (n<0) return false;
    if (n!=3) {
      LOG("%d bytes read",n);
    }
    else break;
  }
#ifdef TEST
  printf("read: %x,%2x,%2x\n", mbuf[0],mbuf[1],mbuf[2]);
#endif
  return true;
}

void close_midi_kb() {
  LOG("midi: closing (fd=%d)",fd_in);
  if (fd_in >= 0) {
    close(fd_in); fd_in=-1;
  }
}

#ifdef TEST

int main() {
  int err;
  if (!open_midi_kb()) exit(0);
  uint8_t midi_buf[3];
  read_midi_bytes(midi_buf);
  close_midi_kb();
  return 0;
}
#endif
