// js_engine_tests.cpp - tests for the VST3 QuickJS evaluator.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BytebeatJsEngine.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace bbvst;
using namespace bytebeat;

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_NEAR(a, b, eps) do { ++g_checks; const double _a = (a), _b = (b); \
    if (!(std::fabs(_a - _b) <= (eps))) { ++g_failures; \
    std::printf("FAIL %s:%d: %s = %.12g, expected %.12g\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

static std::unique_ptr<JsEvaluator> make(const std::string& source, int mode = JsEvaluator::kExpression,
                                         int voices = 1)
{
    std::string error;
    auto e = JsEvaluator::create(source, mode, voices, error);
    if (e == nullptr)
        std::printf("JS compile failed: %s\n", error.c_str());
    return e;
}

static void eval(JsEvaluator& e, double t, double a, double& left, double& right, int voice = 0)
{
    double g[kNumGlobals] = {};
    g[G_T] = t;
    g[G_SR] = 8000.0;
    g[G_TIME] = t / 8000.0;
    g[G_A] = a;
    g[G_B] = 0.25;
    g[G_NOTE] = 60.0;
    g[G_FREQ] = 261.6255653;
    g[G_VEL] = 0.8;
    g[G_GATE] = 1.0;
    g[G_VOICE] = voice;
    g[G_AT] = 0.25;
    double state[kMaxVars] = {};
    e.evaluate(voice, g, state, left, right);
}

static void testExpression()
{
    auto e = make("t+1");
    CHECK(e != nullptr);
    if (!e) return;
    double l = 0, r = 0;
    eval(*e, 41, 0.5, l, r);
    CHECK_NEAR(l, 42.0, 1e-9);
    CHECK_NEAR(r, 42.0, 1e-9);

    auto s = make("[t, t+1]");
    CHECK(s != nullptr);
    if (!s) return;
    eval(*s, 7, 0.5, l, r);
    CHECK_NEAR(l, 7.0, 1e-9);
    CHECK_NEAR(r, 8.0, 1e-9);

    auto m = make("a*100 + b*10");
    CHECK(m != nullptr);
    if (!m) return;

    auto at = make("aftertouch*100");
    CHECK(at != nullptr);
    if (at)
    {
        eval(*at, 0, 0.5, l, r);
        CHECK_NEAR(l, 25.0, 1e-9);
    }
    eval(*m, 0, 0.5, l, r);
    CHECK_NEAR(l, 52.5, 1e-9);

    auto math = make("sin(t/20) + int(3.9) + PI");
    CHECK(math != nullptr);
    if (!math) return;
    eval(*math, 0, 0.5, l, r);
    CHECK_NEAR(l, 0.0 + 3.0 + 3.141592653589793, 1e-9);
}

static void testFunctionBody()
{
    auto e = make("x = (typeof x === 'undefined' ? 0 : x) + 1; return x;",
                  JsEvaluator::kFunctionBody);
    CHECK(e != nullptr);
    if (!e) return;
    double l = 0, r = 0;
    eval(*e, 0, 0.5, l, r);
    CHECK_NEAR(l, 1.0, 1e-9);
    eval(*e, 1, 0.5, l, r);
    CHECK_NEAR(l, 2.0, 1e-9);
    eval(*e, 2, 0.5, l, r);
    CHECK_NEAR(l, 3.0, 1e-9);
}

static void testScript()
{
    auto e = make("const f = (t, a, b) => t * 2 + a * 100 + b; return f;",
                  JsEvaluator::kScript);
    CHECK(e != nullptr);
    if (!e) return;
    double l = 0, r = 0;
    eval(*e, 3, 0.5, l, r);
    CHECK_NEAR(l, 6.0 + 50.0 + 0.25, 1e-9);

    std::string scriptError;
    auto err = JsEvaluator::create("const x = 1; 42;", JsEvaluator::kScript, 1, scriptError);
    CHECK(err == nullptr);
}

static void testErrors()
{
    std::string error;
    auto bad = JsEvaluator::create("sin(", JsEvaluator::kExpression, 1, error);
    CHECK(bad == nullptr);
    CHECK(!error.empty());

    auto runtime = make("return t > 0 ? null.x : 0;", JsEvaluator::kFunctionBody);
    CHECK(runtime != nullptr);
    if (!runtime) return;
    double l = 1, r = 1;
    eval(*runtime, 1, 0.5, l, r);
    CHECK_NEAR(l, 0.0, 1e-9);
    CHECK(runtime->runtimeError());
    runtime->clearRuntimeError();
    CHECK(!runtime->runtimeError());
}

static void testVoices()
{
    auto e = make("x = (typeof x === 'undefined' ? 0 : x) + 1; return x + voice*100;",
                  JsEvaluator::kFunctionBody, 8);
    CHECK(e != nullptr);
    if (!e) return;
    CHECK(e->numVoices() == 8);

    double l = 0, r = 0;
    eval(*e, 0, 0.5, l, r, 0);
    CHECK_NEAR(l, 1.0, 1e-9);
    eval(*e, 0, 0.5, l, r, 1);
    CHECK_NEAR(l, 101.0, 1e-9);
    eval(*e, 0, 0.5, l, r, 0);
    CHECK_NEAR(l, 2.0, 1e-9);
}

static void testPerformance()
{
    auto e = make("sin(t/20) + (a*100)", JsEvaluator::kExpression, 8);
    CHECK(e != nullptr);
    if (!e) return;

    const int callsPerVoice = 8000;
    double l = 0, r = 0, sum = 0;
    for (int v = 0; v < 8; ++v)
    {
        for (int t = 0; t < callsPerVoice; ++t)
        {
            eval(*e, (double)t, 0.5, l, r, v);
            sum += l;
        }
    }
    CHECK(std::isfinite(sum));
}

int main()
{
    testExpression();
    testFunctionBody();
    testScript();
    testErrors();
    testVoices();
    testPerformance();

    std::printf("%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
