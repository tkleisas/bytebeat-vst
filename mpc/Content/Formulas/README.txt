Bytebeat Machine - user formulas
================================

Put .bb files in this folder (Content/Formulas on the plugin drive). They are
scanned when the plugin loads. Restart/reload the plugin after adding files.

The plugin picks the first 32 files alphabetically. The file name (without .bb)
is the display name unless the file has a "name:" directive.

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

  t, sr, time, beat, bar, tempo, playing, note, freq, vel, gate, voice
  a..h        macros, 0..1 (the MPC shows them as 0..100%)
  PI, TAU, E
  sin cos tan asin acos atan atan2 sinh cosh tanh
  floor ceil round trunc int abs sign sqrt cbrt exp log log2 log10
  pow min max clamp hypot frac
  + - * / % ** & | ^ ~ << >> >>> && || ! == != < <= > >= ?:
  x, y, z and any other identifier are persistent per-voice variables
  [left, right] as the final expression makes a stereo formula

Examples: see the .bb files next to this README. You can validate a file with
the bbtool command from the repository:

    bbtool check myformula.bb
    bbtool render myformula.bb out.wav 5

On the MPC, use the SOURCE page: turn User Bank on, then step User Slot.
