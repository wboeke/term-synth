CC := clang
TW_DIR := ../term-win
OPT := -O -Wall -Wno-logical-op-parentheses -Wno-unused-function -fblocks -ferror-limit=2
LIBS := $(TW_DIR)/term-win.o -lm -lsndfile -lasound -lBlocksRuntime
LFLAGS := -fsanitize=safe-stack
OBJ := shared.o
.PHONY: tw_obj
.SUFFIXES=

all: wform ks-piano

wform: wform.o $(OBJ) tw_obj
	$(CC) $< -o $@ $(OBJ) $(OPT) $(LIBS) -I$(TW_DIR)  $(LFLAGS)

ks-piano: ks-piano.o $(OBJ) tw_obj
	$(CC) $< -o $@ $(OBJ) $(OPT) $(LIBS) -I$(TW_DIR)  $(LFLAGS)

%.o: %.c shared.h $(TW_DIR)/term-win.h
	$(CC) -c $< $(OPT) -I$(TW_DIR)

tw_obj:
	make -C $(TW_DIR)

