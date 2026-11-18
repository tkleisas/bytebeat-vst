// BytebeatParams.h - VST3 parameter IDs and layout.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep

#pragma once

#include <JuceHeader.h>

namespace bbvst
{

namespace pid
{
inline constexpr const char* volume   = "volume";
inline constexpr const char* mode     = "mode";
inline constexpr const char* clock    = "clock";
inline constexpr const char* playMode = "playmode";
inline constexpr const char* run      = "run";
inline constexpr const char* speed    = "speed";
inline constexpr const char* smooth   = "smooth";
inline constexpr const char* tone     = "tone";
inline constexpr const char* drive    = "drive";
inline constexpr const char* dcBlock  = "dcblock";
inline constexpr const char* width    = "width";
inline constexpr const char* pan      = "pan";
inline constexpr const char* voices   = "voices";
inline constexpr const char* root     = "root";
inline constexpr const char* pitch    = "pitch";
inline constexpr const char* attack   = "attack";
inline constexpr const char* release  = "release";
inline constexpr const char* resetNote = "resetnote";

inline const char* macro(int i)
{
    static const char* const names[8] = { "a", "b", "c", "d", "e", "f", "g", "h" };
    return (i >= 0 && i < 8) ? names[i] : "a";
}
} // namespace pid

inline constexpr int kNumMacros = 8;
inline constexpr int kNumClockRates = 8;
inline constexpr int kClockRates[kNumClockRates] = { 4000, 8000, 11025, 16000, 22050, 32000, 44100, 48000 };

inline int clockIndexForRate(int rate)
{
    int best = 1;
    int bestDist = 1 << 30;
    for (int i = 0; i < kNumClockRates; ++i)
    {
        const int d = std::abs(kClockRates[i] - rate);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&](const char* id, const char* name, float lo, float hi, float def,
                        const char* suffix, float skew = 1.0f)
    {
        auto attrs = AudioParameterFloatAttributes()
                        .withStringFromValueFunction([suffix](float v, int)
                        {
                            return String(v, 1) + " " + suffix;
                        });
        NormalisableRange<float> range(lo, hi, 0.01f, skew);
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, range, def, attrs));
    };

    addFloat(pid::volume, "Volume", -40.0f, 6.0f, -12.0f, "dB");
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID { pid::mode, 1 }, "Mode",
        StringArray { "Bytebeat", "Signed", "Floatbeat" }, 0));
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID { pid::clock, 1 }, "Clock",
        StringArray { "4000", "8000", "11025", "16000", "22050", "32000", "44100", "48000" }, 1));
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID { pid::playMode, 1 }, "Play Mode",
        StringArray { "Free", "Sync", "Trigger" }, 2));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { pid::run, 1 }, "Run", true));

    addFloat(pid::speed, "Speed", 0.25f, 4.0f, 1.0f, "x", 0.5f);
    addFloat(pid::smooth, "Smooth", 0.0f, 100.0f, 0.0f, "%");

    for (int i = 0; i < kNumMacros; ++i)
    {
        const char name[2] = { (char)('A' + i), 0 };
        addFloat(pid::macro(i), name, 0.0f, 100.0f, 50.0f, "%");
    }

    addFloat(pid::tone, "Tone", 0.0f, 100.0f, 100.0f, "%");
    addFloat(pid::drive, "Drive", 0.0f, 100.0f, 0.0f, "%");
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { pid::dcBlock, 1 }, "DC Block", true));
    addFloat(pid::width, "Width", 0.0f, 100.0f, 100.0f, "%");
    addFloat(pid::pan, "Pan", -100.0f, 100.0f, 0.0f, "%");

    layout.add(std::make_unique<AudioParameterInt>(ParameterID { pid::voices, 1 }, "Voices", 1, 8, 4));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { pid::root, 1 }, "Root", 0, 127, 60));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { pid::pitch, 1 }, "Pitch Track", false));
    addFloat(pid::attack, "Attack", 0.0f, 100.0f, 2.0f, "ms");
    addFloat(pid::release, "Release", 1.0f, 2000.0f, 80.0f, "ms", 0.35f);
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { pid::resetNote, 1 }, "Reset", true));

    return layout;
}

} // namespace bbvst
