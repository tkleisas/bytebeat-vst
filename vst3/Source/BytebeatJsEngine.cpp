// BytebeatJsEngine.cpp - QuickJS evaluator for the VST3 build.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep

#include "BytebeatJsEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace bytebeat;

namespace bbvst
{

namespace
{

constexpr int kMaxScriptVoices = 8;
constexpr size_t kMemoryLimit = 64u * 1024u * 1024u;
constexpr size_t kStackSize = 1u * 1024u * 1024u;
constexpr auto kInterruptBudget = std::chrono::microseconds(1500);

const char* kPrelude = R"JS(
globalThis.sin = Math.sin;
globalThis.cos = Math.cos;
globalThis.tan = Math.tan;
globalThis.asin = Math.asin;
globalThis.acos = Math.acos;
globalThis.atan = Math.atan;
globalThis.atan2 = Math.atan2;
globalThis.sinh = Math.sinh;
globalThis.cosh = Math.cosh;
globalThis.tanh = Math.tanh;
globalThis.floor = Math.floor;
globalThis.ceil = Math.ceil;
globalThis.round = Math.round;
globalThis.trunc = Math.trunc;
globalThis.int = Math.trunc;
globalThis.abs = Math.abs;
globalThis.sign = Math.sign;
globalThis.sqrt = Math.sqrt;
globalThis.cbrt = Math.cbrt;
globalThis.exp = Math.exp;
globalThis.log = Math.log;
globalThis.log2 = Math.log2;
globalThis.log10 = Math.log10;
globalThis.pow = Math.pow;
globalThis.min = Math.min;
globalThis.max = Math.max;
globalThis.hypot = Math.hypot;
globalThis.random = Math.random;
globalThis.frac = (x) => x - Math.trunc(x);
globalThis.clamp = (x, lo, hi) => Math.min(Math.max(x, lo), hi);
globalThis.PI = Math.PI;
globalThis.TAU = Math.PI * 2;
globalThis.E = Math.E;
)JS";

const char* kParams = "t,a,b,c,d,e,f,g,h,sr,time,beat,bar,tempo,playing,note,freq,vel,gate,voice";

std::string wrapSource(const std::string& source, int mode)
{
    std::string wrapped;
    if (mode == JsEvaluator::kExpression)
    {
        wrapped.reserve(source.size() + 160);
        wrapped += "(function(";
        wrapped += kParams;
        wrapped += "){return (";
        wrapped += source;
        wrapped += "\n);})";
    }
    else if (mode == JsEvaluator::kFunctionBody)
    {
        wrapped.reserve(source.size() + 140);
        wrapped += "(function(";
        wrapped += kParams;
        wrapped += "){\n";
        wrapped += source;
        wrapped += "\n})";
    }
    else
    {
        wrapped.reserve(source.size() + 80);
        wrapped += "(function(){\n";
        wrapped += source;
        wrapped += "\n})()";
    }
    return wrapped;
}

} // namespace

const char* JsEvaluator::modeName(int mode)
{
    switch (mode)
    {
        case kFunctionBody: return "Function";
        case kScript:       return "Script";
        default:            return "Expression";
    }
}

std::unique_ptr<JsEvaluator> JsEvaluator::create(const std::string& source, int mode,
                                                 int numVoices, std::string& error)
{
    auto evaluator = std::unique_ptr<JsEvaluator>(new JsEvaluator());
    if (!evaluator->initialize(source, mode, numVoices, error))
        return nullptr;
    return evaluator;
}

JsEvaluator::~JsEvaluator()
{
    for (int i = 0; i < kMaxScriptVoices; ++i)
    {
        if (ctx_[i] != nullptr)
        {
            if (hasFn_[(size_t)i])
                JS_FreeValue(ctx_[i], fn_[(size_t)i]);
            JS_FreeContext(ctx_[i]);
        }
    }
    if (runtime_ != nullptr)
        JS_FreeRuntime(runtime_);
}

namespace
{
bool isNumericResult(JSContext* ctx, JSValueConst value)
{
    if (JS_IsNumber(value))
        return true;
    if (JS_IsArray(value))
    {
        JSValue first = JS_GetPropertyUint32(ctx, value, 0);
        const bool ok = JS_IsNumber(first);
        JS_FreeValue(ctx, first);
        return ok;
    }
    return false;
}
} // namespace

bool JsEvaluator::initialize(const std::string& source, int mode, int numVoices,
                             std::string& error)
{
    if (source.empty())
    {
        error = "The JavaScript formula is empty.";
        return false;
    }
    if (mode < kExpression || mode > kScript)
        mode = kExpression;

    numVoices_ = std::min(std::max(numVoices, 1), kMaxScriptVoices);

    runtime_ = JS_NewRuntime();
    if (runtime_ == nullptr)
    {
        error = "Could not create the QuickJS runtime.";
        return false;
    }

    JS_SetMemoryLimit(runtime_, kMemoryLimit);
    JS_SetMaxStackSize(runtime_, kStackSize);
    JS_SetCanBlock(runtime_, false);
    JS_SetInterruptHandler(runtime_, &JsEvaluator::interruptCallback, this);
    deadline_ = std::chrono::steady_clock::now() + std::chrono::hours(1);

    // Compile and smoke-test the source once in a throwaway context. This
    // catches Function mode without a return and formulas that throw at t=0,
    // without polluting the persistent state of the real voice contexts.
    {
        JSContext* probe = JS_NewContext(runtime_);
        if (probe == nullptr)
        {
            error = "Could not create a QuickJS probe context.";
            return false;
        }

        if (!installPrelude(probe, error))
        {
            JS_FreeContext(probe);
            return false;
        }

        JSValue fn = JS_UNDEFINED;
        if (!buildFunction(probe, source, mode, fn, error))
        {
            JS_FreeContext(probe);
            return false;
        }

        if (!JS_IsFunction(probe, fn))
        {
            JS_FreeValue(probe, fn);
            JS_FreeContext(probe);
            error = "The JavaScript source did not produce a function.";
            return false;
        }

        JSValue args[20];
        args[0] = JS_NewFloat64(probe, 0.0);
        for (int k = 0; k < kNumMacros; ++k)
            args[1 + k] = JS_NewFloat64(probe, 0.5);
        args[9]  = JS_NewFloat64(probe, 8000.0);
        args[10] = JS_NewFloat64(probe, 0.0);
        args[11] = JS_NewFloat64(probe, 0.0);
        args[12] = JS_NewFloat64(probe, 0.0);
        args[13] = JS_NewFloat64(probe, 120.0);
        args[14] = JS_NewFloat64(probe, 1.0);
        args[15] = JS_NewFloat64(probe, 60.0);
        args[16] = JS_NewFloat64(probe, 261.6255653);
        args[17] = JS_NewFloat64(probe, 0.8);
        args[18] = JS_NewFloat64(probe, 1.0);
        args[19] = JS_NewFloat64(probe, 0.0);

        JSValue sample = JS_Call(probe, fn, JS_UNDEFINED, 20, args);
        if (JS_IsException(sample))
        {
            error = "JavaScript error at t=0: " + exceptionText(probe);
            JS_FreeValue(probe, sample);
            JS_FreeValue(probe, fn);
            JS_FreeContext(probe);
            return false;
        }
        if (!isNumericResult(probe, sample))
        {
            error = "The formula did not return a number or [left, right] at t=0.";
            JS_FreeValue(probe, sample);
            JS_FreeValue(probe, fn);
            JS_FreeContext(probe);
            return false;
        }
        JS_FreeValue(probe, sample);
        JS_FreeValue(probe, fn);
        JS_FreeContext(probe);
    }

    for (int i = 0; i < numVoices_; ++i)
    {
        JSContext* ctx = JS_NewContext(runtime_);
        if (ctx == nullptr)
        {
            error = "Could not create a QuickJS context.";
            return false;
        }
        ctx_[(size_t)i] = ctx;

        if (!installPrelude(ctx, error))
            return false;

        JSValue fn = JS_UNDEFINED;
        if (!buildFunction(ctx, source, mode, fn, error))
            return false;

        if (!JS_IsFunction(ctx, fn))
        {
            JS_FreeValue(ctx, fn);
            error = "The JavaScript source did not produce a function.";
            return false;
        }

        fn_[(size_t)i] = fn;
        hasFn_[(size_t)i] = true;
    }

    return true;
}

bool JsEvaluator::installPrelude(JSContext* ctx, std::string& error)
{
    JSValue result = JS_Eval(ctx, kPrelude, std::strlen(kPrelude), "<bytebeat prelude>",
                             JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        error = "JavaScript prelude error: " + exceptionText(ctx);
        JS_FreeValue(ctx, result);
        return false;
    }
    JS_FreeValue(ctx, result);
    return true;
}

bool JsEvaluator::buildFunction(JSContext* ctx, const std::string& source, int mode,
                                JSValue& out, std::string& error)
{
    const std::string wrapped = wrapSource(source, mode);
    JSValue result = JS_Eval(ctx, wrapped.c_str(), wrapped.size(), "<bytebeat formula>",
                             JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        error = exceptionText(ctx);
        JS_FreeValue(ctx, result);
        return false;
    }
    out = result;
    return true;
}

std::string JsEvaluator::exceptionText(JSContext* ctx)
{
    JSValue exception = JS_GetException(ctx);
    const char* text = JS_ToCString(ctx, exception);
    std::string out = text != nullptr ? text : "JavaScript error";
    if (text != nullptr)
        JS_FreeCString(ctx, text);
    JS_FreeValue(ctx, exception);
    return out;
}

int JsEvaluator::interruptCallback(JSContext*, void* opaque)
{
    auto* self = static_cast<JsEvaluator*>(opaque);
    return std::chrono::steady_clock::now() > self->deadline_ ? 1 : 0;
}

void JsEvaluator::evaluate(int voice, const double* globals, double* state,
                           double& left, double& right)
{
    (void)state; // JavaScript state lives in the per-voice context
    left = 0.0;
    right = 0.0;

    const int v = (voice >= 0 && voice < numVoices_) ? voice : 0;
    JSContext* ctx = ctx_[(size_t)v];
    if (ctx == nullptr || !hasFn_[(size_t)v])
        return;

    // The native VM never allocates on the audio thread. QuickJS can; this is
    // the documented trade-off of the JavaScript engine. The interrupt budget
    // keeps a runaway loop from blocking the audio callback indefinitely.
    deadline_ = std::chrono::steady_clock::now() + kInterruptBudget;

    // Keep this argument order in sync with kParams:
    // t, a..h, sr, time, beat, bar, tempo, playing, note, freq, vel, gate, voice.
    JSValue args[20];
    args[0] = JS_NewFloat64(ctx, globals[G_T]);
    for (int i = 0; i < kNumMacros; ++i)
        args[1 + i] = JS_NewFloat64(ctx, globals[G_A + i]);
    args[9]  = JS_NewFloat64(ctx, globals[G_SR]);
    args[10] = JS_NewFloat64(ctx, globals[G_TIME]);
    args[11] = JS_NewFloat64(ctx, globals[G_BEAT]);
    args[12] = JS_NewFloat64(ctx, globals[G_BAR]);
    args[13] = JS_NewFloat64(ctx, globals[G_TEMPO]);
    args[14] = JS_NewFloat64(ctx, globals[G_PLAYING]);
    args[15] = JS_NewFloat64(ctx, globals[G_NOTE]);
    args[16] = JS_NewFloat64(ctx, globals[G_FREQ]);
    args[17] = JS_NewFloat64(ctx, globals[G_VEL]);
    args[18] = JS_NewFloat64(ctx, globals[G_GATE]);
    args[19] = JS_NewFloat64(ctx, globals[G_VOICE]);

    JSValue result = JS_Call(ctx, fn_[(size_t)v], JS_UNDEFINED, 20, args);
    if (JS_IsException(result))
    {
        runtimeError_.store(true, std::memory_order_relaxed);
        JSValue exception = JS_GetException(ctx);
        JS_FreeValue(ctx, exception);
        JS_FreeValue(ctx, result);
        return;
    }

    if (JS_IsArray(result))
    {
        JSValue lv = JS_GetPropertyUint32(ctx, result, 0);
        JSValue rv = JS_GetPropertyUint32(ctx, result, 1);
        double l = 0.0, r = 0.0;

        if (JS_ToFloat64(ctx, &l, lv) < 0)
        {
            JS_FreeValue(ctx, JS_GetException(ctx));
            l = 0.0;
        }

        if (JS_IsUndefined(rv))
            r = l;
        else if (JS_ToFloat64(ctx, &r, rv) < 0)
        {
            JS_FreeValue(ctx, JS_GetException(ctx));
            r = l;
        }

        JS_FreeValue(ctx, lv);
        JS_FreeValue(ctx, rv);
        JS_FreeValue(ctx, result);
        left = std::isfinite(l) ? l : 0.0;
        right = std::isfinite(r) ? r : 0.0;
        return;
    }

    double value = 0.0;
    if (JS_ToFloat64(ctx, &value, result) < 0)
    {
        JS_FreeValue(ctx, JS_GetException(ctx));
        value = 0.0;
    }
    JS_FreeValue(ctx, result);

    if (!std::isfinite(value))
        value = 0.0;
    left = value;
    right = value;
}

} // namespace bbvst
