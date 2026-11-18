// PluginEditor.h - Bytebeat Machine VST3 editor.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep

#pragma once

#include <JuceHeader.h>

#include <memory>
#include <vector>

#include "BytebeatTokeniser.h"
#include "FormulaLibraryModel.h"

class BytebeatAudioProcessor;
class ScopeComponent;

class BytebeatAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer,
                                    private juce::CodeDocument::Listener,
                                    private juce::KeyListener
{
public:
    explicit BytebeatAudioProcessorEditor(BytebeatAudioProcessor&);
    ~BytebeatAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    using juce::Component::keyPressed;

    void codeDocumentTextInserted(const juce::String&, int) override;
    void codeDocumentTextDeleted(int, int) override;

    bool keyPressed(const juce::KeyPress&, juce::Component*) override;

    void loadLibraryRow(int row);

private:
    void timerCallback() override;
    bool applyCurrentFormula();
    void loadSelectedLibraryEntry();
    void refreshLibrary();
    void saveFormulaAs();
    void openFormulaFolder();
    void newFormula();
    void setStatus(const juce::String& text, bool error);
    void loadFromProcessor(bool force);
    void updateErrorMarker();

    BytebeatAudioProcessor& proc;

    std::unique_ptr<juce::LookAndFeel_V4> lnf_;

    bbvst::FormulaLibraryModel library_;
    std::vector<bbvst::LibraryEntry> libraryEntries_;

    juce::TextEditor nameEditor_;
    juce::TextButton applyButton_ { "Apply" };
    juce::ToggleButton autoApply_ { "Auto" };
    juce::Label statusLabel_;
    juce::Label opsLabel_;

    juce::CodeDocument document_;
    bbvst::BytebeatTokeniser tokeniser_;
    std::unique_ptr<juce::CodeEditorComponent> codeEditor_;

    juce::ListBox libraryList_;
    std::unique_ptr<juce::ListBoxModel> libraryModel_;
    juce::TextButton loadButton_ { "Load" };
    juce::TextButton saveButton_ { "Save .bb" };
    juce::TextButton folderButton_ { "Folder" };
    juce::TextButton refreshButton_ { "Refresh" };
    juce::TextButton newButton_ { "New" };

    std::unique_ptr<ScopeComponent> scope_;
    std::unique_ptr<juce::Component> macrosPanel_;
    std::unique_ptr<juce::Component> playPanel_;
    std::unique_ptr<juce::TabbedComponent> tabs_;

    juce::ComboBox* engineCombo_ = nullptr;
    juce::ComboBox* jsModeCombo_ = nullptr;
    bool updatingCombos_ = false;

    int lastRevision_ = -1;
    bool suppressEdits_ = false;
    bool dirty_ = false;
    juce::uint32 lastEditMs_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BytebeatAudioProcessorEditor)
};
