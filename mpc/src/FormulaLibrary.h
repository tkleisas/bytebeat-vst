// FormulaLibrary.h - the built-in bytebeat bank.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep
//
// These are original studies in the bytebeat idiom. Users can add their own
// .bb files in Content/Formulas; they are appended to this bank at load time.

#ifndef BYTEBEAT_FORMULA_LIBRARY_H
#define BYTEBEAT_FORMULA_LIBRARY_H

#include "BytebeatCompile.h"

#include <vector>

namespace bytebeat
{

struct BuiltinFormula
{
    const char* name;
    const char* category;
    int         mode;      // kOutBytebeat / kOutSigned / kOutFloat
    int         rate;
    const char* source;
};

inline const std::vector<BuiltinFormula>& builtinFormulas()
{
    static const std::vector<BuiltinFormula> bank = {
        // ---- Rhythmic -------------------------------------------------------
        { "Copper Bars", "Rhythmic", 0, 8000,
          "t*(t>>9&31)>>3" },

        { "Sierpinski", "Rhythmic", 0, 8000,
          "t&t>>8" },

        { "Bit Grid", "Rhythmic", 0, 8000,
          "(t>>4&t>>7)|(t>>9&t>>3)" },

        { "Rusty", "Rhythmic", 0, 8000,
          "((t>>7)*(t>>5))&255" },

        { "Step Bass", "Rhythmic", 0, 8000,
          "(t>>6)*(1+(t>>12&7))&255" },

        { "Hollow", "Rhythmic", 0, 8000,
          "((t>>5)^(t>>11))*3&255" },

        { "Metal", "Rhythmic", 0, 8000,
          "(t*(t>>8|t>>13)&255)>>1" },

        { "Morse", "Rhythmic", 0, 8000,
          "(t>>2&t>>9)?0:((t>>10)&7)*32" },

        { "Dust", "Noise", 0, 8000,
          "(t>>3)^(t>>7)^(t>>11)" },

        { "Hoover", "Noise", 0, 8000,
          "((t>>4)&(t>>9))*2+(t>>13&7)" },

        { "LFSR", "Noise", 0, 8000,
          "(t*1103515245+12345)>>16&255" },

        // ---- Melodic / tonal -----------------------------------------------
        { "Chime", "Tonal", 0, 8000,
          "sin(t/18)*sin(t/47)*127+128" },

        { "Warble", "Tonal", 0, 8000,
          "sin(t*0.03+sin(t*0.007)*4)*100+128" },

        { "Sub", "Tonal", 0, 8000,
          "sin(t*0.01)*90+100" },

        { "FM Pluck", "Tonal", 0, 8000,
          "sin(t/12+sin(t/7)*3)*(1-(t>>9&63)/64)*100+128" },

        // ---- Floatbeat ------------------------------------------------------
        { "Glass", "Floatbeat", 2, 8000,
          "sin(t/20)+sin(t/31)*0.5" },

        { "Choir", "Floatbeat", 2, 8000,
          "sin(t/16)*0.6+sin(t/23)*0.4" },

        { "Drift", "Floatbeat", 2, 8000,
          "sin(t/40+sin(t/121)*2)*0.7" },

        { "Stereo Maze", "Floatbeat", 2, 8000,
          "[sin(t/20)*0.7, sin(t/27)*0.7]" },

        // ---- Signed ---------------------------------------------------------
        { "Longline-ish", "Signed", 1, 8000,
          "(t>>6&t>>10)-64" },

        { "Tape", "Signed", 1, 8000,
          "((t>>5)*(t>>9)&255)-128" },

        // ---- Stateful -------------------------------------------------------
        { "Accumulator", "Stateful", 0, 8000,
          "x=x*0.995+(t>>9&7); x&255" },

        { "Filtered", "Stateful", 2, 8000,
          "x=x*0.98+((t>>4&255)-128)*0.02; x*3" },

        { "Pulse Train", "Stateful", 0, 8000,
          "x=x+1; (x&255)<((t>>10&31)+8)?192:0" },
    };
    return bank;
}

inline const char* outputModeName(int mode)
{
    switch (mode)
    {
        case 1: return "Signed";
        case 2: return "Floatbeat";
        default: return "Bytebeat";
    }
}

} // namespace bytebeat

#endif // BYTEBEAT_FORMULA_LIBRARY_H
