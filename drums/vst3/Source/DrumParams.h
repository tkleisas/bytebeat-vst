// DrumParams.h - VST3 parameter layout for Bytebeat Drums.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep

#pragma once

#include <JuceHeader.h>

#include <array>
#include <vector>

namespace bbdvst
{

constexpr int kNumPads = 16;
constexpr int kPadFields = 11;
constexpr int kNumMasterParams = 4;
constexpr int kNumParams = kNumMasterParams + kNumPads * kPadFields; // 180

namespace pid
{
inline constexpr const char* masterVolume = "master_vol";
inline constexpr const char* masterTone   = "master_tone";
inline constexpr const char* masterDrive  = "master_drive";
inline constexpr const char* noteBase     = "note_base";
}

enum PadField
{
    kFormula = 0,
    kLevel,
    kPan,
    kTune,
    kDecay,
    kChoke,
    kMode,
    kMacroA,
    kMacroB,
    kMacroC,
    kMacroD
};

inline const char* fieldSuffix(int field)
{
    static const char* const suffixes[kPadFields] = {
        "formula", "level", "pan", "tune", "decay", "choke", "mode", "a", "b", "c", "d"
    };
    return (field >= 0 && field < kPadFields) ? suffixes[field] : "formula";
}

inline juce::String padParamId(int pad, int field)
{
    return "p" + juce::String(pad + 1) + "_" + fieldSuffix(field);
}

inline juce::String padParamName(int pad, int field)
{
    static const char* const names[kPadFields] = {
        "Formula", "Level", "Pan", "Tune", "Decay", "Choke", "Mode", "A", "B", "C", "D"
    };
    return "P" + juce::String(pad + 1) + " " + names[field];
}

inline std::vector<juce::String> allParamIds()
{
    std::vector<juce::String> ids;
    ids.reserve((size_t)kNumParams);
    ids.push_back(pid::masterVolume);
    ids.push_back(pid::masterTone);
    ids.push_back(pid::masterDrive);
    ids.push_back(pid::noteBase);
    for (int p = 0; p < kNumPads; ++p)
        for (int f = 0; f < kPadFields; ++f)
            ids.push_back(padParamId(p, f));
    return ids;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout(
    const std::vector<juce::String>& formulaNames)
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&](const String& id, const String& name, float lo, float hi, float def,
                        const char* suffix, float skew = 1.0f)
    {
        NormalisableRange<float> range(lo, hi);
        if (skew != 1.0f)
            range.setSkewForCentre((lo + hi) * 0.5f);
        auto attrs = AudioParameterFloatAttributes().withStringFromValueFunction(
            [suffix](float v, int) { return String(v, 0) + " " + suffix; });
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, range, def, attrs));
    };

    addFloat(pid::masterVolume, "Volume", -40.0f, 6.0f, -12.0f, "dB");
    addFloat(pid::masterTone, "Tone", 0.0f, 100.0f, 100.0f, "%");
    addFloat(pid::masterDrive, "Drive", 0.0f, 100.0f, 0.0f, "%");
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { pid::noteBase, 1 },
                                                   "Note Base", 0, 111, 36));

    StringArray formulas;
    if (formulaNames.empty())
        formulas.add("-");
    else
        for (const auto& n : formulaNames)
            formulas.add(n);

    for (int p = 0; p < kNumPads; ++p)
    {
        const auto formulaId = padParamId(p, kFormula);
        layout.add(std::make_unique<AudioParameterChoice>(
            ParameterID { formulaId, 1 }, padParamName(p, kFormula), formulas, 0));

        addFloat(padParamId(p, kLevel), padParamName(p, kLevel), 0.0f, 100.0f, 70.0f, "%");
        addFloat(padParamId(p, kPan), padParamName(p, kPan), -100.0f, 100.0f, 0.0f, "%");
        addFloat(padParamId(p, kTune), padParamName(p, kTune), -24.0f, 24.0f, 0.0f, "st");

        NormalisableRange<float> decayRange(10.0f, 4000.0f);
        decayRange.setSkewForCentre(250.0f);
        layout.add(std::make_unique<AudioParameterFloat>(
            ParameterID { padParamId(p, kDecay), 1 }, padParamName(p, kDecay),
            decayRange, 300.0f));

        layout.add(std::make_unique<AudioParameterChoice>(
            ParameterID { padParamId(p, kChoke), 1 }, padParamName(p, kChoke),
            StringArray { "Off", "1", "2", "3", "4" }, 0));
        layout.add(std::make_unique<AudioParameterChoice>(
            ParameterID { padParamId(p, kMode), 1 }, padParamName(p, kMode),
            StringArray { "One-shot", "Hold" }, 0));

        addFloat(padParamId(p, kMacroA), padParamName(p, kMacroA), 0.0f, 100.0f, 50.0f, "%");
        addFloat(padParamId(p, kMacroB), padParamName(p, kMacroB), 0.0f, 100.0f, 50.0f, "%");
        addFloat(padParamId(p, kMacroC), padParamName(p, kMacroC), 0.0f, 100.0f, 50.0f, "%");
        addFloat(padParamId(p, kMacroD), padParamName(p, kMacroD), 0.0f, 100.0f, 50.0f, "%");
    }

    return layout;
}

} // namespace bbdvst
