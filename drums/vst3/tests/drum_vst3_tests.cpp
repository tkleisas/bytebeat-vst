// drum_vst3_tests.cpp - offline tests for the Bytebeat Drums VST3 processor.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep

#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition) ++failures;
}

void checkNear(float actual, float expected, float tolerance, const char* what)
{
    const bool ok = std::fabs(actual - expected) <= tolerance;
    std::printf("%s %s (got %.3f, expected %.3f)\n",
                ok ? "PASS" : "FAIL", what, actual, expected);
    if (!ok) ++failures;
}

void setParam(BytebeatDrumsAudioProcessor& p, const char* id, float value)
{
    if (auto* param = p.apvts().getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(value));
}

float getParam(BytebeatDrumsAudioProcessor& p, const char* id)
{
    if (auto* v = p.apvts().getRawParameterValue(id))
        return v->load();
    return 0.0f;
}

void render(BytebeatDrumsAudioProcessor& p, juce::AudioBuffer<float>& buffer,
            juce::MidiBuffer& midi, int blocks)
{
    const int n = buffer.getNumSamples();
    for (int i = 0; i < blocks; ++i)
    {
        if (i > 0) midi.clear();
        p.processBlock(buffer, midi);
        midi.clear();
    }
}

float peak(const juce::AudioBuffer<float>& buffer, int start, int length)
{
    float maxAbs = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int i = start; i < start + length; ++i)
            maxAbs = std::max(maxAbs, std::fabs(data[i]));
    }
    return maxAbs;
}

bool allFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (!std::isfinite(data[i])) return false;
    }
    return true;
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BytebeatDrumsAudioProcessor proc;
    proc.prepareToPlay(48000.0, 512);

    // ---- programs / kits -------------------------------------------------
    check(proc.getNumPrograms() == 10, "10 kit programs");
    check(proc.getKitName(0) == "Init", "kit 0 is Init");
    check(proc.getKitName(6) == "User 1", "kit 6 is User 1");
    check(proc.bank().entries().size() >= 24, "built-in formula bank loaded");
    const auto formulaError = proc.getFormulaError();
    check(formulaError.isEmpty(), "all 16 pad formulas compile");
    if (formulaError.isNotEmpty())
        std::printf("       %s\n", formulaError.toRawUTF8());

    const auto* formula0 = proc.apvts().getParameter(bbdvst::padParamId(0, bbdvst::kFormula));
    check(formula0 != nullptr, "pad formula parameter exists");

    // ---- load / save user kits ------------------------------------------
    setParam(proc, bbdvst::padParamId(0, bbdvst::kLevel).toRawUTF8(), 12.0f);
    setParam(proc, bbdvst::padParamId(0, bbdvst::kFormula).toRawUTF8(), 6.0f);
    proc.saveKit(0);

    proc.loadKit(0);
    checkNear(getParam(proc, bbdvst::padParamId(0, bbdvst::kLevel).toRawUTF8()), 85.0f, 0.01f,
              "factory kit load restores pad level");
    checkNear(getParam(proc, bbdvst::padParamId(0, bbdvst::kFormula).toRawUTF8()), 0.0f, 0.01f,
              "factory kit load restores pad formula");

    proc.loadKit(6);
    checkNear(getParam(proc, bbdvst::padParamId(0, bbdvst::kLevel).toRawUTF8()), 12.0f, 0.01f,
              "user kit load restores pad level");
    checkNear(getParam(proc, bbdvst::padParamId(0, bbdvst::kFormula).toRawUTF8()), 6.0f, 0.01f,
              "user kit load restores pad formula");

    // ---- project state round-trip ---------------------------------------
    juce::MemoryBlock state;
    proc.getStateInformation(state);

    setParam(proc, bbdvst::padParamId(0, bbdvst::kLevel).toRawUTF8(), 99.0f);
    setParam(proc, bbdvst::padParamId(0, bbdvst::kFormula).toRawUTF8(), 12.0f);
    proc.loadKit(0);
    proc.setStateInformation(state.getData(), (int)state.getSize());

    checkNear(getParam(proc, bbdvst::padParamId(0, bbdvst::kLevel).toRawUTF8()), 12.0f, 0.01f,
              "state restore brings back current pad level");
    checkNear(getParam(proc, bbdvst::padParamId(0, bbdvst::kFormula).toRawUTF8()), 6.0f, 0.01f,
              "state restore brings back current pad formula");

    proc.loadKit(0);
    proc.loadKit(6);
    checkNear(getParam(proc, bbdvst::padParamId(0, bbdvst::kLevel).toRawUTF8()), 12.0f, 0.01f,
              "state restore keeps user kit slot");

    // Make the Init kit the starting point for the audio tests.
    proc.loadKit(0);
    setParam(proc, bbdvst::pid::masterVolume, 0.0f);
    setParam(proc, bbdvst::padParamId(0, bbdvst::kLevel).toRawUTF8(), 100.0f);

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)110), 0);
    render(proc, buffer, midi, 8);
    check(allFinite(buffer), "audio is finite");
    check(peak(buffer, 0, 512) > 0.001f, "one-shot pad produces audio");

    // One-shot pads ignore note-off, but the 2 ms gate plus decay releases.
    midi.clear();
    render(proc, buffer, midi, 100);
    check(allFinite(buffer), "one-shot tail is finite");
    check(peak(buffer, 0, 512) < 0.01f, "one-shot pad releases without note-off");

    // Hold mode sustains until note-off and then releases.
    proc.loadKit(0);
    setParam(proc, bbdvst::padParamId(1, bbdvst::kMode).toRawUTF8(), 1.0f);
    setParam(proc, bbdvst::padParamId(1, bbdvst::kTune).toRawUTF8(), 0.0f);
    setParam(proc, bbdvst::padParamId(1, bbdvst::kDecay).toRawUTF8(), 100.0f);
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 37, (juce::uint8)110), 0);
    render(proc, buffer, midi, 8);
    check(peak(buffer, 0, 512) > 0.001f, "hold pad produces audio");

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 37), 0);
    render(proc, buffer, midi, 40);
    check(peak(buffer, 0, 512) < 0.01f, "hold pad releases on note-off");

    // All-notes-off quiets everything.
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)110), 0);
    render(proc, buffer, midi, 4);
    midi.clear();
    midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    render(proc, buffer, midi, 60);
    check(peak(buffer, 0, 512) < 0.01f, "all-notes-off silences output");

    // ---- editor preview scope -------------------------------------------
    proc.previewPad(0);
    std::vector<float> preview(1024, 0.0f);
    proc.renderPreview(preview.data(), (int)preview.size());
    float previewPeak = 0.0f;
    bool previewFinite = true;
    for (float v : preview)
    {
        previewFinite = previewFinite && std::isfinite(v);
        previewPeak = std::max(previewPeak, std::fabs(v));
    }
    check(previewFinite, "preview samples are finite");
    check(previewPeak > 0.0001f, "preview renders audio");

    // ---- invalid state is ignored ---------------------------------------
    const char garbage[] = "not a plugin state";
    proc.setStateInformation(garbage, (int)sizeof(garbage));
    check(true, "invalid state does not crash");

    proc.releaseResources();

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
