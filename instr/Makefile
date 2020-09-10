CC := clang
TW_DIR := ../term-win
CFLAGS := -g -O -fblocks -fsanitize=safe-stack -I$(TW_DIR) \
  -Wall -Wno-logical-op-parentheses -Wno-unused-function -ferror-limit=2
LIBS := $(TW_DIR)/term-win.o -lm -lpthread -lsndfile -lasound -lBlocksRuntime
AS_LIBS := -lm -lpthread -lsndfile -lasound -lBlocksRuntime
PA_LIBS := -lm -lpthread -lsndfile -lpulse -lpulse-simple -lBlocksRuntime
LFLAGS := -fsanitize=safe-stack  # safe-stack moet bij CFLAGS en LFLAGS
ALSA_OBJ := midi.o pcm_snd.o dump-wave.o
PA_OBJ := midi.o pulse.o dump-wave.o

.PHONY: tw_obj
.SUFFIXES=

all: synth ks-piano harms

synth: synth.o tw_obj $(ALSA_OBJ)
	$(CC) $< $(ALSA_OBJ) $(LIBS) -o $@ $(LFLAGS)

synth-pa: synth-pa.o tw_obj $(PA_OBJ)
	$(CC) $< $(PA_OBJ) $(LIBS) $(PA_LIBS) -o $@ $(LFLAGS) 

ks-piano: ks-piano.o tw_obj $(ALSA_OBJ)
	$(CC) $< $(ALSA_OBJ) $(AS_LIBS) -o $@ $(LFLAGS)

harms: harms.o tw_obj $(ALSA_OBJ)
	$(CC) $< $(ALSA_OBJ) $(LIBS) -o $@ $(LFLAGS)

%.o: %.c shared.h
	$(CC) -c $< $(CFLAGS)

tw_obj:
	make -C $(TW_DIR)

midi-tst: midi.c
	$(CC) midi.c -o $@ -DTEST

pcm-tst: pcm_snd.c
	$(CC) pcm_snd.c -o $@ -lm -lasound -DTEST

pulse-tst: pulse.c
	$(CC) pulse.c -o $@ -lm -lpulse -lpulse-simple -DTEST

