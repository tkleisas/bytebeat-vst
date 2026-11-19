// BytebeatCompile.h - formula lexer, parser and bytecode compiler.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep
//
// The dialect is deliberately small and JS-like so classic bytebeat one-liners
// work unchanged:
//   t, sr, time, beat, bar, tempo, playing, note, freq, vel, gate, voice
//   a..h macros (0..1), PI, TAU, E
//   sin cos tan asin acos atan atan2 floor ceil round trunc int abs sign
//   min max pow sqrt cbrt exp log log2 log10 hypot clamp frac
//   + - * / % ** & | ^ ~ << >> >>> && || ! == != < <= > >= ?:
//   persistent per-voice variables x, y, z, s0..sN (any identifier)
//   final expression [left, right] for stereo
//
// Compilation happens on the message thread. The resulting Program is a fixed
// block of memory that the audio thread only reads.

#ifndef BYTEBEAT_COMPILE_H
#define BYTEBEAT_COMPILE_H

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace bytebeat
{

constexpr int kMaxCode        = 1024;
constexpr int kMaxConsts      = 64;
constexpr int kMaxVars        = 16;
constexpr int kMaxStack       = 256;
constexpr int kMaxSource      = 8192;
constexpr int kNumMacros      = 8;
constexpr int kNumGlobals     = 32;
constexpr int kMaxNest        = 96;

enum GlobalIndex : int
{
    G_T = 0,
    G_SR,
    G_TIME,
    G_BEAT,
    G_BAR,
    G_TEMPO,
    G_PLAYING,
    G_NOTE,
    G_FREQ,
    G_VEL,
    G_GATE,
    G_VOICE,
    G_A,
    G_B = G_A + 1,
    G_C = G_A + 2,
    G_D = G_A + 3,
    G_E = G_A + 4,
    G_F = G_A + 5,
    G_G = G_A + 6,
    G_H = G_A + 7,
    G_AT,
    G_COUNT
};

enum FuncId : int
{
    F_SIN = 0,
    F_COS,
    F_TAN,
    F_ASIN,
    F_ACOS,
    F_ATAN,
    F_ATAN2,
    F_SINH,
    F_COSH,
    F_TANH,
    F_FLOOR,
    F_CEIL,
    F_ROUND,
    F_TRUNC,
    F_INT,
    F_ABS,
    F_SIGN,
    F_SQRT,
    F_CBRT,
    F_EXP,
    F_LOG,
    F_LOG2,
    F_LOG10,
    F_POW,
    F_MIN,
    F_MAX,
    F_CLAMP,
    F_HYPOT,
    F_FRAC,
    F_COUNT
};

enum Op : uint8_t
{
    OP_END = 0,
    OP_PUSH_CONST,
    OP_PUSH_GLOBAL,
    OP_PUSH_VAR,
    OP_STORE_VAR,
    OP_POP,
    OP_DUP,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_POW,
    OP_BAND,
    OP_BOR,
    OP_BXOR,
    OP_SHL,
    OP_SHR,
    OP_USHR,
    OP_NEG,
    OP_POS,
    OP_LNOT,
    OP_BNOT,
    OP_EQ,
    OP_NE,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,
    OP_JMP,
    OP_JZ,
    OP_JNZ,
    OP_CALL,
    OP_STEREO
};

struct Instruction
{
    uint8_t  op   = OP_END;
    uint8_t  func = 0;
    uint16_t arg  = 0;
};

struct Program
{
    Instruction code[kMaxCode] {};
    int  codeSize = 0;
    double consts[kMaxConsts] {};
    int  nConsts = 0;
    int  nVars   = 0;
    bool stereo  = false;
};

struct CompileError
{
    int line = 0;
    int col  = 0;
    std::string message;
};

struct CompileResult
{
    std::unique_ptr<Program> program;
    bool ok = false;
    CompileError error;
};

struct FormulaMeta
{
    std::string name;
    std::string mode;       // "bytebeat", "signed", "floatbeat" or empty
    std::string author;
    std::string about;
    int rate = 8000;
    std::string labels[kNumMacros];
};

// ---------------------------------------------------------------------------
// Header directives: lines beginning with "// key: value" (or "//! key: value")
// ---------------------------------------------------------------------------
inline std::string bbTrim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline std::string bbLower(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline FormulaMeta parseMeta(const std::string& source)
{
    FormulaMeta meta;
    size_t pos = 0;

    while (pos <= source.size())
    {
        size_t end = source.find('\n', pos);
        if (end == std::string::npos) end = source.size();

        std::string line = bbTrim(source.substr(pos, end - pos));
        if (line.rfind("//", 0) == 0)
        {
            std::string body = bbTrim(line.substr(2));
            if (!body.empty() && body[0] == '!')
                body = bbTrim(body.substr(1));

            const size_t colon = body.find(':');
            if (colon != std::string::npos)
            {
                const std::string key = bbLower(bbTrim(body.substr(0, colon)));
                const std::string val = bbTrim(body.substr(colon + 1));

                if (key == "name")        meta.name = val;
                else if (key == "mode")   meta.mode = bbLower(val);
                else if (key == "author") meta.author = val;
                else if (key == "about")  meta.about = val;
                else if (key == "rate")
                {
                    const int n = std::atoi(val.c_str());
                    if (n >= 1000 && n <= 192000) meta.rate = n;
                }
                else if (key.size() == 1 && key[0] >= 'a' && key[0] < 'a' + kNumMacros)
                    meta.labels[key[0] - 'a'] = val;
            }
        }

        if (end == source.size()) break;
        pos = end + 1;
    }

    return meta;
}

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------
enum class TokType
{
    End,
    Num,
    Ident,
    Op,
    LParen,
    RParen,
    LBracket,
    RBracket,
    Comma,
    Semi,
    Question,
    Colon,
    Invalid
};

enum class OpKind
{
    None = 0,
    Add, Sub, Mul, Div, Mod, Pow,
    Band, Bor, Bxor,
    Shl, Shr, Ushr,
    Eq, Ne, Lt, Le, Gt, Ge,
    LAnd, LOr, Not, BNot,
    Assign, AddAssign, SubAssign, MulAssign, DivAssign, ModAssign,
    BandAssign, BorAssign, BxorAssign, ShlAssign, ShrAssign, UshrAssign
};

struct Token
{
    TokType type = TokType::End;
    OpKind  op   = OpKind::None;
    double  num  = 0.0;
    std::string text;
    int line = 1;
    int col  = 1;
};

class Lexer
{
public:
    explicit Lexer(const std::string& src) : s_(src) {}

    bool tokenize(std::vector<Token>& out, CompileError& err)
    {
        if (s_.size() > (size_t)kMaxSource)
        {
            err.message = "formula is too long (limit is 8192 bytes)";
            return false;
        }

        while (true)
        {
            skipSpaceAndComments(err);
            if (!err.message.empty()) return false;

            const int line = line_;
            const int col  = col_;
            if (pos_ >= s_.size())
            {
                Token t; t.type = TokType::End; t.line = line; t.col = col;
                out.push_back(std::move(t));
                return true;
            }

            const char c = s_[pos_];

            if (std::isdigit((unsigned char)c) || (c == '.' && pos_ + 1 < s_.size() && std::isdigit((unsigned char)s_[pos_ + 1])))
            {
                if (!lexNumber(out, err)) return false;
                out.back().line = line;
                out.back().col  = col;
                continue;
            }

            if (std::isalpha((unsigned char)c) || c == '_')
            {
                const size_t start = pos_;
                while (pos_ < s_.size() && (std::isalnum((unsigned char)s_[pos_]) || s_[pos_] == '_'))
                    advance();
                Token t;
                t.type = TokType::Ident;
                t.text = s_.substr(start, pos_ - start);
                t.line = line; t.col = col;
                out.push_back(std::move(t));
                continue;
            }

            Token t;
            t.line = line; t.col = col;

            auto simple = [&](TokType type) { t.type = type; advance(); out.push_back(std::move(t)); };

            switch (c)
            {
                case '(': simple(TokType::LParen); break;
                case ')': simple(TokType::RParen); break;
                case '[': simple(TokType::LBracket); break;
                case ']': simple(TokType::RBracket); break;
                case ',': simple(TokType::Comma); break;
                case ';': simple(TokType::Semi); break;
                case '?': simple(TokType::Question); break;
                case ':': simple(TokType::Colon); break;
                default:
                    if (!lexOperator(out, err)) return false;
                    break;
            }
        }
    }

private:
    void advance()
    {
        if (pos_ < s_.size())
        {
            if (s_[pos_] == '\n') { ++line_; col_ = 1; }
            else ++col_;
            ++pos_;
        }
    }

    void skipSpaceAndComments(CompileError& err)
    {
        while (pos_ < s_.size())
        {
            const char c = s_[pos_];
            if (std::isspace((unsigned char)c)) { advance(); continue; }

            if (c == '/' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '/')
            {
                while (pos_ < s_.size() && s_[pos_] != '\n') advance();
                continue;
            }
            if (c == '/' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '*')
            {
                advance(); advance();
                while (pos_ + 1 < s_.size() && !(s_[pos_] == '*' && s_[pos_ + 1] == '/')) advance();
                if (pos_ + 1 >= s_.size())
                {
                    err.line = line_; err.col = col_;
                    err.message = "unterminated /* comment";
                    return;
                }
                advance(); advance();
                continue;
            }
            return;
        }
    }

    bool lexNumber(std::vector<Token>& out, CompileError& err)
    {
        Token t;
        t.type = TokType::Num;
        t.line = line_;
        t.col  = col_;

        const size_t start = pos_;

        if (s_[pos_] == '0' && pos_ + 1 < s_.size() && (s_[pos_ + 1] == 'x' || s_[pos_ + 1] == 'X'))
        {
            advance(); advance();
            uint64_t v = 0;
            int digits = 0;
            while (pos_ < s_.size() && std::isxdigit((unsigned char)s_[pos_]))
            {
                const char d = s_[pos_];
                v = v * 16u + (uint64_t)(std::isdigit((unsigned char)d) ? d - '0' : (std::tolower((unsigned char)d) - 'a' + 10));
                ++digits; advance();
            }
            if (digits == 0) { err.line = t.line; err.col = t.col; err.message = "invalid hexadecimal number"; return false; }
            t.num = (double)v;
        }
        else if (s_[pos_] == '0' && pos_ + 1 < s_.size() && (s_[pos_ + 1] == 'b' || s_[pos_ + 1] == 'B'))
        {
            advance(); advance();
            uint64_t v = 0;
            int digits = 0;
            while (pos_ < s_.size() && (s_[pos_] == '0' || s_[pos_] == '1'))
            {
                v = v * 2u + (uint64_t)(s_[pos_] - '0');
                ++digits; advance();
            }
            if (digits == 0) { err.line = t.line; err.col = t.col; err.message = "invalid binary number"; return false; }
            t.num = (double)v;
        }
        else
        {
            char* end = nullptr;
            t.num = std::strtod(s_.c_str() + start, &end);
            if (end == s_.c_str() + start)
            {
                err.line = t.line; err.col = t.col;
                err.message = "invalid number";
                return false;
            }
            pos_ = (size_t)(end - s_.c_str());
            // strtod does not update line/col; numbers cannot contain newlines.
            col_ += (int)(pos_ - start);
        }

        if (pos_ < s_.size() && (std::isalnum((unsigned char)s_[pos_]) || s_[pos_] == '_'))
        {
            err.line = t.line; err.col = t.col;
            err.message = "invalid character after number";
            return false;
        }

        out.push_back(std::move(t));
        return true;
    }

    bool lexOperator(std::vector<Token>& out, CompileError& err)
    {
        const int line = line_;
        const int col  = col_;

        auto emit = [&](OpKind op, int len)
        {
            Token t;
            t.type = TokType::Op;
            t.op = op;
            t.line = line; t.col = col;
            for (int i = 0; i < len; ++i) advance();
            out.push_back(std::move(t));
            return true;
        };

        auto starts = [&](const char* s2) { return s_.compare(pos_, std::strlen(s2), s2) == 0; };

        // Longest first.
        if (starts(">>>=")) return emit(OpKind::UshrAssign, 4);
        if (starts("<<="))  return emit(OpKind::ShlAssign, 3);
        if (starts(">>="))  return emit(OpKind::ShrAssign, 3);
        if (starts(">>>"))  return emit(OpKind::Ushr, 3);
        if (starts("&&"))   return emit(OpKind::LAnd, 2);
        if (starts("||"))   return emit(OpKind::LOr, 2);
        if (starts("=="))   return emit(OpKind::Eq, 2);
        if (starts("!="))   return emit(OpKind::Ne, 2);
        if (starts("<="))   return emit(OpKind::Le, 2);
        if (starts(">="))   return emit(OpKind::Ge, 2);
        if (starts("<<"))   return emit(OpKind::Shl, 2);
        if (starts(">>"))   return emit(OpKind::Shr, 2);
        if (starts("+="))   return emit(OpKind::AddAssign, 2);
        if (starts("-="))   return emit(OpKind::SubAssign, 2);
        if (starts("*="))   return emit(OpKind::MulAssign, 2);
        if (starts("/="))   return emit(OpKind::DivAssign, 2);
        if (starts("%="))   return emit(OpKind::ModAssign, 2);
        if (starts("&="))   return emit(OpKind::BandAssign, 2);
        if (starts("|="))   return emit(OpKind::BorAssign, 2);
        if (starts("^="))   return emit(OpKind::BxorAssign, 2);
        if (starts("**"))   return emit(OpKind::Pow, 2);

        const char c = s_[pos_];
        switch (c)
        {
            case '+': return emit(OpKind::Add, 1);
            case '-': return emit(OpKind::Sub, 1);
            case '*': return emit(OpKind::Mul, 1);
            case '/': return emit(OpKind::Div, 1);
            case '%': return emit(OpKind::Mod, 1);
            case '&': return emit(OpKind::Band, 1);
            case '|': return emit(OpKind::Bor, 1);
            case '^': return emit(OpKind::Bxor, 1);
            case '~': return emit(OpKind::BNot, 1);
            case '!': return emit(OpKind::Not, 1);
            case '<': return emit(OpKind::Lt, 1);
            case '>': return emit(OpKind::Gt, 1);
            case '=': return emit(OpKind::Assign, 1);
            default: break;
        }

        err.line = line; err.col = col;
        err.message = std::string("unexpected character '") + c + "'";
        return false;
    }

    const std::string& s_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
};

// ---------------------------------------------------------------------------
// Parser / compiler
// ---------------------------------------------------------------------------
struct FuncDef { const char* name; uint8_t id; uint8_t arity; };

inline const FuncDef* findFunc(const std::string& name)
{
    static const FuncDef kFuncs[] = {
        { "sin", F_SIN, 1 }, { "cos", F_COS, 1 }, { "tan", F_TAN, 1 },
        { "asin", F_ASIN, 1 }, { "acos", F_ACOS, 1 }, { "atan", F_ATAN, 1 },
        { "atan2", F_ATAN2, 2 },
        { "sinh", F_SINH, 1 }, { "cosh", F_COSH, 1 }, { "tanh", F_TANH, 1 },
        { "floor", F_FLOOR, 1 }, { "ceil", F_CEIL, 1 }, { "round", F_ROUND, 1 },
        { "trunc", F_TRUNC, 1 }, { "int", F_INT, 1 },
        { "abs", F_ABS, 1 }, { "sign", F_SIGN, 1 },
        { "sqrt", F_SQRT, 1 }, { "cbrt", F_CBRT, 1 },
        { "exp", F_EXP, 1 }, { "log", F_LOG, 1 }, { "log2", F_LOG2, 1 },
        { "log10", F_LOG10, 1 },
        { "pow", F_POW, 2 }, { "min", F_MIN, 2 }, { "max", F_MAX, 2 },
        { "clamp", F_CLAMP, 3 }, { "hypot", F_HYPOT, 2 }, { "frac", F_FRAC, 1 },
    };
    for (const auto& f : kFuncs)
        if (name == f.name) return &f;
    return nullptr;
}

struct GlobalDef { const char* name; uint8_t index; };

inline const GlobalDef* findGlobal(const std::string& name)
{
    static const GlobalDef kGlobals[] = {
        { "t", G_T }, { "sr", G_SR }, { "sampleRate", G_SR },
        { "time", G_TIME }, { "beat", G_BEAT }, { "bar", G_BAR },
        { "tempo", G_TEMPO }, { "playing", G_PLAYING },
        { "note", G_NOTE }, { "freq", G_FREQ }, { "vel", G_VEL },
        { "gate", G_GATE }, { "voice", G_VOICE }, { "aftertouch", G_AT },
        { "a", G_A }, { "b", G_B }, { "c", G_C }, { "d", G_D },
        { "e", G_E }, { "f", G_F }, { "g", G_G }, { "h", G_H },
    };
    for (const auto& g : kGlobals)
        if (name == g.name) return &g;
    return nullptr;
}

inline bool findConstant(const std::string& name, double& out)
{
    if (name == "PI" || name == "pi") { out = 3.14159265358979323846; return true; }
    if (name == "TAU" || name == "tau") { out = 6.28318530717958647692; return true; }
    if (name == "E") { out = 2.71828182845904523536; return true; }
    return false;
}

class Compiler
{
public:
    Compiler(const std::string& source, Program& program, CompileError& error)
        : program_(program), error_(error)
    {
        // Strip a UTF-8 BOM so a file saved by a Windows editor still compiles.
        if (source.size() >= 3 && (unsigned char)source[0] == 0xEF
            && (unsigned char)source[1] == 0xBB && (unsigned char)source[2] == 0xBF)
            src_ = source.substr(3);
        else
            src_ = source;
    }

    bool run()
    {
        Lexer lex(src_);
        if (!lex.tokenize(tokens_, error_)) return false;
        if (tokens_.empty()) { fail(1, 1, "empty formula"); return false; }

        if (!parseProgram()) return false;
        if (!error_.message.empty()) return false;
        if (program_.codeSize == 0) { fail(1, 1, "empty formula"); return false; }

        emit(OP_END);
        return true;
    }

private:
    const Token& cur() const { return tokens_[std::min(pos_, tokens_.size() - 1)]; }
    const Token& next() const { return tokens_[std::min(pos_ + 1, tokens_.size() - 1)]; }
    void advance() { if (pos_ + 1 < tokens_.size()) ++pos_; }
    bool atEnd() const { return cur().type == TokType::End; }

    void fail(int line, int col, const std::string& msg)
    {
        if (!error_.message.empty()) return;
        error_.line = line;
        error_.col = col;
        error_.message = msg;
    }

    void fail(const Token& t, const std::string& msg) { fail(t.line, t.col, msg); }

    int emit(uint8_t op, uint16_t arg = 0, uint8_t func = 0)
    {
        if (program_.codeSize >= kMaxCode)
        {
            fail(cur(), "formula is too complex (bytecode limit reached)");
            return -1;
        }
        const int at = program_.codeSize++;
        program_.code[at].op = op;
        program_.code[at].arg = arg;
        program_.code[at].func = func;
        return at;
    }

    void patch(int at, int target)
    {
        if (at >= 0 && at < program_.codeSize)
            program_.code[at].arg = (uint16_t)target;
    }

    int addConst(double v)
    {
        for (int i = 0; i < program_.nConsts; ++i)
            if (program_.consts[i] == v) return i;
        if (program_.nConsts >= kMaxConsts)
        {
            fail(cur(), "too many constants");
            return 0;
        }
        program_.consts[program_.nConsts] = v;
        return program_.nConsts++;
    }

    int varSlot(const std::string& name, bool create)
    {
        for (int i = 0; i < program_.nVars; ++i)
            if (varNames_[i] == name) return i;
        if (!create) return -1;
        if (program_.nVars >= kMaxVars)
        {
            fail(cur(), "too many variables (limit is 16)");
            return 0;
        }
        varNames_[program_.nVars] = name;
        return program_.nVars++;
    }

    bool parseProgram()
    {
        bool sawAny = false;
        bool lastStereo = false;

        while (!atEnd() && error_.message.empty())
        {
            const bool isStereo = (cur().type == TokType::LBracket);
            if (!parseStatement()) return false;
            sawAny = true;
            lastStereo = isStereo;

            if (isStereo && !atEnd())
            {
                const bool trailingSeparator = (cur().type == TokType::Semi
                                                || cur().type == TokType::Comma)
                                               && next().type == TokType::End;
                if (!trailingSeparator)
                {
                    fail(cur(), "a stereo [left, right] expression must be the final statement");
                    return false;
                }
            }

            if (atEnd()) break;

            if (cur().type == TokType::Semi || cur().type == TokType::Comma)
            {
                const Token sep = cur();
                advance();
                if (atEnd()) break; // trailing separator
                emit(OP_POP);
                if (!error_.message.empty()) return false;
                (void)sep;
                continue;
            }

            fail(cur(), "expected ';' between statements");
            return false;
        }

        if (!sawAny) { fail(1, 1, "empty formula"); return false; }
        program_.stereo = lastStereo;
        return error_.message.empty();
    }

    bool parseStatement()
    {
        if (cur().type == TokType::LBracket)
            return parseStereo();

        if (cur().type == TokType::Ident && isAssignOp(next().type == TokType::Op ? next().op : OpKind::None))
            return parseAssignment();

        return parseExpression();
    }

    static bool isAssignOp(OpKind op)
    {
        switch (op)
        {
            case OpKind::Assign:
            case OpKind::AddAssign:
            case OpKind::SubAssign:
            case OpKind::MulAssign:
            case OpKind::DivAssign:
            case OpKind::ModAssign:
            case OpKind::BandAssign:
            case OpKind::BorAssign:
            case OpKind::BxorAssign:
            case OpKind::ShlAssign:
            case OpKind::ShrAssign:
            case OpKind::UshrAssign:
                return true;
            default: return false;
        }
    }

    bool parseStereo()
    {
        const Token open = cur();
        advance(); // '['
        if (!parseExpression()) return false;
        if (cur().type != TokType::Comma) { fail(cur(), "expected ',' in [left, right]"); return false; }
        advance();
        if (!parseExpression()) return false;
        if (cur().type != TokType::RBracket) { fail(cur(), "expected ']'"); return false; }
        advance();
        emit(OP_STEREO);
        if (!error_.message.empty()) return false;
        (void)open;
        return true;
    }

    bool parseAssignment()
    {
        const Token nameTok = cur();
        const std::string name = nameTok.text;
        advance(); // name
        const OpKind assign = cur().op;
        advance(); // assignment operator

        if (findGlobal(name) != nullptr) { fail(nameTok, "cannot assign to read-only variable '" + name + "'"); return false; }
        if (findFunc(name) != nullptr)   { fail(nameTok, "'" + name + "' is a function"); return false; }
        double dummy = 0.0;
        if (findConstant(name, dummy))   { fail(nameTok, "cannot assign to constant '" + name + "'"); return false; }

        const int slot = varSlot(name, true);
        if (!error_.message.empty()) return false;

        if (assign == OpKind::Assign)
        {
            if (!parseExpression()) return false;
        }
        else
        {
            emit(OP_PUSH_VAR, (uint16_t)slot);
            if (!parseExpression()) return false;

            uint8_t binOp = OP_ADD;
            switch (assign)
            {
                case OpKind::AddAssign:  binOp = OP_ADD;  break;
                case OpKind::SubAssign:  binOp = OP_SUB;  break;
                case OpKind::MulAssign:  binOp = OP_MUL;  break;
                case OpKind::DivAssign:  binOp = OP_DIV;  break;
                case OpKind::ModAssign:  binOp = OP_MOD;  break;
                case OpKind::BandAssign: binOp = OP_BAND; break;
                case OpKind::BorAssign:  binOp = OP_BOR;  break;
                case OpKind::BxorAssign: binOp = OP_BXOR; break;
                case OpKind::ShlAssign:  binOp = OP_SHL;  break;
                case OpKind::ShrAssign:  binOp = OP_SHR;  break;
                case OpKind::UshrAssign: binOp = OP_USHR; break;
                default: break;
            }
            emit(binOp);
        }

        emit(OP_STORE_VAR, (uint16_t)slot);
        return error_.message.empty();
    }

    bool parseExpression() { return parseConditional(); }

    bool parseConditional()
    {
        if (!parseOr()) return false;
        if (cur().type == TokType::Question)
        {
            advance();
            const int jz = emit(OP_JZ);
            if (!parseConditional()) return false;
            if (cur().type != TokType::Colon) { fail(cur(), "expected ':'"); return false; }
            advance();
            const int jmp = emit(OP_JMP);
            patch(jz, program_.codeSize);
            if (!parseConditional()) return false;
            patch(jmp, program_.codeSize);
        }
        return error_.message.empty();
    }

    bool parseOr()
    {
        if (!parseAnd()) return false;
        while (cur().type == TokType::Op && cur().op == OpKind::LOr)
        {
            advance();
            emit(OP_DUP);
            const int jnz = emit(OP_JNZ);
            emit(OP_POP);
            if (!parseAnd()) return false;
            patch(jnz, program_.codeSize);
        }
        return error_.message.empty();
    }

    bool parseAnd()
    {
        if (!parseBitOr()) return false;
        while (cur().type == TokType::Op && cur().op == OpKind::LAnd)
        {
            advance();
            emit(OP_DUP);
            const int jz = emit(OP_JZ);
            emit(OP_POP);
            if (!parseBitOr()) return false;
            patch(jz, program_.codeSize);
        }
        return error_.message.empty();
    }

    bool parseBitOr()
    {
        if (!parseBitXor()) return false;
        while (cur().type == TokType::Op && cur().op == OpKind::Bor)
        {
            advance(); if (!parseBitXor()) return false; emit(OP_BOR);
        }
        return error_.message.empty();
    }

    bool parseBitXor()
    {
        if (!parseBitAnd()) return false;
        while (cur().type == TokType::Op && cur().op == OpKind::Bxor)
        {
            advance(); if (!parseBitAnd()) return false; emit(OP_BXOR);
        }
        return error_.message.empty();
    }

    bool parseBitAnd()
    {
        if (!parseEquality()) return false;
        while (cur().type == TokType::Op && cur().op == OpKind::Band)
        {
            advance(); if (!parseEquality()) return false; emit(OP_BAND);
        }
        return error_.message.empty();
    }

    bool parseEquality()
    {
        if (!parseRelational()) return false;
        while (cur().type == TokType::Op && (cur().op == OpKind::Eq || cur().op == OpKind::Ne))
        {
            const OpKind op = cur().op;
            advance(); if (!parseRelational()) return false; emit(op == OpKind::Eq ? OP_EQ : OP_NE);
        }
        return error_.message.empty();
    }

    bool parseRelational()
    {
        if (!parseShift()) return false;
        while (cur().type == TokType::Op
               && (cur().op == OpKind::Lt || cur().op == OpKind::Le
                   || cur().op == OpKind::Gt || cur().op == OpKind::Ge))
        {
            const OpKind op = cur().op;
            advance(); if (!parseShift()) return false;
            emit(op == OpKind::Lt ? OP_LT : op == OpKind::Le ? OP_LE : op == OpKind::Gt ? OP_GT : OP_GE);
        }
        return error_.message.empty();
    }

    bool parseShift()
    {
        if (!parseAdditive()) return false;
        while (cur().type == TokType::Op
               && (cur().op == OpKind::Shl || cur().op == OpKind::Shr || cur().op == OpKind::Ushr))
        {
            const OpKind op = cur().op;
            advance(); if (!parseAdditive()) return false;
            emit(op == OpKind::Shl ? OP_SHL : op == OpKind::Shr ? OP_SHR : OP_USHR);
        }
        return error_.message.empty();
    }

    bool parseAdditive()
    {
        if (!parseMultiplicative()) return false;
        while (cur().type == TokType::Op && (cur().op == OpKind::Add || cur().op == OpKind::Sub))
        {
            const OpKind op = cur().op;
            advance(); if (!parseMultiplicative()) return false;
            emit(op == OpKind::Add ? OP_ADD : OP_SUB);
        }
        return error_.message.empty();
    }

    bool parseMultiplicative()
    {
        if (!parseUnary()) return false;
        while (cur().type == TokType::Op
               && (cur().op == OpKind::Mul || cur().op == OpKind::Div || cur().op == OpKind::Mod))
        {
            const OpKind op = cur().op;
            advance(); if (!parseUnary()) return false;
            emit(op == OpKind::Mul ? OP_MUL : op == OpKind::Div ? OP_DIV : OP_MOD);
        }
        return error_.message.empty();
    }

    bool parseUnary()
    {
        if (cur().type == TokType::Op)
        {
            const OpKind op = cur().op;
            if (op == OpKind::Add || op == OpKind::Sub || op == OpKind::Not || op == OpKind::BNot)
            {
                advance();
                if (++nest_ > kMaxNest) { fail(cur(), "expression is nested too deeply"); return false; }
                if (!parseUnary()) { --nest_; return false; }
                --nest_;
                emit(op == OpKind::Add ? OP_POS : op == OpKind::Sub ? OP_NEG : op == OpKind::Not ? OP_LNOT : OP_BNOT);
                return error_.message.empty();
            }
        }
        return parsePower();
    }

    bool parsePower()
    {
        if (!parsePrimary()) return false;
        if (cur().type == TokType::Op && cur().op == OpKind::Pow)
        {
            advance();
            if (++nest_ > kMaxNest) { fail(cur(), "expression is nested too deeply"); return false; }
            if (!parseUnary()) { --nest_; return false; }
            --nest_;
            emit(OP_POW);
        }
        return error_.message.empty();
    }

    bool parsePrimary()
    {
        if (!error_.message.empty()) return false;

        if (cur().type == TokType::Num)
        {
            const double v = cur().num;
            advance();
            emit(OP_PUSH_CONST, (uint16_t)addConst(v));
            return error_.message.empty();
        }

        if (cur().type == TokType::Ident)
        {
            const Token nameTok = cur();
            const std::string name = nameTok.text;
            advance();

            if (cur().type == TokType::LParen)
            {
                const FuncDef* fn = findFunc(name);
                if (fn == nullptr) { fail(nameTok, "unknown function '" + name + "'"); return false; }
                advance(); // '('

                int argc = 0;
                if (cur().type != TokType::RParen)
                {
                    while (true)
                    {
                        if (!parseExpression()) return false;
                        ++argc;
                        if (cur().type == TokType::Comma) { advance(); continue; }
                        break;
                    }
                }
                if (cur().type != TokType::RParen) { fail(cur(), "expected ')'"); return false; }
                advance();

                if (argc != (int)fn->arity)
                {
                    fail(nameTok, "function '" + name + "' expects " + std::to_string((int)fn->arity)
                                   + " argument(s), got " + std::to_string(argc));
                    return false;
                }

                emit(OP_CALL, (uint16_t)argc, fn->id);
                return error_.message.empty();
            }

            if (const GlobalDef* g = findGlobal(name))
            {
                emit(OP_PUSH_GLOBAL, g->index);
                return error_.message.empty();
            }

            double c = 0.0;
            if (findConstant(name, c))
            {
                emit(OP_PUSH_CONST, (uint16_t)addConst(c));
                return error_.message.empty();
            }

            if (findFunc(name) != nullptr)
            {
                fail(nameTok, "'" + name + "' is a function; add parentheses");
                return false;
            }

            const int slot = varSlot(name, true);
            if (!error_.message.empty()) return false;
            emit(OP_PUSH_VAR, (uint16_t)slot);
            return true;
        }

        if (cur().type == TokType::LParen)
        {
            advance();
            if (++nest_ > kMaxNest) { fail(cur(), "expression is nested too deeply"); return false; }
            if (!parseExpression()) { --nest_; return false; }
            --nest_;
            if (cur().type != TokType::RParen) { fail(cur(), "expected ')'"); return false; }
            advance();
            return true;
        }

        if (cur().type == TokType::LBracket)
        {
            fail(cur(), "array literal is only allowed as the final [left, right] expression");
            return false;
        }

        if (cur().type == TokType::End)
        {
            fail(cur(), "unexpected end of formula");
            return false;
        }

        fail(cur(), "expected a number, variable or '('");
        return false;
    }

    std::string src_;
    Program& program_;
    CompileError& error_;
    std::vector<Token> tokens_;
    size_t pos_ = 0;
    int nest_ = 0;
    std::string varNames_[kMaxVars];
};

inline CompileResult compile(const std::string& source, FormulaMeta* metaOut = nullptr)
{
    CompileResult result;
    result.program = std::make_unique<Program>();

    if (metaOut != nullptr)
        *metaOut = parseMeta(source);

    Compiler compiler(source, *result.program, result.error);
    result.ok = compiler.run();
    if (!result.ok)
    {
        result.program.reset();
        if (result.error.message.empty())
        {
            result.error.line = 1;
            result.error.col = 1;
            result.error.message = "compile failed";
        }
        return result;
    }

    if (metaOut != nullptr)
    {
        // The directive parser must not treat ordinary comments such as
        // "// a: something" inside code as part of the formula body. It is
        // intentionally permissive; the shell only uses the leading block for
        // library metadata.
        *metaOut = parseMeta(source);
    }
    return result;
}

} // namespace bytebeat

#endif // BYTEBEAT_COMPILE_H
