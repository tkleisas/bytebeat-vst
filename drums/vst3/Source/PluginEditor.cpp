// PluginEditor.cpp - Bytebeat Drums VST3 editor.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep

#include "PluginEditor.h"

#include <array>
#include <cmath>
#include <functional>

namespace
{

const juce::Colour kBackground { 0xff0e0e12 };
const juce::Colour kPanel      { 0xff16161c };
const juce::Colour kPanel2     { 0xff1c1c24 };
const juce::Colour kOutline    { 0xff2c2c38 };
const juce::Colour kText       { 0xffd8d8e0 };
const juce::Colour kMuted      { 0xff8a8a98 };
const juce::Colour kAccent     { 0xffffb347 };
const juce::Colour kError      { 0xffff5c5c };

juce::LookAndFeel_V4::ColourScheme makeColourScheme()
{
    return juce::LookAndFeel_V4::ColourScheme(
        juce::Colour(0xff0e0e12), juce::Colour(0xff16161c), juce::Colour(0xff1c1c24),
        juce::Colour(0xff2c2c38), juce::Colour(0xffd8d8e0), juce::Colour(0xffffb347),
        juce::Colour(0xffffffff), juce::Colour(0xffffb347), juce::Colour(0xffffffff));
}

} // namespace

class DrumPadGrid : public juce::Component
{
public:
    explicit DrumPadGrid(BytebeatDrumsAudioProcessor& p) : proc_(p) {}

    std::function<void(int)> onPad;

    void setSelected(int pad) { selected_ = pad; repaint(); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(kPanel);
        const int cols = 4;
        const int rows = 4;
        const float cw = getWidth() / (float)cols;
        const float ch = getHeight() / (float)rows;

        for (int p = 0; p < 16; ++p)
        {
            const int col = p % cols;
            const int row = p / cols;
            auto cell = juce::Rectangle<float>(col * cw, row * ch, cw, ch).reduced(3.0f);

            g.setColour(p == selected_ ? kAccent.withAlpha(0.28f) : kPanel2);
            g.fillRoundedRectangle(cell, 5.0f);
            g.setColour(p == selected_ ? kAccent : kOutline);
            g.drawRoundedRectangle(cell.reduced(0.5f), 5.0f, p == selected_ ? 2.0f : 1.0f);

            g.setColour(p == selected_ ? juce::Colours::white : kText);
            g.setFont(12.0f);
            g.drawText("P" + juce::String(p + 1), cell.reduced(7.0f, 4.0f),
                       juce::Justification::topLeft, false);

            g.setColour(kMuted);
            g.setFont(10.0f);
            g.drawFittedText(proc_.getPadFormulaName(p),
                             cell.reduced(7.0f, 4.0f).withTrimmedTop(16.0f).toNearestInt(),
                             juce::Justification::topLeft, 2);

            const float level = juce::jlimit(0.0f, 1.0f, proc_.getPadLevel(p));
            auto bar = cell.removeFromBottom(6.0f).reduced(7.0f, 0.0f);
            g.setColour(kOutline);
            g.fillRect(bar);
            g.setColour(kAccent.withAlpha(0.85f));
            g.fillRect(bar.withWidth(bar.getWidth() * level));
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const int col = juce::jlimit(0, 3, (int)(e.position.x / (getWidth() / 4.0f)));
        const int row = juce::jlimit(0, 3, (int)(e.position.y / (getHeight() / 4.0f)));
        const int pad = row * 4 + col;
        selected_ = pad;
        repaint();
        if (onPad)
            onPad(pad);
    }

private:
    BytebeatDrumsAudioProcessor& proc_;
    int selected_ = 0;
};

class DrumScope : public juce::Component,
                  private juce::Timer
{
public:
    explicit DrumScope(BytebeatDrumsAudioProcessor& p) : proc_(p)
    {
        setOpaque(true);
        startTimerHz(30);
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(kPanel);
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(kOutline);
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);

        auto inner = r.reduced(10.0f);
        const float mid = inner.getCentreY();
        const float scale = inner.getHeight() * 0.42f;

        g.setColour(kOutline.withAlpha(0.7f));
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine((int)(inner.getY() + inner.getHeight() * (float)i / 4.0f),
                                 inner.getX(), inner.getRight());

        juce::Path path;
        for (int i = 0; i < (int)preview_.size(); ++i)
        {
            const float x = inner.getX() + inner.getWidth() * (float)i / (float)(preview_.size() - 1);
            const float y = mid - juce::jlimit(-1.0f, 1.0f, preview_[(size_t)i]) * scale;
            if (i == 0) path.startNewSubPath(x, y);
            else path.lineTo(x, y);
        }
        g.setColour(kAccent);
        g.strokePath(path, juce::PathStrokeType(1.4f));

        g.setColour(kMuted);
        g.setFont(11.0f);
        const int pad = proc_.getPreviewPad();
        g.drawText(pad >= 0 ? "PAD " + juce::String(pad + 1) + " PREVIEW" : "CLICK A PAD TO PREVIEW",
                   8, 5, getWidth() - 16, 14, juce::Justification::topRight);
    }

private:
    void timerCallback() override
    {
        proc_.renderPreview(preview_.data(), (int)preview_.size());
        repaint();
    }

    BytebeatDrumsAudioProcessor& proc_;
    std::array<float, 1024> preview_ {};
};

BytebeatDrumsAudioProcessorEditor::BytebeatDrumsAudioProcessorEditor(BytebeatDrumsAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setOpaque(true);
    setResizable(false, false);

    lnf_ = std::make_unique<juce::LookAndFeel_V4>(makeColourScheme());
    setLookAndFeel(lnf_.get());

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        kitBox_.addItem(proc.getKitName(i), i + 1);
    kitBox_.setSelectedItemIndex(juce::jlimit(0, 9, proc.getCurrentKit()), juce::dontSendNotification);
    lastSyncedKit_ = kitBox_.getSelectedItemIndex();
    kitBox_.setColour(juce::ComboBox::backgroundColourId, kPanel2);
    kitBox_.setColour(juce::ComboBox::textColourId, kText);
    kitBox_.setColour(juce::ComboBox::outlineColourId, kOutline);
    addAndMakeVisible(kitBox_);

    for (auto* b : { &loadKitButton_, &saveKitButton_, &folderButton_ })
    {
        b->setColour(juce::TextButton::buttonColourId, kPanel2);
        b->setColour(juce::TextButton::textColourOffId, kText);
        addAndMakeVisible(*b);
    }

    loadKitButton_.onClick = [this] { loadSelectedKit(); };
    saveKitButton_.onClick = [this] { saveSelectedKit(); };
    folderButton_.onClick = [this] { openFormulaFolder(); };

    statusLabel_.setJustificationType(juce::Justification::centredRight);
    statusLabel_.setColour(juce::Label::textColourId, kMuted);
    addAndMakeVisible(statusLabel_);

    padTitle_.setColour(juce::Label::textColourId, kAccent);
    padTitle_.setFont(juce::Font(16.0f, juce::Font::bold));
    addAndMakeVisible(padTitle_);

    configureLabel(formulaLabel_, "Formula");
    configureLabel(chokeLabel_, "Choke");
    configureLabel(modeLabel_, "Mode");
    configureLabel(levelLabel_, "Level");
    configureLabel(panLabel_, "Pan");
    configureLabel(tuneLabel_, "Tune");
    configureLabel(decayLabel_, "Decay");
    configureLabel(aLabel_, "Macro A");
    configureLabel(bLabel_, "Macro B");
    configureLabel(cLabel_, "Macro C");
    configureLabel(dLabel_, "Macro D");
    configureLabel(volumeLabel_, "Volume");
    configureLabel(toneLabel_, "Tone");
    configureLabel(driveLabel_, "Drive");
    configureLabel(noteBaseLabel_, "Note Base");
    configureLabel(helpLabel_, "Pads: MIDI 36-51  ·  .bb formulas in Folder  ·  click a pad to preview");

    helpLabel_.setJustificationType(juce::Justification::centredLeft);
    helpLabel_.setColour(juce::Label::textColourId, kMuted);
    addAndMakeVisible(helpLabel_);

    for (auto* l : { &formulaLabel_, &chokeLabel_, &modeLabel_, &levelLabel_, &panLabel_,
                     &tuneLabel_, &decayLabel_, &aLabel_, &bLabel_, &cLabel_, &dLabel_,
                     &volumeLabel_, &toneLabel_, &driveLabel_, &noteBaseLabel_ })
        addAndMakeVisible(*l);

    const auto names = proc.bank().names();
    for (int i = 0; i < (int)names.size(); ++i)
        formulaBox_.addItem(names[(size_t)i], i + 1);
    formulaBox_.setColour(juce::ComboBox::backgroundColourId, kPanel2);
    formulaBox_.setColour(juce::ComboBox::textColourId, kText);
    formulaBox_.setColour(juce::ComboBox::outlineColourId, kOutline);
    addAndMakeVisible(formulaBox_);

    chokeBox_.addItemList({ "Off", "1", "2", "3", "4" }, 1);
    modeBox_.addItemList({ "One-shot", "Hold" }, 1);
    for (auto* c : { &chokeBox_, &modeBox_ })
    {
        c->setColour(juce::ComboBox::backgroundColourId, kPanel2);
        c->setColour(juce::ComboBox::textColourId, kText);
        c->setColour(juce::ComboBox::outlineColourId, kOutline);
        addAndMakeVisible(*c);
    }

    for (auto* s : { &level_, &pan_, &tune_, &decay_, &macroA_, &macroB_, &macroC_, &macroD_,
                     &volume_, &tone_, &drive_, &noteBase_ })
    {
        configureKnob(*s);
        addAndMakeVisible(*s);
    }

    auto& state = proc.apvts();
    volumeAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        state, bbdvst::pid::masterVolume, volume_);
    toneAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        state, bbdvst::pid::masterTone, tone_);
    driveAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        state, bbdvst::pid::masterDrive, drive_);
    noteBaseAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        state, bbdvst::pid::noteBase, noteBase_);

    padGrid_ = std::make_unique<DrumPadGrid>(proc);
    padGrid_->onPad = [this](int pad)
    {
        selectPad(pad);
        proc.previewPad(pad);
    };
    addAndMakeVisible(*padGrid_);

    scope_ = std::make_unique<DrumScope>(proc);
    addAndMakeVisible(*scope_);

    kitBox_.onChange = [this]
    {
        // Browsing the kit list does not load; the Load button commits it.
        setStatus("Kit selected: " + proc.getKitName(kitBox_.getSelectedItemIndex()), false);
    };

    rebuildPadControls();
    setStatus("Ready", false);
    setSize(1120, 720);
    startTimerHz(8);
}

BytebeatDrumsAudioProcessorEditor::~BytebeatDrumsAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void BytebeatDrumsAudioProcessorEditor::configureKnob(juce::Slider& s)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    s.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    s.setColour(juce::Slider::rotarySliderOutlineColourId, kOutline);
    s.setColour(juce::Slider::thumbColourId, kAccent.brighter(0.3f));
    s.setColour(juce::Slider::textBoxTextColourId, kText);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

void BytebeatDrumsAudioProcessorEditor::configureLabel(juce::Label& l, const juce::String& text)
{
    l.setText(text, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(juce::Font(11.0f));
    l.setColour(juce::Label::textColourId, kMuted);
}

void BytebeatDrumsAudioProcessorEditor::rebuildPadControls()
{
    levelAtt_.reset();
    panAtt_.reset();
    tuneAtt_.reset();
    decayAtt_.reset();
    aAtt_.reset();
    bAtt_.reset();
    cAtt_.reset();
    dAtt_.reset();
    formulaAtt_.reset();
    chokeAtt_.reset();
    modeAtt_.reset();

    auto& state = proc.apvts();
    const auto id = [this](int field) { return bbdvst::padParamId(selectedPad_, field); };

    levelAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        state, id(bbdvst::kLevel), level_);
    panAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        state, id(bbdvst::kPan), pan_);
    tuneAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        state, id(bbdvst::kTune), tune_);
    decayAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        state, id(bbdvst::kDecay), decay_);
    aAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        state, id(bbdvst::kMacroA), macroA_);
    bAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        state, id(bbdvst::kMacroB), macroB_);
    cAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        state, id(bbdvst::kMacroC), macroC_);
    dAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        state, id(bbdvst::kMacroD), macroD_);
    formulaAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        state, id(bbdvst::kFormula), formulaBox_);
    chokeAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        state, id(bbdvst::kChoke), chokeBox_);
    modeAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        state, id(bbdvst::kMode), modeBox_);

    padTitle_.setText("PAD " + juce::String(selectedPad_ + 1), juce::dontSendNotification);
    if (padGrid_)
        padGrid_->setSelected(selectedPad_);
}

void BytebeatDrumsAudioProcessorEditor::selectPad(int pad)
{
    pad = juce::jlimit(0, (int)BytebeatDrumsAudioProcessor::kNumPads - 1, pad);
    if (pad == selectedPad_)
        return;
    selectedPad_ = pad;
    rebuildPadControls();
}

void BytebeatDrumsAudioProcessorEditor::timerCallback()
{
    const auto formulaError = proc.getFormulaError();
    if (formulaError.isNotEmpty())
    {
        statusLabel_.setText(formulaError, juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, kError);
    }
    else
    {
        statusLabel_.setText(statusText_, juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, statusIsError_ ? kError : kMuted);
    }

    // Only follow kit changes made by the processor (load/save/program
    // change). Do not snap back while the user is browsing the list before
    // pressing Load.
    const int currentKit = proc.getCurrentKit();
    if (currentKit >= 0 && currentKit < proc.getNumPrograms()
        && currentKit != lastSyncedKit_)
    {
        kitBox_.setSelectedItemIndex(currentKit, juce::dontSendNotification);
        lastSyncedKit_ = currentKit;
    }

    if (padGrid_)
        padGrid_->repaint();
}

void BytebeatDrumsAudioProcessorEditor::loadSelectedKit()
{
    const int index = kitBox_.getSelectedItemIndex();
    if (index < 0) return;
    proc.setCurrentProgram(index);
    lastSyncedKit_ = index;
    kitBox_.setSelectedItemIndex(index, juce::dontSendNotification);
    setStatus("Loaded " + proc.getKitName(index), false);
}

void BytebeatDrumsAudioProcessorEditor::saveSelectedKit()
{
    int index = kitBox_.getSelectedItemIndex();
    if (index < 0) index = 0;
    const int slot = (index >= 6 && index <= 9) ? index - 6 : 0;
    proc.saveKit(slot);
    lastSyncedKit_ = 6 + slot;
    kitBox_.setSelectedItemIndex(6 + slot, juce::dontSendNotification);
    setStatus("Saved to " + proc.getKitName(6 + slot), false);
}

void BytebeatDrumsAudioProcessorEditor::openFormulaFolder()
{
    auto folder = bbdvst::DrumBank::userFolder();
    folder.createDirectory();
    folder.revealToUser();
    setStatus("Folder opened - restart the plugin after adding .bb files", false);
}

void BytebeatDrumsAudioProcessorEditor::setStatus(const juce::String& text, bool error)
{
    statusText_ = text;
    statusIsError_ = error;
    statusLabel_.setText(text, juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId, error ? kError : kMuted);
}

void BytebeatDrumsAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);

    auto header = getLocalBounds().removeFromTop(52);
    g.setColour(kPanel);
    g.fillRect(header);
    g.setColour(kOutline);
    g.drawHorizontalLine(51, 0.0f, (float)getWidth());

    g.setColour(kAccent);
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText("BYTEBEAT DRUMS", 14, 12, 190, 28, juce::Justification::centredLeft);

    g.setColour(kMuted);
    g.setFont(11.0f);
    g.drawText("KIT", 210, 10, 30, 12, juce::Justification::centredLeft);
}

void BytebeatDrumsAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop(52);

    kitBox_.setBounds(210, 14, 168, 28);
    loadKitButton_.setBounds(386, 14, 58, 28);
    saveKitButton_.setBounds(450, 14, 58, 28);
    folderButton_.setBounds(514, 14, 64, 28);
    statusLabel_.setBounds(590, 14, 518, 28);

    auto body = r.reduced(12, 8);
    auto left = body.removeFromLeft(450);
    body.removeFromLeft(16);
    auto right = body;

    if (padGrid_)
        padGrid_->setBounds(left.removeFromTop(450));
    left.removeFromTop(10);
    if (scope_)
        scope_->setBounds(left.removeFromTop(180));

    padTitle_.setBounds(right.removeFromTop(24));
    right.removeFromTop(2);

    auto formulaRow = right.removeFromTop(48);
    formulaLabel_.setBounds(formulaRow.removeFromTop(16));
    formulaBox_.setBounds(formulaRow);

    auto placeKnob = [this](juce::Slider& s, juce::Label& l, int x, int y, int w, int h)
    {
        l.setBounds(x, y, w, 16);
        s.setBounds(x, y + 16, w, h - 16);
    };

    const int colW = right.getWidth() / 4;
    const int x0 = right.getX();

    placeKnob(level_, levelLabel_, x0, right.getY() + 4, colW, 96);
    placeKnob(pan_, panLabel_, x0 + colW, right.getY() + 4, colW, 96);
    placeKnob(tune_, tuneLabel_, x0 + colW * 2, right.getY() + 4, colW, 96);
    placeKnob(decay_, decayLabel_, x0 + colW * 3, right.getY() + 4, colW, 96);

    placeKnob(macroA_, aLabel_, x0, right.getY() + 108, colW, 96);
    placeKnob(macroB_, bLabel_, x0 + colW, right.getY() + 108, colW, 96);
    placeKnob(macroC_, cLabel_, x0 + colW * 2, right.getY() + 108, colW, 96);
    placeKnob(macroD_, dLabel_, x0 + colW * 3, right.getY() + 108, colW, 96);

    int comboY = right.getY() + 218;
    chokeLabel_.setBounds(x0, comboY, 140, 16);
    chokeBox_.setBounds(x0, comboY + 18, 170, 28);
    modeLabel_.setBounds(x0 + 190, comboY, 140, 16);
    modeBox_.setBounds(x0 + 190, comboY + 18, 170, 28);

    int masterY = comboY + 60;
    placeKnob(volume_, volumeLabel_, x0, masterY, colW, 96);
    placeKnob(tone_, toneLabel_, x0 + colW, masterY, colW, 96);
    placeKnob(drive_, driveLabel_, x0 + colW * 2, masterY, colW, 96);
    placeKnob(noteBase_, noteBaseLabel_, x0 + colW * 3, masterY, colW, 96);

    helpLabel_.setBounds(x0, masterY + 104, right.getWidth(), 22);
}
