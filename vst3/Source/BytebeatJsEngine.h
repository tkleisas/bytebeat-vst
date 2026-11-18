// BytebeatJsEngine.h - optional QuickJS evaluator for the VST3 build.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep
//
// This is deliberately VST3-only. QuickJS allocates and may run a garbage
// collector, so it is not used on the MPC; the MPC keeps the allocation-free
// native bytecode VM. The audio thread calls into a precompiled function once
// per clock tick, with a small interrupt budget to protect against runaway JS.

#pragma once

#include <quickjs.h>

#include "BytebeatEngine.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace bbvst
{

class JsEvaluator : public bytebeat::Evaluator
{
public:
    enum Mode
    {
        kExpression = 0,   // the source is an expression: return (source)
        kFunctionBody = 1, // the source is a function body with return statements
        kScript = 2        // the source is a script that returns a function
    };

    static std::unique_ptr<JsEvaluator> create(const std::string& source, int mode,
                                               int numVoices, std::string& error);

    ~JsEvaluator() override;

    void evaluate(int voice, const double* globals, double* state,
                  double& left, double& right) override;

    bool runtimeError() const { return runtimeError_.load(std::memory_order_relaxed); }
    void clearRuntimeError() { runtimeError_.store(false, std::memory_order_relaxed); }

    int numVoices() const { return numVoices_; }
    static const char* modeName(int mode);

private:
    JsEvaluator() = default;

    bool initialize(const std::string& source, int mode, int numVoices, std::string& error);
    bool installPrelude(JSContext* ctx, std::string& error);
    bool buildFunction(JSContext* ctx, const std::string& source, int mode,
                       JSValue& out, std::string& error);
    static std::string exceptionText(JSContext* ctx);
    static int interruptCallback(JSContext* ctx, void* opaque);

    JSRuntime* runtime_ = nullptr;
    std::array<JSContext*, 8> ctx_ {};
    std::array<JSValue, 8> fn_ {};
    std::array<bool, 8> hasFn_ {};
    int numVoices_ = 1;

    std::atomic<bool> runtimeError_ { false };
    std::chrono::steady_clock::time_point deadline_;
};

} // namespace bbvst
