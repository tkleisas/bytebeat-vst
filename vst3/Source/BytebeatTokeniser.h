// BytebeatTokeniser.h - syntax highlighting for the formula editor.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <JuceHeader.h>

namespace bbvst
{

class BytebeatTokeniser : public juce::CodeTokeniser
{
public:
    enum TokenType
    {
        Error = 0,
        Comment,
        Keyword,
        Operator,
        Identifier,
        Integer,
        Float,
        Bracket,
        Punctuation,
        Function,
        Macro,
        NumTypes
    };

    BytebeatTokeniser() = default;
    ~BytebeatTokeniser() override = default;

    int readNextToken(juce::CodeDocument::Iterator& source) override
    {
        while (!source.isEOF() && juce::CharacterFunctions::isWhitespace(source.peekNextChar()))
            source.skip();

        if (source.isEOF())
            return Punctuation;

        const juce::juce_wchar c = source.peekNextChar();

        if (c == '/' && peekAt(source, 1) == '/')
        {
            while (!source.isEOF() && source.peekNextChar() != '\n')
                source.skip();
            return Comment;
        }

        if (c == '/' && peekAt(source, 1) == '*')
        {
            source.skip(); source.skip();
            while (!source.isEOF())
            {
                if (source.peekNextChar() == '*' && peekAt(source, 1) == '/')
                {
                    source.skip(); source.skip();
                    return Comment;
                }
                source.skip();
            }
            return Error;
        }

        if (juce::CharacterFunctions::isDigit(c)
            || (c == '.' && juce::CharacterFunctions::isDigit(peekAt(source, 1))))
        {
            bool isFloat = false;
            while (!source.isEOF())
            {
                const auto d = source.peekNextChar();
                if (juce::CharacterFunctions::isDigit(d) || d == 'x' || d == 'X'
                    || d == 'b' || d == 'B'
                    || (d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F'))
                {
                    source.skip();
                }
                else if (d == '.')
                {
                    isFloat = true;
                    source.skip();
                }
                else if (d == 'e' || d == 'E')
                {
                    isFloat = true;
                    source.skip();
                    if (source.peekNextChar() == '+' || source.peekNextChar() == '-')
                        source.skip();
                }
                else
                {
                    break;
                }
            }
            return isFloat ? Float : Integer;
        }

        if (juce::CharacterFunctions::isLetter(c) || c == '_')
        {
            juce::String ident;
            while (!source.isEOF())
            {
                const auto d = source.peekNextChar();
                if (juce::CharacterFunctions::isLetterOrDigit(d) || d == '_')
                {
                    ident << (juce::juce_wchar)source.nextChar();
                }
                else
                {
                    break;
                }
            }

            if (isKeyword(ident)) return Keyword;
            if (isMacro(ident)) return Macro;
            if (source.peekNextChar() == '(') return Function;
            return Identifier;
        }

        if (c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}')
        {
            source.skip();
            return Bracket;
        }

        if (c == ',' || c == ';' || c == ':')
        {
            source.skip();
            return Punctuation;
        }

        // Operators, including multi-character ones.
        const juce::juce_wchar n1 = peekAt(source, 1);
        const juce::juce_wchar n2 = peekAt(source, 2);
        const juce::juce_wchar n3 = peekAt(source, 3);
        juce::juce_wchar len = 1;

        if (c == '>' && n1 == '>' && n2 == '>' && n3 == '=') len = 4;
        else if (c == '>' && n1 == '>' && n2 == '>') len = 3;
        else if ((c == '<' || c == '>' || c == '*' || c == '&' || c == '|' || c == '^')
                 && n1 == c) len = 2;
        else if ((c == '=' || c == '!' || c == '<' || c == '>') && n1 == '=') len = 2;
        else if ((c == '+' || c == '-' || c == '*' || c == '/' || c == '%'
                  || c == '&' || c == '|' || c == '^') && n1 == '=') len = 2;
        else if (c == '&' && n1 == '&') len = 2;
        else if (c == '|' && n1 == '|') len = 2;

        const bool isOp = juce::String("+-*/%&|^~!<>=?").containsChar(c);
        for (int i = 0; i < (int)len; ++i) source.skip();
        return isOp ? Operator : Error;
    }

    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override
    {
        using C = juce::Colour;
        juce::CodeEditorComponent::ColourScheme cs;
        cs.set("Error",      C(0xffff5c5c));
        cs.set("Comment",    C(0xff6a9955));
        cs.set("Keyword",    C(0xffc586c0));
        cs.set("Operator",   C(0xff9b63ff));
        cs.set("Identifier", C(0xffd4d4d4));
        cs.set("Integer",    C(0xffb5cea8));
        cs.set("Float",      C(0xffb5cea8));
        cs.set("Bracket",    C(0xffe0c989));
        cs.set("Punctuation",C(0xffa0a0a0));
        cs.set("Function",   C(0xff4fc1ff));
        cs.set("Macro",      C(0xffffd166));
        return cs;
    }

    static juce::juce_wchar peekAt(const juce::CodeDocument::Iterator& it, int offset)
    {
        if (offset == 0) return it.peekNextChar();
        auto copy = it;
        for (int i = 0; i < offset && !copy.isEOF(); ++i) copy.skip();
        return copy.isEOF() ? 0 : copy.peekNextChar();
    }

    static bool isKeyword(const juce::String& s)
    {
        static const juce::StringArray k = {
            "t", "sr", "sampleRate", "time", "beat", "bar", "tempo", "playing",
            "note", "freq", "vel", "gate", "voice", "PI", "TAU", "E", "pi"
        };
        return k.contains(s);
    }

    static bool isMacro(const juce::String& s)
    {
        return s.length() == 1 && s[0] >= 'a' && s[0] <= 'h';
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BytebeatTokeniser)
};

} // namespace bbvst
