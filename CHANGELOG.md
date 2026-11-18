# Changelog

## 0.2.0

- Optional QuickJS-NG JavaScript engine in the VST3: Expression, Function and
  Script modes; persistent per-voice state; Math aliases; number or
  `[left, right]` results; compile-time smoke test at t=0; runtime error
  reporting; 1.5 ms interrupt budget. The MPC stays on the native VM.
- Engine and JS mode are stored in the VST3 state; changing either applies the
  current editor text. Ctrl+E toggles the engine, Ctrl+J cycles the JS mode.
- A library entry or factory program that is not valid in the selected JS mode
  falls back to the native engine instead of being silently ignored.
- QuickJS-NG MIT licence shipped with the VST3.
- Version 0.2.0.

## 0.1.0

- Initial release.
- Formula compiler and bytecode VM with JS-like arithmetic and bitwise
  semantics, persistent per-voice state, stereo `[left, right]` output.
- Bytebeat, signed bytebeat and floatbeat modes; 4 kHz–48 kHz clock with
  free/sync/trigger play modes and 1–8 voices.
- A–H macro parameters; tone, drive, DC block, width, pan and soft clip.
- 24 built-in formulas plus user `.bb` files with metadata directives.
- MPC OS 3.10.1+ plugin (ARM32/ARM64) with four scripted UI pages and 13
  factory programs.
- Desktop VST2 build for testing.
- Linux VST3 with a syntax-highlighted formula editor, library, preview scope,
  macro/play pages, state persistence and factory programs.
- Windows VST3 via the included `build.ps1` and GitHub Actions workflow.
- Optional QuickJS-NG JavaScript engine in the VST3 with Expression, Function
  and Script modes, persistent per-voice JS state, compile-time smoke tests and
  runtime error reporting. The MPC stays on the native VM.
- YAWN integration through its VST3 hosting and the state-sync patch.
- Core unit tests, `bbtool` validator/renderer, MPC host-simulator validation
  and pluginval validation.
