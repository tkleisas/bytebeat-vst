# Bytebeat Drums

A 16-pad bytebeat drum instrument for Akai MPC OS 3.10.1+ and desktop VST2
hosts. It is a separate plugin from **Bytebeat Machine** and shares the same
formula compiler, VM and one-voice engine.

Every pad has its own formula, level, pan, tune, decay, choke group, play mode
and four macro controls. MIDI notes (default **36–51**) trigger the pads; the
MPC's own sequencer supplies the patterns, so there is no internal step
sequencer.

## Pads

| Pads | MIDI notes (default) |
|---|---|
| 1–16 | 36–51 |

The **Note Base** master parameter shifts the mapping (0–111). The MPC's pads
are normally set to notes 36–51 for bank A.

Per pad:

| Control | Meaning |
|---|---|
| **Formula** | A built-in or user `.bb` formula. |
| **Level** | Pad gain (0–100 %). |
| **Pan** | Stereo position. |
| **Tune** | ±24 semitones, implemented as a clock-rate multiplier. |
| **Decay** | Release time after the one-shot gate, or after note-off in Hold mode. |
| **Choke** | Off, or group 1–4. Triggering another pad in the same group fades the previous pad out in 5 ms. |
| **Mode** | One-shot (2 ms gate, then Decay) or Hold (plays while the note is held). |
| **A–D** | Four macro values available to the formula as `a`–`d`; `e`–`h` are fixed at 0.5. |

The pad engine is monophonic: retriggering a pad restarts its formula. Choke
fades are independent of the pad envelope, so a long Ambient pad still stops
quickly when a pad in its choke group triggers.

## UI pages

The MPC UI has thirteen tabs:

- **P1-4 F / P5-8 F / P9-12 F / P13-16 F** — formula, level, pan and tune for
  four pads. The four knobs in each column belong to one pad, so the Q-Link
  column is a pad's control set.
- **P1-4 E / P5-8 E / P9-12 E / P13-16 E** — decay, choke, mode and macro A.
- **P1-4 M / P5-8 M / P9-12 M / P13-16 M** — macros B, C and D.
- **MASTER** — master volume, tone, drive, note base, status and the kit
  information text.

The formula knobs open a menu of the built-in bank plus the `.bb` files in
`Content/Formulas/`. The choke knobs open the group menu; mode is a two-way
switch.

## Kits

Factory programs:

| Kit | Character |
|---|---|
| Init | 16 mixed one-shot pads; the playable starting kit. |
| Bytebeat Kit | Classic rhythmic/textural pads with a broad stereo spread. |
| Metal Kit | Short metallic and noise pads, tight chokes. |
| Acid Kit | Rhythmic bass/pluck pads with overlapping decay. |
| Ambient Kit | Long Hold pads and slow one-shot pads. |
| Glitch Kit | Very short digital/noise bursts and chokes. |

Kits are normal MPC programs. The host's program browser changes all 181
parameters at once. Save your own kits as MPC programs/projects.

## Formulas

The formula language and `.bb` format are the same as
[Bytebeat Machine](../README.md#formula-language). The drum bank starts with the
24 built-ins and appends the first 32 `.bb` files in `Content/Formulas/`
alphabetically. The name comes from the file name or the `name:` directive.

The formula's `t` starts at 0 on every pad trigger. Its output is shaped by the
pad's Decay envelope. The formula can read `note`, `freq`, `vel`, `gate`,
`voice`, the macros `a`–`d`, and all the usual globals. It can use persistent
state (`x`, `y`, `z`) per pad voice.

User `.bb` files are selected per pad on the **P… F** pages. The MPC state saves
the selected formula index; keep the user `.bb` files with the project (or use
the built-in formulas) if you need the project to be self-contained.

## Build and install

The top-level build builds both plugins:

```bash
git clone --recursive https://github.com/tkleisas/bytebeat-vst.git
cd bytebeat-vst
./build.sh
```

The drum release assets are:

```
dist/bytebeat-drums-0.1.0-mpc.zip
dist/bytebeat-drums-0.1.0-linux-vst2.zip
```

For the MPC, copy `bokontep - VST - Bytebeat Drums` into `VST2 Plugins` on the
drive and enable **External Plugin Support** in **Preferences → Enhancements
(Beta)**. The folder includes the ARM32/ARM64 binaries, the JSON UI, the
factory formulas and the licences.

The desktop VST2 is a generic-parameter build for testing and automation; the
MPC scripted UI is the primary interface.

### Manual SDK build

The drum source shares `../mpc/src/Bytebeat*.h`. The SDK build has no outside
include path, so copy the core next to the drum source:

```bash
cp -a drums/mpc "plugins/Bytebeat Drums"
cp mpc/src/BytebeatCompile.h mpc/src/BytebeatVM.h \
   mpc/src/BytebeatEngine.h mpc/src/FormulaLibrary.h \
   "plugins/Bytebeat Drums/src/"
python3 tools/build.py "plugins/Bytebeat Drums" --target all
python3 tools/build.py "plugins/Bytebeat Drums" --target desktop
```

The top-level `build.sh` and `build.ps1` do this automatically.

## CPU and limits

Each pad is one native bytebeat voice. Sixteen active pads at an 8 kHz formula
rate are cheap on the MPC; the host simulator's three-pad Init test runs at
about 0.3 % of one block. Dense pads or high formula rates use more, and you can
shorten decays or use fewer simultaneous pads if needed.

The drum plugin does not include the QuickJS engine; it always uses the native
VM. There is no drum VST3 yet — the MPC / desktop VST2 builds are the first
release.

## Licence

SPDX-License-Identifier: GPL-3.0-or-later. Copyright (C) 2026 bokontep. The MPC
Plugin SDK is MIT; see `Content/THIRD_PARTY_LICENSES.txt`.
