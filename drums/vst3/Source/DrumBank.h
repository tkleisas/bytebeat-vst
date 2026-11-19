// DrumBank.h - built-in and user .bb formulas for Bytebeat Drums VST3.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep

#pragma once

#include <JuceHeader.h>

#include "BytebeatCompile.h"
#include "FormulaLibrary.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace bbdvst
{

struct DrumEntry
{
    juce::String name;
    juce::String source;
    int mode = 0;
    int rate = 8000;
    std::unique_ptr<bytebeat::Program> program;
    bool ok = false;
    int errorLine = 0;
};

class DrumBank
{
public:
    DrumBank()
    {
        build();
    }

    const std::vector<DrumEntry>& entries() const { return entries_; }

    std::vector<juce::String> names() const
    {
        std::vector<juce::String> out;
        out.reserve(entries_.size());
        for (const auto& e : entries_)
            out.push_back(e.name);
        return out;
    }

    const DrumEntry* entry(int index) const
    {
        if (index >= 0 && index < (int)entries_.size())
            return &entries_[(size_t)index];
        return nullptr;
    }

    const bytebeat::Program* program(int index) const
    {
        if (const auto* e = entry(index))
            if (e->ok && e->program)
                return e->program.get();
        return fallback_.ok ? fallback_.program.get() : nullptr;
    }

    const bytebeat::Program* fallbackProgram() const
    {
        return fallback_.ok ? fallback_.program.get() : nullptr;
    }

    static juce::File userFolder()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("Bytebeat Drums")
                   .getChildFile("Formulas");
    }

private:
    static int modeFromName(const std::string& s)
    {
        const auto m = bytebeat::bbLower(s);
        if (m == "signed") return bytebeat::kOutSigned;
        if (m == "floatbeat" || m == "float") return bytebeat::kOutFloat;
        return bytebeat::kOutBytebeat;
    }

    static void compileEntry(DrumEntry& e)
    {
        bytebeat::FormulaMeta meta;
        auto result = bytebeat::compile(e.source.toStdString(), &meta);
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

    void build()
    {
        entries_.clear();

        for (const auto& f : bytebeat::builtinFormulas())
        {
            DrumEntry e;
            e.name = f.name;
            e.source = f.source;
            e.mode = f.mode;
            e.rate = f.rate;
            compileEntry(e);
            entries_.push_back(std::move(e));
        }

        const auto folder = userFolder();
        folder.createDirectory();
        juce::Array<juce::File> files;
        folder.findChildFiles(files, juce::File::findFiles, false, "*.bb");
        std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
        {
            return a.getFileName().compareIgnoreCase(b.getFileName()) < 0;
        });
        for (const auto& file : files)
        {
            const auto text = file.loadFileAsString();
            if (text.isEmpty()) continue;

            DrumEntry e;
            e.name = file.getFileNameWithoutExtension();
            e.source = text;
            compileEntry(e);
            entries_.push_back(std::move(e));
        }

        fallback_.name = "Fallback";
        fallback_.source = "t>>4";
        fallback_.mode = bytebeat::kOutBytebeat;
        fallback_.rate = 8000;
        compileEntry(fallback_);
    }

    std::vector<DrumEntry> entries_;
    DrumEntry fallback_;
};

} // namespace bbdvst
