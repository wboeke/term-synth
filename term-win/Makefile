CC := clang
CFLAGS := -O -ferror-limit=2 -Wuninitialized -Wno-logical-op-parentheses -fblocks -fsanitize=safe-stack

.SUFFIXES=

term-win.o: term-win.c term-win.h
	$(CC) -c term-win.c $(CFLAGS)

test: term-win.o test.c
	$(CC) test.c term-win.o -o test $(CFLAGS) -lBlocksRuntime
