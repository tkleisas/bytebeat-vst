// PluginEditor.cpp - Bytebeat Machine VST3 editor.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "BytebeatParams.h"

#include <array>
#include <cmath>

namespace
{

const juce::Colour kBackground  { 0xff0e0e12 };
const juce::Colour kPanel       { 0xff16161c };
const juce::Colour kPanel2      { 0xff1c1c24 };
const juce::Colour kOutline     { 0xff2c2c38 };
const juce::Colour kText        { 0xffd8d8e0 };
const juce::Colour kMuted       { 0xff8a8a98 };
const juce::Colour kAccent      { 0xff9b63ff };
const juce::Colour kError       { 0xffff5c5c };

void configureKnob(juce::Slider& s)
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

void configureLabel(juce::Label& l, const juce::String& text)
{
    l.setText(text, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(juce::Font(12.0f));
    l.setColour(juce::Label::textColourId, kMuted);
}

} // namespace

// ---------------------------------------------------------------------------
// Scope
// ---------------------------------------------------------------------------
class ScopeComponent : public juce::Component,
                       private juce::Timer
{
public:
    explicit ScopeComponent(BytebeatAudioProcessor& p)
        : proc_(p), fft_(10)
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
        auto wave = inner.removeFromTop(inner.getHeight() * 0.56f);
        inner.removeFromTop(6.0f);
        drawWave(g, wave);
        drawSpectrum(g, inner);

        g.setColour(kMuted);
        g.setFont(11.0f);
        g.drawText("t " + juce::String(proc_.getPreviewT()) + "  preview",
                   8, 6, getWidth() - 16, 14, juce::Justification::topRight);
        g.drawText(juce::String(proc_.getPreviewClock()) + " Hz clock",
                   8, getHeight() - 20, 150, 14, juce::Justification::bottomLeft);

        float peak = 0.0f;
        for (float v : preview_) peak = juce::jmax(peak, std::abs(v));
        const float db = peak > 0.0f ? juce::Decibels::gainToDecibels(peak) : -100.0f;
        g.drawText("peak " + juce::String(db, 1) + " dB",
                   getWidth() - 130, getHeight() - 20, 122, 14,
                   juce::Justification::bottomRight);
    }

private:
    void timerCallback() override
    {
        proc_.renderPreview(preview_.data(), (int)preview_.size());
        repaint();
    }

    void drawWave(juce::Graphics& g, juce::Rectangle<float> area)
    {
        const int n = (int)preview_.size();
        const auto& vals = preview_;

        g.setColour(kOutline.withAlpha(0.7f));
        for (int i = 1; i < 4; ++i)
        {
            const float y = area.getY() + area.getHeight() * (float)i / 4.0f;
            g.drawHorizontalLine((int)y, area.getX(), area.getRight());
        }

        juce::Path p;
        const float mid = area.getCentreY();
        const float scale = area.getHeight() * 0.46f;
        for (int i = 0; i < n; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float)i / (float)(n - 1);
            const float y = mid - juce::jlimit(-1.0f, 1.0f, vals[(size_t)i]) * scale;
            if (i == 0) p.startNewSubPath(x, y);
            else p.lineTo(x, y);
        }

        g.setColour(kAccent);
        g.strokePath(p, juce::PathStrokeType(1.3f));
    }

    void drawSpectrum(juce::Graphics& g, juce::Rectangle<float> area)
    {
        constexpr int fftOrder = 10;
        constexpr int fftSize = 1 << fftOrder;

        std::array<float, fftSize * 2> data {};

        for (int i = 0; i < fftSize; ++i)
        {
            const size_t idx = (size_t)(i % (int)preview_.size());
            const float win = 0.5f - 0.5f * std::cos(6.28318530718f * (float)i / (float)(fftSize - 1));
            data[(size_t)i] = preview_[idx] * win;
        }

        fft_.performFrequencyOnlyForwardTransform(data.data(), true);

        const int bins = fftSize / 2;
        const double sr = 44100.0; // the preview engine renders at this rate
        const int bars = 64;
        const float x0 = area.getX();
        const float y0 = area.getBottom();
        const float bh = area.getHeight();
        const float bw = area.getWidth() / (float)bars;

        juce::Path p;
        p.startNewSubPath(x0, y0);

        for (int b = 0; b < bars; ++b)
        {
            const double f0 = 40.0 * std::pow(20000.0 / 40.0, (double)b / (double)bars);
            const double f1 = 40.0 * std::pow(20000.0 / 40.0, (double)(b + 1) / (double)bars);
            int i0 = (int)std::floor(f0 / (sr * 0.5) * (double)bins);
            int i1 = (int)std::ceil(f1 / (sr * 0.5) * (double)bins);
            i0 = juce::jlimit(1, bins - 1, i0);
            i1 = juce::jlimit(i0 + 1, bins, i1);

            float mag = 0.0f;
            for (int i = i0; i < i1; ++i)
                mag = juce::jmax(mag, data[(size_t)i]);

            const float db = juce::Decibels::gainToDecibels(mag + 1e-9f);
            const float norm = juce::jlimit(0.0f, 1.0f, (db + 72.0f) / 72.0f);
            const float x = x0 + (float)b * bw;
            const float y = y0 - norm * bh;
            p.lineTo(x, y);
        }
        p.lineTo(x0 + area.getWidth(), y0);
        p.closeSubPath();

        g.setColour(kAccent.withAlpha(0.22f));
        g.fillPath(p);
        g.setColour(kAccent.withAlpha(0.65f));
        g.strokePath(p, juce::PathStrokeType(1.0f));
    }

    BytebeatAudioProcessor& proc_;
    juce::dsp::FFT fft_;
    std::array<float, 1024> preview_ {};
};

namespace
{

// ---------------------------------------------------------------------------
// Generic grid panel
// ---------------------------------------------------------------------------
class GridPanel : public juce::Component
{
public:
    explicit GridPanel(BytebeatAudioProcessor& p) : proc_(p) {}

    void addKnob(const char* id, const juce::String& name)
    {
        auto slider = std::make_unique<juce::Slider>();
        auto label = std::make_unique<juce::Label>();
        configureKnob(*slider);
        configureLabel(*label, name);
        addAndMakeVisible(*slider);
        addAndMakeVisible(*label);

        auto attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            proc_.apvts(), id, *slider);

        Control c;
        c.slider = std::move(slider);
        c.label = std::move(label);
        c.sliderAttach = std::move(attach);
        controls_.push_back(std::move(c));
    }

    void addToggle(const char* id, const juce::String& name)
    {
        auto toggle = std::make_unique<juce::ToggleButton>(name);
        toggle->setColour(juce::ToggleButton::textColourId, kText);
        toggle->setColour(juce::ToggleButton::tickColourId, kAccent);
        addAndMakeVisible(*toggle);

        auto attach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            proc_.apvts(), id, *toggle);

        Control c;
        c.toggle = std::move(toggle);
        c.buttonAttach = std::move(attach);
        controls_.push_back(std::move(c));
    }

    void addCombo(const char* id, const juce::String& name, const juce::StringArray& items)
    {
        auto label = std::make_unique<juce::Label>();
        auto combo = std::make_unique<juce::ComboBox>();
        configureLabel(*label, name);
        combo->setColour(juce::ComboBox::backgroundColourId, kPanel2);
        combo->setColour(juce::ComboBox::textColourId, kText);
        combo->setColour(juce::ComboBox::outlineColourId, kOutline);
        combo->setColour(juce::ComboBox::arrowColourId, kMuted);
        combo->addItemList(items, 1);
        addAndMakeVisible(*combo);
        addAndMakeVisible(*label);

        auto attach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            proc_.apvts(), id, *combo);

        Control c;
        c.combo = std::move(combo);
        c.label = std::move(label);
        c.comboAttach = std::move(attach);
        controls_.push_back(std::move(c));
    }

    void resized() override
    {
        const int cols = 4;
        const int rows = juce::jmax(1, (int)((controls_.size() + (size_t)cols - 1) / (size_t)cols));
        const int cw = getWidth() / cols;
        const int ch = getHeight() / rows;

        for (size_t i = 0; i < controls_.size(); ++i)
        {
            const int col = (int)(i % (size_t)cols);
            const int row = (int)(i / (size_t)cols);
            juce::Rectangle<int> cell(col * cw, row * ch, cw, ch);

            auto& c = controls_[i];
            if (c.slider != nullptr)
            {
                if (c.label != nullptr)
                {
                    c.label->setBounds(cell.removeFromTop(18).reduced(4, 0));
                }
                c.slider->setBounds(cell.reduced(6, 1));
            }
            else if (c.toggle != nullptr)
            {
                c.toggle->setBounds(cell.reduced(10, 8));
            }
            else if (c.combo != nullptr)
            {
                if (c.label != nullptr)
                    c.label->setBounds(cell.removeFromTop(18).reduced(4, 0));
                c.combo->setBounds(cell.removeFromTop(30).reduced(8, 0));
            }
        }
    }

private:
    struct Control
    {
        // Components must outlive their attachments (attachments are
        // declared last so they are destroyed first).
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttach;
    };

    BytebeatAudioProcessor& proc_;
    std::vector<Control> controls_;
};

// ---------------------------------------------------------------------------
// Library list
// ---------------------------------------------------------------------------
class LibraryListModel : public juce::ListBoxModel
{
public:
    LibraryListModel(BytebeatAudioProcessorEditor& owner,
                     const std::vector<bbvst::LibraryEntry>& entries)
        : owner_(owner), entries_(entries) {}

    int getNumRows() override { return (int)entries_.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height,
                          bool selected) override
    {
        if (row < 0 || row >= (int)entries_.size()) return;
        const auto& e = entries_[(size_t)row];

        if (selected)
        {
            g.setColour(kAccent.withAlpha(0.20f));
            g.fillRect(0, 0, width, height);
        }

        const auto catColour = e.user ? juce::Colour(0xff63d29b) : kAccent;

        g.setColour(catColour.withAlpha(0.9f));
        g.setFont(10.0f);
        g.drawText(e.user ? "USER" : e.category.toUpperCase(),
                   8, 4, width - 16, 12, juce::Justification::topLeft);

        g.setColour(selected ? juce::Colours::white : kText);
        g.setFont(13.0f);
        g.drawText(e.name, 8, 15, width - 16, height - 18, juce::Justification::topLeft);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
    {
        owner_.loadLibraryRow(row);
    }

    void returnKeyPressed(int row) override { owner_.loadLibraryRow(row); }

private:
    BytebeatAudioProcessorEditor& owner_;
    const std::vector<bbvst::LibraryEntry>& entries_;
};

juce::LookAndFeel_V4::ColourScheme makeColourScheme()
{
    return juce::LookAndFeel_V4::ColourScheme(
        juce::Colour(0xff0e0e12), juce::Colour(0xff16161c), juce::Colour(0xff1c1c24),
        juce::Colour(0xff2c2c38), juce::Colour(0xffd8d8e0), juce::Colour(0xff9b63ff),
        juce::Colour(0xffffffff), juce::Colour(0xff9b63ff), juce::Colour(0xffffffff));
}

} // namespace

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------
BytebeatAudioProcessorEditor::BytebeatAudioProcessorEditor(BytebeatAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setOpaque(true);
    setResizable(true, true);
    setResizeLimits(980, 640, 1600, 1100);

    lnf_ = std::make_unique<juce::LookAndFeel_V4>(makeColourScheme());
    setLookAndFeel(lnf_.get());

    nameEditor_.setTextToShowWhenEmpty("Formula name", kMuted);
    nameEditor_.setColour(juce::TextEditor::backgroundColourId, kPanel2);
    nameEditor_.setColour(juce::TextEditor::textColourId, kText);
    nameEditor_.setColour(juce::TextEditor::outlineColourId, kOutline);
    nameEditor_.setColour(juce::TextEditor::focusedOutlineColourId, kAccent);
    addAndMakeVisible(nameEditor_);

    applyButton_.setColour(juce::TextButton::buttonColourId, kAccent.withAlpha(0.25f));
    applyButton_.setColour(juce::TextButton::textColourOffId, kText);
    applyButton_.onClick = [this] { applyCurrentFormula(); };
    addAndMakeVisible(applyButton_);

    autoApply_.setColour(juce::ToggleButton::textColourId, kText);
    autoApply_.setColour(juce::ToggleButton::tickColourId, kAccent);
    addAndMakeVisible(autoApply_);

    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setColour(juce::Label::textColourId, kMuted);
    addAndMakeVisible(statusLabel_);

    opsLabel_.setJustificationType(juce::Justification::centredRight);
    opsLabel_.setColour(juce::Label::textColourId, kMuted);
    addAndMakeVisible(opsLabel_);

    document_.replaceAllContent(proc.getFormulaSource());
    nameEditor_.setText(proc.getFormulaName(), juce::dontSendNotification);

    codeEditor_ = std::make_unique<juce::CodeEditorComponent>(document_, &tokeniser_);
    codeEditor_->setLineNumbersShown(true);
    codeEditor_->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(),
                                    15.0f, juce::Font::plain));
    codeEditor_->setColour(juce::CodeEditorComponent::backgroundColourId,
                           juce::Colour(0xff121218));
    codeEditor_->setColour(juce::CodeEditorComponent::defaultTextColourId, kText);
    codeEditor_->setColour(juce::CodeEditorComponent::lineNumberBackgroundId,
                           juce::Colour(0xff16161c));
    codeEditor_->setColour(juce::CodeEditorComponent::lineNumberTextId, kMuted);
    codeEditor_->setColour(juce::CodeEditorComponent::highlightColourId,
                           kAccent.withAlpha(0.25f));
    codeEditor_->addKeyListener(this);
    addAndMakeVisible(*codeEditor_);

    document_.addListener(this);

    libraryEntries_ = library_.entries();

    auto refreshModel = [this]
    {
        libraryEntries_ = library_.entries();
        libraryList_.updateContent();
    };

    libraryModel_ = std::make_unique<LibraryListModel>(*this, libraryEntries_);
    libraryList_.setModel(libraryModel_.get());
    libraryList_.setRowHeight(38);
    libraryList_.setColour(juce::ListBox::backgroundColourId, kPanel);
    libraryList_.setColour(juce::ListBox::outlineColourId, kOutline);
    libraryList_.setOutlineThickness(1);
    addAndMakeVisible(libraryList_);

    loadButton_.onClick = [this] { loadSelectedLibraryEntry(); };
    saveButton_.onClick = [this] { saveFormulaAs(); };
    folderButton_.onClick = [this] { openFormulaFolder(); };
    refreshButton_.onClick = [this, refreshModel] { library_.rescanUserFiles(); refreshModel(); };
    newButton_.onClick = [this] { newFormula(); };

    for (auto* b : { &loadButton_, &saveButton_, &folderButton_, &refreshButton_, &newButton_ })
    {
        b->setColour(juce::TextButton::buttonColourId, kPanel2);
        b->setColour(juce::TextButton::textColourOffId, kText);
        addAndMakeVisible(*b);
    }

    scope_ = std::make_unique<ScopeComponent>(proc);
    addAndMakeVisible(*scope_);

    auto* macros = new GridPanel(proc);
    macros->addKnob(bbvst::pid::macro(0), "A");
    macros->addKnob(bbvst::pid::macro(1), "B");
    macros->addKnob(bbvst::pid::macro(2), "C");
    macros->addKnob(bbvst::pid::macro(3), "D");
    macros->addKnob(bbvst::pid::macro(4), "E");
    macros->addKnob(bbvst::pid::macro(5), "F");
    macros->addKnob(bbvst::pid::macro(6), "G");
    macros->addKnob(bbvst::pid::macro(7), "H");
    macros->addKnob(bbvst::pid::tone, "Tone");
    macros->addKnob(bbvst::pid::drive, "Drive");
    macros->addKnob(bbvst::pid::width, "Width");
    macros->addKnob(bbvst::pid::pan, "Pan");
    macros->addKnob(bbvst::pid::volume, "Volume");
    macros->addToggle(bbvst::pid::dcBlock, "DC Block");
    macrosPanel_.reset(macros);

    auto* play = new GridPanel(proc);
    play->addCombo(bbvst::pid::mode, "Mode", { "Bytebeat", "Signed", "Floatbeat" });
    play->addCombo(bbvst::pid::clock, "Clock",
                   { "4000", "8000", "11025", "16000", "22050", "32000", "44100", "48000" });
    play->addCombo(bbvst::pid::playMode, "Play Mode", { "Free", "Sync", "Trigger" });
    play->addToggle(bbvst::pid::run, "Run");
    play->addKnob(bbvst::pid::speed, "Speed");
    play->addKnob(bbvst::pid::smooth, "Smooth");
    play->addKnob(bbvst::pid::voices, "Voices");
    play->addKnob(bbvst::pid::root, "Root");
    play->addToggle(bbvst::pid::pitch, "Pitch Track");
    play->addKnob(bbvst::pid::attack, "Attack");
    play->addKnob(bbvst::pid::release, "Release");
    play->addToggle(bbvst::pid::resetNote, "Reset on Note");
    playPanel_.reset(play);

    tabs_ = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    tabs_->setTabBarDepth(26);
    tabs_->setOutline(0);
    tabs_->addTab("MACROS", kPanel2, macrosPanel_.get(), false);
    tabs_->addTab("PLAY", kPanel2, playPanel_.get(), false);
    tabs_->setColour(juce::TabbedComponent::backgroundColourId, kBackground);
    tabs_->setColour(juce::TabbedComponent::outlineColourId, kOutline);
    addAndMakeVisible(*tabs_);

    setStatus("Ready", false);
    lastRevision_ = proc.getFormulaRevision();
    setSize(1120, 720);
    startTimerHz(6);
}

BytebeatAudioProcessorEditor::~BytebeatAudioProcessorEditor()
{
    stopTimer();
    document_.removeListener(this);
    if (codeEditor_ != nullptr)
        codeEditor_->removeKeyListener(this);
    setLookAndFeel(nullptr);
}

void BytebeatAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);

    auto header = getLocalBounds().removeFromTop(52);
    g.setColour(kPanel);
    g.fillRect(header);
    g.setColour(kOutline);
    g.drawHorizontalLine(51, 0.0f, (float)getWidth());

    g.setColour(kAccent);
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText("BYTEBEAT MACHINE", 14, 12, 195, 28, juce::Justification::centredLeft);

    g.setColour(kOutline);
    g.drawVerticalLine(212, 60.0f, (float)getHeight() - 12.0f);
    g.drawVerticalLine(656, 60.0f, (float)getHeight() - 12.0f);
}

void BytebeatAudioProcessorEditor::resized()
{
    if (codeEditor_ == nullptr || scope_ == nullptr || tabs_ == nullptr)
        return;

    auto r = getLocalBounds();

    auto header = r.removeFromTop(52);

    nameEditor_.setBounds(header.getX() + 212, 11, 286, 30);
    applyButton_.setBounds(header.getX() + 506, 11, 72, 30);
    autoApply_.setBounds(header.getX() + 584, 11, 62, 30);
    statusLabel_.setBounds(header.getX() + 660, 12, 300, 28);
    opsLabel_.setBounds(header.getRight() - 120, 12, 108, 28);

    r.reduce(12, 8);
    auto left = r.removeFromLeft(190);
    r.removeFromLeft(12);
    auto centre = r.removeFromLeft(430);
    r.removeFromLeft(12);
    auto right = r;

    libraryList_.setBounds(left.removeFromTop(500));
    left.removeFromTop(8);
    loadButton_.setBounds(left.removeFromTop(26));
    left.removeFromTop(4);
    saveButton_.setBounds(left.removeFromTop(26));
    left.removeFromTop(4);
    folderButton_.setBounds(left.removeFromTop(26));
    left.removeFromTop(4);
    auto row = left.removeFromTop(26);
    refreshButton_.setBounds(row.removeFromLeft(90));
    row.removeFromLeft(8);
    newButton_.setBounds(row);

    codeEditor_->setBounds(centre);

    scope_->setBounds(right.removeFromTop(184));
    right.removeFromTop(10);
    tabs_->setBounds(right);
}

void BytebeatAudioProcessorEditor::codeDocumentTextInserted(const juce::String&, int)
{
    if (suppressEdits_) return;
    dirty_ = true;
    lastEditMs_ = juce::Time::getMillisecondCounter();
    setStatus("Modified - press Apply (Ctrl+Enter)", false);
}

void BytebeatAudioProcessorEditor::codeDocumentTextDeleted(int, int)
{
    if (suppressEdits_) return;
    dirty_ = true;
    lastEditMs_ = juce::Time::getMillisecondCounter();
    setStatus("Modified - press Apply (Ctrl+Enter)", false);
}

bool BytebeatAudioProcessorEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    const auto mods = key.getModifiers();
    if ((mods.isCommandDown() || mods.isCtrlDown())
        && key.getKeyCode() == juce::KeyPress::returnKey)
    {
        applyCurrentFormula();
        return true;
    }
    if ((mods.isCommandDown() || mods.isCtrlDown())
        && (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S'))
    {
        saveFormulaAs();
        return true;
    }
    return false;
}

void BytebeatAudioProcessorEditor::timerCallback()
{
    if (proc.getFormulaRevision() != lastRevision_ && !dirty_)
        loadFromProcessor(true);

    if (autoApply_.getToggleState() && dirty_
        && juce::Time::getMillisecondCounter() - lastEditMs_ > 650)
    {
        applyCurrentFormula();
    }

    opsLabel_.setText("ops " + juce::String(proc.getActiveProgramOps()),
                      juce::dontSendNotification);
}

void BytebeatAudioProcessorEditor::applyCurrentFormula()
{
    const auto source = document_.getAllContent();
    const auto name = nameEditor_.getText().trim();
    juce::String error;

    if (proc.applyFormula(source, name, error))
    {
        dirty_ = false;
        lastRevision_ = proc.getFormulaRevision();
        setStatus("Applied: " + juce::String(proc.getActiveProgramOps()) + " ops", false);
    }
    else
    {
        setStatus(error, true);
        updateErrorMarker();
    }
}

void BytebeatAudioProcessorEditor::loadLibraryRow(int row)
{
    if (row < 0 || row >= (int)libraryEntries_.size()) return;
    const auto entry = libraryEntries_[(size_t)row];

    suppressEdits_ = true;
    document_.replaceAllContent(entry.source);
    nameEditor_.setText(entry.name, juce::dontSendNotification);
    suppressEdits_ = false;

    juce::String error;
    if (proc.loadLibraryEntry(entry, error))
    {
        dirty_ = false;
        lastRevision_ = proc.getFormulaRevision();
        setStatus("Loaded " + entry.name, false);
    }
    else
    {
        setStatus(error, true);
        updateErrorMarker();
    }
}

void BytebeatAudioProcessorEditor::loadSelectedLibraryEntry()
{
    loadLibraryRow(libraryList_.getSelectedRow());
}

void BytebeatAudioProcessorEditor::refreshLibrary()
{
    library_.rescanUserFiles();
    libraryEntries_ = library_.entries();
    libraryList_.updateContent();
}

void BytebeatAudioProcessorEditor::saveFormulaAs()
{
    juce::String name = nameEditor_.getText().trim();
    if (name.isEmpty()) name = "formula";

    const auto folder = bbvst::FormulaLibraryModel::userFolder();
    folder.createDirectory();
    const auto target = folder.getChildFile(juce::File::createLegalFileName(name) + ".bb");

    const int mode = (int)std::lround(
        proc.apvts().getRawParameterValue(bbvst::pid::mode)->load());
    const int clockIndex = (int)std::lround(
        proc.apvts().getRawParameterValue(bbvst::pid::clock)->load());
    const int rate = bbvst::kClockRates[juce::jlimit(
        0, bbvst::kNumClockRates - 1, clockIndex)];

    juce::String error;
    if (bbvst::FormulaLibraryModel::saveEntry(target, document_.getAllContent(),
                                              nameEditor_.getText(), mode, rate, error))
    {
        refreshLibrary();
        setStatus("Saved " + target.getFileName(), false);
        statusLabel_.setTooltip(target.getFullPathName());
    }
    else
    {
        setStatus(error, true);
    }
}

void BytebeatAudioProcessorEditor::openFormulaFolder()
{
    const auto folder = bbvst::FormulaLibraryModel::userFolder();
    folder.createDirectory();
    folder.revealToUser();
    setStatus("Formula folder opened", false);
    statusLabel_.setTooltip(folder.getFullPathName());
}

void BytebeatAudioProcessorEditor::newFormula()
{
    const juce::String starter =
        "// name: New Formula\n"
        "// mode: bytebeat\n"
        "// rate: 8000\n"
        "\n"
        "t*(t>>9&31)>>3\n";

    suppressEdits_ = true;
    document_.replaceAllContent(starter);
    nameEditor_.setText("New Formula", juce::dontSendNotification);
    suppressEdits_ = false;

    juce::String error;
    if (proc.applyFormula(starter, "New Formula", error))
    {
        dirty_ = false;
        lastRevision_ = proc.getFormulaRevision();
        setStatus("New formula", false);
    }
    else
    {
        setStatus(error, true);
        updateErrorMarker();
    }
}

void BytebeatAudioProcessorEditor::loadFromProcessor(bool force)
{
    const int rev = proc.getFormulaRevision();
    if (!force && rev == lastRevision_) return;

    suppressEdits_ = true;
    document_.replaceAllContent(proc.getFormulaSource());
    nameEditor_.setText(proc.getFormulaName(), juce::dontSendNotification);
    suppressEdits_ = false;

    lastRevision_ = rev;
    dirty_ = false;
    setStatus("Loaded from host", false);
}

void BytebeatAudioProcessorEditor::setStatus(const juce::String& text, bool error)
{
    statusLabel_.setText(text, juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId, error ? kError : kMuted);
    statusLabel_.setTooltip(text);
}

void BytebeatAudioProcessorEditor::updateErrorMarker()
{
    const int line = proc.getCompileErrorLine();
    const int col = proc.getCompileErrorCol();
    if (line <= 0 || line > document_.getNumLines()) return;

    const juce::CodeDocument::Position pos(document_, line - 1, juce::jmax(0, col - 1));
    codeEditor_->moveCaretTo(pos, false);
    codeEditor_->scrollToKeepCaretOnScreen();
}
