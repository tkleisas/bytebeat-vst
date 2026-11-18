// PluginProcessor.cpp - Bytebeat Machine VST3 processor.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

BytebeatAudioProcessor::BytebeatAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "BytebeatState", bbvst::createLayout())
{
    for (auto& s : scope_)
        s.store(0.0f, std::memory_order_relaxed);

    buildFactoryPresets();
    loadDefaultFormula();
    currentProgram_.store(-1, std::memory_order_relaxed);

    previewEngine_.prepare(44100.0);
    previewPrepared_ = true;
}

BytebeatAudioProcessor::~BytebeatAudioProcessor() = default;

bool BytebeatAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (!layouts.getMainInputChannelSet().isDisabled()) return false;
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void BytebeatAudioProcessor::prepareToPlay(double sampleRate, int)
{
    engine_.prepare(sampleRate);
    for (auto& s : scope_)
        s.store(0.0f, std::memory_order_relaxed);
    scopeWrite_.store(0, std::memory_order_relaxed);
    outputPeak_.store(0.0f, std::memory_order_relaxed);
}

void BytebeatAudioProcessor::releaseResources() {}

void BytebeatAudioProcessor::renderPreview(float* mono, int n)
{
    if (mono == nullptr || n <= 0) return;
    if (!previewPrepared_)
    {
        previewEngine_.prepare(44100.0);
        previewPrepared_ = true;
    }

    auto s = makeSettings(44100.0);
    if (engineMode_.load(std::memory_order_acquire) == kEngineJs)
    {
        s.program = nullptr;
        s.evaluator = jsPreview_.get();
    }
    else
    {
        s.program = activeProgram_.load(std::memory_order_acquire);
        s.evaluator = nullptr;
    }
    s.playMode = bytebeat::kPlayFree;
    s.run = true;
    s.playing = true;
    s.hostRate = 44100.0;
    s.transportSamples = 0.0;
    s.tempo = 120.0;

    previewEngine_.setSettings(s);
    previewEngine_.render(mono, nullptr, n);

    previewT_.store(previewEngine_.primaryT(), std::memory_order_relaxed);
    previewClock_.store((int)std::lround(s.clockRate), std::memory_order_relaxed);
}

void BytebeatAudioProcessor::reset()
{
    engine_.reset();
}

float BytebeatAudioProcessor::plain(const juce::AudioProcessorValueTreeState& apvts,
                                    const char* id)
{
    if (auto* v = apvts.getRawParameterValue(id))
        return v->load(std::memory_order_relaxed);
    return 0.0f;
}

void BytebeatAudioProcessor::setParam(const char* id, float plainValue)
{
    if (auto* p = apvts_.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(plainValue));
}

bytebeat::Settings BytebeatAudioProcessor::makeSettings(double sampleRate) const
{
    bytebeat::Settings s;

    if (engineMode_.load(std::memory_order_acquire) == kEngineJs)
    {
        s.program = nullptr;
        s.evaluator = jsAudioActive_.load(std::memory_order_acquire);
    }
    else
    {
        s.program = activeProgram_.load(std::memory_order_acquire);
        s.evaluator = nullptr;
    }

    s.outputMode = juce::jlimit(0, 2, (int)std::lround(plain(apvts_, bbvst::pid::mode)));

    const int clockIndex = juce::jlimit(0, bbvst::kNumClockRates - 1,
                                        (int)std::lround(plain(apvts_, bbvst::pid::clock)));
    s.clockRate = (double)bbvst::kClockRates[clockIndex];
    s.speed = (double)plain(apvts_, bbvst::pid::speed);
    s.smooth = plain(apvts_, bbvst::pid::smooth) * 0.01f;

    s.playMode = juce::jlimit(0, 2, (int)std::lround(plain(apvts_, bbvst::pid::playMode)));
    s.run = plain(apvts_, bbvst::pid::run) > 0.5f;
    s.voices = (int)std::lround(plain(apvts_, bbvst::pid::voices));
    s.rootNote = (int)std::lround(plain(apvts_, bbvst::pid::root));
    s.pitchTrack = plain(apvts_, bbvst::pid::pitch) > 0.5f;
    s.attackMs = plain(apvts_, bbvst::pid::attack);
    s.releaseMs = plain(apvts_, bbvst::pid::release);
    s.resetOnNote = plain(apvts_, bbvst::pid::resetNote) > 0.5f;

    for (int i = 0; i < bbvst::kNumMacros; ++i)
        s.macro[i] = plain(apvts_, bbvst::pid::macro(i)) * 0.01f;

    s.hostRate = sampleRate;
    s.volume = juce::Decibels::decibelsToGain(plain(apvts_, bbvst::pid::volume));

    const float tone = juce::jlimit(0.0f, 1.0f, plain(apvts_, bbvst::pid::tone) * 0.01f);
    s.toneHz = tone >= 0.995f ? 24000.0f : (float)(200.0 * std::pow(100.0, (double)tone));
    s.drive = plain(apvts_, bbvst::pid::drive) * 0.01f;
    s.dcBlock = plain(apvts_, bbvst::pid::dcBlock) > 0.5f;
    s.width = plain(apvts_, bbvst::pid::width) * 0.01f;
    s.pan = plain(apvts_, bbvst::pid::pan) * 0.01f;

    return s;
}

void BytebeatAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (n <= 0 || channels <= 0) return;

    auto s = makeSettings(getSampleRate());

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            s.playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm())
                s.tempo = *bpm > 1.0 ? *bpm : 120.0;
            if (auto ts = pos->getTimeSignature())
                s.beatsPerBar = ts->numerator > 0 ? (double)ts->numerator : 4.0;

            if (auto t = pos->getTimeInSamples())
                s.transportSamples = (double)*t;
            else if (auto ppq = pos->getPpqPosition())
                s.transportSamples = *ppq * 60.0 / s.tempo * getSampleRate();
        }
    }

    engine_.setSettings(s);

    float* left = buffer.getWritePointer(0);
    float* right = channels > 1 ? buffer.getWritePointer(1) : nullptr;

    auto render = [&](int start, int len)
    {
        if (len > 0)
            engine_.render(left + start, right != nullptr ? right + start : nullptr, len);
    };

    int pos = 0;
    for (const auto meta : midiMessages)
    {
        const auto msg = meta.getMessage();
        const int off = juce::jlimit(0, n, (int)meta.samplePosition);
        if (off > pos)
        {
            render(pos, off - pos);
            pos = off;
        }

        if (msg.isNoteOn())
            engine_.noteOn(msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())
            engine_.noteOff(msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            engine_.allNotesOff();
    }
    if (pos < n)
        render(pos, n - pos);

    updateScope(left, right != nullptr ? right : left, n,
                (int)std::lround(s.clockRate));
}

void BytebeatAudioProcessor::updateScope(const float* left, const float* right, int n, int clockRate)
{
    if (left == nullptr || n <= 0) return;

    const int step = juce::jmax(1, n / 512);
    int w = scopeWrite_.load(std::memory_order_relaxed);
    float peak = 0.0f;

    for (int i = 0; i < n; i += step)
    {
        const float v = 0.5f * (left[i] + right[i]);
        scope_[(size_t)w].store(v, std::memory_order_relaxed);
        w = (w + 1) % kScopeSize;
        peak = juce::jmax(peak, std::abs(v));
    }

    scopeWrite_.store(w, std::memory_order_relaxed);
    outputPeak_.store(peak, std::memory_order_relaxed);
    displayT_.store(engine_.primaryT(), std::memory_order_relaxed);
    displayClock_.store(clockRate, std::memory_order_relaxed);
}

bool BytebeatAudioProcessor::applyFormula(const juce::String& source,
                                          const juce::String& name,
                                          juce::String& errorOut)
{
    return applyFormulaAs(source, name, engineMode_.load(std::memory_order_acquire),
                          errorOut, true);
}

void BytebeatAudioProcessor::commitFormulaLocked(const juce::String& source,
                                                 const juce::String& name)
{
    formulaSource_ = source;
    formulaName_ = name.trim().isEmpty() ? juce::String("User Formula") : name.trim();
    compileError_.clear();
    compileErrorLine_ = 0;
    compileErrorCol_ = 0;
    formulaRevision_.fetch_add(1, std::memory_order_relaxed);
}

bool BytebeatAudioProcessor::compileJs(const juce::String& source, int jsMode,
                                       std::unique_ptr<bbvst::JsEvaluator>& audio,
                                       std::unique_ptr<bbvst::JsEvaluator>& preview,
                                       std::string& errorOut) const
{
    std::string error;
    auto a = bbvst::JsEvaluator::create(source.toStdString(), jsMode, 8, error);
    if (!a)
    {
        errorOut = error;
        return false;
    }
    auto p = bbvst::JsEvaluator::create(source.toStdString(), jsMode, 1, error);
    if (!p)
    {
        errorOut = error;
        return false;
    }
    audio = std::move(a);
    preview = std::move(p);
    return true;
}

bool BytebeatAudioProcessor::applyFormulaAs(const juce::String& source,
                                            const juce::String& name,
                                            int targetEngine,
                                            juce::String& errorOut,
                                            bool commit)
{
    if (source.trim().isEmpty())
    {
        errorOut = "The formula is empty.";
        return false;
    }

    if (targetEngine == kEngineJs)
    {
        std::unique_ptr<bbvst::JsEvaluator> audio, preview;
        std::string jsError;
        if (!compileJs(source, jsMode_.load(std::memory_order_acquire), audio, preview, jsError))
        {
            std::lock_guard<std::mutex> lock(formulaMutex_);
            compileErrorLine_ = 0;
            compileErrorCol_ = 0;
            compileError_ = "JS: " + juce::String(jsError);
            errorOut = compileError_;
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(formulaMutex_);
            if (jsAudio_)
            {
                jsHistory_.push_back(std::move(jsAudio_));
                if (jsHistory_.size() > 4)
                    jsHistory_.erase(jsHistory_.begin());
            }
            jsAudio_ = std::move(audio);
            jsPreview_ = std::move(preview);

            if (commit)
                commitFormulaLocked(source, name);
            else
            {
                compileError_.clear();
                compileErrorLine_ = 0;
                compileErrorCol_ = 0;
            }
        }

        activeProgramOps_.store(0, std::memory_order_relaxed);
        jsAudioActive_.store(jsAudio_.get(), std::memory_order_release);
        engineMode_.store(kEngineJs, std::memory_order_release);
        if (commit)
            currentProgram_.store(-1, std::memory_order_relaxed);
        clearJsRuntimeError();
        errorOut.clear();
        return true;
    }

    bytebeat::FormulaMeta meta;
    auto result = bytebeat::compile(source.toStdString(), &meta);
    if (!result.ok)
    {
        std::lock_guard<std::mutex> lock(formulaMutex_);
        compileErrorLine_ = result.error.line;
        compileErrorCol_ = result.error.col;
        compileError_ = juce::String(result.error.message)
                      + " (line " + juce::String(result.error.line)
                      + ", col " + juce::String(result.error.col) + ")";
        errorOut = compileError_;
        return false;
    }

    auto* raw = result.program.get();
    const int ops = result.program->codeSize;
    {
        std::lock_guard<std::mutex> lock(formulaMutex_);
        programs_.push_back(std::move(result.program));
        if (commit)
            commitFormulaLocked(source, name);
        else
        {
            compileError_.clear();
            compileErrorLine_ = 0;
            compileErrorCol_ = 0;
        }
    }

    activeProgramOps_.store(ops, std::memory_order_relaxed);
    activeProgram_.store(raw, std::memory_order_release);
    engineMode_.store(kEngineNative, std::memory_order_release);
    if (commit)
        currentProgram_.store(-1, std::memory_order_relaxed);
    errorOut.clear();
    return true;
}

bool BytebeatAudioProcessor::setEngineMode(int mode, juce::String& errorOut)
{
    if (mode != kEngineNative && mode != kEngineJs)
    {
        errorOut = "Unknown engine.";
        return false;
    }
    if (mode == getEngineMode())
        return true;

    const auto source = getFormulaSource();
    if (source.trim().isEmpty())
    {
        errorOut = "No formula loaded.";
        return false;
    }
    return applyFormulaAs(source, getFormulaName(), mode, errorOut, false);
}

bool BytebeatAudioProcessor::setJsMode(int mode, juce::String& errorOut)
{
    if (mode < 0 || mode > 2)
    {
        errorOut = "Unknown JavaScript mode.";
        return false;
    }

    // This is a compile setting, not a live engine switch. The editor calls
    // applyFormula() immediately afterwards so the current text is compiled
    // with the new mode (and can be reverted if it fails).
    jsMode_.store(mode, std::memory_order_release);
    errorOut.clear();
    return true;
}

bool BytebeatAudioProcessor::getJsRuntimeError() const
{
    if (auto* audio = jsAudioActive_.load(std::memory_order_acquire))
        if (audio->runtimeError())
            return true;
    return jsPreview_ != nullptr && jsPreview_->runtimeError();
}

void BytebeatAudioProcessor::clearJsRuntimeError()
{
    if (auto* audio = jsAudioActive_.load(std::memory_order_acquire))
        audio->clearRuntimeError();
    if (jsPreview_)
        jsPreview_->clearRuntimeError();
}

bool BytebeatAudioProcessor::loadLibraryEntry(const bbvst::LibraryEntry& entry,
                                              juce::String& errorOut)
{
    setParam(bbvst::pid::mode, (float)juce::jlimit(0, 2, entry.mode));
    setParam(bbvst::pid::clock, (float)bbvst::clockIndexForRate(entry.rate));
    return applyFormula(entry.source, entry.name, errorOut);
}

juce::String BytebeatAudioProcessor::getFormulaSource() const
{
    std::lock_guard<std::mutex> lock(formulaMutex_);
    return formulaSource_;
}

juce::String BytebeatAudioProcessor::getFormulaName() const
{
    std::lock_guard<std::mutex> lock(formulaMutex_);
    return formulaName_;
}

juce::String BytebeatAudioProcessor::getCompileError() const
{
    std::lock_guard<std::mutex> lock(formulaMutex_);
    return compileError_;
}

int BytebeatAudioProcessor::getCompileErrorLine() const
{
    std::lock_guard<std::mutex> lock(formulaMutex_);
    return compileErrorLine_;
}

int BytebeatAudioProcessor::getCompileErrorCol() const
{
    std::lock_guard<std::mutex> lock(formulaMutex_);
    return compileErrorCol_;
}

void BytebeatAudioProcessor::loadDefaultFormula()
{
    const auto& bank = bytebeat::builtinFormulas();
    if (bank.empty()) return;
    juce::String err;
    applyFormula(juce::String(bank[0].source), juce::String(bank[0].name), err);
}

void BytebeatAudioProcessor::buildFactoryPresets()
{
    presets_.clear();
    for (const auto& f : bytebeat::builtinFormulas())
    {
        FactoryPreset p;
        p.name = f.name;
        p.source = f.source;
        p.mode = f.mode;
        p.rate = f.rate;
        p.params = {
            { bbvst::pid::volume, -10.0f },
            { bbvst::pid::tone, 90.0f },
            { bbvst::pid::playMode, 2.0f },
            { bbvst::pid::voices, 4.0f },
            { bbvst::pid::macro(0), 50.0f },
            { bbvst::pid::macro(1), 50.0f },
            { bbvst::pid::macro(2), 50.0f },
            { bbvst::pid::macro(3), 50.0f },
        };
        presets_.push_back(std::move(p));
    }
}

int BytebeatAudioProcessor::getNumPrograms()
{
    return (int)presets_.size();
}

int BytebeatAudioProcessor::getCurrentProgram()
{
    return currentProgram_.load(std::memory_order_relaxed);
}

void BytebeatAudioProcessor::setCurrentProgram(int index)
{
    if (index < 0 || index >= (int)presets_.size()) return;
    const auto& p = presets_[(size_t)index];

    setParam(bbvst::pid::mode, (float)p.mode);
    setParam(bbvst::pid::clock, (float)bbvst::clockIndexForRate(p.rate));
    for (const auto& kv : p.params)
        setParam(kv.first.toRawUTF8(), kv.second);

    juce::String err;
    if (applyFormula(p.source, p.name, err))
    {
        currentProgram_.store(index, std::memory_order_relaxed);
    }
    else if (getEngineMode() == kEngineJs)
    {
        // Factory presets are native-dialect formulas. If one is not valid in
        // the selected JS mode, load it with the native engine rather than
        // leaving the program change silently ignored.
        juce::String nativeError;
        if (applyFormulaAs(p.source, p.name, kEngineNative, nativeError, true))
            currentProgram_.store(index, std::memory_order_relaxed);
    }
}

const juce::String BytebeatAudioProcessor::getProgramName(int index)
{
    if (index >= 0 && index < (int)presets_.size())
        return presets_[(size_t)index].name;
    return {};
}

void BytebeatAudioProcessor::changeProgramName(int, const juce::String&) {}

void BytebeatAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement xml("BytebeatState");
    xml.setAttribute("version", 2);
    xml.setAttribute("program", currentProgram_.load(std::memory_order_relaxed));
    xml.setAttribute("engine", getEngineMode());
    xml.setAttribute("jsmode", getJsMode());

    if (auto state = apvts_.copyState().createXml())
        xml.addChildElement(state.release());

    juce::String name, source;
    {
        std::lock_guard<std::mutex> lock(formulaMutex_);
        name = formulaName_;
        source = formulaSource_;
    }

    auto* f = xml.createNewChildElement("Formula");
    f->setAttribute("name", name);
    f->addTextElement(source);

    const auto text = xml.toString();
    destData.replaceAll(text.toRawUTF8(), (size_t)text.getNumBytesAsUTF8());
}

void BytebeatAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0) return;

    auto xml = juce::XmlDocument::parse(
        juce::String::fromUTF8((const char*)data, sizeInBytes));
    if (xml == nullptr) return;

    juce::String source, name;
    forEachXmlChildElement(*xml, child)
    {
        if (child->hasTagName("Formula"))
        {
            name = child->getStringAttribute("name");
            source = child->getAllSubText();
        }
        else
        {
            auto tree = juce::ValueTree::fromXml(*child);
            if (tree.isValid())
                apvts_.replaceState(tree);
        }
    }

    const int savedEngine = xml->getIntAttribute("engine", kEngineNative);
    const int savedJsMode = juce::jlimit(0, 2, xml->getIntAttribute("jsmode", 0));
    jsMode_.store(savedJsMode, std::memory_order_release);

    if (source.isNotEmpty())
    {
        juce::String err;
        if (!applyFormulaAs(source, name,
                            savedEngine == kEngineJs ? kEngineJs : kEngineNative,
                            err, true))
        {
            if (savedEngine == kEngineJs)
            {
                // Open the project in Native mode rather than with no formula.
                juce::String nativeError;
                applyFormulaAs(source, name, kEngineNative, nativeError, true);
            }
        }
    }

    currentProgram_.store(xml->getIntAttribute("program", -1),
                          std::memory_order_relaxed);
}

juce::AudioProcessorEditor* BytebeatAudioProcessor::createEditor()
{
    return new BytebeatAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BytebeatAudioProcessor();
}
