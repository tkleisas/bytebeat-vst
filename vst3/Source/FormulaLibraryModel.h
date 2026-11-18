// FormulaLibrary.h - built-in and user .bb formulas for the VST3 editor.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep

#pragma once

#include <JuceHeader.h>

#include "BytebeatCompile.h"
#include "FormulaLibrary.h"

#include <algorithm>
#include <vector>

namespace bbvst
{

struct LibraryEntry
{
    juce::String name;
    juce::String category;
    juce::String source;
    int mode = 0;
    int rate = 8000;
    bool user = false;
    juce::File file;
};

class FormulaLibraryModel
{
public:
    FormulaLibraryModel()
    {
        loadBuiltins();
        rescanUserFiles();
    }

    void rescanUserFiles()
    {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                      [](const LibraryEntry& e) { return e.user; }),
                       entries_.end());

        const auto folder = userFolder();
        folder.createDirectory();

        juce::Array<juce::File> files;
        folder.findChildFiles(files, juce::File::findFiles, false, "*.bb");

        for (const auto& f : files)
        {
            LibraryEntry e;
            if (readEntry(f, e))
            {
                e.user = true;
                e.file = f;
                entries_.push_back(std::move(e));
            }
        }

        auto rank = [](const juce::String& category)
        {
            if (category == "Rhythmic") return 0;
            if (category == "Tonal")    return 1;
            if (category == "Floatbeat")return 2;
            if (category == "Signed")   return 3;
            if (category == "Noise")    return 4;
            if (category == "Stateful") return 5;
            return 9;
        };

        std::sort(entries_.begin(), entries_.end(),
                  [&rank](const LibraryEntry& a, const LibraryEntry& b)
                  {
                      if (a.user != b.user) return !a.user; // built-ins first
                      const int ra = rank(a.category);
                      const int rb = rank(b.category);
                      if (ra != rb) return ra < rb;
                      return a.name.compareIgnoreCase(b.name) < 0;
                  });
    }

    const std::vector<LibraryEntry>& entries() const { return entries_; }

    static juce::File userFolder()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("Bytebeat Machine")
                   .getChildFile("Formulas");
    }

    static bool readEntry(const juce::File& file, LibraryEntry& out)
    {
        if (!file.existsAsFile()) return false;
        const auto text = file.loadFileAsString();
        if (text.isEmpty()) return false;

        const auto meta = bytebeat::parseMeta(text.toStdString());
        out.source = text;
        out.name = meta.name.empty() ? file.getFileNameWithoutExtension() : juce::String(meta.name);
        out.category = "User";
        out.mode = modeFromName(meta.mode);
        out.rate = meta.rate > 0 ? meta.rate : 8000;
        return true;
    }

    static juce::String stripDirectives(const juce::String& source)
    {
        auto lines = juce::StringArray::fromLines(source);
        int i = 0;
        while (i < lines.size())
        {
            const auto t = lines[i].trim();
            if (t.isEmpty()) { ++i; continue; }
            if (!t.startsWith("//")) break;

            auto body = t.substring(2).trim();
            if (body.startsWith("!")) body = body.substring(1).trim();
            const auto key = body.upToFirstOccurrenceOf(":", false, false).trim().toLowerCase();
            const bool directive = key == "name" || key == "mode" || key == "rate"
                                || key == "author" || key == "about"
                                || (key.length() == 1 && key[0] >= 'a' && key[0] <= 'h');
            if (!directive) break;
            ++i;
        }

        return i >= lines.size() ? juce::String() : lines.joinIntoString("\n", i);
    }

    static bool saveEntry(const juce::File& target, const juce::String& source,
                          const juce::String& name, int mode, int rate, juce::String& error)
    {
        juce::String header;
        header << "// name: " << name << "\n";
        header << "// mode: " << modeName(mode) << "\n";
        header << "// rate: " << rate << "\n\n";

        if (!target.getParentDirectory().createDirectory())
        {
            error = "Could not create " + target.getParentDirectory().getFullPathName();
            return false;
        }

        if (!target.replaceWithText(header + stripDirectives(source)))
        {
            error = "Could not write " + target.getFullPathName();
            return false;
        }
        return true;
    }

    static juce::String modeName(int mode)
    {
        switch (mode)
        {
            case 1: return "signed";
            case 2: return "floatbeat";
            default: return "bytebeat";
        }
    }

private:
    void loadBuiltins()
    {
        entries_.clear();
        for (const auto& f : bytebeat::builtinFormulas())
        {
            LibraryEntry e;
            e.name = f.name;
            e.category = f.category;
            e.source = f.source;
            e.mode = f.mode;
            e.rate = f.rate;
            e.user = false;
            entries_.push_back(std::move(e));
        }
    }

    static int modeFromName(const std::string& s)
    {
        const auto m = bytebeat::bbLower(s);
        if (m == "signed") return 1;
        if (m == "floatbeat" || m == "float") return 2;
        return 0;
    }

    std::vector<LibraryEntry> entries_;
};

} // namespace bbvst
