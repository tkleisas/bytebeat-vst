// BytebeatDrums.cpp - 16-pad bytebeat drum instrument for Akai MPC / VST2.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep
//
// Each pad is a one-voice bytebeat engine with its own formula, clock rate,
// decay envelope, pan, tune, choke group and four macro controls. MIDI notes
// (default 36..51) trigger the pads; the MPC's own sequencer provides patterns.
// One-shot pads gate for 2 ms and then release over their Decay; Hold pads run
// until the note is released.

#include "mpc_plugin.h"
#include "dsp/math.h"

#include <algorithm>
#include <array>
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
#include "DrumKits.h"
#include "FormulaLibrary.h"

namespace
{

constexpr int kPads = 16;
constexpr int kPadFields = 11;
constexpr int kPadBase = 5;
constexpr int kNumParams = kPadBase + kPads * kPadFields + 3;
constexpr int kMaxUserFormulas = 32;
constexpr int kMaxNameLength = 64;
constexpr int kTempBlock = 8192;

enum PadField
{
    kFormula = 0,
    kLevel,
    kPan,
    kTune,
    kDecay,
    kChoke,
    kMode,
    kMacroA,
    kMacroB,
    kMacroC,
    kMacroD
};

constexpr int padParam(int pad, int field)
{
    return kPadBase + pad * kPadFields + field;
}

enum ParamIndex
{
    kStatus = 0,
    kMasterVolume,
    kMasterTone,
    kMasterDrive,
    kNoteBase,
    kKit = kPadBase + kPads * kPadFields,
    kKitLoad,
    kKitSave
};

struct FormulaEntry
{
    std::string name;
    std::string source;
    int mode = bytebeat::kOutBytebeat;
    int rate = 8000;
    std::unique_ptr<bytebeat::Program> program;
    bool ok = false;
    int errorLine = 0;
    std::string error;
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
    return bytebeat::bbLower(s.substr(s.size() - 3)) == ".bb";
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

class BytebeatDrums : public mpc::Plugin
{
public:
    BytebeatDrums() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;

        buildBank();

        mpc::Param status = intParam("status", "Status", 0, 9999, 0);
        status.automatable = false;
        addParam(status);

        addParam(floatParam("master_vol", "Volume", -40.0f, 6.0f, -12.0f, "dB", 1));
        addParam(floatParam("master_tone", "Tone", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("master_drive", "Drive", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(intParam("note_base", "Note Base", 0, 111, 36));

        for (int p = 0; p < kPads; ++p)
        {
            char id[32];
            char name[24];

            std::snprintf(id, sizeof(id), "p%d_formula", p + 1);
            std::snprintf(name, sizeof(name), "P%d Formula", p + 1);
            addParam(choiceParam(id, name, formulaNames_, 0));

            std::snprintf(id, sizeof(id), "p%d_level", p + 1);
            std::snprintf(name, sizeof(name), "P%d Level", p + 1);
            addParam(floatParam(id, name, 0.0f, 100.0f, 70.0f, "%", 0));

            std::snprintf(id, sizeof(id), "p%d_pan", p + 1);
            std::snprintf(name, sizeof(name), "P%d Pan", p + 1);
            addParam(floatParam(id, name, -100.0f, 100.0f, 0.0f, "%", 0));

            std::snprintf(id, sizeof(id), "p%d_tune", p + 1);
            std::snprintf(name, sizeof(name), "P%d Tune", p + 1);
            addParam(floatParam(id, name, -24.0f, 24.0f, 0.0f, "st", 0));

            std::snprintf(id, sizeof(id), "p%d_decay", p + 1);
            std::snprintf(name, sizeof(name), "P%d Decay", p + 1);
            addParam(logParam(id, name, 10.0f, 4000.0f, 300.0f, "ms", 0));

            std::snprintf(id, sizeof(id), "p%d_choke", p + 1);
            std::snprintf(name, sizeof(name), "P%d Choke", p + 1);
            addParam(choiceParam(id, name, { "Off", "1", "2", "3", "4" }, 0));

            std::snprintf(id, sizeof(id), "p%d_mode", p + 1);
            std::snprintf(name, sizeof(name), "P%d Mode", p + 1);
            addParam(choiceParam(id, name, { "One-shot", "Hold" }, 0));

            std::snprintf(id, sizeof(id), "p%d_a", p + 1);
            std::snprintf(name, sizeof(name), "P%d A", p + 1);
            addParam(floatParam(id, name, 0.0f, 100.0f, 50.0f, "%", 0));

            std::snprintf(id, sizeof(id), "p%d_b", p + 1);
            std::snprintf(name, sizeof(name), "P%d B", p + 1);
            addParam(floatParam(id, name, 0.0f, 100.0f, 50.0f, "%", 0));

            std::snprintf(id, sizeof(id), "p%d_c", p + 1);
            std::snprintf(name, sizeof(name), "P%d C", p + 1);
            addParam(floatParam(id, name, 0.0f, 100.0f, 50.0f, "%", 0));

            std::snprintf(id, sizeof(id), "p%d_d", p + 1);
            std::snprintf(name, sizeof(name), "P%d D", p + 1);
            addParam(floatParam(id, name, 0.0f, 100.0f, 50.0f, "%", 0));
        }

        addParam(choiceParam("kit", "Kit",
                             { "Init", "Bytebeat", "Metal", "Acid", "Ambient", "Glitch",
                               "User 1", "User 2", "User 3", "User 4" }, 0));
        addParam(boolParam("kit_load", "Load Kit", false));
        addParam(boolParam("kit_save", "Save Kit", false));

        if (numParams() != kNumParams)
            mpc::log("BytebeatDrums: parameter count mismatch (%d vs %d)", numParams(), (int)kNumParams);

        addFactoryKits();

        // Start on the Init kit so the default sound matches the Kit menu.
        applyKit(0);
    }

    void prepare(double sampleRate, int maxBlockSize) override
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        (void)maxBlockSize;

        tempL_.assign((size_t)kTempBlock, 0.0f);
        tempR_.assign((size_t)kTempBlock, 0.0f);
        chokeStep_ = 1.0f / (0.005f * (float)sampleRate_);

        for (int p = 0; p < kPads; ++p)
        {
            pads_[p].engine.prepare(sampleRate_);
            pads_[p].chokeGain = 1.0f;
            pads_[p].choking = false;
            pads_[p].oneShot = false;
            pads_[p].lastNote = clampInt(getInt(kNoteBase) + p, 0, 127);
        }
    }

    void reset() override
    {
        for (auto& pad : pads_)
        {
            pad.engine.reset();
            pad.chokeGain = 1.0f;
            pad.choking = false;
            pad.oneShot = false;
        }
        lastKitLoad_ = getBool(kKitLoad);
        lastKitSave_ = getBool(kKitSave);
    }

    void process(mpc::ProcessContext& ctx) override
    {
        float* outL = (ctx.outputs != nullptr && ctx.numOutputs > 0) ? ctx.outputs[0] : nullptr;
        float* outR = (ctx.outputs != nullptr && ctx.numOutputs > 1) ? ctx.outputs[1] : nullptr;
        if (outL == nullptr || ctx.numFrames <= 0) return;
        if (ctx.numFrames > kTempBlock) return; // the MPC never asks for this much

        const int n = ctx.numFrames;

        handleKitActions();

        for (int p = 0; p < kPads; ++p)
            pads_[p].engine.setSettings(makePadSettings(p));

        refreshStatus();

        std::fill(outL, outL + n, 0.0f);
        if (outR != nullptr)
            std::fill(outR, outR + n, 0.0f);

        int pos = 0;
        for (int e = 0; e < ctx.numMidiEvents; ++e)
        {
            const mpc::MidiEvent& ev = ctx.midi[e];
            const int off = mpc::clamp(ev.sampleOffset, 0, n);
            if (off > pos)
            {
                renderPads(outL + pos, outR != nullptr ? outR + pos : nullptr, off - pos);
                pos = off;
            }
            handleMidi(ev);
        }
        if (pos < n)
            renderPads(outL + pos, outR != nullptr ? outR + pos : nullptr, n - pos);

        const float master = mpc::dsp::dbToGain(get(kMasterVolume));
        for (int i = 0; i < n; ++i)
        {
            outL[i] = softClip(outL[i] * master);
            if (outR != nullptr)
                outR[i] = softClip(outR[i] * master);
        }
    }

    bool formatValue(int index, float plainValue, std::string& out) override
    {
        if (index != kStatus) return false;

        const int code = (int)std::lround(plainValue);
        if (code == 0) out = "OK";
        else if (code >= 1000 && code < 2000) out = "Err L" + std::to_string(code - 1000);
        else if (code == 2000) out = "No formula";
        else out = "Error";
        return true;
    }

private:
    struct Pad
    {
        bytebeat::Engine engine;
        int lastNote = 36;
        float chokeGain = 1.0f;
        bool choking = false;
        bool oneShot = false;
    };

    static int juceClamp(int v, int lo, int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    static float softClip(float x)
    {
        if (!std::isfinite(x)) return 0.0f;
        constexpr float knee = 0.8f;
        const float a = std::fabs(x);
        if (a <= knee) return x;
        const float y = knee + (1.0f - knee) * std::tanh((a - knee) / (1.0f - knee));
        return x < 0.0f ? -y : y;
    }

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name         = "Bytebeat Drums";
        info.vendor       = "bokontep";
        info.uniqueId     = MPC_FOURCC('B', 'b', 't', 'D');
        info.version      = 100;
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
        }
        else
        {
            e.ok = false;
            e.errorLine = result.error.line;
        }
    }

    void buildBank()
    {
        bank_.clear();
        formulaNames_.clear();

        for (const auto& f : bytebeat::builtinFormulas())
        {
            FormulaEntry e;
            e.name = f.name;
            e.source = f.source;
            e.mode = f.mode;
            e.rate = f.rate;
            compileEntry(e);
            formulaNames_.push_back(e.name);
            bank_.push_back(std::move(e));
        }

        std::vector<std::string> files;
        listUserFormulaFiles(files);
        for (const auto& file : files)
        {
            std::string text;
            if (!mpc::readTextFile(mpc::contentPath("Formulas/" + file), text) || text.empty())
                continue;

            FormulaEntry e;
            e.name = fileStem(file);
            e.source = std::move(text);
            compileEntry(e);
            formulaNames_.push_back(e.name);
            bank_.push_back(std::move(e));
        }

        FormulaEntry fallback;
        fallback.name = "Fallback";
        fallback.source = "t>>4";
        fallback.mode = bytebeat::kOutBytebeat;
        fallback.rate = 8000;
        compileEntry(fallback);
        fallback_ = std::move(fallback);
    }

    const FormulaEntry* entryForPad(int pad) const
    {
        const int idx = getInt(padParam(pad, kFormula));
        if (idx >= 0 && idx < (int)bank_.size())
            return &bank_[(size_t)idx];
        return nullptr;
    }

    const bytebeat::Program* programForPad(int pad) const
    {
        const FormulaEntry* e = entryForPad(pad);
        if (e != nullptr && e->ok && e->program != nullptr)
            return e->program.get();
        if (fallback_.ok && fallback_.program != nullptr)
            return fallback_.program.get();
        return nullptr;
    }

    bytebeat::Settings makePadSettings(int pad) const
    {
        bytebeat::Settings s;
        const FormulaEntry* e = entryForPad(pad);

        s.program = programForPad(pad);
        s.evaluator = nullptr;
        s.outputMode = e != nullptr ? e->mode : bytebeat::kOutBytebeat;
        s.clockRate = e != nullptr ? (double)e->rate : 8000.0;

        const float tune = get(padParam(pad, kTune));
        s.speed = std::pow(2.0, (double)tune / 12.0);
        s.smooth = 0.0f;

        const int mode = getInt(padParam(pad, kMode));
        s.playMode = bytebeat::kPlayTrigger;
        s.run = true;
        s.voices = 1;
        s.rootNote = clampInt(getInt(kNoteBase) + pad, 0, 127);
        s.pitchTrack = false;
        s.attackMs = 0.5f;
        s.releaseMs = get(padParam(pad, kDecay));
        s.resetOnNote = true;
        s.oneShotSamples = mode == 0 ? std::max(1, (int)(0.002 * sampleRate_)) : 0;

        s.macro[0] = get(padParam(pad, kMacroA)) * 0.01f;
        s.macro[1] = get(padParam(pad, kMacroB)) * 0.01f;
        s.macro[2] = get(padParam(pad, kMacroC)) * 0.01f;
        s.macro[3] = get(padParam(pad, kMacroD)) * 0.01f;
        s.macro[4] = 0.5f;
        s.macro[5] = 0.5f;
        s.macro[6] = 0.5f;
        s.macro[7] = 0.5f;

        s.tempo = 120.0;
        s.playing = true;
        s.transportSamples = 0.0;
        s.beatsPerBar = 4.0;
        s.hostRate = sampleRate_;

        s.volume = get(padParam(pad, kLevel)) * 0.01f;
        const float tone = std::clamp(get(kMasterTone) * 0.01f, 0.0f, 1.0f);
        s.toneHz = tone >= 0.995f ? 24000.0f : (float)(200.0 * std::pow(100.0, (double)tone));
        s.drive = get(kMasterDrive) * 0.01f;
        s.dcBlock = true;
        s.width = 1.0f;
        s.pan = get(padParam(pad, kPan)) * 0.01f;

        return s;
    }

    static int clampInt(int v, int lo, int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    void refreshStatus()
    {
        int code = 0;
        for (int p = 0; p < kPads; ++p)
        {
            const int idx = getInt(padParam(p, kFormula));
            if (idx < 0 || idx >= (int)bank_.size())
            {
                code = 2000;
                break;
            }
            const auto& e = bank_[(size_t)idx];
            if (!e.ok)
            {
                code = 1000 + std::min(999, e.errorLine);
                break;
            }
        }

        if (getInt(kStatus) != code)
            set(kStatus, (float)code);
    }

    void handleMidi(const mpc::MidiEvent& ev)
    {
        if (ev.isNoteOn())
        {
            const int pad = ev.note() - getInt(kNoteBase);
            if (pad >= 0 && pad < kPads)
                triggerPad(pad, ev.note(), ev.velocity());
        }
        else if (ev.isNoteOff())
        {
            const int pad = ev.note() - getInt(kNoteBase);
            if (pad >= 0 && pad < kPads)
                releasePad(pad, ev.note());
        }
        else if (ev.isAllNotesOff())
        {
            allPadsOff();
        }
        else if (ev.status() == 0xD0)
        {
            for (auto& pad : pads_)
                pad.engine.setChannelPressure(ev.data[1] / 127.0);
        }
        else if (ev.status() == 0xA0)
        {
            for (auto& pad : pads_)
                pad.engine.setPolyPressure(ev.data[1], ev.data[2] / 127.0);
        }
    }

    void triggerPad(int pad, int note, int velocity)
    {
        const int group = getInt(padParam(pad, kChoke));
        if (group > 0)
        {
            for (int q = 0; q < kPads; ++q)
            {
                if (q == pad) continue;
                if (getInt(padParam(q, kChoke)) != group) continue;
                if (!pads_[q].engine.hasActiveVoices()) continue;
                pads_[q].choking = true;
                pads_[q].chokeGain = 1.0f;
            }
        }

        pads_[pad].choking = false;
        pads_[pad].chokeGain = 1.0f;
        pads_[pad].oneShot = (getInt(padParam(pad, kMode)) == 0);
        pads_[pad].lastNote = note;
        pads_[pad].engine.noteOn(note, velocity);
    }

    void releasePad(int pad, int note)
    {
        // One-shot pads ignore note-off; their 2 ms gate auto-releases. Use the
        // mode captured at trigger time so changing the pad mode while a note
        // is held cannot leave a Hold voice stuck on.
        if (pads_[pad].oneShot)
            return;
        if (pads_[pad].lastNote == note)
            pads_[pad].engine.noteOff(note);
    }

    void allPadsOff()
    {
        for (auto& pad : pads_)
        {
            if (!pad.engine.hasActiveVoices()) continue;
            pad.choking = true;
            pad.chokeGain = 1.0f;
            pad.engine.allNotesOff();
        }
    }

    void renderPads(float* outL, float* outR, int n)
    {
        if (outL == nullptr || n <= 0) return;

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
                if (outR != nullptr)
                {
                    outL[i] += l;
                    outR[i] += r;
                }
                else
                {
                    outL[i] += (l + r) * 0.5f;
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

    std::vector<float> defaults() const
    {
        std::vector<float> v((size_t)numParams());
        for (int i = 0; i < numParams(); ++i)
            v[(size_t)i] = param(i).def;
        return v;
    }

    void setPad(std::vector<float>& v, int pad, const bbdrums::PadSpec& s)
    {
        v[(size_t)padParam(pad, kFormula)] = (float)s.formula;
        v[(size_t)padParam(pad, kLevel)]   = s.level;
        v[(size_t)padParam(pad, kPan)]     = s.pan;
        v[(size_t)padParam(pad, kTune)]    = s.tune;
        v[(size_t)padParam(pad, kDecay)]   = s.decay;
        v[(size_t)padParam(pad, kChoke)]   = (float)s.choke;
        v[(size_t)padParam(pad, kMode)]    = (float)s.mode;
        v[(size_t)padParam(pad, kMacroA)]  = s.a;
        v[(size_t)padParam(pad, kMacroB)]  = s.b;
        v[(size_t)padParam(pad, kMacroC)]  = s.c;
        v[(size_t)padParam(pad, kMacroD)]  = s.d;
    }

    void addKit(const bbdrums::KitSpec& kit)
    {
        auto v = defaults();
        v[(size_t)kMasterVolume] = kit.masterVolume;
        v[(size_t)kMasterTone] = kit.tone;
        v[(size_t)kMasterDrive] = kit.drive;
        v[(size_t)kKit] = (float)factoryKits_.size();
        for (int p = 0; p < kPads; ++p)
            setPad(v, p, kit.pads[p]);

        factoryKits_.push_back(v);
        addProgram(kit.name, std::move(v));
    }

    void addFactoryKits()
    {
        int count = 0;
        const bbdrums::KitSpec* kits = bbdrums::factoryKits(count);
        for (int i = 0; i < count; ++i)
            addKit(kits[i]);

        // Start all four user slots from the Init kit so Load always has
        // something useful before the user saves their own kit.
        if (!factoryKits_.empty())
        {
            for (auto& user : userKits_)
                for (int i = 0; i < (int)kKit; ++i)
                    user[(size_t)i].store(factoryKits_[0][(size_t)i], std::memory_order_relaxed);
        }
    }

    void handleKitActions()
    {
        const bool load = getBool(kKitLoad);
        if (load && !lastKitLoad_)
            applyKit(getInt(kKit));
        lastKitLoad_ = load;

        const bool save = getBool(kKitSave);
        if (save && !lastKitSave_)
            saveKit();
        lastKitSave_ = save;
    }

    void applyKit(int index)
    {
        // Real-time path: read the preallocated kit and write parameter
        // atomics. No allocation and no locks.
        if (index >= 0 && index < (int)factoryKits_.size())
        {
            const auto& values = factoryKits_[(size_t)index];
            const int count = std::min((int)values.size(), (int)kKit);
            for (int i = 0; i < count; ++i)
                set(i, values[(size_t)i]);
        }
        else if (index >= 6 && index < 10)
        {
            const auto& values = userKits_[(size_t)(index - 6)];
            for (int i = 0; i < (int)kKit; ++i)
                set(i, values[(size_t)i].load(std::memory_order_relaxed));
        }
    }

    void saveKit()
    {
        const int selected = getInt(kKit);
        const int slot = (selected >= 6 && selected <= 9) ? selected - 6 : 0;

        for (int i = 0; i < (int)kKit; ++i)
            userKits_[(size_t)slot][(size_t)i].store(get(i), std::memory_order_relaxed);

        if (selected < 6 || selected > 9)
            set(kKit, 6.0f); // reveal the slot that just received the sound
    }

    void saveExtraState(std::vector<uint8_t>& out) override
    {
        constexpr int n = (int)kKit; // params 0..180, excluding kit/action params
        const size_t base = out.size();
        const size_t total = 8 + (size_t)n * 4u * 4u;
        out.resize(base + total);
        uint8_t* p = out.data() + base;
        std::memcpy(p, "BDK1", 4);
        p[4] = 1; p[5] = 0; p[6] = 0; p[7] = 0;

        size_t off = 8;
        for (int u = 0; u < 4; ++u)
        {
            for (int i = 0; i < n; ++i)
            {
                const float v = userKits_[(size_t)u][(size_t)i].load(std::memory_order_relaxed);
                uint32_t bits = 0;
                std::memcpy(&bits, &v, 4);
                p[off++] = (uint8_t)(bits & 0xff);
                p[off++] = (uint8_t)((bits >> 8) & 0xff);
                p[off++] = (uint8_t)((bits >> 16) & 0xff);
                p[off++] = (uint8_t)((bits >> 24) & 0xff);
            }
        }
    }

    void loadExtraState(const uint8_t* data, size_t size) override
    {
        constexpr int n = (int)kKit;
        if (data == nullptr || size < 8 + (size_t)n * 4u * 4u) return;
        if (std::memcmp(data, "BDK1", 4) != 0 || data[4] != 1) return;

        size_t off = 8;
        for (int u = 0; u < 4; ++u)
        {
            for (int i = 0; i < n; ++i)
            {
                const uint32_t bits = (uint32_t)data[off] | ((uint32_t)data[off + 1] << 8)
                                    | ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 3] << 24);
                off += 4;
                float v = 0.0f;
                std::memcpy(&v, &bits, 4);
                userKits_[(size_t)u][(size_t)i].store(v, std::memory_order_relaxed);
            }
        }
    }

    std::vector<FormulaEntry> bank_;
    std::vector<std::string> formulaNames_;
    FormulaEntry fallback_;
    std::vector<std::vector<float>> factoryKits_;
    std::array<std::array<std::atomic<float>, (size_t)kKit>, 4> userKits_ {};
    bool lastKitLoad_ = false;
    bool lastKitSave_ = false;
    Pad pads_[kPads];
    std::vector<float> tempL_;
    std::vector<float> tempR_;
    double sampleRate_ = 44100.0;
    float chokeStep_ = 0.0045f;
};

MPC_PLUGIN_ENTRY(BytebeatDrums)
