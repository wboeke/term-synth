### Introduction
This project provides two musical instruments, running in a terminal-based GUI.
The aim is to create good sounds, and not so much weird or funny tones. It's Linux-only, and
barebones Alsa is used for sound. A midi keyboard is to be connected. If this keyboard
is provided with modulation- and pitch wheels, then sound can be modified while playing.

### Instruments
The first instrument is a synthesizer with not too much controls. Classic waveforms
are provided, plus 4 special sounds:
- **Harmonics**. Amplitude of partials can be choosen individually, like a
Hammond organ.
- **Perlin noise**, as used in 3D graphics. Adapted for the creation of sound it gives a rough
fluctuating tone, good for deep basses.
- **Formant**, as used in speech synthesis. In mode 0 the sounds of 7 vowels are provided, choosen
at random, with interesting results. In mode 1 the formant shape can be modelled arbitrarily.
- **Karplus-Strong**, an classic piano-synthesis algorithm. Like with a real piano, the sound
is evolving in subtle ways. In mode 0 the buffers are initialised with noise, in mode 1
with sine's.

The 2 ADSR's (for filter and amplitude) have graphical interfaces, much easier to control
then sliders. About 20 built-in patches. The modulation wheel controls the filter,
the pitchbend wheel invokes a tremolo at 8 or 4 Hz. Clicking button `record` enables
recording to a file named out.wav.

The second instrument is more or less an organ, the amplitude of harmonics can be choosen
arbitrarily. Clicking with the right mouse button on a grey-colored harmonic will turn it to
black, indicating that the harmonic is to be created 2 times, at slightly different frequency.
Then rich-sounding tones will originate.

The harmonics can be FM modulated. The modulation source can be sine(s) derived from the
base frequency but slightly different, or the harmonic signal itself. While playing,
the amount of FM can be modified by the modulation wheel of your midi keyboard.
The pitchbend wheel again is for tremolo.

Key-click can be created at different frequencies. The amplitude and duration also can be
choosen. There are about 20 built-in patches. 

### Building
Compilation only is possible with clang, because use is made of closures, a.k.a. code blocks.
Gcc does'nt provide these.

Go to directory `instr` and rum make. Two programs, `synth` and `harms` are created. They
only will run if a midi keyboard is connected. Maybe you will find interesting patches
later, which can be added to the built-in patches by typing character 'p'.
Then this patch will be written to stderr. It can be copy/paste'd into the source code (struct `bi_patches`).
Run the program with stderr redirected to another terminal, e.g. `./synth 2>/dev/pts/3`,
supposed that the other terminal is numbered 3.

If for some reason a crash has occurred, then the terminal may have been left in an undefined state.
You can bring it back to normal by Linux command `reset`.

### The GUI
You can test the GUI in directory `demo`. Here, the program `demo` shows all widgets that
are provided. Don't forget to click the blue button, it makes the puppet wink.

The utilities `runes` and `colors` are for investigating unicode char's and
terminal colors respectivily.

Note that for the GUI several unicode char's are used with so-called ambiguous width. In
your terminal settings set them to Narrow (that's a width of 1).

You may have noticed that this README is rather rudimentary. If needed it will be augmented
later on, but anyhow, here is a screenshot of the synth:


![synth](http://members.chello.nl/w.boeke/tw-synth/tw-synth.png)
