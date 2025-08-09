### Introduction
This project provides 4 musical instruments, each with a terminal-based GUI.
The aim is to create good sounds, and not so much weird or funny ones. It's Linux-only,
barebones Alsa is used for sound. A midi keyboard should be connected. If this keyboard
is provided with modulation- and pitchbend wheels, then the sound can be modified while playing.

### Instruments
The first instrument is a **synthesizer** with not too much controls. Classic waveforms
are provided, plus 4 special ones:
- **Harmonics**. Amplitude of harmonics can be choosen individually, like a Hammond organ.
- **Perlin noise**, as used in 3D graphics. Adapted for the creation of sound, it gives a rough
fluctuating tone, good for deep basses.
- **Formant**, as used in speech synthesis. In mode `rand` the sounds of 7 vowels are provided, choosen
at random, with interesting results. In mode `cust` the formant shape can be modelled arbitrarily.
- **Karplus-Strong**, a classic piano-synthesis algorithm. Like with a real piano, the sound
is evolving in subtle ways. In mode `noise` the buffers are initialised with a random signal,
in mode `sines` with sine's.

The 2 ADSR's (for filter and amplitude) have graphical interfaces, much easier to control
then sliders. The filter is a Moog filter, which has desirable properties for a good sound.
About 20 built-in patches are provided. The modulation wheel controls the filter,
the pitchbend wheel invokes a tremolo at 8 or 4 Hz. Clicking button `record` will start
the recording to a file named out.wav, clicking again will stop it.

The second instrument is a kind of **organ**. The amplitude of harmonics can be choosen
arbitrarily. Clicking with the right mouse button on a grey-colored harmonic line will turn it to
black, indicating that the harmonic is to be created 2 times, at slightly different frequency.
The result is rich-sounding tones.

The harmonics can be FM modulated. The modulation source can be sines derived from the
base frequency but at slightly shifted frequency, or the harmonic signal itself. While playing,
the amount of FM can be modified by the modulation wheel of the midi keyboard.
The pitchbend wheel again is for tremolo.

Key-click can be created at different frequencies. The amplitude and duration also can be
choosen. There are about 20 built-in patches. Overdrive for the output is provided as well.

The directory `one-thread` contains 2 simple instruments. Both operate without separate threads
for sound and midi, which is possible because streams can be opened in async mode. Thus the 
program can run single-threaded, where the audio will synchronize the loop.

The third instrument is a **piano**, modelled with the Karplus-Strong algorithm. Different
initial waveforms can be choosen. The tone can be decaying or steady.
The piano strings are sensed by 2 virtual pickups, moving slowly,
providing a nice stereo effect.

The last insrument is called `wform`, it is mainly ment to investigate new sounds. Distortion
is an important part of sound creation. The extent of distortion can be controlled by
a slider and by the mod wheel of the midi keyboard. Menu `ampl ctrl` determines whether
amplitude is controlled before or after distortion.

### Building
Compilation only is possible with clang, because use is made of closures, a.k.a. code blocks.
Gcc does not provide these.

Go to directory `instr` and run make. Two programs, `synth`and `harms` are created. In directory
`one-thread` programs `ks-piano` and `wform` are residing.
All programs only will run if a midi keyboard is connected.

Maybe you will find interesting patches for `synth` or `harms`.
They can be added to the built-in patches by typing character 'p'.
Then this patch will be written to stderr. It can be copy/paste'd into the source code (struct `bi_patches`).
You can run the programs with stderr redirected to another terminal, e.g. `./synth 2>/dev/pts/3`,
supposing that the other terminal is numbered 3. Programs can be terminated with <ctrl> C.

If for some reason a crash has occurred, then the terminal can have been left in some undefined state.
It can be brought back to normal by Linux command `reset`.

### The GUI
You can test the GUI in directory `demo`. Here, the program `demo` shows all widgets that
are provided. Don't forget to click the blue button, it will make the puppet wink.

The utilities `runes` and `colors` are for investigating unicode char's and
terminal colors respectivily.

Note that for the GUI several unicode char's are used with so-called ambiguous width. In
your terminal settings set them to Narrow (that's a width of 1).
