// BytebeatVM.h - real-time bytecode interpreter for a compiled formula.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep
//
// Numbers are doubles, bitwise operators follow JavaScript ToInt32 semantics
// (shifts mask the count by 31, >>> is unsigned). The interpreter performs no
// allocation and has no locks.

#ifndef BYTEBEAT_VM_H
#define BYTEBEAT_VM_H

#include "BytebeatCompile.h"

#include <cmath>
#include <cstdint>

namespace bytebeat
{

inline int32_t toInt32(double d)
{
    if (!std::isfinite(d) || d == 0.0) return 0;
    double t = std::trunc(d);
    t = std::fmod(t, 4294967296.0);
    if (t < 0.0) t += 4294967296.0;
    return (int32_t)(uint32_t)t;
}

inline uint32_t toUint32(double d)
{
    if (!std::isfinite(d) || d == 0.0) return 0;
    double t = std::trunc(d);
    t = std::fmod(t, 4294967296.0);
    if (t < 0.0) t += 4294967296.0;
    return (uint32_t)t;
}

inline bool truthy(double d)
{
    return d != 0.0 && !std::isnan(d);
}

inline double jsRound(double x)
{
    if (!std::isfinite(x)) return x;
    return std::floor(x + 0.5);
}

inline void run(const Program& program,
                const double* globals,
                double* vars,
                double& outL,
                double& outR)
{
    const Instruction* code = program.code;
    double stack[kMaxStack];
    int sp = 0;
    int pc = 0;
    bool stereoSet = false;

    outL = 0.0;
    outR = 0.0;

    for (;;)
    {
        const Instruction& in = code[pc++];

        switch (in.op)
        {
            case OP_END:
                if (!stereoSet)
                {
                    const double v = (sp > 0) ? stack[sp - 1] : 0.0;
                    outL = v;
                    outR = v;
                }
                return;

            case OP_PUSH_CONST:  stack[sp++] = program.consts[in.arg]; break;
            case OP_PUSH_GLOBAL: stack[sp++] = globals[in.arg]; break;
            case OP_PUSH_VAR:    stack[sp++] = vars[in.arg]; break;
            case OP_STORE_VAR:   vars[in.arg] = stack[sp - 1]; break;
            case OP_POP:         if (sp > 0) --sp; break;
            case OP_DUP:         stack[sp] = stack[sp - 1]; ++sp; break;

            case OP_ADD: stack[sp - 2] = stack[sp - 2] + stack[sp - 1]; --sp; break;
            case OP_SUB: stack[sp - 2] = stack[sp - 2] - stack[sp - 1]; --sp; break;
            case OP_MUL: stack[sp - 2] = stack[sp - 2] * stack[sp - 1]; --sp; break;
            case OP_DIV: stack[sp - 2] = stack[sp - 2] / stack[sp - 1]; --sp; break;
            case OP_MOD: stack[sp - 2] = std::fmod(stack[sp - 2], stack[sp - 1]); --sp; break;
            case OP_POW: stack[sp - 2] = std::pow(stack[sp - 2], stack[sp - 1]); --sp; break;

            case OP_BAND: stack[sp - 2] = (double)(toInt32(stack[sp - 2]) & toInt32(stack[sp - 1])); --sp; break;
            case OP_BOR:  stack[sp - 2] = (double)(toInt32(stack[sp - 2]) | toInt32(stack[sp - 1])); --sp; break;
            case OP_BXOR: stack[sp - 2] = (double)(toInt32(stack[sp - 2]) ^ toInt32(stack[sp - 1])); --sp; break;

            case OP_SHL:
            {
                const int shift = (int)(toUint32(stack[sp - 1]) & 31u);
                const uint32_t a = (uint32_t)toInt32(stack[sp - 2]);
                stack[sp - 2] = (double)(int32_t)(a << shift);
                --sp;
                break;
            }
            case OP_SHR:
            {
                const int shift = (int)(toUint32(stack[sp - 1]) & 31u);
                const int32_t a = toInt32(stack[sp - 2]);
                stack[sp - 2] = (double)(a >> shift);
                --sp;
                break;
            }
            case OP_USHR:
            {
                const int shift = (int)(toUint32(stack[sp - 1]) & 31u);
                const uint32_t a = (uint32_t)toInt32(stack[sp - 2]);
                stack[sp - 2] = (double)(a >> shift);
                --sp;
                break;
            }

            case OP_NEG:  stack[sp - 1] = -stack[sp - 1]; break;
            case OP_POS:  break;
            case OP_LNOT: stack[sp - 1] = truthy(stack[sp - 1]) ? 0.0 : 1.0; break;
            case OP_BNOT: stack[sp - 1] = (double)(~toInt32(stack[sp - 1])); break;

            case OP_EQ: stack[sp - 2] = (stack[sp - 2] == stack[sp - 1]) ? 1.0 : 0.0; --sp; break;
            case OP_NE: stack[sp - 2] = (stack[sp - 2] != stack[sp - 1]) ? 1.0 : 0.0; --sp; break;
            case OP_LT: stack[sp - 2] = (stack[sp - 2] <  stack[sp - 1]) ? 1.0 : 0.0; --sp; break;
            case OP_LE: stack[sp - 2] = (stack[sp - 2] <= stack[sp - 1]) ? 1.0 : 0.0; --sp; break;
            case OP_GT: stack[sp - 2] = (stack[sp - 2] >  stack[sp - 1]) ? 1.0 : 0.0; --sp; break;
            case OP_GE: stack[sp - 2] = (stack[sp - 2] >= stack[sp - 1]) ? 1.0 : 0.0; --sp; break;

            case OP_JMP: pc = (int)in.arg; break;
            case OP_JZ:
                if (!truthy(stack[--sp])) pc = (int)in.arg;
                break;
            case OP_JNZ:
                if (truthy(stack[--sp])) pc = (int)in.arg;
                break;

            case OP_CALL:
            {
                const int argc = (int)in.arg;
                const double* a = &stack[sp - argc];
                double r = 0.0;

                switch (in.func)
                {
                    case F_SIN:   r = std::sin(a[0]); break;
                    case F_COS:   r = std::cos(a[0]); break;
                    case F_TAN:   r = std::tan(a[0]); break;
                    case F_ASIN:  r = std::asin(a[0]); break;
                    case F_ACOS:  r = std::acos(a[0]); break;
                    case F_ATAN:  r = std::atan(a[0]); break;
                    case F_ATAN2: r = std::atan2(a[0], a[1]); break;
                    case F_SINH:  r = std::sinh(a[0]); break;
                    case F_COSH:  r = std::cosh(a[0]); break;
                    case F_TANH:  r = std::tanh(a[0]); break;
                    case F_FLOOR: r = std::floor(a[0]); break;
                    case F_CEIL:  r = std::ceil(a[0]); break;
                    case F_ROUND: r = jsRound(a[0]); break;
                    case F_TRUNC: r = std::trunc(a[0]); break;
                    case F_INT:   r = std::trunc(a[0]); break;
                    case F_ABS:   r = std::fabs(a[0]); break;
                    case F_SIGN:  r = std::isnan(a[0]) ? 0.0 : (a[0] > 0.0 ? 1.0 : (a[0] < 0.0 ? -1.0 : 0.0)); break;
                    case F_SQRT:  r = std::sqrt(a[0]); break;
                    case F_CBRT:  r = std::cbrt(a[0]); break;
                    case F_EXP:   r = std::exp(a[0]); break;
                    case F_LOG:   r = std::log(a[0]); break;
                    case F_LOG2:  r = std::log2(a[0]); break;
                    case F_LOG10: r = std::log10(a[0]); break;
                    case F_POW:   r = std::pow(a[0], a[1]); break;
                    case F_MIN:   r = (a[0] < a[1]) ? a[0] : a[1]; break;
                    case F_MAX:   r = (a[0] > a[1]) ? a[0] : a[1]; break;
                    case F_CLAMP: r = std::fmin(std::fmax(a[0], a[1]), a[2]); break;
                    case F_HYPOT: r = std::hypot(a[0], a[1]); break;
                    case F_FRAC:  r = a[0] - std::trunc(a[0]); break;
                    default:      r = 0.0; break;
                }

                sp -= argc;
                stack[sp++] = r;
                break;
            }

            case OP_STEREO:
                outR = stack[--sp];
                outL = stack[--sp];
                stereoSet = true;
                break;

            default:
                return;
        }
    }
}

} // namespace bytebeat

#endif // BYTEBEAT_VM_H
