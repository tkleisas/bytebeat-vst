Bytebeat Drums - user formulas
==============================

Put .bb files in this folder (Content/Formulas on the plugin drive). They are
appended to the **Formula** choices for every pad when the plugin loads.
Restart/reload the plugin after adding files.

The plugin picks the first 32 files alphabetically. The file name (without .bb)
is the display name unless the file has a "name:" directive. Select a formula
per pad on the P... F pages with the pad's Formula knob/menu.

File format
-----------
A .bb file is a text formula with optional leading comment directives:

    // name: Copper Bars
    // mode: bytebeat          (bytebeat, signed or floatbeat)
    // rate: 8000              (evaluation clock in Hz)
    // a: Density              (optional label for macro A)
    // b: Tone
    // author: your name
    t*(t>>9&31)>>3

The formula itself supports:

  t, sr, time, beat, bar, tempo, playing, note, freq, vel, gate, voice,
  aftertouch  (0..1, smoothed MIDI pressure)
  a..d        per-pad macros, 0..1 (the pads show A-D as 0..100%)
  e..h        fixed at 0.5 in the drum plugin
  PI, TAU, E
  sin cos tan asin acos atan atan2 sinh cosh tanh
  floor ceil round trunc int abs sign sqrt cbrt exp log log2 log10
  pow min max clamp hypot frac
  + - * / % ** & | ^ ~ << >> >>> && || ! == != < <= > >= ?:
  x, y, z and any other identifier are persistent per-pad variables
  [left, right] as the final expression makes a stereo formula

Examples: see the .bb files next to this README. Validate a file with bbtool:

    bbtool check myformula.bb
    bbtool render myformula.bb out.wav 5

The drum formula's t starts at 0 on every pad trigger. One-shot pads run for a
2 ms gate and then fade over the pad's Decay; Hold pads run until note-off.
