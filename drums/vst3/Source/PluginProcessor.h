// PluginProcessor.h - Bytebeat Drums VST3 processor.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep

#pragma once

#include <JuceHeader.h>

#include "BytebeatEngine.h"
#include "DrumBank.h"
#include "DrumKits.h"
#include "DrumParams.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

class BytebeatDrumsAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kNumPads = bbdvst::kNumPads;
    static constexpr int kNumParams = bbdvst::kNumParams;

    BytebeatDrumsAudioProcessor();
    ~BytebeatDrumsAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Bytebeat Drums"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 10; }
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& apvts() { return apvts_; }
    const bbdvst::DrumBank& bank() const { return bank_; }

    // ---- kit browser (message thread) -----------------------------------
    int getCurrentKit() const { return currentKit_.load(std::memory_order_relaxed); }
    juce::String getKitName(int index) const;
    void loadKit(int index);
    void saveKit(int slot); // 0..3

    // ---- pad audition / preview scope -----------------------------------
    void previewPad(int pad, int velocity = 110);
    void renderPreview(float* mono, int n);
    int getPreviewPad() const { return previewPad_; }

    // ---- editor helpers -------------------------------------------------
    juce::String getFormulaError() const;
    juce::String getPadFormulaName(int pad) const;
    int getPadFormulaIndex(int pad) const;
    float getPadLevel(int pad) const;

private:
    struct Pad
    {
        bytebeat::Engine engine;
        int lastNote = 36;
        float chokeGain = 1.0f;
        bool choking = false;
        bool oneShot = false;
    };

    float plain(const char* id) const;
    int plainInt(const char* id) const;
    void cacheParamPointers();
    float rawValue(int index) const;
    int rawInt(int index) const;
    bytebeat::Settings makePadSettings(int pad, double sampleRate) const;
    void triggerPad(int pad, int note, int velocity);
    void releasePad(int pad, int note);
    void allPadsOff();
    void renderPads(float* left, float* right, int n);
    static float softClip(float x);
    static int clampInt(int v, int lo, int hi);

    std::array<float, kNumParams> readParams() const;
    void applyParams(const std::array<float, kNumParams>& values);
    static std::array<float, kNumParams> kitValues(const bbdrums::KitSpec& kit);
    void buildUserKits();

    bbdvst::DrumBank bank_;
    juce::AudioProcessorValueTreeState apvts_;
    std::array<std::atomic<float>*, bbdvst::kNumParams> raw_ {};

    Pad pads_[kNumPads];
    std::vector<float> tempL_;
    std::vector<float> tempR_;
    double sampleRate_ = 44100.0;
    float chokeStep_ = 0.0045f;

    std::array<std::array<float, kNumParams>, 4> userKits_ {};
    std::atomic<int> currentKit_ { -1 };

    bytebeat::Engine previewEngine_;
    int previewPad_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BytebeatDrumsAudioProcessor)
};
