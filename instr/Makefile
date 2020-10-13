CC := clang
TW_DIR := ../term-win
CFLAGS := -g -O -fblocks -fsanitize=safe-stack -I$(TW_DIR) \
  -Wall -Wno-logical-op-parentheses -Wno-unused-function -ferror-limit=2
LIBS := $(TW_DIR)/term-win.o -lm -lpthread -lsndfile -lasound -lBlocksRuntime
LFLAGS := -fsanitize=safe-stack  # safe-stack moet bij CFLAGS en LFLAGS
OBJ := midi.o pcm_snd.o dump-wave.o

.PHONY: tw_obj
.SUFFIXES=

all: synth ks-piano harms

synth: synth.o tw_obj $(OBJ)
	$(CC) $< $(OBJ) $(LIBS) -o $@ $(LFLAGS)
 
synth-svar: synth-svar.o tw_obj $(OBJ)
	$(CC) $< $(OBJ) $(LIBS) -o $@ $(LFLAGS)
 
ks-piano: ks-piano.o tw_obj $(OBJ)
	$(CC) $< $(OBJ) $(LIBS) -o $@ $(LFLAGS)

harms: harms.o tw_obj $(OBJ)
	$(CC) $< $(OBJ) $(LIBS) -o $@ $(LFLAGS)

%.o: %.c shared.h
	$(CC) -c $< $(CFLAGS)

tw_obj:
	make -C $(TW_DIR)

midi-tst: midi.c
	$(CC) midi.c -o $@ -DTEST

pcm-tst: pcm_snd.c
	$(CC) pcm_snd.c -o $@ -lm -lasound -DTEST

