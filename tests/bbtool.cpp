// bbtool.cpp - validate and render .bb formulas without a DAW.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Usage:
//   bbtool list
//   bbtool check <file.bb>
//   bbtool render <file.bb|index> <out.wav> [seconds] [--clock N] [--mode N]

#include "BytebeatCompile.h"
#include "BytebeatEngine.h"
#include "FormulaLibrary.h"

#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace bytebeat;

static std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static int modeFromName(const std::string& sIn)
{
    const std::string s = bbLower(sIn);
    if (s == "signed") return kOutSigned;
    if (s == "floatbeat" || s == "float") return kOutFloat;
    return kOutBytebeat;
}

static void writeWav16(const std::string& path, const std::vector<float>& l,
                       const std::vector<float>& r, int sampleRate)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        return;
    }

    const uint32_t frames = (uint32_t)l.size();
    const uint32_t dataBytes = frames * 2u * 2u;
    const uint32_t riffSize = 36u + dataBytes;

    auto u32 = [&](uint32_t v) { out.write((const char*)&v, 4); };
    auto u16 = [&](uint16_t v) { out.write((const char*)&v, 2); };

    out.write("RIFF", 4);
    u32(riffSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    u32(16);
    u16(1);
    u16(2);
    u32((uint32_t)sampleRate);
    u32((uint32_t)sampleRate * 4u);
    u16(4);
    u16(16);
    out.write("data", 4);
    u32(dataBytes);

    for (uint32_t i = 0; i < frames; ++i)
    {
        const auto conv = [](float x) -> int16_t {
            const float c = std::max(-1.0f, std::min(1.0f, x));
            return (int16_t)std::lround(c * 32767.0f);
        };
        const int16_t a = conv(l[i]);
        const int16_t b = conv(r[i]);
        out.write((const char*)&a, 2);
        out.write((const char*)&b, 2);
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage:\n"
                    "  bbtool list\n"
                    "  bbtool check <file.bb>\n"
                    "  bbtool render <file.bb|index> <out.wav> [seconds] [--clock N] [--mode N]\n");
        return 1;
    }

    const std::string cmd = argv[1];

    if (cmd == "list")
    {
        const auto& bank = builtinFormulas();
        for (size_t i = 0; i < bank.size(); ++i)
        {
            const auto& f = bank[i];
            std::printf("%2zu  %-14s %-9s %5d  %s\n", i, f.name, outputModeName(f.mode), f.rate, f.category);
        }
        return 0;
    }

    if (cmd == "check")
    {
        if (argc < 3) { std::fprintf(stderr, "missing file\n"); return 1; }
        const std::string src = readFile(argv[2]);
        if (src.empty()) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
        FormulaMeta meta;
        auto r = compile(src, &meta);
        std::printf("name:  %s\n", meta.name.empty() ? "(unnamed)" : meta.name.c_str());
        std::printf("mode:  %s\n", meta.mode.empty() ? "bytebeat" : meta.mode.c_str());
        std::printf("rate:  %d\n", meta.rate);
        std::printf("bytes: %zu\n", src.size());
        if (!r.ok)
        {
            std::printf("ERROR line %d, col %d: %s\n", r.error.line, r.error.col, r.error.message.c_str());
            return 2;
        }
        std::printf("OK: %d bytecode ops, %d constants, %d variables, %s\n",
                    r.program->codeSize, r.program->nConsts, r.program->nVars,
                    r.program->stereo ? "stereo" : "mono");
        return 0;
    }

    if (cmd == "render")
    {
        if (argc < 4)
        {
            std::fprintf(stderr, "usage: bbtool render <file.bb|index> <out.wav> [seconds] [--clock N] [--mode N]\n");
            return 1;
        }

        std::string src;
        FormulaMeta meta;

        const std::string arg = argv[2];
        bool isIndex = !arg.empty();
        for (char c : arg) if (!std::isdigit((unsigned char)c)) isIndex = false;

        if (isIndex)
        {
            const int idx = std::atoi(arg.c_str());
            const auto& bank = builtinFormulas();
            if (idx < 0 || idx >= (int)bank.size())
            {
                std::fprintf(stderr, "builtin index out of range\n");
                return 1;
            }
            src = bank[(size_t)idx].source;
            meta.name = bank[(size_t)idx].name;
            meta.mode = outputModeName(bank[(size_t)idx].mode);
            meta.rate = bank[(size_t)idx].rate;
        }
        else
        {
            src = readFile(arg);
            if (src.empty()) { std::fprintf(stderr, "cannot read %s\n", arg.c_str()); return 1; }
            compile(src, &meta);
        }

        int mode = meta.mode.empty() ? kOutBytebeat : modeFromName(meta.mode);
        int clock = meta.rate > 0 ? meta.rate : 8000;
        double seconds = 3.0;

        if (argc >= 5 && argv[4][0] != '-')
            seconds = std::atof(argv[4]);

        for (int i = 4; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--clock") == 0 && i + 1 < argc) clock = std::atoi(argv[++i]);
            else if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) mode = std::atoi(argv[++i]);
        }

        auto r = compile(src, &meta);
        if (!r.ok)
        {
            std::fprintf(stderr, "compile error line %d, col %d: %s\n",
                         r.error.line, r.error.col, r.error.message.c_str());
            return 2;
        }

        const int hostRate = 44100;
        const int frames = (int)(seconds * hostRate);
        std::vector<float> l((size_t)frames), rr((size_t)frames);

        Engine engine;
        Settings s;
        s.program = r.program.get();
        s.playMode = kPlayFree;
        s.run = true;
        s.outputMode = mode;
        s.clockRate = (double)clock;
        s.hostRate = (double)hostRate;
        s.volume = 0.8f;
        s.toneHz = 20000.0f;
        s.dcBlock = true;

        engine.prepare((double)hostRate);
        engine.setSettings(s);
        engine.render(l.data(), rr.data(), frames);

        writeWav16(argv[3], l, rr, hostRate);
        std::printf("wrote %s: %.2f s, %s, %d Hz clock\n", argv[3], seconds,
                    outputModeName(mode), clock);
        return 0;
    }

    std::fprintf(stderr, "unknown command '%s'\n", cmd.c_str());
    return 1;
}
