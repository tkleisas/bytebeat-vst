// PluginProcessor.cpp - Bytebeat Drums VST3 processor.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

BytebeatDrumsAudioProcessor::BytebeatDrumsAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      bank_(),
      apvts_(*this, nullptr, "BytebeatDrums", bbdvst::createLayout(bank_.names()))
{
    cacheParamPointers();
    buildUserKits();
    previewEngine_.prepare(44100.0);
    loadKit(0);
}

BytebeatDrumsAudioProcessor::~BytebeatDrumsAudioProcessor() = default;

bool BytebeatDrumsAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (!layouts.getMainInputChannelSet().isDisabled()) return false;
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void BytebeatDrumsAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    const int block = std::max(samplesPerBlock, 4096);
    tempL_.assign((size_t)block, 0.0f);
    tempR_.assign((size_t)block, 0.0f);
    chokeStep_ = 1.0f / (0.005f * (float)sampleRate_);

    for (int p = 0; p < kNumPads; ++p)
    {
        pads_[p].engine.prepare(sampleRate_);
        pads_[p].lastNote = clampInt(plainInt(bbdvst::pid::noteBase) + p, 0, 127);
        pads_[p].chokeGain = 1.0f;
        pads_[p].choking = false;
        pads_[p].oneShot = false;
    }

    previewEngine_.prepare(44100.0);
    previewPad_ = -1;
}

void BytebeatDrumsAudioProcessor::releaseResources() {}

void BytebeatDrumsAudioProcessor::reset()
{
    for (auto& pad : pads_)
    {
        pad.engine.reset();
        pad.chokeGain = 1.0f;
        pad.choking = false;
        pad.oneShot = false;
    }
    previewEngine_.reset();
    previewPad_ = -1;
}

float BytebeatDrumsAudioProcessor::plain(const char* id) const
{
    if (auto* v = apvts_.getRawParameterValue(id))
        return v->load(std::memory_order_relaxed);
    return 0.0f;
}

int BytebeatDrumsAudioProcessor::plainInt(const char* id) const
{
    return (int)std::lround(plain(id));
}

void BytebeatDrumsAudioProcessor::cacheParamPointers()
{
    const auto ids = bbdvst::allParamIds();
    for (int i = 0; i < bbdvst::kNumParams; ++i)
        raw_[(size_t)i] = apvts_.getRawParameterValue(ids[(size_t)i]);
}

float BytebeatDrumsAudioProcessor::rawValue(int index) const
{
    if (index < 0 || index >= bbdvst::kNumParams) return 0.0f;
    return raw_[(size_t)index] != nullptr
               ? raw_[(size_t)index]->load(std::memory_order_relaxed)
               : 0.0f;
}

int BytebeatDrumsAudioProcessor::rawInt(int index) const
{
    return (int)std::lround(rawValue(index));
}

int BytebeatDrumsAudioProcessor::clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

float BytebeatDrumsAudioProcessor::softClip(float x)
{
    if (!std::isfinite(x)) return 0.0f;
    constexpr float knee = 0.8f;
    const float a = std::fabs(x);
    if (a <= knee) return x;
    const float y = knee + (1.0f - knee) * std::tanh((a - knee) / (1.0f - knee));
    return x < 0.0f ? -y : y;
}

bytebeat::Settings BytebeatDrumsAudioProcessor::makePadSettings(int pad, double sr) const
{
    bytebeat::Settings s;

    const int base = bbdvst::kNumMasterParams + pad * bbdvst::kPadFields;
    const int formulaIndex = rawInt(base + bbdvst::kFormula);
    const bbdvst::DrumEntry* entry = bank_.entry(formulaIndex);

    s.program = bank_.program(formulaIndex);
    s.evaluator = nullptr;
    s.outputMode = entry != nullptr ? entry->mode : bytebeat::kOutBytebeat;
    s.clockRate = entry != nullptr ? (double)entry->rate : 8000.0;

    const float tune = rawValue(base + bbdvst::kTune);
    s.speed = std::pow(2.0, (double)tune / 12.0);
    s.smooth = 0.0f;

    const int mode = rawInt(base + bbdvst::kMode);
    s.playMode = bytebeat::kPlayTrigger;
    s.run = true;
    s.voices = 1;
    s.rootNote = clampInt(rawInt(3) + pad, 0, 127);
    s.pitchTrack = false;
    s.attackMs = 0.5f;
    s.releaseMs = rawValue(base + bbdvst::kDecay);
    s.resetOnNote = true;
    s.oneShotSamples = mode == 0 ? std::max(1, (int)(0.002 * sr)) : 0;

    s.macro[0] = rawValue(base + bbdvst::kMacroA) * 0.01f;
    s.macro[1] = rawValue(base + bbdvst::kMacroB) * 0.01f;
    s.macro[2] = rawValue(base + bbdvst::kMacroC) * 0.01f;
    s.macro[3] = rawValue(base + bbdvst::kMacroD) * 0.01f;
    s.macro[4] = 0.5f;
    s.macro[5] = 0.5f;
    s.macro[6] = 0.5f;
    s.macro[7] = 0.5f;

    s.tempo = 120.0;
    s.playing = true;
    s.transportSamples = 0.0;
    s.beatsPerBar = 4.0;
    s.hostRate = sr;

    s.volume = rawValue(base + bbdvst::kLevel) * 0.01f;
    const float tone = std::clamp(rawValue(1) * 0.01f, 0.0f, 1.0f);
    s.toneHz = tone >= 0.995f ? 24000.0f : (float)(200.0 * std::pow(100.0, (double)tone));
    s.drive = rawValue(2) * 0.01f;
    s.dcBlock = true;
    s.width = 1.0f;
    s.pan = rawValue(base + bbdvst::kPan) * 0.01f;

    return s;
}

void BytebeatDrumsAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (n <= 0 || channels <= 0) return;
    if (n > (int)tempL_.size()) return;

    for (int p = 0; p < kNumPads; ++p)
        pads_[p].engine.setSettings(makePadSettings(p, sampleRate_));

    float* left = buffer.getWritePointer(0);
    float* right = channels > 1 ? buffer.getWritePointer(1) : nullptr;

    int pos = 0;
    for (const auto meta : midiMessages)
    {
        const auto msg = meta.getMessage();
        const int off = juce::jlimit(0, n, (int)meta.samplePosition);
        if (off > pos)
        {
            renderPads(left + pos, right != nullptr ? right + pos : nullptr, off - pos);
            pos = off;
        }

        if (msg.isNoteOn())
        {
            const int pad = msg.getNoteNumber() - rawInt(3);
            if (pad >= 0 && pad < kNumPads)
                triggerPad(pad, msg.getNoteNumber(), msg.getVelocity());
        }
        else if (msg.isNoteOff())
        {
            const int pad = msg.getNoteNumber() - rawInt(3);
            if (pad >= 0 && pad < kNumPads)
                releasePad(pad, msg.getNoteNumber());
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            allPadsOff();
        }
    }
    if (pos < n)
        renderPads(left + pos, right != nullptr ? right + pos : nullptr, n - pos);

    const float master = juce::Decibels::decibelsToGain(rawValue(0));
    for (int i = 0; i < n; ++i)
    {
        left[i] = softClip(left[i] * master);
        if (right != nullptr)
            right[i] = softClip(right[i] * master);
    }
}

void BytebeatDrumsAudioProcessor::triggerPad(int pad, int note, int velocity)
{
    const int group = rawInt(bbdvst::kNumMasterParams + pad * bbdvst::kPadFields + bbdvst::kChoke);
    if (group > 0)
    {
        for (int q = 0; q < kNumPads; ++q)
        {
            if (q == pad) continue;
            if (rawInt(bbdvst::kNumMasterParams + q * bbdvst::kPadFields + bbdvst::kChoke) != group) continue;
            if (!pads_[q].engine.hasActiveVoices()) continue;
            pads_[q].choking = true;
            pads_[q].chokeGain = 1.0f;
        }
    }

    pads_[pad].choking = false;
    pads_[pad].chokeGain = 1.0f;
    pads_[pad].oneShot = rawInt(bbdvst::kNumMasterParams + pad * bbdvst::kPadFields + bbdvst::kMode) == 0;
    pads_[pad].lastNote = note;
    pads_[pad].engine.noteOn(note, velocity);
}

void BytebeatDrumsAudioProcessor::releasePad(int pad, int note)
{
    if (pad < 0 || pad >= kNumPads) return;
    if (pads_[pad].oneShot) return;
    if (pads_[pad].lastNote == note)
        pads_[pad].engine.noteOff(note);
}

void BytebeatDrumsAudioProcessor::allPadsOff()
{
    for (auto& pad : pads_)
    {
        if (!pad.engine.hasActiveVoices()) continue;
        pad.choking = true;
        pad.chokeGain = 1.0f;
        pad.engine.allNotesOff();
    }
}

void BytebeatDrumsAudioProcessor::renderPads(float* left, float* right, int n)
{
    if (left == nullptr || n <= 0) return;

    for (auto& pad : pads_)
    {
        if (!pad.engine.hasActiveVoices() && !pad.choking)
            continue;

        pad.engine.render(tempL_.data(), tempR_.data(), n);

        float gain = pad.chokeGain;
        const float step = pad.choking ? chokeStep_ : 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float l = tempL_[(size_t)i] * gain;
            const float r = tempR_[(size_t)i] * gain;
            if (right != nullptr)
            {
                left[i] += l;
                right[i] += r;
            }
            else
            {
                left[i] += (l + r) * 0.5f;
            }

            if (step > 0.0f)
                gain = std::max(0.0f, gain - step);
        }

        pad.chokeGain = gain;
        if (pad.choking && gain <= 0.0f)
        {
            pad.engine.reset();
            pad.choking = false;
            pad.chokeGain = 1.0f;
        }
    }
}

std::array<float, BytebeatDrumsAudioProcessor::kNumParams>
BytebeatDrumsAudioProcessor::readParams() const
{
    std::array<float, kNumParams> out {};
    const auto ids = bbdvst::allParamIds();
    for (int i = 0; i < kNumParams; ++i)
        if (auto* v = apvts_.getRawParameterValue(ids[(size_t)i]))
            out[(size_t)i] = v->load(std::memory_order_relaxed);
    return out;
}

void BytebeatDrumsAudioProcessor::applyParams(const std::array<float, kNumParams>& values)
{
    const auto ids = bbdvst::allParamIds();
    for (int i = 0; i < kNumParams; ++i)
    {
        if (auto* p = apvts_.getParameter(ids[(size_t)i]))
            p->setValueNotifyingHost(p->convertTo0to1(values[(size_t)i]));
    }
}

std::array<float, BytebeatDrumsAudioProcessor::kNumParams>
BytebeatDrumsAudioProcessor::kitValues(const bbdrums::KitSpec& kit)
{
    std::array<float, kNumParams> v {};
    v[0] = kit.masterVolume;
    v[1] = kit.tone;
    v[2] = kit.drive;
    v[3] = 36.0f;

    for (int p = 0; p < kNumPads; ++p)
    {
        const auto& s = kit.pads[p];
        const int base = bbdvst::kNumMasterParams + p * bbdvst::kPadFields;
        v[(size_t)base + bbdvst::kFormula] = (float)s.formula;
        v[(size_t)base + bbdvst::kLevel] = s.level;
        v[(size_t)base + bbdvst::kPan] = s.pan;
        v[(size_t)base + bbdvst::kTune] = s.tune;
        v[(size_t)base + bbdvst::kDecay] = s.decay;
        v[(size_t)base + bbdvst::kChoke] = (float)s.choke;
        v[(size_t)base + bbdvst::kMode] = (float)s.mode;
        v[(size_t)base + bbdvst::kMacroA] = s.a;
        v[(size_t)base + bbdvst::kMacroB] = s.b;
        v[(size_t)base + bbdvst::kMacroC] = s.c;
        v[(size_t)base + bbdvst::kMacroD] = s.d;
    }
    return v;
}

void BytebeatDrumsAudioProcessor::buildUserKits()
{
    int count = 0;
    const auto* kits = bbdrums::factoryKits(count);
    const auto init = count > 0 ? kitValues(kits[0]) : std::array<float, kNumParams> {};
    for (auto& kit : userKits_)
        kit = init;
}

juce::String BytebeatDrumsAudioProcessor::getKitName(int index) const
{
    if (index >= 0 && index < 6)
    {
        int count = 0;
        const auto* kits = bbdrums::factoryKits(count);
        if (index < count) return kits[index].name;
    }
    if (index >= 6 && index < 10)
        return "User " + juce::String(index - 5);
    return {};
}

void BytebeatDrumsAudioProcessor::loadKit(int index)
{
    if (index < 0 || index >= 10) return;

    if (index < 6)
    {
        int count = 0;
        const auto* kits = bbdrums::factoryKits(count);
        if (index >= count) return;
        applyParams(kitValues(kits[index]));
    }
    else
    {
        applyParams(userKits_[(size_t)(index - 6)]);
    }

    currentKit_.store(index, std::memory_order_relaxed);
    updateHostDisplay();
}

void BytebeatDrumsAudioProcessor::saveKit(int slot)
{
    if (slot < 0 || slot >= 4) return;
    userKits_[(size_t)slot] = readParams();
    currentKit_.store(6 + slot, std::memory_order_relaxed);
    updateHostDisplay();
}

int BytebeatDrumsAudioProcessor::getCurrentProgram()
{
    const int kit = currentKit_.load(std::memory_order_relaxed);
    return kit >= 0 && kit < getNumPrograms() ? kit : 0;
}

void BytebeatDrumsAudioProcessor::setCurrentProgram(int index)
{
    loadKit(index);
}

const juce::String BytebeatDrumsAudioProcessor::getProgramName(int index)
{
    return getKitName(index);
}

void BytebeatDrumsAudioProcessor::changeProgramName(int, const juce::String&) {}

void BytebeatDrumsAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement xml("BytebeatDrumsState");
    xml.setAttribute("version", 1);
    xml.setAttribute("kit", getCurrentKit());

    if (auto state = apvts_.copyState().createXml())
        xml.addChildElement(state.release());

    juce::String kitText;
    for (const auto& kit : userKits_)
        for (float v : kit)
            kitText += juce::String(v, 6) + " ";

    auto* user = xml.createNewChildElement("UserKits");
    user->addTextElement(kitText);

    const auto text = xml.toString();
    destData.replaceAll(text.toRawUTF8(), (size_t)text.getNumBytesAsUTF8());
}

void BytebeatDrumsAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0) return;

    auto xml = juce::XmlDocument::parse(
        juce::String::fromUTF8((const char*)data, sizeInBytes));
    if (xml == nullptr) return;

    juce::String kitText;
    forEachXmlChildElement(*xml, child)
    {
        if (child->hasTagName("UserKits"))
        {
            kitText = child->getAllSubText();
        }
        else
        {
            auto tree = juce::ValueTree::fromXml(*child);
            if (tree.isValid())
                apvts_.replaceState(tree);
        }
    }

    if (kitText.isNotEmpty())
    {
        auto tokens = juce::StringArray::fromTokens(kitText, " ", "");
        if (tokens.size() >= 4 * kNumParams)
        {
            int t = 0;
            for (auto& kit : userKits_)
                for (float& v : kit)
                    v = tokens[t++].getFloatValue();
        }
    }

    currentKit_.store(xml->getIntAttribute("kit", -1), std::memory_order_relaxed);
}

void BytebeatDrumsAudioProcessor::previewPad(int pad, int velocity)
{
    if (pad < 0 || pad >= kNumPads) return;
    previewPad_ = pad;

    // Preview is always a short one-shot, even when the pad itself is in
    // Hold mode: the editor's scope should not sustain forever after a click.
    auto settings = makePadSettings(pad, 44100.0);
    settings.oneShotSamples = std::max(1, (int)(0.25 * 44100.0));
    previewEngine_.reset();
    previewEngine_.setSettings(settings);
    previewEngine_.noteOn(clampInt(rawInt(3) + pad, 0, 127), velocity);
}

void BytebeatDrumsAudioProcessor::renderPreview(float* mono, int n)
{
    if (mono == nullptr || n <= 0) return;
    if (previewPad_ < 0)
    {
        std::memset(mono, 0, sizeof(float) * (size_t)n);
        return;
    }

    previewEngine_.setSettings(makePadSettings(previewPad_, 44100.0));
    previewEngine_.render(mono, nullptr, n);
}

juce::String BytebeatDrumsAudioProcessor::getFormulaError() const
{
    for (int p = 0; p < kNumPads; ++p)
    {
        const int idx = getPadFormulaIndex(p);
        const auto* e = bank_.entry(idx);
        if (e == nullptr) return "P" + juce::String(p + 1) + ": missing formula";
        if (!e->ok) return "P" + juce::String(p + 1) + ": line " + juce::String(e->errorLine);
    }
    return {};
}

juce::String BytebeatDrumsAudioProcessor::getPadFormulaName(int pad) const
{
    const int idx = getPadFormulaIndex(pad);
    if (const auto* e = bank_.entry(idx))
        return e->ok ? e->name : (e->name + " (error)");
    return "-";
}

int BytebeatDrumsAudioProcessor::getPadFormulaIndex(int pad) const
{
    if (pad < 0 || pad >= kNumPads) return 0;
    return rawInt(bbdvst::kNumMasterParams + pad * bbdvst::kPadFields + bbdvst::kFormula);
}

float BytebeatDrumsAudioProcessor::getPadLevel(int pad) const
{
    if (pad < 0 || pad >= kNumPads) return 0.0f;
    return rawValue(bbdvst::kNumMasterParams + pad * bbdvst::kPadFields + bbdvst::kLevel) * 0.01f;
}

juce::AudioProcessorEditor* BytebeatDrumsAudioProcessor::createEditor()
{
    return new BytebeatDrumsAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BytebeatDrumsAudioProcessor();
}
