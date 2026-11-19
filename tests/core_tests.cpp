// core_tests.cpp - unit tests for the bytebeat compiler, VM and engine.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BytebeatCompile.h"
#include "BytebeatEngine.h"
#include "DrumKits.h"
#include "FormulaLibrary.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace bytebeat;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_NEAR(a, b, eps) do { ++g_checks; const double _a = (a), _b = (b); \
    if (!(std::fabs(_a - _b) <= (eps))) { ++g_failures; \
    std::printf("FAIL %s:%d: %s = %.12g, expected %.12g\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

static bool compileOk(const std::string& src, Program& out, CompileError& err)
{
    auto r = compile(src);
    if (!r.ok)
    {
        err = r.error;
        return false;
    }
    out = *r.program;
    return true;
}

static double evalMono(const std::string& src, int64_t t, double* vars = nullptr,
                       const double* macros = nullptr, int64_t clock = 8000)
{
    auto r = compile(src);
    if (!r.ok)
    {
        std::printf("compile failed for [%s]: %s (line %d)\n", src.c_str(),
                    r.error.message.c_str(), r.error.line);
        ++g_failures;
        return 0.0;
    }

    double localVars[kMaxVars] = {};
    double g[kNumGlobals] = {};
    g[G_T] = (double)t;
    g[G_SR] = (double)clock;
    g[G_TIME] = (double)t / (double)clock;
    for (int i = 0; i < kNumMacros; ++i)
        g[G_A + i] = macros ? macros[i] : 0.5;

    double l = 0.0, rr = 0.0;
    run(*r.program, g, vars ? vars : localVars, l, rr);
    return l;
}

static void testPrecedence()
{
    CHECK_NEAR(evalMono("1+2*3", 0), 7.0, 1e-12);
    CHECK_NEAR(evalMono("(1+2)*3", 0), 9.0, 1e-12);
    CHECK_NEAR(evalMono("2**3**2", 0), 512.0, 1e-12);
    CHECK_NEAR(evalMono("-2**2", 0), -4.0, 1e-12);
    CHECK_NEAR(evalMono("2**-1", 0), 0.5, 1e-12);
    CHECK_NEAR(evalMono("1<<3", 0), 8.0, 1e-12);
    CHECK_NEAR(evalMono("8>>2", 0), 2.0, 1e-12);
    CHECK_NEAR(evalMono("1|2&3", 0), 3.0, 1e-12);
    CHECK_NEAR(evalMono("1^3&1", 0), 0.0, 1e-12);
}

static void testJsSemantics()
{
    CHECK_NEAR(evalMono("-1>>1", 0), -1.0, 1e-12);
    CHECK_NEAR(evalMono("-1>>>1", 0), 2147483647.0, 1e-12);
    CHECK_NEAR(evalMono("-7%3", 0), -1.0, 1e-12);
    CHECK_NEAR(evalMono("~0", 0), -1.0, 1e-12);
    CHECK(std::isinf(evalMono("1/0", 0)));
    CHECK(std::isnan(evalMono("0/0", 0)));
    CHECK_NEAR(evalMono("1<<33", 0), 2.0, 1e-12);
    CHECK_NEAR(evalMono("1>2", 0), 0.0, 1e-12);
    CHECK_NEAR(evalMono("2>=2", 0), 1.0, 1e-12);
    CHECK_NEAR(evalMono("1==1", 0), 1.0, 1e-12);
    CHECK_NEAR(evalMono("1!=1", 0), 0.0, 1e-12);
}

static void testLogic()
{
    CHECK_NEAR(evalMono("1?2:3", 0), 2.0, 1e-12);
    CHECK_NEAR(evalMono("0?2:3", 0), 3.0, 1e-12);
    CHECK_NEAR(evalMono("0||5", 0), 5.0, 1e-12);
    CHECK_NEAR(evalMono("3||5", 0), 3.0, 1e-12);
    CHECK_NEAR(evalMono("3&&7", 0), 7.0, 1e-12);
    CHECK_NEAR(evalMono("0&&7", 0), 0.0, 1e-12);
    CHECK_NEAR(evalMono("!0", 0), 1.0, 1e-12);
    CHECK_NEAR(evalMono("!3", 0), 0.0, 1e-12);
    CHECK_NEAR(evalMono("1?0||4:9", 0), 4.0, 1e-12);
}

static void testFunctions()
{
    CHECK_NEAR(evalMono("sin(0)", 0), 0.0, 1e-12);
    CHECK_NEAR(evalMono("cos(0)", 0), 1.0, 1e-12);
    CHECK_NEAR(evalMono("floor(1.9)", 0), 1.0, 1e-12);
    CHECK_NEAR(evalMono("floor(-1.1)", 0), -2.0, 1e-12);
    CHECK_NEAR(evalMono("ceil(-1.1)", 0), -1.0, 1e-12);
    CHECK_NEAR(evalMono("int(-1.7)", 0), -1.0, 1e-12);
    CHECK_NEAR(evalMono("trunc(-1.7)", 0), -1.0, 1e-12);
    CHECK_NEAR(evalMono("abs(-3)", 0), 3.0, 1e-12);
    CHECK_NEAR(evalMono("sign(-3)", 0), -1.0, 1e-12);
    CHECK_NEAR(evalMono("min(3,2)", 0), 2.0, 1e-12);
    CHECK_NEAR(evalMono("max(3,2)", 0), 3.0, 1e-12);
    CHECK_NEAR(evalMono("clamp(5,0,3)", 0), 3.0, 1e-12);
    CHECK_NEAR(evalMono("sqrt(9)", 0), 3.0, 1e-12);
    CHECK_NEAR(evalMono("cbrt(27)", 0), 3.0, 1e-12);
    CHECK_NEAR(evalMono("pow(2,10)", 0), 1024.0, 1e-12);
    CHECK_NEAR(evalMono("atan2(1,0)", 0), 3.141592653589793 / 2.0, 1e-12);
    CHECK_NEAR(evalMono("frac(1.25)", 0), 0.25, 1e-12);
    CHECK_NEAR(evalMono("round(1.5)", 0), 2.0, 1e-12);
}

static void testVariables()
{
    CHECK_NEAR(evalMono("t", 1234), 1234.0, 1e-12);
    CHECK_NEAR(evalMono("sr", 0), 8000.0, 1e-12);
    CHECK_NEAR(evalMono("time", 8000), 1.0, 1e-12);
    CHECK_NEAR(evalMono("apple*2+1", 0), 1.0, 1e-12);

    double vars[kMaxVars] = {};
    CHECK_NEAR(evalMono("x=x+1; x", 0, vars), 1.0, 1e-12);
    CHECK_NEAR(evalMono("x=x+1; x", 1, vars), 2.0, 1e-12);
    CHECK_NEAR(evalMono("x+=2; x", 2, vars), 4.0, 1e-12);

    const double macros[kNumMacros] = { 0.5, 1.0, 0.0, 0.25, 0.75, 0.1, 0.9, 0.33 };
    CHECK_NEAR(evalMono("a*100", 0, nullptr, macros), 50.0, 1e-12);
    CHECK_NEAR(evalMono("b*100", 0, nullptr, macros), 100.0, 1e-12);
    CHECK_NEAR(evalMono("c+7", 0, nullptr, macros), 7.0, 1e-12);
}

static void testStereo()
{
    auto r = compile("[1,2]");
    CHECK(r.ok);
    if (!r.ok) return;

    double g[kNumGlobals] = {};
    double vars[kMaxVars] = {};
    double l = 0, rr = 0;
    run(*r.program, g, vars, l, rr);
    CHECK_NEAR(l, 1.0, 1e-12);
    CHECK_NEAR(rr, 2.0, 1e-12);
    CHECK(r.program->stereo);

    auto r2 = compile("[3,4];");
    CHECK(r2.ok);
    if (r2.ok)
    {
        double g[kNumGlobals] = {};
        double vars[kMaxVars] = {};
        double l = 0, rr = 0;
        run(*r2.program, g, vars, l, rr);
        CHECK_NEAR(l, 3.0, 1e-12);
        CHECK_NEAR(rr, 4.0, 1e-12);
    }
}

static void testMeta()
{
    const std::string src =
        "// name: Test Tune\n"
        "// mode: floatbeat\n"
        "// rate: 11025\n"
        "// a: Cutoff\n"
        "// author: Someone\n"
        "t>>4\n";
    const FormulaMeta m = parseMeta(src);
    CHECK(m.name == "Test Tune");
    CHECK(m.mode == "floatbeat");
    CHECK(m.rate == 11025);
    CHECK(m.labels[0] == "Cutoff");
    CHECK(m.author == "Someone");
}

static void testErrors()
{
    Program p;
    CompileError e;

    CHECK(!compileOk("t = 1", p, e));
    CHECK(!e.message.empty());
    e = {};

    CHECK(!compileOk("1 +", p, e));
    CHECK(!e.message.empty());
    e = {};

    CHECK(!compileOk("sin(1,2)", p, e));
    CHECK(!e.message.empty());
    e = {};

    CHECK(!compileOk("[1,2]; 3", p, e));
    CHECK(!e.message.empty());
    e = {};

    CHECK(!compileOk("1 /**/ +", p, e));
    CHECK(!e.message.empty());
    e = {};

    CHECK(!compileOk("/* unterminated", p, e));
    CHECK(!e.message.empty());
    e = {};

    CHECK(!compileOk("", p, e));
    CHECK(!e.message.empty());
    e = {};

    CHECK(!compileOk("foo(1)", p, e));
    CHECK(!e.message.empty());
    e = {};

    CHECK(!compileOk("1 2", p, e));
    CHECK(!e.message.empty());
}

static void testBuiltins()
{
    for (const auto& f : builtinFormulas())
    {
        FormulaMeta meta;
        auto r = compile(f.source, &meta);
        if (!r.ok)
        {
            std::printf("builtin '%s' failed to compile: %s (line %d)\n",
                        f.name, r.error.message.c_str(), r.error.line);
            ++g_failures;
        }
        ++g_checks;
    }
}

static float peakOf(const std::vector<float>& a)
{
    float peak = 0.0f;
    for (float v : a) peak = std::max(peak, std::fabs(v));
    return peak;
}

static void testEngine()
{
    FormulaMeta meta;
    auto r = compile("(t>>4)^(t>>7)", &meta);
    CHECK(r.ok);
    if (!r.ok) return;

    Engine engine;
    Settings s;
    s.program = r.program.get();
    s.playMode = kPlayFree;
    s.outputMode = kOutBytebeat;
    s.run = true;
    s.volume = 0.8f;
    s.hostRate = 44100.0;
    engine.prepare(44100.0);
    engine.setSettings(s);

    std::vector<float> l(1024), rr(1024);
    engine.render(l.data(), rr.data(), 1024);

    bool any = false;
    for (int i = 0; i < 1024; ++i)
    {
        CHECK(std::isfinite(l[i]));
        CHECK(std::isfinite(rr[i]));
        if (std::fabs(l[i]) > 1e-5f) any = true;
    }
    CHECK(any);

    // Free mode keeps its clock, but notes transpose the running voice.
    engine.noteOn(72, 100);
    CHECK(engine.primaryNote() == 72);
    engine.noteOff(72);
    CHECK(engine.primaryNote() == 60);

    // Trigger mode: a note must produce sound and release to silence.
    Settings t = s;
    t.playMode = kPlayTrigger;
    t.voices = 4;
    t.attackMs = 1.0f;
    t.releaseMs = 10.0f;
    engine.setSettings(t);
    engine.noteOn(60, 100);
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(rr.begin(), rr.end(), 0.0f);
    engine.render(l.data(), rr.data(), 1024);
    any = false;
    for (int i = 0; i < 1024; ++i)
        if (std::fabs(l[i]) > 1e-5f) any = true;
    CHECK(any);

    engine.noteOff(60);
    for (int block = 0; block < 32; ++block)
    {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(rr.begin(), rr.end(), 0.0f);
        engine.render(l.data(), rr.data(), 1024);
    }
    any = false;
    for (int i = 0; i < 1024; ++i)
        if (std::fabs(l[i]) > 1e-3f) any = true;
    CHECK(!any);

    // Sync mode must be deterministic for a fixed transport position.
    Settings sy = s;
    sy.playMode = kPlaySync;
    sy.playing = true;
    sy.transportSamples = 44100.0;
    engine.setSettings(sy);
    std::vector<float> a(256), b(256);
    engine.render(a.data(), b.data(), 256);
    float first = a[0];
    engine.reset();
    engine.setSettings(sy);
    std::vector<float> c(256), d(256);
    engine.render(c.data(), d.data(), 256);
    CHECK_NEAR(c[0], first, 1e-6);

    // A stopped transport free-runs instead of going silent.
    Settings stopped = sy;
    stopped.playing = false;
    engine.setSettings(stopped);
    std::vector<float> e(1024), f(1024);
    engine.render(e.data(), f.data(), (int)e.size());
    CHECK(peakOf(e) > 1e-5f);
}

static void testPitchTracking()
{
    auto prog = compile("0.5");
    CHECK(prog.ok);
    if (!prog.ok) return;

    Engine engine;
    Settings s;
    s.program = prog.program.get();
    s.playMode = kPlayFree;
    s.outputMode = kOutFloat;
    s.clockRate = 8000.0;
    s.speed = 1.0;
    s.run = true;
    s.rootNote = 60;
    s.pitchTrack = true;
    s.volume = 0.5f;
    s.hostRate = 44100.0;
    engine.prepare(44100.0);
    engine.setSettings(s);

    std::vector<float> l(1024), r(1024);
    engine.render(l.data(), r.data(), 1024);
    const int64_t base = engine.primaryT();
    engine.render(l.data(), r.data(), 1024);
    const int64_t normalDelta = engine.primaryT() - base;

    const int64_t beforeNote = engine.primaryT();
    engine.noteOn(72, 127); // one octave above root
    engine.render(l.data(), r.data(), 1024);
    const int64_t pitchDelta = engine.primaryT() - beforeNote;

    CHECK(normalDelta > 0);
    CHECK(pitchDelta > normalDelta * 3 / 2);
}

static void testGatedFree()
{
    auto gateProg = compile("0.5");
    CHECK(gateProg.ok);
    if (!gateProg.ok) return;

    Engine engine;
    Settings s;
    s.program = gateProg.program.get();
    s.playMode = kPlayGatedFree;
    s.outputMode = kOutFloat;
    s.clockRate = 8000.0;
    s.speed = 1.0;
    s.run = true;
    s.voices = 1;
    s.rootNote = 60;
    s.pitchTrack = false;
    s.attackMs = 0.0f;
    s.releaseMs = 5.0f;
    s.volume = 1.0f;
    s.dcBlock = false;
    s.smooth = 0.0f;
    s.hostRate = 44100.0;

    engine.prepare(44100.0);
    engine.setSettings(s);

    std::vector<float> l(1024), r(1024);
    engine.render(l.data(), r.data(), (int)l.size());
    CHECK(engine.primaryT() > 0);
    CHECK(peakOf(l) < 1e-5f); // free clock, but no pad is gating it

    const int64_t beforeNote = engine.primaryT();
    engine.noteOn(60, 127);
    CHECK(engine.primaryT() == beforeNote); // gated free never resets t
    engine.render(l.data(), r.data(), (int)l.size());
    CHECK(peakOf(l) > 0.1f);

    // Last-note priority: releasing the newer note falls back to the held one.
    engine.noteOn(64, 100);
    CHECK(engine.primaryNote() == 64);
    engine.noteOff(64);
    CHECK(engine.primaryNote() == 60);

    engine.noteOff(60);
    for (int i = 0; i < 32; ++i)
        engine.render(l.data(), r.data(), (int)l.size());
    CHECK(peakOf(l) < 1e-3f);
    const int64_t silentT = engine.primaryT();
    engine.render(l.data(), r.data(), (int)l.size());
    CHECK(engine.primaryT() > silentT); // the clock keeps running while silent

    // aftertouch is a real formula global, smoothed and available while held.
    auto atProg = compile("aftertouch");
    CHECK(atProg.ok);
    if (!atProg.ok) return;

    Settings at = s;
    at.program = atProg.program.get();
    engine.allNotesOff();
    engine.setSettings(at);
    engine.setChannelPressure(0.0);
    engine.noteOn(60, 127);
    engine.setChannelPressure(1.0);
    for (int i = 0; i < 8; ++i)
        engine.render(l.data(), r.data(), (int)l.size());
    CHECK(peakOf(l) > 0.5f);

    engine.setChannelPressure(0.0);
    for (int i = 0; i < 32; ++i)
        engine.render(l.data(), r.data(), (int)l.size());
    CHECK(peakOf(l) < 0.1f);

    engine.setPolyPressure(60, 1.0);
    for (int i = 0; i < 8; ++i)
        engine.render(l.data(), r.data(), (int)l.size());
    CHECK(peakOf(l) > 0.5f);
}

static void testDrumKits()
{
    int count = 0;
    const bbdrums::KitSpec* kits = bbdrums::factoryKits(count);
    CHECK(count == 6);
    CHECK(bbdrums::kPads == 16);

    const auto& builtins = builtinFormulas();
    for (int k = 0; k < count; ++k)
    {
        const auto& kit = kits[k];
        CHECK(kit.name != nullptr && kit.name[0] != '\0');
        CHECK(kit.pads != nullptr);
        CHECK(kit.masterVolume >= -40.0f && kit.masterVolume <= 6.0f);
        CHECK(kit.tone >= 0.0f && kit.tone <= 100.0f);
        CHECK(kit.drive >= 0.0f && kit.drive <= 100.0f);

        for (int p = 0; p < bbdrums::kPads; ++p)
        {
            const auto& pad = kit.pads[p];
            CHECK(pad.formula >= 0 && pad.formula < (int)builtins.size());
            if (pad.formula >= 0 && pad.formula < (int)builtins.size())
            {
                const auto result = compile(builtins[(size_t)pad.formula].source);
                CHECK(result.ok);
            }
            CHECK(pad.level >= 0.0f && pad.level <= 100.0f);
            CHECK(pad.pan >= -100.0f && pad.pan <= 100.0f);
            CHECK(pad.tune >= -24.0f && pad.tune <= 24.0f);
            CHECK(pad.decay >= 10.0f && pad.decay <= 4000.0f);
            CHECK(pad.choke >= 0 && pad.choke <= 4);
            CHECK(pad.mode == 0 || pad.mode == 1);
            CHECK(pad.a >= 0.0f && pad.a <= 100.0f);
            CHECK(pad.b >= 0.0f && pad.b <= 100.0f);
            CHECK(pad.c >= 0.0f && pad.c <= 100.0f);
            CHECK(pad.d >= 0.0f && pad.d <= 100.0f);
        }
    }
}

int main()
{
    testPrecedence();
    testJsSemantics();
    testLogic();
    testFunctions();
    testVariables();
    testStereo();
    testMeta();
    testErrors();
    testBuiltins();
    testDrumKits();
    testEngine();
    testPitchTracking();
    testGatedFree();

    std::printf("%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
