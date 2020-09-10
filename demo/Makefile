TW_DIR := ../term-win
CFLAGS := -g -O -I$(TW_DIR) \
  -Wuninitialized -Wno-logical-op-parentheses -fblocks -lBlocksRuntime -fsanitize=safe-stack -ferror-limit=6
CC := clang

.PHONY: term-win
all: demo runes colors

demo: demo.c term-win
	$(CC) -g demo.c $(TW_DIR)/term-win.o -o demo $(CFLAGS)

runes: term-win runes.c
	$(CC) -g runes.c $(TW_DIR)/term-win.o -o runes $(CFLAGS)

colors: term-win colors.c
	$(CC) -g colors.c $(TW_DIR)/term-win.o -o colors $(CFLAGS)

term-win:
	make -C $(TW_DIR) term-win.o
