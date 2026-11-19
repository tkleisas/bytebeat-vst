// Bytebeat.cpp - Akai MPC / desktop VST2 shell for the bytebeat engine.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep
//
// The MPC has no plugin windows and no text-valued parameters, so formulas are
// selected from a built-in bank or from .bb files placed in Content/Formulas/.
// The VST3 build in vst3/ shares the same engine and adds a code editor.

#include "mpc_plugin.h"
#include "dsp/math.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dirent.h>
#endif

#include "BytebeatCompile.h"
#include "BytebeatEngine.h"
#include "FormulaLibrary.h"

namespace
{

constexpr int kMaxUserFormulas = 32;
constexpr int kMaxNameLength = 64;

enum ParamIndex
{
    kStatus = 0,
    kVolume,
    kFormula,
    kUserBank,
    kUserSlot,
    kMode,
    kClock,
    kPlayMode,
    kRun,
    kSpeed,
    kSmooth,
    kA,
    kB,
    kC,
    kD,
    kE,
    kF,
    kG,
    kH,
    kTone,
    kDrive,
    kDCBlock,
    kWidth,
    kPan,
    kVoices,
    kRoot,
    kPitch,
    kAttack,
    kRelease,
    kResetOnNote,
    kNumParams
};

struct FormulaEntry
{
    std::string name;
    std::string category;
    std::string source;
    int mode = bytebeat::kOutBytebeat;
    int rate = 8000;
    std::unique_ptr<bytebeat::Program> program;
    bool ok = false;
    int errorLine = 0;
    int errorCol = 0;
    std::string error;
    bool user = false;
};

int modeFromName(const std::string& name)
{
    const std::string m = bytebeat::bbLower(name);
    if (m == "signed") return bytebeat::kOutSigned;
    if (m == "floatbeat" || m == "float") return bytebeat::kOutFloat;
    return bytebeat::kOutBytebeat;
}

std::string fileStem(const std::string& filename)
{
    std::string s = filename;
    const size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    const size_t dot = s.find_last_of('.');
    if (dot != std::string::npos) s = s.substr(0, dot);
    if (s.size() > (size_t)kMaxNameLength) s.resize((size_t)kMaxNameLength);
    return s;
}

bool hasBbExtension(const std::string& s)
{
    if (s.size() < 3) return false;
    const std::string ext = bytebeat::bbLower(s.substr(s.size() - 3));
    return ext == ".bb";
}

void listUserFormulaFiles(std::vector<std::string>& files)
{
    const std::string dir = mpc::contentPath("Formulas");

#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    const std::string pattern = dir + "/*.bb";
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                files.push_back(fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR* d = opendir(dir.c_str());
    if (d != nullptr)
    {
        while (dirent* e = readdir(d))
        {
            const std::string name = e->d_name;
            if (name != "." && name != ".." && hasBbExtension(name))
                files.push_back(name);
        }
        closedir(d);
    }
#endif

    std::sort(files.begin(), files.end());
    if ((int)files.size() > kMaxUserFormulas)
        files.resize((size_t)kMaxUserFormulas);
}

} // namespace

class BytebeatPlugin : public mpc::Plugin
{
public:
    BytebeatPlugin() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;

        // Parameter 0 gets nudged by the MPC host on every prepare. Keep it
        // display-only so the nudge can never change the sound.
        mpc::Param status = intParam("status", "Status", 0, 9999, 0);
        status.automatable = false;
        addParam(status);

        addParam(floatParam("volume", "Volume", -40.0f, 6.0f, -12.0f, "dB", 1));

        std::vector<std::string> formulaNames;
        for (const auto& f : bytebeat::builtinFormulas())
            formulaNames.push_back(f.name);
        addParam(choiceParam("formula", "Formula", formulaNames, 0));
        addParam(boolParam ("user_bank", "User Bank", false));
        addParam(intParam  ("user_slot", "User Slot", 1, kMaxUserFormulas, 1));
        addParam(choiceParam("mode", "Mode",
                             { "Formula", "Bytebeat", "Signed", "Floatbeat" }, 0));
        addParam(choiceParam("clock", "Clock",
                             { "Formula", "4000", "8000", "11025", "16000",
                               "22050", "32000", "44100", "48000" }, 0));
        addParam(choiceParam("play_mode", "Play Mode",
                             { "Free", "Sync", "Trigger", "Gated Free" }, 2));
        addParam(boolParam ("run", "Run", true));
        addParam(logParam  ("speed", "Speed", 0.25f, 4.0f, 1.0f, "x", 2));
        addParam(floatParam("smooth", "Smooth", 0.0f, 100.0f, 0.0f, "%", 0));

        addParam(floatParam("a", "A", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("b", "B", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("c", "C", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("d", "D", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("e", "E", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("f", "F", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("g", "G", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("h", "H", 0.0f, 100.0f, 50.0f, "%", 0));

        addParam(floatParam("tone", "Tone", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("drive", "Drive", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(boolParam ("dc_block", "DC Block", true));
        addParam(floatParam("width", "Width", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("pan", "Pan", -100.0f, 100.0f, 0.0f, "%", 0));

        addParam(intParam  ("voices", "Voices", 1, bytebeat::kMaxVoices, 4));
        addParam(intParam  ("root", "Root", 0, 127, 36));
        addParam(boolParam ("pitch", "Pitch", true));
        addParam(floatParam("attack", "Attack", 0.0f, 100.0f, 2.0f, "ms", 1));
        addParam(logParam  ("release", "Release", 1.0f, 2000.0f, 80.0f, "ms", 0));
        addParam(boolParam ("reset_note", "Reset", true));

        if (numParams() != kNumParams)
            mpc::log("Bytebeat: parameter count mismatch (%d vs %d)", numParams(), (int)kNumParams);

        buildBank();
        addFactoryPrograms();
    }

    void prepare(double sampleRate, int maxBlockSize) override
    {
        (void)maxBlockSize;
        engine_.prepare(sampleRate);
        selectionDirty_.store(true, std::memory_order_release);
    }

    void reset() override
    {
        const bool staged = stagedValid_.exchange(false, std::memory_order_acq_rel);
        if (staged && stagedProgram_ != nullptr)
        {
            // The engine's current settings may still point at the program we
            // are replacing. Do not call engine_.reset() here: the next
            // process() calls setSettings(), which sees the new program pointer
            // and resets with it before rendering. This avoids a use-after-free
            // when a second project state replaces a stored formula.
            storedProgram_ = std::move(stagedProgram_);
            storedMode_.store(stagedMode_, std::memory_order_release);
            storedRate_.store(stagedRate_, std::memory_order_release);
            useStored_.store(true, std::memory_order_release);
            storedSource_ = std::move(stagedSource_);
            storedName_ = std::move(stagedName_);
        }
        else
        {
            engine_.reset();
        }
        selectionDirty_.store(true, std::memory_order_release);
    }

    void parameterChanged(int index, float plainValue) override
    {
        (void)plainValue;
        switch (index)
        {
            case kFormula:
            case kUserBank:
            case kUserSlot:
                selectionDirty_.store(true, std::memory_order_release);
                useStored_.store(false, std::memory_order_release);
                break;
            default:
                break;
        }
    }

    void process(mpc::ProcessContext& ctx) override
    {
        float* outL = (ctx.numOutputs > 0 && ctx.outputs != nullptr) ? ctx.outputs[0] : nullptr;
        float* outR = (ctx.numOutputs > 1 && ctx.outputs != nullptr) ? ctx.outputs[1] : nullptr;
        if (outL == nullptr) return;

        const bool dirty = selectionDirty_.exchange(false, std::memory_order_acq_rel);
        if (dirty)
            refreshSelection();

        bytebeat::Settings s = makeSettings(ctx);
        engine_.setSettings(s);

        int pos = 0;
        for (int e = 0; e < ctx.numMidiEvents; ++e)
        {
            const mpc::MidiEvent& ev = ctx.midi[e];
            const int off = mpc::clamp(ev.sampleOffset, 0, ctx.numFrames);
            if (off > pos)
            {
                engine_.render(outL + pos, outR ? outR + pos : nullptr, off - pos);
                pos = off;
            }
            handleMidi(ev);
        }
        if (pos < ctx.numFrames)
            engine_.render(outL + pos, outR ? outR + pos : nullptr, ctx.numFrames - pos);
    }

    bool formatValue(int index, float plainValue, std::string& out) override
    {
        if (index == kUserSlot)
        {
            const int slot = (int)std::lround(plainValue);
            if (slot >= 1 && slot <= (int)user_.size())
                out = user_[(size_t)(slot - 1)].name;
            else
                out = "-";
            return true;
        }

        if (index == kStatus)
        {
            const int code = (int)std::lround(plainValue);
            if (code == 0) out = "OK";
            else if (code >= 1000 && code < 2000) out = "Err L" + std::to_string(code - 1000);
            else if (code == 2000) out = "No user file";
            else if (code == 3000) out = "No formula";
            else if (code == 4000) out = "Project err";
            else out = "Error";
            return true;
        }

        return false;
    }

    void saveExtraState(std::vector<uint8_t>& out) override
    {
        const std::string* source = nullptr;
        std::string name;
        int mode = bytebeat::kOutBytebeat;
        int rate = 8000;

        if (useStored_.load(std::memory_order_acquire) && !storedSource_.empty())
        {
            source = &storedSource_;
            name = storedName_;
            mode = storedMode_.load(std::memory_order_acquire);
            rate = storedRate_.load(std::memory_order_acquire);
        }
        else
        {
            const FormulaEntry* e = selectedEntry();
            if (e == nullptr) return;
            source = &e->source;
            name = e->name;
            mode = e->mode;
            rate = e->rate;
        }

        if (source == nullptr || source->size() > 16000) return;

        const std::string trimmedName = name.substr(0, (size_t)kMaxNameLength);
        const size_t base = out.size();
        const size_t header = 20;
        out.resize(base + header + trimmedName.size() + source->size());

        uint8_t* p = out.data() + base;
        std::memcpy(p, "BBX1", 4);
        p[4] = 1;                                   // version
        p[5] = 0;                                   // flags
        p[6] = (uint8_t)(mode & 0xff);
        p[7] = 0;
        p[8] = (uint8_t)(rate & 0xff);
        p[9] = (uint8_t)((rate >> 8) & 0xff);
        p[10] = (uint8_t)((rate >> 16) & 0xff);
        p[11] = (uint8_t)((rate >> 24) & 0xff);
        p[12] = (uint8_t)(trimmedName.size() & 0xff);
        p[13] = (uint8_t)((trimmedName.size() >> 8) & 0xff);
        const uint32_t srcLen = (uint32_t)source->size();
        p[14] = (uint8_t)(srcLen & 0xff);
        p[15] = (uint8_t)((srcLen >> 8) & 0xff);
        p[16] = (uint8_t)((srcLen >> 16) & 0xff);
        p[17] = (uint8_t)((srcLen >> 24) & 0xff);
        p[18] = 0;
        p[19] = 0;

        std::memcpy(p + header, trimmedName.data(), trimmedName.size());
        std::memcpy(p + header + trimmedName.size(), source->data(), source->size());
    }

    void loadExtraState(const uint8_t* data, size_t size) override
    {
        stagedValid_.store(false, std::memory_order_release);
        if (data == nullptr || size < 20) return;
        if (std::memcmp(data, "BBX1", 4) != 0) return;
        const uint8_t version = data[4];
        if (version != 1) return;

        const int mode = (int)data[6];
        const uint32_t rate = (uint32_t)data[8] | ((uint32_t)data[9] << 8)
                            | ((uint32_t)data[10] << 16) | ((uint32_t)data[11] << 24);
        const uint16_t nameLen = (uint16_t)(data[12] | (data[13] << 8));
        const uint32_t srcLen = (uint32_t)data[14] | ((uint32_t)data[15] << 8)
                              | ((uint32_t)data[16] << 16) | ((uint32_t)data[17] << 24);

        if (nameLen > kMaxNameLength || srcLen == 0 || srcLen > 16000) return;
        if (size < 20u + nameLen + (size_t)srcLen) return;

        const std::string name((const char*)(data + 20), nameLen);
        const std::string source((const char*)(data + 20 + nameLen), srcLen);

        bytebeat::FormulaMeta meta;
        auto result = bytebeat::compile(source, &meta);
        if (!result.ok)
        {
            stagedValid_.store(false, std::memory_order_release);
            // Fall back to the library selection rather than continuing to
            // play a formula from a previous project.
            useStored_.store(false, std::memory_order_release);
            return;
        }

        stagedProgram_ = std::move(result.program);
        stagedSource_ = source;
        stagedName_ = name.empty() ? meta.name : name;
        stagedMode_ = mode >= 0 && mode <= 2 ? mode : modeFromName(meta.mode);
        stagedRate_ = rate >= 1000 && rate <= 192000 ? (int)rate : meta.rate;
        stagedValid_.store(true, std::memory_order_release);
    }

private:
    void handleMidi(const mpc::MidiEvent& ev)
    {
        if (ev.isNoteOn())
            engine_.noteOn(ev.note(), ev.velocity());
        else if (ev.isNoteOff())
            engine_.noteOff(ev.note());
        else if (ev.isAllNotesOff())
            engine_.allNotesOff();
        else if (ev.status() == 0xD0)
            engine_.setChannelPressure(ev.data[1] / 127.0);
        else if (ev.status() == 0xA0)
            engine_.setPolyPressure(ev.data[1], ev.data[2] / 127.0);
    }

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name         = "Bytebeat Machine";
        info.vendor       = "bokontep";
        info.uniqueId     = MPC_FOURCC('B', 'b', 't', '1');
        info.version      = 301;
        info.isInstrument = true;
        info.numInputs    = 0;
        info.numOutputs   = 2;
        info.tailSamples  = 0;
        return info;
    }

    void compileEntry(FormulaEntry& e)
    {
        bytebeat::FormulaMeta meta;
        auto result = bytebeat::compile(e.source, &meta);
        if (!meta.name.empty()) e.name = meta.name;
        if (!meta.mode.empty()) e.mode = modeFromName(meta.mode);
        if (meta.rate > 0) e.rate = meta.rate;

        if (result.ok)
        {
            e.program = std::move(result.program);
            e.ok = true;
            e.errorLine = 0;
            e.errorCol = 0;
            e.error.clear();
        }
        else
        {
            e.ok = false;
            e.errorLine = result.error.line;
            e.errorCol = result.error.col;
            e.error = result.error.message;
        }
    }

    void buildBank()
    {
        builtin_.clear();
        user_.clear();

        for (const auto& f : bytebeat::builtinFormulas())
        {
            FormulaEntry e;
            e.name = f.name;
            e.category = f.category;
            e.source = f.source;
            e.mode = f.mode;
            e.rate = f.rate;
            e.user = false;
            compileEntry(e);
            builtin_.push_back(std::move(e));
        }

        // A small, always-valid fallback so a broken user file cannot silence
        // the instrument.
        {
            FormulaEntry e;
            e.name = "Fallback";
            e.source = "t>>4";
            e.mode = bytebeat::kOutBytebeat;
            e.rate = 8000;
            compileEntry(e);
            fallback_ = std::move(e);
        }

        std::vector<std::string> files;
        listUserFormulaFiles(files);

        for (const auto& file : files)
        {
            const std::string path = mpc::contentPath("Formulas/" + file);
            std::string text;
            if (!mpc::readTextFile(path, text)) continue;
            if (text.empty()) continue;

            FormulaEntry e;
            e.name = fileStem(file);
            e.source = std::move(text);
            e.user = true;
            compileEntry(e);
            user_.push_back(std::move(e));
        }
    }

    const FormulaEntry* selectedEntry() const
    {
        const bool userBank = getBool(kUserBank);
        if (userBank)
        {
            const int slot = getInt(kUserSlot);
            if (slot >= 1 && slot <= (int)user_.size())
                return &user_[(size_t)(slot - 1)];
            return nullptr;
        }

        const int idx = getInt(kFormula);
        if (idx >= 0 && idx < (int)builtin_.size())
            return &builtin_[(size_t)idx];
        return nullptr;
    }

    static int clockChoiceToRate(int choice, int fallback)
    {
        switch (choice)
        {
            case 1: return 4000;
            case 2: return 8000;
            case 3: return 11025;
            case 4: return 16000;
            case 5: return 22050;
            case 6: return 32000;
            case 7: return 44100;
            case 8: return 48000;
            default: return fallback;
        }
    }

    void refreshSelection()
    {
        const FormulaEntry* e = selectedEntry();
        int status = 0;

        if (e == nullptr)
        {
            status = 2000;
            if (!getBool(kUserBank)) status = 3000;
        }
        else if (!e->ok)
        {
            status = 1000 + std::min(999, e->errorLine);
        }

        status_.store(status, std::memory_order_release);

        // Status is display-only. Setting it here (audio thread, atomic store)
        // nudges the host to re-read the value text on the next UI refresh.
        if (getInt(kStatus) != status)
            set(kStatus, (float)status);
    }

    bytebeat::Settings makeSettings(const mpc::ProcessContext& ctx)
    {
        bytebeat::Settings s;

        const FormulaEntry* entry = selectedEntry();
        s.program = nullptr;

        if (useStored_.load(std::memory_order_acquire) && storedProgram_ != nullptr)
        {
            s.program = storedProgram_.get();
        }
        else if (entry != nullptr && entry->ok && entry->program != nullptr)
        {
            s.program = entry->program.get();
        }
        else if (fallback_.ok && fallback_.program != nullptr)
        {
            s.program = fallback_.program.get();
        }

        int modeChoice = getInt(kMode);
        int outputMode = entry != nullptr ? entry->mode : bytebeat::kOutBytebeat;
        if (modeChoice > 0)
            outputMode = modeChoice - 1;

        int rate = entry != nullptr ? entry->rate : 8000;
        if (useStored_.load(std::memory_order_acquire))
        {
            if (modeChoice == 0)
                outputMode = storedMode_.load(std::memory_order_acquire);
            if (getInt(kClock) == 0)
                rate = storedRate_.load(std::memory_order_acquire);
        }

        const int clockChoice = getInt(kClock);
        rate = clockChoiceToRate(clockChoice, rate);

        s.outputMode = std::clamp(outputMode, 0, 2);
        s.clockRate  = (double)rate;
        s.speed      = (double)get(kSpeed);
        s.smooth     = get(kSmooth) * 0.01f;

        s.playMode    = std::clamp(getInt(kPlayMode), 0, 3);
        s.run         = getBool(kRun);
        s.voices      = getInt(kVoices);
        s.rootNote    = getInt(kRoot);
        s.pitchTrack  = getBool(kPitch);
        s.attackMs    = get(kAttack);
        s.releaseMs   = get(kRelease);
        s.resetOnNote = getBool(kResetOnNote);

        s.macro[0] = get(kA) * 0.01f;
        s.macro[1] = get(kB) * 0.01f;
        s.macro[2] = get(kC) * 0.01f;
        s.macro[3] = get(kD) * 0.01f;
        s.macro[4] = get(kE) * 0.01f;
        s.macro[5] = get(kF) * 0.01f;
        s.macro[6] = get(kG) * 0.01f;
        s.macro[7] = get(kH) * 0.01f;

        const mpc::Transport& tr = ctx.transport;
        s.tempo   = (tr.tempoValid && tr.bpm > 1.0) ? tr.bpm : 120.0;
        s.playing = tr.valid && tr.playing;
        s.transportSamples = tr.valid ? tr.samplePos : 0.0;
        s.beatsPerBar = tr.timeSigNumerator > 0 ? (double)tr.timeSigNumerator : 4.0;
        s.hostRate = ctx.sampleRate > 0.0 ? ctx.sampleRate : sampleRate();

        s.volume  = mpc::dsp::dbToGain(get(kVolume));
        const float tone = std::clamp(get(kTone) * 0.01f, 0.0f, 1.0f);
        s.toneHz  = tone >= 0.995f ? 24000.0f : (float)(200.0 * std::pow(100.0, (double)tone));
        s.drive   = get(kDrive) * 0.01f;
        s.width   = get(kWidth) * 0.01f;
        s.pan     = get(kPan) * 0.01f;
        s.dcBlock = getBool(kDCBlock);

        return s;
    }

    void addFactoryPrograms()
    {
        auto make = [this](std::initializer_list<std::pair<int, float>> overrides)
        {
            std::vector<float> v((size_t)numParams());
            for (int i = 0; i < numParams(); ++i)
                v[(size_t)i] = param(i).def;
            for (const auto& o : overrides)
                v[(size_t)o.first] = o.second;
            return v;
        };

        addProgram("Init", make({
            { kPlayMode, 2 }, { kVoices, 4 }, { kRun, 1 },
            { kRoot, 36 }, { kPitch, 1 }, { kVolume, -12 } }));
        addProgram("Copper Bars", make({
            { kFormula, 0 }, { kRun, 1 }, { kPlayMode, 3 },
            { kA, 35 }, { kB, 70 }, { kC, 50 }, { kD, 50 },
            { kVolume, -8 } }));
        addProgram("Sierpinski", make({
            { kFormula, 1 }, { kRun, 1 }, { kPlayMode, 3 }, { kTone, 75 },
            { kVolume, -10 } }));
        addProgram("Rusty", make({
            { kFormula, 3 }, { kRun, 1 }, { kPlayMode, 3 }, { kDrive, 35 },
            { kTone, 65 }, { kVolume, -10 } }));
        addProgram("Chime", make({
            { kFormula, 11 }, { kRun, 1 }, { kPlayMode, 3 }, { kTone, 85 },
            { kB, 25 }, { kRelease, 300 }, { kVolume, -8 } }));
        addProgram("Glass", make({
            { kFormula, 15 }, { kRun, 1 }, { kPlayMode, 3 }, { kClock, 0 },
            { kTone, 90 }, { kWidth, 80 }, { kVolume, -8 } }));
        addProgram("Stereo Maze", make({
            { kFormula, 18 }, { kRun, 1 }, { kPlayMode, 3 }, { kWidth, 100 },
            { kPan, 0 }, { kVolume, -10 } }));
        addProgram("Longline", make({
            { kFormula, 19 }, { kRun, 1 }, { kPlayMode, 3 }, { kClock, 0 },
            { kTone, 80 }, { kVolume, -6 } }));
        addProgram("Accumulator", make({
            { kFormula, 21 }, { kRun, 1 }, { kPlayMode, 3 }, { kA, 20 }, { kB, 60 },
            { kTone, 70 }, { kVolume, -10 } }));
        addProgram("Filtered", make({
            { kFormula, 22 }, { kRun, 1 }, { kPlayMode, 3 }, { kClock, 0 },
            { kTone, 75 }, { kVolume, -8 } }));
        addProgram("Trigger Keys", make({
            { kFormula, 0 }, { kPlayMode, 2 }, { kVoices, 4 },
            { kPitch, 1 }, { kRoot, 60 }, { kAttack, 2 },
            { kRelease, 120 }, { kA, 40 }, { kB, 80 },
            { kVolume, -10 } }));
        addProgram("Sync Grid", make({
            { kFormula, 2 }, { kPlayMode, 1 }, { kClock, 0 },
            { kTone, 75 }, { kVolume, -10 } }));
        addProgram("Noise Wash", make({
            { kFormula, 8 }, { kRun, 1 }, { kPlayMode, 3 }, { kTone, 55 },
            { kDrive, 20 }, { kVolume, -14 } }));
    }

    bytebeat::Engine engine_;

    std::vector<FormulaEntry> builtin_;
    std::vector<FormulaEntry> user_;
    FormulaEntry fallback_;

    std::atomic<bool> selectionDirty_ { true };
    std::atomic<bool> useStored_ { false };
    std::atomic<bool> stagedValid_ { false };
    std::atomic<int>  status_ { 0 };
    std::atomic<int>  storedMode_ { bytebeat::kOutBytebeat };
    std::atomic<int>  storedRate_ { 8000 };

    std::unique_ptr<bytebeat::Program> stagedProgram_;
    std::unique_ptr<bytebeat::Program> storedProgram_;

    std::string stagedSource_;
    std::string stagedName_;
    std::string storedSource_;
    std::string storedName_;
    int stagedMode_ = bytebeat::kOutBytebeat;
    int stagedRate_ = 8000;
};

MPC_PLUGIN_ENTRY(BytebeatPlugin)
