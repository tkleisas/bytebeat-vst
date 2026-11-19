// BytebeatEngine.h - clock, voicing, resampling and tone for bytebeat formulas.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep
//
// The engine is shared by the MPC plugin and the desktop VST3. It performs no
// allocation, locking or file I/O in render().

#ifndef BYTEBEAT_ENGINE_H
#define BYTEBEAT_ENGINE_H

#include "BytebeatVM.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace bytebeat
{

enum OutputMode : int
{
    kOutBytebeat = 0,
    kOutSigned   = 1,
    kOutFloat    = 2
};

enum PlayMode : int
{
    kPlayFree       = 0,
    kPlaySync       = 1,
    kPlayTrigger    = 2,
    kPlayGatedFree  = 3
};

constexpr int kMaxVoices = 8;

// Optional alternative evaluation backend (the VST3 JavaScript engine).
// The engine does not own this object; the caller keeps it alive for as long
// as any Settings pointing to it may be rendered.
struct Evaluator
{
    virtual ~Evaluator() = default;
    virtual void evaluate(int voice, const double* globals, double* state,
                          double& left, double& right) = 0;
};

struct Settings
{
    const Program* program = nullptr;
    Evaluator* evaluator = nullptr;

    int    outputMode = kOutBytebeat;
    double clockRate  = 8000.0;
    double speed      = 1.0;
    float  smooth     = 0.0f;

    int    playMode      = kPlayFree;
    bool   run           = true;
    int    voices        = 4;
    int    rootNote      = 60;
    bool   pitchTrack    = false;
    float  attackMs      = 2.0f;
    float  releaseMs     = 80.0f;
    bool   resetOnNote   = true;
    int    oneShotSamples = 0;   // >0: gate auto-releases after this many host samples

    float  macro[kNumMacros] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };

    double tempo          = 120.0;
    bool   playing        = false;
    double transportSamples = 0.0;
    double beatsPerBar    = 4.0;
    double hostRate       = 44100.0;

    float  volume = 0.5f;
    float  toneHz = 20000.0f;
    float  drive  = 0.0f;
    float  width  = 1.0f;
    float  pan    = 0.0f;
    bool   dcBlock = true;
};

class Engine
{
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        pressureCoeff_ = 1.0 - std::exp(-1.0 / (0.005 * sampleRate_));
        reset();
    }

    void reset()
    {
        for (auto& v : voices_)
            v = Voice {};

        counter_ = 0;
        dcX1L_ = dcY1L_ = dcX1R_ = dcY1R_ = 0.0;
        toneZL_ = toneZR_ = 0.0;
        runGain_ = 0.0;
        freeVoiceActive_ = false;
        numHeld_ = 0;
        channelPressure_ = 0.0;

        for (int i = 0; i < kNumMacros; ++i)
            macroSm_[i] = std::clamp((double)settings_.macro[i], 0.0, 1.0);

        if (settings_.playMode == kPlayGatedFree)
        {
            Voice& v = voices_[0];
            v = Voice {};
            v.active = true;
            v.gate = false;
            v.note = settings_.rootNote;
            v.vel = 1.0;
            v.ampVel = 1.0;
            v.env = 0.0;
            v.t = 0;
            prime(v, 0);
            freeVoiceActive_ = true;
        }
        else if (settings_.playMode != kPlayTrigger)
        {
            Voice& v = voices_[0];
            v = Voice {};
            v.active = true;
            v.gate = false;
            v.note = settings_.rootNote;
            v.vel = 1.0;
            v.env = 1.0;
            v.t = syncT();
            prime(v, 0);
            freeVoiceActive_ = true;
        }
    }

    void setSettings(const Settings& s)
    {
        const bool programChanged   = (s.program != settings_.program);
        const bool evaluatorChanged = (s.evaluator != settings_.evaluator);
        const bool modeChanged      = (s.playMode != settings_.playMode);

        settings_ = s;
        settings_.clockRate = std::clamp(settings_.clockRate, 100.0, 384000.0);
        settings_.speed     = std::clamp(settings_.speed, 0.01, 64.0);
        settings_.smooth    = std::clamp(settings_.smooth, 0.0f, 1.0f);
        settings_.volume    = std::clamp(settings_.volume, 0.0f, 4.0f);
        settings_.drive     = std::clamp(settings_.drive, 0.0f, 1.0f);
        settings_.width     = std::clamp(settings_.width, 0.0f, 2.0f);
        settings_.pan       = std::clamp(settings_.pan, -1.0f, 1.0f);
        settings_.voices    = std::clamp(settings_.voices, 1, kMaxVoices);
        settings_.rootNote  = std::clamp(settings_.rootNote, 0, 127);
        settings_.attackMs  = std::clamp(settings_.attackMs, 0.0f, 500.0f);
        settings_.releaseMs = std::clamp(settings_.releaseMs, 0.0f, 10000.0f);

        if (programChanged || evaluatorChanged || modeChanged)
            reset();
    }

    void noteOn(int note, int velocity)
    {
        if (settings_.playMode == kPlayGatedFree)
        {
            gatedNoteOn(note, velocity);
            return;
        }
        if (settings_.playMode != kPlayTrigger || settings_.program == nullptr)
            return;

        note = std::clamp(note, 0, 127);
        const double vel = std::clamp(velocity / 127.0, 0.0, 1.0);

        Voice* chosen = nullptr;

        if (settings_.voices <= 1)
        {
            chosen = &voices_[0];
        }
        else
        {
            for (int i = 0; i < settings_.voices; ++i)
            {
                if (!voices_[i].active) { chosen = &voices_[i]; break; }
            }
            if (chosen == nullptr)
            {
                uint64_t bestAge = UINT64_MAX;
                for (int i = 0; i < settings_.voices; ++i)
                {
                    if (voices_[i].age < bestAge)
                    {
                        bestAge = voices_[i].age;
                        chosen = &voices_[i];
                    }
                }
            }
        }

        if (chosen == nullptr) return;

        const bool wasActive = chosen->active;
        const bool retrigger = wasActive && settings_.voices <= 1;

        chosen->active = true;
        chosen->gate = true;
        chosen->note = note;
        chosen->vel = vel;
        chosen->pressureTarget = channelPressure_;
        chosen->gateRemaining = settings_.oneShotSamples;
        chosen->age = ++counter_;

        if (!wasActive)
            chosen->env = 0.0;
        else if (!retrigger)
            chosen->env = 1.0;

        // A stolen voice should not glide from the previous note's waveform.
        if (!retrigger)
            chosen->prevL = chosen->prevR = 0.0;

        if (settings_.resetOnNote || !wasActive)
        {
            chosen->t = 0;
            chosen->phase = 0.0;
            std::memset(chosen->state, 0, sizeof(chosen->state));
        }

        evalVoice(*chosen, (int)(chosen - voices_));
        chosen->prevL = chosen->currL;
        chosen->prevR = chosen->currR;

        // The amplitude envelope uses a slightly compressed velocity curve.
        chosen->ampVel = 0.25 + 0.75 * vel;
    }

    void noteOff(int note)
    {
        if (settings_.playMode == kPlayGatedFree)
        {
            gatedNoteOff(note);
            return;
        }
        if (settings_.playMode != kPlayTrigger) return;
        for (auto& v : voices_)
            if (v.active && v.note == note)
            {
                v.gate = false;
                v.pressureTarget = 0.0;
            }
    }

    void allNotesOff()
    {
        numHeld_ = 0;
        channelPressure_ = 0.0;
        for (auto& v : voices_)
        {
            v.gate = false;
            v.pressureTarget = 0.0;
        }
        if (settings_.playMode == kPlayGatedFree && freeVoiceActive_)
        {
            Voice& v = voices_[0];
            v.note = settings_.rootNote;
            v.vel = 1.0;
            v.ampVel = 1.0;
        }
    }

    void setChannelPressure(double pressure)
    {
        channelPressure_ = std::clamp(pressure, 0.0, 1.0);
        for (auto& v : voices_)
            if (v.active)
                v.pressureTarget = channelPressure_;
    }

    void setPolyPressure(int note, double pressure)
    {
        const double p = std::clamp(pressure, 0.0, 1.0);
        for (int i = 0; i < numHeld_; ++i)
            if (held_[i].note == note)
                held_[i].pressure = p;

        for (auto& v : voices_)
            if (v.active && v.note == note)
                v.pressureTarget = p;
    }

    int64_t primaryT() const
    {
        return voices_[0].t;
    }

    bool hasActiveVoices() const
    {
        for (const auto& v : voices_)
            if (v.active)
                return true;
        return false;
    }

    int primaryNote() const
    {
        return voices_[0].note;
    }

    void render(float* left, float* right, int frames)
    {
        if (left == nullptr || frames <= 0) return;
        if (right == nullptr) right = left;

        if (settings_.program == nullptr && settings_.evaluator == nullptr)
        {
            std::memset(left, 0, sizeof(float) * (size_t)frames);
            if (right != left) std::memset(right, 0, sizeof(float) * (size_t)frames);
            return;
        }

        const double sr = sampleRate_;
        const double macroCoeff = 1.0 - std::exp(-1.0 / (0.010 * sr));
        const double runCoeff   = 1.0 - std::exp(-1.0 / (0.005 * sr));
        const double runTarget  = (settings_.playMode == kPlayFree)
                                    ? (settings_.run ? 1.0 : 0.0)
                                    : (settings_.playMode == kPlaySync)
                                        ? (settings_.playing ? 1.0 : 0.0)
                                        : (settings_.playMode == kPlayGatedFree)
                                            ? (settings_.run ? 1.0 : 0.0)
                                            : 1.0;

        attackInc_  = settings_.attackMs  <= 0.0f ? 1.0 : 1.0 / (settings_.attackMs  * 0.001 * sr);
        releaseInc_ = settings_.releaseMs <= 0.0f ? 1.0 : 1.0 / (settings_.releaseMs * 0.001 * sr);

        for (int i = 0; i < kNumMacros; ++i)
        {
            const double target = std::clamp((double)settings_.macro[i], 0.0, 1.0);
            macroSm_[i] += (target - macroSm_[i]) * macroCoeff;
        }

        for (int n = 0; n < frames; ++n)
        {
            double sumL = 0.0, sumR = 0.0;

            if (settings_.playMode == kPlayTrigger)
            {
                for (int i = 0; i < kMaxVoices; ++i)
                {
                    Voice& v = voices_[i];
                    if (!v.active) continue;

                    advanceVoice(v, i);

                    if (v.gate && v.gateRemaining > 0)
                    {
                        if (--v.gateRemaining <= 0)
                            v.gate = false;
                    }

                    if (v.gate)
                    {
                        v.env = std::min(1.0, v.env + attackInc_);
                    }
                    else
                    {
                        v.env = std::max(0.0, v.env - releaseInc_);
                        if (v.env <= 0.0)
                        {
                            v.active = false;
                            continue;
                        }
                    }

                    const double amp = v.env * v.ampVel;
                    sumL += v.outL * amp;
                    sumR += v.outR * amp;
                }
            }
            else if (settings_.playMode == kPlayGatedFree)
            {
                Voice& v = voices_[0];
                if (!freeVoiceActive_)
                {
                    v = Voice {};
                    v.active = true;
                    v.gate = false;
                    v.note = settings_.rootNote;
                    v.vel = 1.0;
                    v.ampVel = 1.0;
                    v.env = 0.0;
                    v.t = 0;
                    prime(v, 0);
                    freeVoiceActive_ = true;
                }

                // The clock and formula state run freely; the note gate only
                // controls the amplitude envelope.
                advanceVoice(v, 0);
                if (v.gate)
                    v.env = std::min(1.0, v.env + attackInc_);
                else
                    v.env = std::max(0.0, v.env - releaseInc_);

                const double amp = v.env * v.ampVel;
                sumL += v.outL * amp;
                sumR += v.outR * amp;
            }
            else
            {
                Voice& v = voices_[0];
                if (!freeVoiceActive_)
                {
                    v = Voice {};
                    v.active = true;
                    v.gate = false;
                    v.note = settings_.rootNote;
                    v.vel = 1.0;
                    v.env = 1.0;
                    v.t = syncT();
                    prime(v, 0);
                    freeVoiceActive_ = true;
                }

                if (settings_.playMode == kPlaySync)
                    advanceSync(v);
                else if (settings_.run)
                    advanceVoice(v, 0);

                sumL += v.outL;
                sumR += v.outR;
            }

            runGain_ += (runTarget - runGain_) * runCoeff;
            sumL *= runGain_ * settings_.volume;
            sumR *= runGain_ * settings_.volume;

            if (settings_.drive > 0.0001f)
            {
                const double g = 1.0 + settings_.drive * 9.0;
                const double norm = 1.0 / std::tanh(g);
                sumL = std::tanh(sumL * g) * norm;
                sumR = std::tanh(sumR * g) * norm;
            }

            if (settings_.toneHz > 20.0f && settings_.toneHz < 0.45f * sr)
            {
                const double a = std::exp(-6.283185307179586 * settings_.toneHz / sr);
                toneZL_ = (1.0 - a) * sumL + a * toneZL_;
                toneZR_ = (1.0 - a) * sumR + a * toneZR_;
                sumL = toneZL_;
                sumR = toneZR_;
            }

            if (settings_.dcBlock)
            {
                const double yL = sumL - dcX1L_ + 0.9995 * dcY1L_;
                dcX1L_ = sumL; dcY1L_ = yL; sumL = yL;
                const double yR = sumR - dcX1R_ + 0.9995 * dcY1R_;
                dcX1R_ = sumR; dcY1R_ = yR; sumR = yR;
            }

            if (settings_.width != 1.0f)
            {
                const double mid  = (sumL + sumR) * 0.5;
                const double side = (sumL - sumR) * 0.5 * settings_.width;
                sumL = mid + side;
                sumR = mid - side;
            }

            if (settings_.pan != 0.0f)
            {
                const double theta = (settings_.pan + 1.0) * 0.7853981633974483;
                const double gl = std::cos(theta) * 1.4142135623730951;
                const double gr = std::sin(theta) * 1.4142135623730951;
                sumL *= gl;
                sumR *= gr;
            }

            if (right == left)
            {
                left[n] = (float)softClip((sumL + sumR) * 0.5);
            }
            else
            {
                left[n]  = (float)softClip(sumL);
                right[n] = (float)softClip(sumR);
            }
        }
    }

private:
    struct Voice
    {
        bool     active = false;
        bool     gate = false;
        int      note = 60;
        double   vel = 1.0;
        double   ampVel = 1.0;
        int64_t  t = 0;
        double   phase = 0.0;
        double   state[kMaxVars] {};
        double   prevL = 0.0, currL = 0.0;
        double   prevR = 0.0, currR = 0.0;
        double   outL = 0.0, outR = 0.0;
        double   env = 0.0;
        double   pressure = 0.0;
        double   pressureTarget = 0.0;
        int      gateRemaining = 0;
        uint64_t age = 0;
    };

    int64_t syncT() const
    {
        if (settings_.playMode != kPlaySync) return 0;
        const double pos = settings_.transportSamples / settings_.hostRate
                         * settings_.clockRate * settings_.speed;
        if (!std::isfinite(pos)) return 0;
        return (int64_t)std::floor(pos);
    }

    void prime(Voice& v, int index)
    {
        v.prevL = v.currL = 0.0;
        v.prevR = v.currR = 0.0;
        evalVoice(v, index);
        v.prevL = v.currL;
        v.prevR = v.currR;
        v.outL = v.currL;
        v.outR = v.currR;
    }

    void advanceVoice(Voice& v, int index)
    {
        v.pressure += (v.pressureTarget - v.pressure) * pressureCoeff_;

        const double effective = effectiveRate(v);
        const double ratio = effective / sampleRate_;
        v.phase += ratio;

        while (v.phase >= 1.0)
        {
            v.phase -= 1.0;
            v.t += 1;
            evalVoice(v, index);
        }

        double l = v.currL;
        double r = v.currR;

        if (settings_.smooth > 0.0f && ratio < 1.0)
        {
            const double f = settings_.smooth * v.phase;
            l = v.prevL + (v.currL - v.prevL) * f;
            r = v.prevR + (v.currR - v.prevR) * f;
        }

        v.outL = l;
        v.outR = r;
    }

    void advanceSync(Voice& v)
    {
        const double pos = settings_.transportSamples / settings_.hostRate
                         * settings_.clockRate * settings_.speed;
        if (!std::isfinite(pos)) { v.outL = v.outR = 0.0; return; }

        const int64_t nt = (int64_t)std::floor(pos);
        const double frac = pos - (double)nt;

        if (nt != v.t)
        {
            const bool step = (nt == v.t + 1);
            v.t = nt;
            evalVoice(v, 0);
            if (!step)
            {
                v.prevL = v.currL;
                v.prevR = v.currR;
            }
            v.phase = frac;
        }
        else
        {
            v.phase = frac;
        }

        double l = v.currL;
        double r = v.currR;
        const double ratio = settings_.clockRate * settings_.speed / sampleRate_;
        if (settings_.smooth > 0.0f && ratio < 1.0)
        {
            const double f = settings_.smooth * v.phase;
            l = v.prevL + (v.currL - v.prevL) * f;
            r = v.prevR + (v.currR - v.prevR) * f;
        }
        v.outL = l;
        v.outR = r;
    }

    double effectiveRate(const Voice& v) const
    {
        double rate = settings_.clockRate * settings_.speed;
        if ((settings_.playMode == kPlayTrigger || settings_.playMode == kPlayGatedFree)
            && settings_.pitchTrack)
            rate *= std::pow(2.0, (v.note - settings_.rootNote) / 12.0);
        return rate;
    }

    void evalVoice(Voice& v, int index)
    {
        if (settings_.program == nullptr && settings_.evaluator == nullptr)
        {
            v.prevL = v.currL; v.prevR = v.currR;
            v.currL = v.currR = 0.0;
            return;
        }

        double g[kNumGlobals] = {};
        const double time = (double)v.t / settings_.clockRate;

        g[G_T]     = (double)v.t;
        g[G_SR]    = settings_.clockRate;
        g[G_TIME]  = time;
        g[G_BEAT]  = time * settings_.tempo / 60.0;
        g[G_BAR]   = g[G_BEAT] / std::max(1.0, settings_.beatsPerBar);
        g[G_TEMPO] = settings_.tempo;
        g[G_PLAYING] = settings_.playing ? 1.0 : 0.0;
        g[G_NOTE]  = (double)v.note;
        g[G_FREQ]  = 440.0 * std::pow(2.0, (v.note - 69) / 12.0);
        g[G_VEL]   = v.vel;
        g[G_GATE]  = v.gate ? 1.0 : 0.0;
        g[G_VOICE] = (double)index;
        g[G_AT]    = v.pressure;

        for (int i = 0; i < kNumMacros; ++i)
            g[G_A + i] = macroSm_[i];

        double l = 0.0, r = 0.0;
        if (settings_.evaluator != nullptr)
            settings_.evaluator->evaluate(index, g, v.state, l, r);
        else
            run(*settings_.program, g, v.state, l, r);

        v.prevL = v.currL;
        v.prevR = v.currR;
        v.currL = mapValue(l);
        v.currR = mapValue(r);
    }

    double mapValue(double x) const
    {
        switch (settings_.outputMode)
        {
            case kOutSigned:
            {
                const int n = (int)(int8_t)(uint8_t)(toInt32(x) & 255);
                return n / 128.0;
            }
            case kOutFloat:
                if (!std::isfinite(x)) return 0.0;
                return std::clamp(x, -1.0, 1.0);
            case kOutBytebeat:
            default:
            {
                const int n = (int)(toInt32(x) & 255);
                return (n - 128) / 128.0;
            }
        }
    }

    static double softClip(double x)
    {
        if (!std::isfinite(x)) return 0.0;
        const double knee = 0.8;
        const double a = std::fabs(x);
        if (a <= knee) return x;
        const double y = knee + (1.0 - knee) * std::tanh((a - knee) / (1.0 - knee));
        return x < 0.0 ? -y : y;
    }

    static constexpr int kMaxHeldNotes = 32;

    struct HeldNote
    {
        int    note = 60;
        double velocity = 1.0;
        double pressure = 0.0;
    };

    void gatedNoteOn(int note, int velocity)
    {
        if (settings_.program == nullptr && settings_.evaluator == nullptr)
            return;

        note = std::clamp(note, 0, 127);
        const double vel = std::clamp(velocity / 127.0, 0.0, 1.0);

        // Last-note priority; re-pressing a held note moves it to the top.
        for (int i = 0; i < numHeld_; ++i)
        {
            if (held_[i].note == note)
            {
                for (int j = i; j + 1 < numHeld_; ++j)
                    held_[j] = held_[j + 1];
                --numHeld_;
                break;
            }
        }
        if (numHeld_ >= kMaxHeldNotes)
        {
            for (int i = 0; i + 1 < numHeld_; ++i)
                held_[i] = held_[i + 1];
            --numHeld_;
        }

        HeldNote& h = held_[numHeld_++];
        h.note = note;
        h.velocity = vel;
        h.pressure = 0.0;

        if (!freeVoiceActive_)
        {
            Voice& v = voices_[0];
            v = Voice {};
            v.active = true;
            v.t = 0;
            freeVoiceActive_ = true;
        }

        Voice& v = voices_[0];
        v.active = true;
        v.gate = true;
        v.note = note;
        v.vel = vel;
        v.ampVel = 0.25 + 0.75 * vel;
        v.pressureTarget = std::max(channelPressure_, h.pressure);
        // t, phase and the formula state deliberately keep running.
    }

    void gatedNoteOff(int note)
    {
        int index = -1;
        for (int i = 0; i < numHeld_; ++i)
            if (held_[i].note == note)
            {
                index = i;
                break;
            }
        if (index < 0) return;

        const bool wasCurrent = (index == numHeld_ - 1);
        for (int i = index; i + 1 < numHeld_; ++i)
            held_[i] = held_[i + 1];
        --numHeld_;

        if (!wasCurrent) return;

        Voice& v = voices_[0];
        if (numHeld_ > 0)
        {
            const HeldNote& h = held_[numHeld_ - 1];
            v.gate = true;
            v.note = h.note;
            v.vel = h.velocity;
            v.ampVel = 0.25 + 0.75 * h.velocity;
            v.pressureTarget = std::max(channelPressure_, h.pressure);
        }
        else
        {
            v.gate = false;
            v.pressureTarget = 0.0;
        }
    }

    Settings settings_;
    double sampleRate_ = 44100.0;
    double pressureCoeff_ = 0.01;
    double channelPressure_ = 0.0;
    HeldNote held_[kMaxHeldNotes] {};
    int numHeld_ = 0;

    Voice voices_[kMaxVoices];
    uint64_t counter_ = 0;
    bool freeVoiceActive_ = false;

    double macroSm_[kNumMacros] = { 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5 };
    double attackInc_ = 0.01;
    double releaseInc_ = 0.001;
    double runGain_ = 0.0;

    double toneZL_ = 0.0, toneZR_ = 0.0;
    double dcX1L_ = 0.0, dcY1L_ = 0.0;
    double dcX1R_ = 0.0, dcY1R_ = 0.0;
};

} // namespace bytebeat

#endif // BYTEBEAT_ENGINE_H
