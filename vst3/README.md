# Bytebeat Machine — VST3

The desktop VST3 with the formula editor, library and scope. It uses the same
engine as the MPC plugin (`../mpc/src/Bytebeat*.h`); the only JUCE-specific
parts are the processor wrapper and the editor.

Bytebeat Drums has a separate VST3 in [`../drums/vst3`](../drums/vst3/README.md).

## Build

Requirements: CMake ≥ 3.22, a C++17 compiler, X11/freetype development files
on Linux, and JUCE 7.0.12. JUCE is the `third_party/JUCE` submodule (GPLv3):

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

`COPY_PLUGIN_AFTER_BUILD` installs the bundle to
`~/.vst3/Bytebeat Machine.vst3`; the built bundle is also at
`build/BytebeatMachine_artefacts/Release/VST3/Bytebeat Machine.vst3`.

On Windows, use the same project with Visual Studio 2022:

```powershell
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64
cmake --build build-windows --config Release
```

The repository's `build.ps1` and `.github/workflows/build.yml` automate this.

## Editor

- **Code pane** — syntax-highlighted formula editor with line numbers.
  **Apply** or Ctrl+Enter compiles. On error the caret moves to the reported
  line/column and the previous valid program keeps playing. **Auto** applies
  after a 650 ms pause.
- **Library** — built-in formulas plus `.bb` files from the user folder.
  Double-click to load. **Save .bb** writes the current text to the user folder;
  **Folder** opens that folder in your file manager; **Refresh** rescans it
  after you copy files in.
- **Scope** — a local preview of the formula: waveform, log-frequency
  spectrum, `t`, clock rate and peak. It renders its own preview, so it works
  even in YAWN's process-isolated editor window.
- **MACROS** — A–H, tone, drive, DC block, width, pan, volume.
- **PLAY** — engine, JS mode, mode, clock, play mode, run, speed, smooth,
  voices, root, pitch track, attack, release, reset-on-note.
- **Top bar** — formula name, Apply, Auto, status, bytecode op count.

The formula source is stored in the VST3 state (`<Formula>` in the state XML)
along with all parameters, so saving the DAW project saves the formula too.

## User formulas

The editor creates and reads:

- Linux: `~/.config/Bytebeat Machine/Formulas`
- Windows: `%APPDATA%\Bytebeat Machine\Formulas`
- macOS: `~/Library/Application Support/Bytebeat Machine/Formulas`

A `.bb` file is the formula with optional `// name:`, `// mode:`, `// rate:`
and `// a:` … `// h:` directives. The MPC build reads the same format from its
`Content/Formulas/` folder.

## YAWN

YAWN scans `~/.vst3` and hosts the plugin like any other VST3 instrument. Click
the **UI** pill on the device header to open the editor in YAWN's
process-isolated editor host.

Because the editor runs in a separate process in YAWN, a formula edit is sent
back to the audio instance by the host's state poll (about twice a second).
Parameter changes use the immediate parameter path. In an in-process host the
editor and processor share an instance, so Apply is immediate. Project state
includes the formula source.

## JavaScript engine

The **Engine** choice in the PLAY page selects the native bytecode VM or
QuickJS-NG. **JS Mode** selects how the source is compiled:

- **Expression** — an expression, e.g. `Math.sin(t/20)*100+128`.
- **Function** — a function body with `return`, e.g.
  `x = (typeof x === 'undefined' ? 0 : x) + 1; return x;`
- **Script** — a script that returns a function, e.g.
  `const f = t => Math.sin(t/20)*100+128; return f;`

The arguments are
`t, a..h, sr, time, beat, bar, tempo, playing, note, freq, vel, gate, voice`.
`Math` aliases and `PI`/`TAU`/`E` are installed. Undeclared variables persist
per voice/context. The result may be a number or `[left, right]`. Changing the
engine or JS mode applies the current editor text. The source is smoke-tested
at `t=0`; runtime errors show `JS ERROR`. Ctrl+E toggles the engine and Ctrl+J
cycles the JS mode.

QuickJS is VST3-only and experimental: it can allocate/GC on the audio thread.
The native engine is the default. The MPC build never includes QuickJS.

## Play mode

The default is **Trigger** (MIDI notes start the formula). Switch to **Free**
for a continuous stream, or **Sync** to follow the host transport.

## Parameters

`Volume`, `Mode`, `Clock`, `Play Mode`, `Run`, `Speed`, `Smooth`, `A`–`H`,
`Tone`, `Drive`, `DC Block`, `Width`, `Pan`, `Voices`, `Root`, `Pitch Track`,
`Attack`, `Release`, `Reset`. All are automatable. The choice parameters use
the engine's menu values; `Mode` = Bytebeat/Signed/Floatbeat, `Clock` =
4 kHz…48 kHz, `Play Mode` = Free/Sync/Trigger.

## Licence

SPDX-License-Identifier: GPL-3.0-or-later. Copyright (C) 2026 bokontep.

JUCE 7.0.12 and the bundled VST3 SDK are used under GPLv3. QuickJS-NG is MIT. “VST” is a
trademark of Steinberg Media Technologies GmbH. See `../NOTICE.md`.
