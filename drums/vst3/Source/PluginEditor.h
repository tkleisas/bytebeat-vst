// PluginEditor.h - Bytebeat Drums VST3 editor.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep

#pragma once

#include <JuceHeader.h>

#include <memory>

#include "PluginProcessor.h"

class DrumPadGrid;
class DrumScope;

class BytebeatDrumsAudioProcessorEditor : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit BytebeatDrumsAudioProcessorEditor(BytebeatDrumsAudioProcessor&);
    ~BytebeatDrumsAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void selectPad(int pad);
    void rebuildPadControls();

private:
    void timerCallback() override;
    void loadSelectedKit();
    void saveSelectedKit();
    void openFormulaFolder();
    void setStatus(const juce::String& text, bool error);
    void configureKnob(juce::Slider& s);
    void configureLabel(juce::Label& l, const juce::String& text);

    BytebeatDrumsAudioProcessor& proc;

    std::unique_ptr<juce::LookAndFeel_V4> lnf_;

    juce::ComboBox kitBox_;
    juce::TextButton loadKitButton_ { "Load" };
    juce::TextButton saveKitButton_ { "Save" };
    juce::TextButton folderButton_ { "Folder" };
    juce::Label statusLabel_;
    juce::Label padTitle_;
    juce::Label formulaLabel_;
    juce::Label chokeLabel_;
    juce::Label modeLabel_;
    juce::Label levelLabel_, panLabel_, tuneLabel_, decayLabel_;
    juce::Label aLabel_, bLabel_, cLabel_, dLabel_;
    juce::Label volumeLabel_, toneLabel_, driveLabel_, noteBaseLabel_;
    juce::Label helpLabel_;

    juce::ComboBox formulaBox_;
    juce::ComboBox chokeBox_;
    juce::ComboBox modeBox_;

    juce::Slider level_, pan_, tune_, decay_;
    juce::Slider macroA_, macroB_, macroC_, macroD_;
    juce::Slider volume_, tone_, drive_, noteBase_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAtt_, panAtt_, tuneAtt_, decayAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aAtt_, bAtt_, cAtt_, dAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> formulaAtt_, chokeAtt_, modeAtt_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAtt_, toneAtt_, driveAtt_, noteBaseAtt_;

    std::unique_ptr<DrumPadGrid> padGrid_;
    std::unique_ptr<DrumScope> scope_;

    int selectedPad_ = 0;
    int lastSyncedKit_ = -1;
    juce::String statusText_;
    bool statusIsError_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BytebeatDrumsAudioProcessorEditor)
};
