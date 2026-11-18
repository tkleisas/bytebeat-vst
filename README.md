# Bytebeat Machine

A bytebeat instrument with a real formula editor, packaged for:

- **Akai MPC OS 3.10.1+** as an external plugin (VST2, ARM32 + ARM64),
- **desktop VST2** for testing and older hosts,
- **Linux and Windows VST3** with a graphical code editor, library and scope,
- **YAWN** through its normal VST3 hosting (the editor opens in YAWN's
  process-isolated editor window).

Bytebeat is music made from a formula of time: `t` counts at the chosen clock
rate, the expression produces a sample, and the result is played. The classic
one-liners work unchanged, and the plugin adds the things a DAW needs:
automation, voices, transport sync, stereo, and a bank of formulas.

The plugin is **GPL-3.0-or-later**. The MPC Plugin SDK used for the hardware
build is MIT-licensed; see [`NOTICE.md`](NOTICE.md) and
[`LICENSES/MPC-Plugin-SDK-MIT.txt`](LICENSES/MPC-Plugin-SDK-MIT.txt).

“VST” is a trademark of Steinberg Media Technologies GmbH. “Akai”, “MPC” and
“inMusic” are trademarks of their respective owners. This project is not
affiliated with or endorsed by them.

---

## Features

- **Bytebeat, signed bytebeat and floatbeat** output modes, plus stereo
  `[left, right]` formulas.
- **Clock** selectable from 4 kHz to 48 kHz (or the formula's own rate).
  Formulas are evaluated at the clock rate and resampled to the host rate, so
  an 8 kHz formula sounds like an 8 kHz formula.
- **A–H macros**: eight automatable 0–1 inputs for the formula (`a`…`h`). This
  is how a fixed formula becomes playable and mixable.
- **Free, Sync and Trigger** play modes:
  - Free: `t` runs continuously from reset.
  - Sync: `t` follows the host transport, so patterns line up with the
    timeline and loop.
  - Trigger: each MIDI note starts its own voice; 1–8 voices, optional pitch
    tracking, attack/release, reset-on-note.
- **Formula globals**: `t`, `sr`, `time`, `beat`, `bar`, `tempo`, `playing`,
  `note`, `freq`, `vel`, `gate`, `voice`, `a`…`h`, `PI`, `TAU`, `E`.
- **Persistent variables**: `x`, `y`, `z` (and any other identifier) keep
  their value between clock ticks, per voice. This enables filters,
  accumulators and stateful rhythms.
- **JS-like semantics**: doubles for arithmetic, JavaScript `ToInt32` for
  bitwise operators, shifts mask by 31, `>>>` is unsigned. Classic formulas
  such as `t*(42&t>>10)` behave as expected.
- **Tone section**: volume, drive, one-pole tone, DC blocker, width, pan and a
  final soft clip.
- **Built-in formula bank** plus user `.bb` files. The VST3 editor saves
  `.bb` files to a per-user folder and lists everything in that folder.
- **Project state** contains the formula source, not just a bank index, so a
  saved project opens correctly even if the library changes.
- **MPC**: JSON skins, Q-Link pages, factory programs, and native ARM32/ARM64
  builds from the same source as the desktop plugin.

## Repository layout

```
mpc/                    MPC + desktop VST2 plugin
  src/BytebeatCompile.h formula lexer, parser and bytecode compiler
  src/BytebeatVM.h      interpreter
  src/BytebeatEngine.h  clock, resampling, voices, tone
  src/Bytebeat.cpp      MPC shell, parameters, state, programs
  src/FormulaLibrary.h  built-in formulas
  Content/Formulas/     example .bb files and the user-formula README
  plugin.json, ui.json
vst3/                   JUCE VST3 (Linux + Windows)
  Source/PluginProcessor.*, PluginEditor.*, ...
  Source/BytebeatTokeniser.h
  Source/FormulaLibraryModel.h
tests/                  compiler/VM/engine tests and bbtool
third_party/JUCE/       JUCE 7.0.12 submodule (GPLv3)
build.sh, build.ps1     one-command builds
.github/workflows/      Linux + Windows CI
```

The DSP core is header-only and has no JUCE or MPC SDK dependency. The MPC
shell includes it from `mpc/src`; the VST3 includes the same files from
`../mpc/src`.

## Install on an MPC

Requires **MPC OS 3.10.1 or later** with **External Plugin Support** enabled:
**Preferences → Enhancements (Beta)** → *Enable Enhancements* and *External
Plugin Support*, then restart the MPC app.

1. Take `bytebeat-machine-0.1.0-mpc.zip` from the release (or build it) and
   unzip it.
2. Copy the whole `bokontep - VST - Bytebeat Machine` folder into a folder
   named exactly `VST2 Plugins` at the root of a USB drive, SD card, or the
   MPC's internal drive.
3. Insert the plugin on an instrument track. It appears under **External
   Plugins**.

The MPC has no plugin windows. Its interface is the JSON scripted UI with four
pages:

- **SOURCE** — Formula, User Bank, User Slot, Mode, Clock, Play Mode, Run,
  Speed, Smooth, Volume, Status.
- **MACROS** — A–H, Tone, Drive, DC Block, Width, Pan, Volume, Status.
- **PLAY** — Play Mode, Voices, Root, Pitch, Attack, Release, Reset, Run,
  Speed, Clock, Smooth, Volume.
- **INFO** — Status, Formula, User Slot and short help text.

The `Formula` slider opens a menu of the built-in bank. To use your own formula,
turn **User Bank** on and step **User Slot**; the value text shows the file
name. User files are scanned from `Content/Formulas/` when the plugin loads, so
add files and reload the plugin (or restart the MPC app) after changing them.

### User formulas on the MPC

A `.bb` file is a text formula with optional leading comments:

```text
// name: Copper Bars
// mode: bytebeat          (bytebeat, signed or floatbeat)
// rate: 8000
// a: Density
// b: Tone
t*(t>>9&31)>>3
```

The plugin reads the first 32 `.bb` files alphabetically. The file name is used
when there is no `name:` directive. See
[`mpc/Content/Formulas/README.txt`](mpc/Content/Formulas/README.txt) for the
full directive list.

## Build

### Linux (MPC + desktop VST2 + VST3)

Requirements: Python 3.9+, Git, CMake ≥ 3.22, a C++17 compiler, X11/freetype
development files, `zip`, and network access on the first build. About 2 GB of
free disk space.

```bash
git clone --recursive https://github.com/tkleisas/bytebeat-vst.git
cd bytebeat-vst
./build.sh
```

`build.sh` clones the pinned MPC Plugin SDK, copies `mpc/` into it, builds the
native/ARM32/ARM64 plugin and desktop VST2, runs the SDK host simulator, builds
the VST3, and writes the release zips to `dist/`.

Useful environment variables:

| Variable | Meaning |
|---|---|
| `SDK_DIR` | Reuse a specific MPC Plugin SDK checkout instead of `.sdk`. |
| `SDK_COMMIT` | Override the pinned SDK commit. |
| `ZIG` | Path to a Zig executable, or a Python interpreter with the `ziglang` package (e.g. `ZIG=python3`). |
| `PYTHON` | Python executable (default `python3`). |
| `SKIP_VST3` | Set to `1` to build only the MPC/desktop targets. |

If Zig is not on `PATH`, install it with `pip install ziglang` and run
`ZIG=python3 ./build.sh`.

### Manual MPC / desktop VST2 build

```bash
git clone https://github.com/inmusic-software/mpc-plugin-builder-sdk.git .sdk
cd .sdk
git checkout a5949cf6fe2cbfb95bcbe0ddf1f7ef555e71f2be
cp -a ../mpc "plugins/Bytebeat Machine"
python3 tools/build.py "plugins/Bytebeat Machine" --target all
python3 tools/build.py "plugins/Bytebeat Machine" --target desktop
```

The MPC output folder is
`plugins/Bytebeat Machine/build/bokontep - VST - Bytebeat Machine/`. The desktop
VST2 is under `plugins/Bytebeat Machine/build/desktop/`.

### Windows

`build.ps1` performs the same steps with the SDK and builds the VST3 with
Visual Studio 2022 + CMake. It is also what the GitHub Actions Windows job
uses, so a Windows VST3 is produced on every push without a local Windows
machine.

### VST3 only

```bash
git submodule update --init --recursive
cmake -S vst3 -B vst3/build -DCMAKE_BUILD_TYPE=Release
cmake --build vst3/build -j"$(nproc)"
```

`COPY_PLUGIN_AFTER_BUILD` installs the bundle to
`~/.vst3/Bytebeat Machine.vst3`. On Windows the same CMake project builds with
MSVC; copy the `.vst3` folder to `C:\Program Files\Common Files\VST3` or
`%LOCALAPPDATA%\Programs\Common\VST3`. See
[`vst3/README.md`](vst3/README.md).

### Tests

```bash
cmake -S tests -B tests/build -DCMAKE_BUILD_TYPE=Debug
cmake --build tests/build -j"$(nproc)"
./tests/build/core_tests
./tests/build/bbtool list
```

`bbtool` validates and renders `.bb` files without a DAW:

```bash
./tests/build/bbtool check myformula.bb
./tests/build/bbtool render myformula.bb out.wav 5
./tests/build/bbtool render 3 out.wav 5 --clock 11025 --mode 0
```

## Formula language

### Output modes

| Mode | Expression result | Mapping |
|---|---|---|
| Bytebeat | 0…255, wrapping | `((int32(v) & 255) - 128) / 128` |
| Signed | -128…127 | `int8(v) / 128` |
| Floatbeat | -1…1 | clamped |

For stereo, make the final statement `[left, right]`. The two expressions share
the same state variables.

### Operators

`+ - * / % **`, `& | ^ ~ << >> >>>`, `&& || !`, `== != < <= > >=`, `?:`,
parentheses, decimal/hex/binary numbers, `//` and `/* */` comments.

Bitwise operators follow JavaScript: operands are converted to signed 32-bit
integers, shift counts are masked with 31, and `>>>` is an unsigned shift. `%`
is `fmod`, `**` is right-associative and uses `pow`.

### Functions

`sin cos tan asin acos atan atan2 sinh cosh tanh floor ceil round trunc int abs
sign sqrt cbrt exp log log2 log10 pow min max clamp hypot frac`

`int` truncates toward zero (C style); `floor` is also available. `round` is
JavaScript-style (half up).

### Statements and state

A formula is a sequence of statements separated by `;` or `,`; the value of the
last statement is the output.

```text
x = x*0.995 + (t>>8&15);
x*6
```

`x`, `y`, `z` and any other identifier are per-voice persistent variables,
initialised to zero. They keep their values between clock ticks, so one-pole
filters and accumulators work. The final result is the output; earlier
statements are evaluated for their side effects.

### Examples

```text
t*(t>>9&31)>>3                  rhythmic bytebeat
t&t>>8                         Sierpinski pattern
sin(t/18)*sin(t/47)*127+128    tonal
sin(t/20)+sin(t/31)*0.5        floatbeat
[sin(t/30)*0.6, sin(t/34)*0.6] stereo floatbeat
x=x*0.98+((t>>4&255)-128)*0.02; x*3   stateful filter
```

### Formula directives

The MPC loader and the VST3 `.bb` files understand these leading comments:

| Directive | Meaning |
|---|---|
| `// name: ...` | Display name. |
| `// mode: bytebeat\|signed\|floatbeat` | Default output mode. |
| `// rate: 8000` | Default clock rate. |
| `// a: ...` … `// h: ...` | Labels for the macro knobs in the VST3 editor. |
| `// author: ...` | Free text. |

## VST3 editor

The editor has a code pane with syntax highlighting, a library list, a preview
waveform/spectrum scope, macro knobs and a play page. **Apply** (Ctrl+Enter)
compiles the formula; a compile error shows the line/column and leaves the
previous valid program running. **Auto** applies after a short pause.
**Save .bb** writes the current formula to the user formula folder. **Folder**
opens that folder in your file manager; copy `.bb` files there and press
**Refresh**. The library lists built-ins and user files; double-click a row to
load it.

User formulas live in:

- Linux: `~/.config/Bytebeat Machine/Formulas`
- Windows: `%APPDATA%\Bytebeat Machine\Formulas`
- macOS: `~/Library/Application Support/Bytebeat Machine/Formulas`

### YAWN

YAWN scans `~/.vst3`. Add **Bytebeat Machine** to an instrument track and click
the **UI** pill on the device header (or right-click the track header → **Open
VST3 Instrument Editor**). The editor opens in YAWN's process-isolated X11 host.

The editor and the audio instance are separate processes in YAWN, so a formula
edit is synchronised through the host's state polling (about twice a second).
Parameter changes use the normal immediate parameter path. In a normal VST3
host (REAPER, Bitwig, etc.) the editor and processor are in the same instance,
so Apply is immediate. Projects save the formula source in the VST3 state.

The editor's scope is a local preview rendered by the editor, so it also works
when the editor runs in YAWN's separate process. The same state-sync patch used
by ANS 1964 is included as
[`vst3/yawn-state-sync.patch`](vst3/yawn-state-sync.patch); the YAWN checkout in
this workspace already has it applied. If you update YAWN, reapply the patch.

The default play mode is **Trigger**: MIDI notes start the formula. Switch to
**Free** on the PLAY page (or the Play Mode combo) for a continuous stream that
does not need notes.

## Status

v0.1.0: MPC ARM32/ARM64 + desktop VST2 + Linux VST3, formula compiler/VM,
bytebeat/signed/floatbeat/stereo, A–H macros, free/sync/trigger modes, 24
built-in formulas, user `.bb` files, MPC scripted UI, and a VST3 editor with
library/scope. Windows VST3 is built by CI.

## Licence

SPDX-License-Identifier: GPL-3.0-or-later. Copyright (C) 2026 bokontep.

JUCE 7.0.12 is GPLv3. The VST3 SDK bundled with JUCE is dual-licensed by
Steinberg. The MPC Plugin SDK is MIT. See [`NOTICE.md`](NOTICE.md),
[`LICENSE`](LICENSE) and [`LICENSES/`](LICENSES/).
