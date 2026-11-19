# Bytebeat Drums — VST3

The desktop VST3 for the 16-pad drum instrument. It uses the same native
engine, compiler and factory kits as the MPC plugin: the shared core is in
`../../mpc/src` and the factory kit table is
`../mpc/src/DrumKits.h`. The JUCE parts are the processor wrapper, the 4×4 pad
editor and the kit browser.

There is no QuickJS in this plugin; pads always use the native bytecode VM.

## Build

Requirements: CMake ≥ 3.22, a C++17 compiler, X11/freetype development files
on Linux, and JUCE 7.0.12. JUCE is the `third_party/JUCE` submodule (GPLv3):

```bash
git submodule update --init --recursive
cmake -S drums/vst3 -B drums/vst3/build -DCMAKE_BUILD_TYPE=Release
cmake --build drums/vst3/build -j"$(nproc)"
```

`COPY_PLUGIN_AFTER_BUILD` installs the bundle to
`~/.vst3/Bytebeat Drums.vst3`; the built bundle is also at
`drums/vst3/build/BytebeatDrums_artefacts/Release/VST3/Bytebeat Drums.vst3`.

On Windows, use the same project with the installed Visual Studio (CMake picks the default generator):

```powershell
cmake -S drums/vst3 -B drums/vst3/build-windows
cmake --build drums/vst3/build-windows --config Release
```

The repository's `build.ps1` and `.github/workflows/build.yml` automate this.

## Editor

- **Pad grid** — 4×4 pads. Click a pad to select it and trigger a short preview.
  Each pad shows its number, formula name and level.
- **Pad panel** — formula menu, level, pan, ±24 semitone tune, decay, choke
  group (Off/1–4), mode (One-shot/Hold) and macros A–D.
- **Master panel** — volume, tone, drive and note base.
- **Kit browser** — the six factory kits plus four user slots. **Load** commits
  the selected kit. **Save** writes the current 16 pads and master settings to
  the selected user slot; if a factory kit is selected, it saves to User 1 and
  switches the menu there.
- **Preview scope** — shows the selected pad's output. Previews are short
  one-shots even for Hold pads.
- **Folder** — opens the user formula folder. Add `.bb` files and reopen the
  plugin/editor to rescan them.

## Kits and state

The VST3 state stores all parameters, the current kit number and the four user
kit slots. The factory kits are the same Init, Bytebeat, Metal, Acid, Ambient
and Glitch kits as the MPC build.

User formulas live in:

- Linux: `~/.config/Bytebeat Drums/Formulas`
- Windows: `%APPDATA%\Bytebeat Drums\Formulas`
- macOS: `~/Library/Application Support/Bytebeat Drums/Formulas`

A `.bb` file uses the same format and native dialect as Bytebeat Machine. The
formula bank starts with the 24 built-ins and appends the user files.

## Parameters

The plugin exposes 180 automatable parameters: master volume/tone/drive/note
base and 11 parameters for each of the 16 pads. Pads map to MIDI notes 36–51
by default; **Note Base** shifts the mapping.

## Tests

```bash
ctest --test-dir drums/vst3/build --output-on-failure
```

The test builds a headless console host that checks kit load/save, state
round-trip, one-shot and Hold behavior, all-notes-off, and the editor preview.
The release build is also validated with pluginval.

## Licence

SPDX-License-Identifier: GPL-3.0-or-later. Copyright (C) 2026 bokontep.

JUCE 7.0.12 and the bundled VST3 SDK are used under GPLv3. “VST” is a
trademark of Steinberg Media Technologies GmbH. See `../../NOTICE.md`.
