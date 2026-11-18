// PluginProcessor.h - Bytebeat Machine VST3 processor.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep

#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "BytebeatCompile.h"
#include "BytebeatEngine.h"
#include "BytebeatJsEngine.h"
#include "BytebeatParams.h"
#include "FormulaLibraryModel.h"

class BytebeatAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kScopeSize = 2048;

    BytebeatAudioProcessor();
    ~BytebeatAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Bytebeat Machine"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& apvts() { return apvts_; }

    // ---- formula API (message thread) -----------------------------------
    bool applyFormula(const juce::String& source, const juce::String& name,
                      juce::String& errorOut);
    bool loadLibraryEntry(const bbvst::LibraryEntry& entry, juce::String& errorOut);

    // ---- alternative JavaScript engine (VST3 only) ----------------------
    enum EngineMode
    {
        kEngineNative = 0,
        kEngineJs = 1
    };

    int getEngineMode() const { return engineMode_.load(std::memory_order_relaxed); }
    int getJsMode() const { return jsMode_.load(std::memory_order_relaxed); }
    bool setEngineMode(int mode, juce::String& errorOut);
    bool setJsMode(int mode, juce::String& errorOut);
    bool getJsRuntimeError() const;
    void clearJsRuntimeError();

    juce::String getFormulaSource() const;
    juce::String getFormulaName() const;
    juce::String getCompileError() const;
    int getCompileErrorLine() const;
    int getCompileErrorCol() const;
    int getFormulaRevision() const { return formulaRevision_.load(); }
    int getActiveProgramOps() const { return activeProgramOps_.load(); }

    // ---- scope / display ------------------------------------------------
    float getScopeSample(int index) const
    {
        return scope_[(size_t)(index % kScopeSize)].load(std::memory_order_relaxed);
    }
    int getScopeWritePos() const { return scopeWrite_.load(std::memory_order_relaxed); }
    float getOutputPeak() const { return outputPeak_.load(std::memory_order_relaxed); }
    int64_t getDisplayT() const { return displayT_.load(std::memory_order_relaxed); }
    int getDisplayClock() const { return displayClock_.load(std::memory_order_relaxed); }

    // Message-thread preview used by the editor's scope. This runs a second
    // engine instance so it also works in process-isolated editors (YAWN),
    // where the editor process never receives audio.
    void renderPreview(float* mono, int n);
    int64_t getPreviewT() const { return previewT_.load(std::memory_order_relaxed); }
    int getPreviewClock() const { return previewClock_.load(std::memory_order_relaxed); }

    // Called by the editor when the user requests a fresh compile without
    // changing the source (e.g. after editing a macro label).
    juce::AudioProcessorValueTreeState& state() { return apvts_; }

private:
    struct FactoryPreset
    {
        juce::String name;
        juce::String source;
        int mode = 0;
        int rate = 8000;
        std::vector<std::pair<juce::String, float>> params;
    };

    void loadDefaultFormula();
    void buildFactoryPresets();
    void setParam(const char* id, float plainValue);
    static float plain(const juce::AudioProcessorValueTreeState& apvts, const char* id);
    bytebeat::Settings makeSettings(double sampleRate) const;
    void updateScope(const float* left, const float* right, int n, int clockRate);

    bool applyFormulaAs(const juce::String& source, const juce::String& name,
                        int targetEngine, juce::String& errorOut, bool commit);
    bool compileJs(const juce::String& source, int jsMode,
                   std::unique_ptr<bbvst::JsEvaluator>& audio,
                   std::unique_ptr<bbvst::JsEvaluator>& preview,
                   std::string& errorOut) const;
    void commitFormulaLocked(const juce::String& source, const juce::String& name);

    juce::AudioProcessorValueTreeState apvts_;

    bytebeat::Engine engine_;
    bytebeat::Engine previewEngine_;
    bool previewPrepared_ = false;
    std::atomic<int64_t> previewT_ { 0 };
    std::atomic<int> previewClock_ { 8000 };

    // The audio thread reads only activeProgram_. Everything else in this
    // group is guarded by formulaMutex_ and only touched on the message thread.
    mutable std::mutex formulaMutex_;
    std::vector<std::unique_ptr<bytebeat::Program>> programs_;
    std::atomic<const bytebeat::Program*> activeProgram_ { nullptr };
    std::atomic<int> activeProgramOps_ { 0 };
    std::atomic<int> formulaRevision_ { 0 };
    juce::String formulaSource_;
    juce::String formulaName_;
    juce::String compileError_;
    int compileErrorLine_ = 0;
    int compileErrorCol_ = 0;

    // JavaScript engine resources. These are message-thread only; the audio
    // thread reads only jsAudioActive_.
    std::atomic<int> engineMode_ { kEngineNative };
    std::atomic<int> jsMode_ { 0 };
    std::unique_ptr<bbvst::JsEvaluator> jsAudio_;
    std::unique_ptr<bbvst::JsEvaluator> jsPreview_;
    std::vector<std::unique_ptr<bbvst::JsEvaluator>> jsHistory_;
    std::atomic<bbvst::JsEvaluator*> jsAudioActive_ { nullptr };

    std::vector<FactoryPreset> presets_;
    std::atomic<int> currentProgram_ { -1 };

    std::atomic<float> scope_[kScopeSize] {};
    std::atomic<int> scopeWrite_ { 0 };
    std::atomic<float> outputPeak_ { 0.0f };
    std::atomic<int64_t> displayT_ { 0 };
    std::atomic<int> displayClock_ { 8000 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BytebeatAudioProcessor)
};
