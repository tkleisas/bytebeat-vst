# Bytebeat - third-party notices

Bytebeat Machine and Bytebeat Drums are licensed GPL-3.0-or-later.
Copyright (C) 2026 bokontep.

The MPC build is made with the **inMusic MPC Plugin Builder SDK**, which is
MIT-licensed. Its licence text is in `LICENSES/MPC-Plugin-SDK-MIT.txt` and is
shipped with the plugin in `Content/THIRD_PARTY_LICENSES.txt`.

The desktop VST3 builds use **JUCE 7.0.12** and the VST3 SDK bundled with JUCE.
Bytebeat Machine also includes **QuickJS-NG** (MIT) for the optional JavaScript
engine; its licence is in `LICENSES/QuickJS-NG-MIT.txt`. JUCE is GPLv3 (or
commercial); the VST3 SDK is dual-licensed by Steinberg. Both plugins are
distributed under GPL-3.0-or-later, which is compatible with JUCE's GPLv3
terms. The VST2 builds contain no Steinberg VST SDK code.

"DSP" here means the bytebeat compiler, virtual machine and engine in
`mpc/src/Bytebeat*.h`. Those files are original work; no third-party bytebeat
player code is copied.

"VST" is a trademark of Steinberg Media Technologies GmbH. "Akai", "MPC" and
"inMusic" are trademarks of their respective owners. This project is not
affiliated with or endorsed by Steinberg, inMusic, or Akai Professional.

The built-in formula bank is original work written for this project, except
for the "Sierpinski" entry (`t&t>>8`), a widely known mathematical identity
included as a demonstration. User formulas placed in `Content/Formulas/` remain
the property of their authors. Bytebeat Drums uses the same bank.

The JavaScript engine uses QuickJS-NG, MIT-licensed by Fabrice Bellard,
Charlie Gordon and Ben Noordhuis. It is used only by the Bytebeat Machine VST3;
neither the MPC plugins nor Bytebeat Drums include it.
