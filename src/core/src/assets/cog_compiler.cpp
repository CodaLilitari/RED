// The COG compiler. The header reader follows the engine's config reader
// (stdConffile), and the lexer, grammar and code generation follow
// OpenJKDF2's reconstruction of the retail COG compiler (src/Cog/cog.l,
// cog.y, sithCogParse.c; 0BSD, see NOTICE-RED.md). Nothing here runs a
// script.

#include "sed/assets/cog_compiler.hpp"

#include "sed/assets/cog_registry_tables.hpp"
#include "sed/assets/cog_verb_signatures.hpp"
#include "sed/update/updater.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>

namespace sed::assets
{
namespace
{

// Opcodes, as numbered by the engine.
enum Op : std::int32_t
{
    kNop = 0,
    kPushInt = 1,
    kPushFloat = 2,
    kPushSymbol = 3,
    kArrayIndex = 4,
    kCallFunc = 5,
    kAssign = 6,
    kPushVector = 7,
    kAdd = 8,
    kSub = 9,
    kMul = 10,
    kDiv = 11,
    kMod = 12,
    kCmpFalse = 13,
    kNeg = 14,
    kCmpGt = 15,
    kCmpLs = 16,
    kCmpEq = 17,
    kCmpLe = 18,
    kCmpGe = 19,
    kCmpAnd = 20,
    kCmpOr = 21,
    kCmpNe = 22,
    kAndI = 23,
    kOrI = 24,
    kXorI = 25,
    kGoFalse = 26,
    kGoTrue = 27,
    kGo = 28,
    kRet = 29,
    kCall = 31,
};

const char* opName(const std::int32_t op)
{
    static const std::array<const char*, 32> names{ "nop", "pushint", "pushfloat", "pushsymbol", "arrayindex",
        "callfunc", "assign", "pushvector", "add", "sub", "mul", "div", "mod", "not", "neg", "cmpgt", "cmplt",
        "cmpeq", "cmple", "cmpge", "and", "or", "cmpne", "bitand", "bitor", "bitxor", "gofalse", "gotrue", "go",
        "ret", "op30", "call" };
    return op >= 0 && op < 32 ? names[static_cast<std::size_t>(op)] : "?";
}

std::int32_t opSize(const std::int32_t op)
{
    switch (op)
    {
        case kNop: return 0;
        case kPushInt:
        case kPushFloat:
        case kPushSymbol:
        case kGoFalse:
        case kGoTrue:
        case kGo:
        case kCall: return 2;
        case kPushVector: return 4;
        default: return 1;
    }
}

// Retail limits (OpenJKDF2 engine_config.h, the non-QOL values).
constexpr std::size_t kLocalSymbolLimit = 256;   // SITHCOG_LINKED_SYMBOL_LIMIT
constexpr std::size_t kLinkLimit = 128;          // SithCogScript::aSymRefs
constexpr std::size_t kHandlerLimit = 32;        // SithCogScript::aHandlers
constexpr std::size_t kJkLabelLimit = 0x200;     // SITHCOG_NODE_STACKDEPTH in JK 1.01
constexpr std::size_t kMotsLabelLimit = 0x400;   // and in MotS
constexpr std::size_t kConfLineBuffer = 1024;    // STDCONF_LINEBUFFER_LEN
constexpr std::size_t kConfArgLimit = 128;       // StdConffileEntry::aArgs
constexpr std::size_t kLinkValueLength = 31;     // SithCogSymbolRef::value[32]

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
    {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// --- The header, read the way stdConffile reads it -------------------------

struct ConfArg
{
    std::string key;
    std::string value;
    bool hasValue{ false };
};

struct ConfLine
{
    std::vector<ConfArg> args;
    std::int32_t line{};      // first physical line
    std::int32_t lastLine{};  // last physical line (continuations)
};

class ConfReader
{
public:
    explicit ConfReader(std::string_view text) : text_(text) {}

    // stdConffile_ReadArgs: the next line with at least one argument.
    bool readArgs(ConfLine& out)
    {
        std::string buffer;
        while (readLine(buffer, out.line, out.lastLine))
        {
            out.args = split(buffer);
            if (!out.args.empty()) return true;
        }
        return false;
    }

    [[nodiscard]] std::size_t position() const noexcept { return pos_; }
    [[nodiscard]] std::int32_t lineNumber() const noexcept { return line_; }
    [[nodiscard]] bool overlong() const noexcept { return overlong_; }
    [[nodiscard]] std::int32_t overlongLine() const noexcept { return overlongLine_; }

private:
    // fgets: up to and including a newline, at most size - 1 characters.
    bool gets(std::string& chunk, const std::size_t size)
    {
        if (pos_ >= text_.size() || size < 2) return false;
        const std::size_t limit = std::min(text_.size(), pos_ + size - 1);
        std::size_t end = pos_;
        while (end < limit && text_[end] != '\n') ++end;
        if (end < limit) ++end;   // include the newline
        else if (end < text_.size() && text_[end] != '\n' && !overlong_)
        {
            overlong_ = true;
            overlongLine_ = line_ + 1;
        }
        chunk.assign(text_.substr(pos_, end - pos_));
        pos_ = end;
        if (!chunk.empty() && chunk.back() == '\n') ++line_;
        else if (pos_ >= text_.size()) ++line_;
        // A NUL ends the C string the engine works with.
        if (const auto nul = chunk.find('\0'); nul != std::string::npos) chunk.resize(nul);
        return true;
    }

    // stdConffile_ReadLine.
    bool readLine(std::string& buffer, std::int32_t& firstLine, std::int32_t& lastLine)
    {
        buffer.clear();
        std::size_t start = 0;   // where the next read lands in the buffer
        std::size_t left = kConfLineBuffer - 1;
        bool begun = false;
        while (left)
        {
            std::string chunk;
            const std::int32_t before = line_ + 1;
            if (!gets(chunk, left)) return begun;   // the engine returns 0 only on a failed first read
            if (!begun)
            {
                firstLine = before;
            }
            lastLine = line_ == 0 ? 1 : line_;
            buffer.resize(start);
            buffer += chunk;
            const char first = chunk.empty() ? '\0' : chunk.front();
            if (first == ';' || first == '#' || first == '\n' || first == '\r' || chunk.empty())
            {
                continue;   // a comment or blank line: read the next one into the same place
            }
            begun = true;
            if (const auto hash = buffer.find('#', start); hash != std::string::npos) buffer.resize(hash);
            for (std::size_t i = start; i < buffer.size(); ++i)
            {
                char& c = buffer[i];
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            }
            const std::size_t length = buffer.size();
            if (length >= 2 && buffer[length - 2] == '\\')
            {
                start = length - 2;
                left = kConfLineBuffer - length;
                continue;
            }
            if (length >= 1 && (buffer[length - 1] == '\r' || buffer[length - 1] == '\n')) buffer.pop_back();
            return true;
        }
        return true;
    }

    // stdConffile_ReadArgsFromStr: strtok on ", \t\n\r", then key=value.
    static std::vector<ConfArg> split(const std::string& line)
    {
        std::vector<ConfArg> args;
        std::size_t i = 0;
        const auto delimiter = [](const char c) { return c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
        while (i < line.size())
        {
            while (i < line.size() && delimiter(line[i])) ++i;
            if (i >= line.size()) break;
            std::size_t j = i;
            while (j < line.size() && !delimiter(line[j])) ++j;
            const std::string token = line.substr(i, j - i);
            ConfArg arg;
            if (const auto eq = token.find('='); eq != std::string::npos)
            {
                arg.key = token.substr(0, eq);
                arg.value = token.substr(eq + 1);
                arg.hasValue = true;
            }
            else
            {
                arg.key = token;
                arg.value = token;
            }
            args.push_back(std::move(arg));
            i = j;
        }
        return args;
    }

    std::string_view text_;
    std::size_t pos_{};
    std::int32_t line_{};
    bool overlong_{ false };
    std::int32_t overlongLine_{};
};

// msvcrt-style conversions used by the engine.
std::int32_t atoiLike(std::string_view text, bool* overflow = nullptr)
{
    std::size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
    bool negative = false;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) negative = text[i++] == '-';
    std::uint64_t value = 0;
    bool big = false;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i)
    {
        value = value * 10 + static_cast<std::uint64_t>(text[i] - '0');
        if (value > 0xFFFFFFFFull) big = true;
        value &= 0xFFFFFFFFFull;
    }
    if (big || (!negative && value > 0x7FFFFFFFull) || (negative && value > 0x80000000ull))
    {
        if (overflow) *overflow = true;
    }
    const auto low = static_cast<std::uint32_t>(value);
    return static_cast<std::int32_t>(negative ? 0u - low : low);
}

float atofLike(std::string_view text)
{
    const std::string copy(text);
    return static_cast<float>(std::strtod(copy.c_str(), nullptr));
}

// sscanf "%x": optional sign, optional 0x, hex digits, low 32 bits.
std::uint32_t hexLike(std::string_view text, bool* any = nullptr)
{
    std::size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
    bool negative = false;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) negative = text[i++] == '-';
    if (i + 1 < text.size() && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) i += 2;
    std::uint32_t value = 0;
    bool digits = false;
    for (; i < text.size(); ++i)
    {
        const char c = text[i];
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d < 0) break;
        value = value * 16u + static_cast<std::uint32_t>(d);
        digits = true;
    }
    if (any) *any = digits;
    return negative ? 0u - value : value;
}

// --- Tokens ----------------------------------------------------------------

enum class Tok
{
    Eof,
    Identifier,
    Int,
    Float,
    String,
    Vector,
    LeOp,
    GeOp,
    EqOp,
    NeOp,
    AndOp,
    OrOp,
    If,
    Else,
    While,
    Do,
    For,
    Goto,
    Call,
    Return,
    Break,
    Continue,
    Char,
};

struct Token
{
    Tok kind{ Tok::Eof };
    char ch{};
    std::string text{};
    std::int32_t yylval{};      // the semantic value the parser sees (symbol id, int, float bits)
    std::int32_t symbol{ -1 };  // the symbol id, -1 when the table was full (yylval is then stale)
    std::array<float, 3> vector{};
    std::int32_t line{};        // line in the file
    std::int32_t engineLine{};  // line as the engine counts it within the code section
};

const char* tokenName(const Token& t)
{
    switch (t.kind)
    {
        case Tok::Eof: return "the end of the code";
        case Tok::Identifier: return "a name";
        case Tok::Int: return "a number";
        case Tok::Float: return "a number";
        case Tok::String: return "a string";
        case Tok::Vector: return "a vector";
        case Tok::If: return "'if'";
        case Tok::Else: return "'else'";
        case Tok::While: return "'while'";
        case Tok::Do: return "'do'";
        case Tok::For: return "'for'";
        case Tok::Goto: return "'goto'";
        case Tok::Call: return "'call'";
        case Tok::Return: return "'return'";
        case Tok::Break: return "'break'";
        case Tok::Continue: return "'continue'";
        default: return nullptr;
    }
}

std::string describe(const Token& t)
{
    if (const char* name = tokenName(t))
    {
        if (t.kind == Tok::Identifier) return "'" + t.text + "'";
        if (t.kind == Tok::Int || t.kind == Tok::Float || t.kind == Tok::String || t.kind == Tok::Vector)
        {
            std::string shown;
            for (const char c : t.text)
            {
                if (shown.size() >= 40)
                {
                    shown += "...";
                    break;
                }
                shown += c == '\n' ? std::string("\\n") : c == '\r' ? std::string() : std::string(1, c);
            }
            return shown;
        }
        return name;
    }
    return "'" + t.text + "'";
}


// --- Symbols ---------------------------------------------------------------

struct Symbol
{
    std::string name;
    std::string kind;          // declared type, "message", "implicit", "string", or for globals "verb"/"message"/"global"
    std::int32_t line{};
    bool local{ false };       // declared `local`
    std::string value;
    std::int32_t label{ -1 };  // the engine's per-symbol label slot (field_14); -1 for globals
    bool global{ false };
    std::int32_t messageId{};
    bool usedAsValue{ false };
    bool definedAsLabel{ false };
    std::int32_t labelLine{};
    bool assigned{ false };
    bool warnedUndeclared{ false };
    std::int32_t firstUseLine{};
};

class SymbolTable
{
public:
    explicit SymbolTable(const CogPreflightTarget target)
    {
        const auto addGlobal = [this](const std::string_view name, const char* kind, const std::int32_t id) {
            if (globalIndex_.count(std::string(name))) return;
            Symbol s;
            s.name = std::string(name);
            s.kind = kind;
            s.global = true;
            s.messageId = id;
            globalIndex_.emplace(s.name, static_cast<std::int32_t>(globals_.size()));
            globals_.push_back(std::move(s));
        };
        if (target == CogPreflightTarget::mots)
        {
            for (const auto name : cog_registry::kMotsVerbs) addGlobal(name, "verb", 0);
            for (const auto& m : cog_verbs::kMotsMessageIds) addGlobal(m.name, "message", m.id);
            for (const auto name : cog_registry::kMotsGlobalFloats) addGlobal(name, "global", 0);
        }
        else
        {
            for (const auto name : cog_registry::kJkVerbs) addGlobal(name, "verb", 0);
            for (const auto& m : cog_verbs::kJkMessageIds) addGlobal(m.name, "message", m.id);
            for (const auto name : cog_registry::kJkGlobalFloats) addGlobal(name, "global", 0);
        }
    }

    // sithCogParse_AddSymbol: -1 when the table is full.
    std::int32_t add(const std::string& name, const std::string& kind, const std::int32_t line)
    {
        if (locals_.size() >= kLocalSymbolLimit) return -1;
        Symbol s;
        s.name = name;
        s.kind = kind;
        s.line = line;
        s.label = nextLabel();
        const auto id = static_cast<std::int32_t>(locals_.size());
        localIndex_.emplace(name, id);   // the first symbol of a name keeps it
        locals_.push_back(std::move(s));
        return id;
    }

    // sithCogParse_GetSymbol: the script's own table, then the global one. Global ids start at 0x100.
    [[nodiscard]] std::int32_t find(const std::string& name) const
    {
        if (const auto it = localIndex_.find(name); it != localIndex_.end()) return it->second;
        if (const auto it = globalIndex_.find(name); it != globalIndex_.end()) return 0x100 + it->second;
        return -1;
    }

    [[nodiscard]] std::int32_t findGlobal(const std::string& name) const
    {
        const auto it = globalIndex_.find(name);
        return it == globalIndex_.end() ? -1 : it->second;
    }

    Symbol* byId(const std::int32_t id)
    {
        if (id < 0) return nullptr;
        if (id >= 0x100)
        {
            const auto g = static_cast<std::size_t>(id - 0x100);
            return g < globals_.size() ? &globals_[g] : nullptr;
        }
        return static_cast<std::size_t>(id) < locals_.size() ? &locals_[static_cast<std::size_t>(id)] : nullptr;
    }

    std::int32_t nextLabel()
    {
        const std::int32_t label = labelCounter_++;
        return label;
    }

    [[nodiscard]] std::int32_t labelsUsed() const noexcept { return labelCounter_ - 1; }
    [[nodiscard]] std::vector<Symbol>& locals() noexcept { return locals_; }
    [[nodiscard]] std::size_t size() const noexcept { return locals_.size(); }

private:
    std::vector<Symbol> locals_;
    std::unordered_map<std::string, std::int32_t> localIndex_;
    std::vector<Symbol> globals_;
    std::unordered_map<std::string, std::int32_t> globalIndex_;
    std::int32_t labelCounter_{ 1 };   // cog_yacc_loop_depth starts at 1 for every script
};

// --- The compilation -------------------------------------------------------

struct Node
{
    std::int32_t op{ kNop };
    std::int32_t value{};
    std::array<float, 3> vector{};
    Node* left{};
    Node* right{};
    std::int32_t parentLabel{};   // a label placed after this node's code
    std::int32_t childLabel{};    // a label placed before it
};

enum class Shape
{
    Other,
    Symbol,
    Index,
    Call,
    Constant,
    StringLiteral,
    VectorLiteral,
};

struct Expr
{
    Node* node{};
    Shape shape{ Shape::Other };
    std::int32_t symbol{ -1 };
    bool unary{ true };           // still a unary_expression in the grammar (may take '=')
    bool returnsNothing{ false }; // a call to a verb that pushes no result
    std::string callee;           // for calls
    std::int32_t line{};
};

struct SyntaxError
{
    Token token;
};

class Compiler
{
public:
    Compiler(std::string_view fileName, std::string_view text, CogPreflightTarget target)
        : text_(text), target_(target), symbols_(target)
    {
        result_.fileName = std::string(fileName);
        result_.target = target;
        result_.sourceSha256 = sed::update::sha256Hex(text);
    }

    CogCompileResult run()
    {
        result_.limits.symbolLimit = kLocalSymbolLimit;
        result_.limits.linkLimit = kLinkLimit;
        result_.limits.handlerLimit = kHandlerLimit;
        result_.limits.labelLimit = target_ == CogPreflightTarget::mots ? kMotsLabelLimit : kJkLabelLimit;
        if (target_ == CogPreflightTarget::ijim)
        {
            diag(CogDiagnosticSeverity::error, "targetUnmeasured", 0,
                "Indiana Jones and the Infernal Machine has no measured COG registry, so its scripts can't be compiled here.");
            return finish();
        }
        if (!readHeader()) return finish();
        lexCode();
        Node* root = nullptr;
        try
        {
            pos_ = 0;
            root = parseStatementList(false);
            if (peek().kind != Tok::Eof) throw SyntaxError{ peek() };
        }
        catch (const SyntaxError& e)
        {
            syntaxError(e.token);
            return finish();
        }
        result_.loads = true;
        generate(root);
        checkAfterParse();
        return finish();
    }

private:
    // ---- diagnostics ----
    void diag(const CogDiagnosticSeverity severity, std::string code, const std::int32_t line, std::string message)
    {
        result_.diagnostics.push_back(CogDiagnostic{ severity, std::move(code), std::move(message), line });
    }

    CogCompileResult finish()
    {
        result_.limits.symbols = symbols_.size();
        result_.limits.links = links_;
        result_.limits.handlers = handlers_.size();
        result_.limits.labels = static_cast<std::size_t>(std::max(0, symbols_.labelsUsed()));
        for (const Symbol& s : symbols_.locals())
        {
            result_.symbols.push_back(CogCompiledSymbol{ s.name, s.kind, s.line, s.local, s.value });
        }
        std::stable_sort(result_.diagnostics.begin(), result_.diagnostics.end(),
            [](const CogDiagnostic& a, const CogDiagnostic& b) {
                if (a.line != b.line) return a.line < b.line;
                return static_cast<int>(a.severity) < static_cast<int>(b.severity);
            });
        return std::move(result_);
    }

    // ---- the header ----
    bool readHeader()
    {
        ConfReader reader(text_);
        ConfLine line;
        if (!reader.readArgs(line))
        {
            diag(CogDiagnosticSeverity::error, "noSymbols", 0,
                "The file has no 'symbols' section, so the engine refuses it.");
            return false;
        }
        if (line.args[0].key == "flags")
        {
            bool any = false;
            result_.flags = hexLike(line.args[0].value, &any);
            if (!line.args[0].hasValue || !any)
            {
                diag(CogDiagnosticSeverity::warning, "flagsUnreadable", line.line,
                    "The flags line has no hexadecimal value, so the script's flags are 0.");
            }
            if (!reader.readArgs(line))
            {
                diag(CogDiagnosticSeverity::error, "noSymbols", 0,
                    "The file has no 'symbols' section, so the engine refuses it.");
                return false;
            }
        }
        if (line.args[0].value != "symbols")
        {
            diag(CogDiagnosticSeverity::error, "noSymbols", line.line,
                "The engine expects 'symbols' here (after an optional flags line) and refuses the script.");
            return false;
        }
        bool ended = false;
        const ConfLine* previous = nullptr;
        ConfLine kept;
        while (reader.readArgs(line))
        {
            if (line.args[0].value == "end")
            {
                ended = true;
                break;
            }
            if (line.args.size() > kConfArgLimit)
            {
                diag(CogDiagnosticSeverity::error, "headerLineTooManyArguments", line.line,
                    "This line has more than 128 arguments, which overruns the engine's argument buffer.");
            }
            if (symbols_.size() >= kLocalSymbolLimit)
            {
                diag(CogDiagnosticSeverity::error, "symbolTableFull", line.line,
                    "The script's symbol table already holds 256 symbols, so the engine skips this declaration.");
                continue;
            }
            declare(line, previous);
            kept = line;
            previous = &kept;
        }
        if (reader.overlong())
        {
            diag(CogDiagnosticSeverity::warning, "headerLineTooLong", reader.overlongLine(),
                "This line is longer than 1023 characters; the engine reads the rest as a separate line.");
        }
        if (!ended)
        {
            diag(CogDiagnosticSeverity::error, "symbolsNotEnded", 0,
                "The 'symbols' section has no 'end', so the engine refuses the script.");
            return false;
        }
        if (!reader.readArgs(line) || line.args[0].value != "code")
        {
            diag(CogDiagnosticSeverity::error, "noCode", ended ? line.line : 0,
                "The engine expects 'code' after the symbols' 'end' and refuses the script.");
            return false;
        }
        codeStart_ = reader.position();
        result_.codeLine = reader.lineNumber() + 1;
        return true;
    }

    void declare(const ConfLine& line, const ConfLine* previous)
    {
        static const std::map<std::string, std::string> refTypes{ { "thing", "thing" }, { "surface", "surface" },
            { "sector", "sector" }, { "sound", "sound" }, { "template", "template" }, { "model", "model" },
            { "keyframe", "keyframe" }, { "cog", "cog" }, { "material", "material" }, { "ai", "ai" } };
        const std::string& type = line.args[0].value;
        const auto known = [](const std::string& key) {
            return key == "local" || key == "desc" || key == "mask" || key == "linkid" || key == "nolink";
        };
        if (const auto ref = refTypes.find(type); ref != refTypes.end())
        {
            if (links_ >= kLinkLimit)
            {
                diag(CogDiagnosticSeverity::error, "linkTableFull", line.line,
                    "The script already has 128 linked symbols, so the engine skips this declaration; code that "
                    "uses the name gets a new integer that is always 0.");
                return;
            }
            if (line.args.size() < 2)
            {
                diag(CogDiagnosticSeverity::warning, "declarationWithoutName", line.line,
                    "A " + type + " declaration needs a name; the engine skips this line.");
                return;
            }
            const ConfArg& arg = line.args[1];
            const std::int32_t id = addDeclared(arg.key, ref->second, line.line);
            if (id < 0) return;
            Symbol& s = *symbols_.byId(id);
            ++links_;
            if (arg.hasValue)
            {
                s.value = arg.value;
                if (arg.value.size() > kLinkValueLength)
                {
                    diag(CogDiagnosticSeverity::warning, "defaultTooLong", line.line,
                        "The default '" + arg.value + "' is longer than 31 characters; the engine keeps the first 31.");
                }
            }
            for (std::size_t i = 2; i < line.args.size(); ++i)
            {
                const ConfArg& extra = line.args[i];
                if (extra.key == "local") s.local = true;
                else if (extra.key == "mask")
                {
                    bool any = false;
                    (void)hexLike(extra.value, &any);
                    if (!extra.hasValue || !any)
                        diag(CogDiagnosticSeverity::warning, "maskUnreadable", line.line,
                            "'mask' needs a hexadecimal value; the engine reads this one as 0.");
                }
                else if (extra.key.rfind("//", 0) == 0)
                    break;
                else if (extra.hasValue && !known(extra.key))
                {
                    diag(CogDiagnosticSeverity::note, "ignoredKeyword", line.line,
                        "The engine ignores '" + extra.key + "=' on a declaration.");
                }
            }
            return;
        }
        if (type == "flex" || type == "float" || type == "int" || type == "vector")
        {
            ConfArg arg;
            if (line.args.size() < 2)
            {
                if (!previous || previous->args.size() < 2)
                {
                    diag(CogDiagnosticSeverity::error, "declarationWithoutName", line.line,
                        "A " + type + " declaration needs a name; the engine reads one left over from an earlier line.");
                    return;
                }
                arg = previous->args[1];
                diag(CogDiagnosticSeverity::error, "declarationWithoutName", line.line,
                    "A " + type + " declaration needs a name; the engine reuses '" + arg.key
                        + "' from the line before and declares it again.");
            }
            else
            {
                arg = line.args[1];
            }
            const std::string kind = type == "float" ? "flex" : type;
            const std::int32_t id = addDeclared(arg.key, kind, line.line);
            if (id < 0) return;
            Symbol& s = *symbols_.byId(id);
            if (kind == "int")
            {
                bool overflow = false;
                const std::int32_t v = atoiLike(arg.value, &overflow);
                s.value = std::to_string(v);
                if (arg.hasValue && arg.value.size() > 1 && arg.value[0] == '0'
                    && (arg.value[1] == 'x' || arg.value[1] == 'X'))
                {
                    diag(CogDiagnosticSeverity::warning, "intDefaultHex", line.line,
                        "The engine reads an int default with atoi, so '" + arg.value + "' is 0.");
                }
                else if (arg.hasValue && overflow)
                {
                    diag(CogDiagnosticSeverity::warning, "intDefaultRange", line.line,
                        "'" + arg.value + "' doesn't fit a 32-bit int.");
                }
            }
            else if (kind == "flex")
            {
                std::ostringstream out;
                out << atofLike(arg.value);
                s.value = out.str();
            }
            else if (arg.hasValue)
            {
                diag(CogDiagnosticSeverity::warning, "vectorDefaultIgnored", line.line,
                    "The engine ignores a vector's default; '" + arg.key + "' starts as (0/0/0).");
            }
            bool isLocal = false;
            for (std::size_t i = 2; i < line.args.size(); ++i)
            {
                if (line.args[i].key == "local")
                {
                    isLocal = true;
                    break;
                }
                if (line.args[i].key.rfind("//", 0) == 0) break;
                if (line.args[i].hasValue && !known(line.args[i].key))
                {
                    diag(CogDiagnosticSeverity::note, "ignoredKeyword", line.line,
                        "The engine ignores '" + line.args[i].key + "=' on a declaration.");
                }
            }
            s.local = isLocal;
            if (!isLocal)
            {
                // Retail checks the link table only for resource symbols; a variable past the 128th
                // link is written beyond the table (OpenJKDF2 added a check here).
                if (links_ >= kLinkLimit)
                {
                    diag(CogDiagnosticSeverity::error, "linkTableOverrun", line.line,
                        "This is the script's 129th or later linked symbol; retail writes it past the end of its "
                        "128-entry link table and corrupts the loaded script. Declare it 'local' if nothing links it.");
                }
                ++links_;
            }
            return;
        }
        if (type == "message")
        {
            if (line.args.size() < 2)
            {
                diag(CogDiagnosticSeverity::warning, "declarationWithoutName", line.line,
                    "A message declaration needs a name; the engine skips this line.");
                return;
            }
            if (handlers_.size() >= kHandlerLimit)
            {
                diag(CogDiagnosticSeverity::error, "handlerTableFull", line.line,
                    "The script already declares 32 messages, so the engine skips this one.");
                return;
            }
            const ConfArg& arg = line.args[1];
            const std::int32_t g = symbols_.findGlobal(arg.value);
            if (g < 0)
            {
                const bool other = target_ == CogPreflightTarget::jk ? cogMessageAvailable(CogPreflightTarget::mots, arg.value)
                                                                     : cogMessageAvailable(CogPreflightTarget::jk, arg.value);
                diag(CogDiagnosticSeverity::error, "unknownMessage", line.line,
                    "'" + arg.value + "' isn't a " + targetName() + " message, so the engine skips the declaration"
                        + (other ? " (it is one in " + otherTargetName() + ")." : "."));
                return;
            }
            const Symbol* global = symbols_.byId(0x100 + g);
            if (global->kind != "message")
            {
                diag(CogDiagnosticSeverity::error, "notAMessage", line.line,
                    "'" + arg.value + "' is a " + (global->kind == "verb" ? "verb" : "global variable")
                        + ", not a message; the engine registers a handler that never matches.");
            }
            const std::int32_t id = addDeclared(arg.key, "message", line.line);
            if (id < 0) return;
            Symbol& s = *symbols_.byId(id);
            s.messageId = global->messageId;
            s.value = arg.value;
            handlers_.push_back(id);
            return;
        }
        if (type.rfind("//", 0) == 0) return;   // not a comment to the engine, but skipped all the same
        diag(CogDiagnosticSeverity::warning, "unknownType", line.line,
            "'" + type + "' isn't a symbol type the engine knows; it skips this line.");
    }

    std::int32_t addDeclared(const std::string& name, const std::string& kind, const std::int32_t line)
    {
        if (const std::int32_t existing = symbols_.find(name); existing >= 0 && existing < 0x100)
        {
            diag(CogDiagnosticSeverity::warning, "duplicateSymbol", line,
                "'" + name + "' is already declared on line " + std::to_string(symbols_.byId(existing)->line)
                    + "; code uses the first declaration.");
        }
        else if (existing >= 0x100)
        {
            const Symbol* g = symbols_.byId(existing);
            if (g->kind == "verb" && kind != "message")
            {
                diag(CogDiagnosticSeverity::warning, "shadowsVerb", line,
                    "'" + name + "' has the name of a verb; in this script the name means the symbol, so the verb "
                        "can't be called.");
            }
        }
        const std::int32_t id = symbols_.add(name, kind, line);
        if (id < 0)
        {
            diag(CogDiagnosticSeverity::error, "symbolTableFull", line,
                "The script's symbol table is full (256 symbols); the engine skips '" + name + "'.");
        }
        return id;
    }

    [[nodiscard]] std::string targetName() const { return target_ == CogPreflightTarget::mots ? "MotS" : "JK"; }
    [[nodiscard]] std::string otherTargetName() const { return target_ == CogPreflightTarget::mots ? "JK" : "MotS"; }

    // ---- the lexer (cog.l: case-insensitive, longest match, earlier rule on a tie) ----
    static bool isLetter(const char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
    static bool isDigit(const char c) { return c >= '0' && c <= '9'; }
    static bool isHex(const char c) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

    void lexCode()
    {
        const std::string_view code = text_.substr(std::min(codeStart_, text_.size()));
        std::int32_t fileLine = result_.codeLine;
        std::int32_t engineLine = 1;
        std::int32_t yylval = 0;
        std::size_t p = 0;
        const std::size_t n = code.size();
        std::map<char, std::int32_t> ignored;
        const auto digitsAt = [&](std::size_t i) {
            std::size_t j = i;
            while (j < n && isDigit(code[j])) ++j;
            return j - i;
        };
        const auto exponentAt = [&](std::size_t i) -> std::size_t {
            if (i >= n || (code[i] != 'e' && code[i] != 'E')) return 0;
            std::size_t j = i + 1;
            if (j < n && (code[j] == '+' || code[j] == '-')) ++j;
            const std::size_t d = digitsAt(j);
            return d ? j + d - i : 0;
        };
        const auto quoted = [&](std::size_t i, const char quote) -> std::size_t {
            // \"(\\.|[^\\"])*\" ; '.' never matches a newline
            std::size_t j = i + 1;
            while (j < n)
            {
                if (code[j] == '\\')
                {
                    if (j + 1 < n && code[j + 1] != '\n') j += 2;
                    else return 0;
                }
                else if (code[j] == quote)
                    return j + 1 - i;
                else
                    ++j;
            }
            return 0;
        };
        while (p < n)
        {
            const char c = code[p];
            Token t;
            t.line = fileLine;
            t.engineLine = engineLine;
            if (c == '\n' || c == '\r')
            {
                ++engineLine;
                if (c == '\n') ++fileLine;
                ++p;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\v' || c == '\f')
            {
                ++p;
                continue;
            }
            if ((c == '/' && p + 1 < n && code[p + 1] == '/') || c == '#')
            {
                while (p < n && code[p] != '\n') ++p;
                continue;
            }
            // Candidates, in rule order.
            std::size_t best = 0;
            int rule = -1;
            const auto offer = [&](const std::size_t length, const int r) {
                if (length > best) { best = length; rule = r; }
            };
            if (isLetter(c))
            {
                std::size_t j = p + 1;
                while (j < n && (isLetter(code[j]) || isDigit(code[j]))) ++j;
                offer(j - p, 13);
            }
            if (isDigit(c) || c == '.')
            {
                if (c == '0' && p + 2 < n && (code[p + 1] == 'x' || code[p + 1] == 'X') && isHex(code[p + 2]))
                {
                    std::size_t j = p + 2;
                    while (j < n && isHex(code[j])) ++j;
                    offer(j - p, 14);
                }
                if (c == '0' && digitsAt(p + 1)) offer(1 + digitsAt(p + 1), 15);
                const std::size_t d = digitsAt(p);
                if (d) offer(d, 16);
                if (d && exponentAt(p + d)) offer(d + exponentAt(p + d), 17);
                if (p + d < n && code[p + d] == '.')
                {
                    const std::size_t f = digitsAt(p + d + 1);
                    if (f) offer(d + 1 + f + exponentAt(p + d + 1 + f), 18);
                    if (d) offer(d + 1 + f + exponentAt(p + d + 1 + f), 19);
                }
            }
            if (c == '"') offer(quoted(p, '"'), 20);
            if (c == '\'') offer(quoted(p, '\''), 21);
            if (rule < 0 && p + 1 < n)
            {
                const std::string_view two = code.substr(p, 2);
                if (two == "&&") { t.kind = Tok::AndOp; best = 2; }
                else if (two == "||") { t.kind = Tok::OrOp; best = 2; }
                else if (two == "<=") { t.kind = Tok::LeOp; best = 2; }
                else if (two == ">=") { t.kind = Tok::GeOp; best = 2; }
                else if (two == "==") { t.kind = Tok::EqOp; best = 2; }
                else if (two == "!=") { t.kind = Tok::NeOp; best = 2; }
                if (best == 2)
                {
                    t.text = std::string(two);
                    tokens_.push_back(std::move(t));
                    p += 2;
                    continue;
                }
            }
            if (rule < 0)
            {
                static const std::string singles = ";{},:=()[].&!~-+*/%<>^|";
                if (singles.find(c) != std::string::npos)
                {
                    t.kind = Tok::Char;
                    t.ch = c;
                    t.text = std::string(1, c);
                    tokens_.push_back(std::move(t));
                }
                else
                {
                    if (c == '"' || c == '\'')
                    {
                        diag(CogDiagnosticSeverity::error, "unterminatedQuote", fileLine,
                            std::string(c == '"' ? "This string" : "This vector")
                                + " isn't closed on its line; the engine skips the quote and reads what follows as code.");
                    }
                    else if (!ignored.count(c))
                    {
                        ignored.emplace(c, fileLine);
                        const unsigned char u = static_cast<unsigned char>(c);
                        std::ostringstream shown;
                        if (u >= 32 && u < 127) shown << "'" << c << "'";
                        else shown << "byte 0x" << std::hex << static_cast<int>(u);
                        diag(CogDiagnosticSeverity::warning, "ignoredCharacter", fileLine,
                            "The engine ignores " + shown.str() + " in code (first seen here).");
                    }
                }
                ++p;
                continue;
            }
            const std::string_view lexeme = code.substr(p, best);
            t.text = std::string(lexeme);
            // Newlines inside strings move the file line but not the engine's count.
            const auto newlines = static_cast<std::int32_t>(std::count(lexeme.begin(), lexeme.end(), '\n'));
            p += best;
            if (rule == 13)
            {
                const std::string word = lower(lexeme);
                static const std::map<std::string, Tok> keywords{ { "break", Tok::Break }, { "continue", Tok::Continue },
                    { "do", Tok::Do }, { "else", Tok::Else }, { "for", Tok::For }, { "goto", Tok::Goto },
                    { "if", Tok::If }, { "stop", Tok::Return }, { "return", Tok::Return }, { "while", Tok::While },
                    { "call", Tok::Call } };
                if (word == "end")
                {
                    endLine_ = fileLine;
                    endOffset_ = codeStart_ + p;
                    break;
                }
                if (const auto k = keywords.find(word); k != keywords.end())
                {
                    t.kind = k->second;
                }
                else
                {
                    t.kind = Tok::Identifier;
                    t.text = word;
                    std::int32_t id = symbols_.find(word);
                    if (id < 0)
                    {
                        id = symbols_.add(word, "implicit", fileLine);
                        if (id < 0) overflowAt(fileLine, "'" + word + "'");
                    }
                    t.symbol = id;
                    if (id >= 0) yylval = id;
                    t.yylval = yylval;
                }
            }
            else if (rule >= 14 && rule <= 16)
            {
                t.kind = Tok::Int;
                bool overflow = false;
                if (rule == 14) yylval = static_cast<std::int32_t>(hexLike(lexeme));
                else if (rule == 15)
                {
                    // sscanf %i: a leading 0 means octal, and reading stops at the first 8 or 9.
                    std::uint32_t v = 0;
                    std::size_t i = 1;
                    for (; i < lexeme.size() && lexeme[i] >= '0' && lexeme[i] <= '7'; ++i) v = v * 8u + static_cast<std::uint32_t>(lexeme[i] - '0');
                    yylval = static_cast<std::int32_t>(v);
                    const std::string shown(lexeme);
                    diag(CogDiagnosticSeverity::warning, "octalNumber", fileLine,
                        shown + " starts with 0, so the engine reads it as octal: " + std::to_string(yylval) + ".");
                }
                else
                {
                    yylval = atoiLike(lexeme, &overflow);
                    if (overflow)
                        diag(CogDiagnosticSeverity::warning, "intRange", fileLine,
                            std::string(lexeme) + " doesn't fit a 32-bit int.");
                }
                t.yylval = yylval;
            }
            else if (rule >= 17 && rule <= 19)
            {
                t.kind = Tok::Float;
                const float f = atofLike(lexeme);
                std::int32_t bits = 0;
                std::memcpy(&bits, &f, sizeof bits);
                yylval = bits;
                t.yylval = bits;
            }
            else if (rule == 20)
            {
                t.kind = Tok::String;
                const std::int32_t id = symbols_.add(std::string(lexeme), "string", fileLine);
                if (id < 0) overflowAt(fileLine, "a string");
                else
                {
                    symbols_.byId(id)->value = std::string(lexeme.substr(1, lexeme.size() - 2));
                    yylval = id;
                }
                t.symbol = id;
                t.yylval = yylval;
            }
            else
            {
                t.kind = Tok::Vector;
                // sscanf "'%f %f %f'": stops at the first value it can't read; the rest stay 0.
                const std::string inner(lexeme.substr(1));
                const char* s = inner.c_str();
                int read = 0;
                for (int k = 0; k < 3; ++k)
                {
                    char* end = nullptr;
                    const double v = std::strtod(s, &end);
                    if (end == s) break;
                    t.vector[static_cast<std::size_t>(k)] = static_cast<float>(v);
                    s = end;
                    ++read;
                }
                if (read < 3)
                {
                    diag(CogDiagnosticSeverity::warning, "vectorUnreadable", fileLine,
                        "The engine reads " + std::to_string(read) + " of 3 numbers from " + std::string(lexeme)
                            + "; it wants three numbers separated by spaces, and the rest are 0.");
                }
                std::memcpy(&yylval, &t.vector[0], sizeof yylval);
                t.yylval = yylval;
            }
            tokens_.push_back(std::move(t));
            fileLine += newlines;
        }
        Token eof;
        eof.kind = Tok::Eof;
        eof.line = fileLine;
        eof.engineLine = engineLine;
        tokens_.push_back(eof);
        if (endOffset_)
        {
            const std::string_view rest = text_.substr(std::min(endOffset_, text_.size()));
            bool content = false;
            std::size_t i = 0;
            while (i < rest.size())
            {
                const char c = rest[i];
                if (c == '#' || (c == '/' && i + 1 < rest.size() && rest[i + 1] == '/'))
                {
                    while (i < rest.size() && rest[i] != '\n') ++i;
                    continue;
                }
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\0' && c != 0x1a)
                {
                    content = true;
                    break;
                }
                ++i;
            }
            if (content)
            {
                diag(CogDiagnosticSeverity::note, "textAfterEnd", endLine_,
                    "The engine stops reading code at 'end'; everything after it is ignored.");
            }
        }
        else
        {
            diag(CogDiagnosticSeverity::note, "noEnd", fileLine, "The code has no 'end'; the engine reads it to the end of the file.");
        }
    }

    void overflowAt(const std::int32_t line, const std::string& what)
    {
        if (overflowReported_) return;
        overflowReported_ = true;
        diag(CogDiagnosticSeverity::error, "symbolTableFull", line,
            "The script's symbol table is full (256 symbols, counting every string): " + what
                + " here and every new name after it compile to the wrong symbol.");
    }

    // ---- the parser (cog.y) ----
    const Token& peek(const std::size_t k = 0) const
    {
        return tokens_[std::min(pos_ + k, tokens_.size() - 1)];
    }
    static bool isChar(const Token& t, const char c) { return t.kind == Tok::Char && t.ch == c; }
    void expectChar(const char c)
    {
        if (!isChar(peek(), c)) throw SyntaxError{ peek() };
        ++pos_;
    }
    Node* make(const std::int32_t op, const std::int32_t value, Node* left, Node* right)
    {
        nodes_.push_back(std::make_unique<Node>());
        Node* node = nodes_.back().get();
        node->op = op;
        node->value = value;
        node->left = left;
        node->right = right;
        return node;
    }
    Node* leaf(const std::int32_t op, const std::int32_t value = 0) { return make(op, value, nullptr, nullptr); }

    Node* parseStatementList(const bool inBraces)
    {
        Node* list = parseStatement();
        while (peek().kind != Tok::Eof && !(inBraces && isChar(peek(), '}')))
        {
            Node* statement = parseStatement();
            list = make(kNop, 0, list, statement);
        }
        return list;
    }

    // The label slot a goto, call or label uses: the symbol's own slot (field_14).
    std::int32_t labelSlot(const Token& t, const bool definition)
    {
        Symbol* s = symbols_.byId(t.yylval);
        if (!s) return symbols_.nextLabel();
        if (s->global)
        {
            // The engine files this label under the global symbol's own slot, numbered when the game
            // registered its verbs and messages, which can land on one of this script's labels.
            auto it = globalLabels_.find(t.yylval);
            if (it == globalLabels_.end()) it = globalLabels_.emplace(t.yylval, symbols_.nextLabel()).first;
            if (definition)
            {
                diag(CogDiagnosticSeverity::warning, "labelOnGlobalName", t.line,
                    "'" + s->name + "' is a " + (s->kind == "message" ? "message this script doesn't declare"
                                                                       : s->kind == "verb" ? "verb" : "global variable")
                        + (s->kind == "message" ? ", so the engine never runs this label for it; " : "; ")
                        + "the engine also keeps this label's address in a slot belonging to the global name, which "
                          "can overwrite one of this script's own labels.");
            }
            return it->second;
        }
        return s->label;
    }

    Node* parseStatement()
    {
        const Token& t = peek();
        if (t.kind == Tok::Identifier && isChar(peek(1), ':'))
        {
            const Token label = t;
            pos_ += 2;
            const std::int32_t slot = labelSlot(label, true);
            if (Symbol* s = symbols_.byId(label.yylval))
            {
                if (s->definedAsLabel && !s->global)
                {
                    diag(CogDiagnosticSeverity::warning, "duplicateLabel", label.line,
                        "'" + s->name + "' is already a label on line " + std::to_string(s->labelLine)
                            + "; jumps and messages go to the last one.");
                }
                if (!s->definedAsLabel) s->labelLine = label.line;
                s->definedAsLabel = true;
                labelLines_[slot] = label.line;
            }
            Node* statement = parseStatement();
            Node* node = make(kNop, 0, statement, nullptr);
            node->childLabel = slot;
            return node;
        }
        if (isChar(t, '{'))
        {
            ++pos_;
            if (isChar(peek(), '}'))
            {
                ++pos_;
                return leaf(kNop);
            }
            Node* list = parseStatementList(true);
            expectChar('}');
            return list;
        }
        if (isChar(t, ';'))
        {
            ++pos_;
            return leaf(kNop);
        }
        switch (t.kind)
        {
            case Tok::If:
            {
                ++pos_;
                expectChar('(');
                Expr condition = parseExpression();
                use(condition, "the condition");
                expectChar(')');
                Node* then = parseStatement();
                if (peek().kind == Tok::Else)
                {
                    ++pos_;
                    Node* otherwise = parseStatement();
                    Node* tmp = make(kNop, 0, otherwise, nullptr);
                    tmp->childLabel = symbols_.nextLabel();
                    tmp->parentLabel = symbols_.nextLabel();
                    Node* test = make(kGoFalse, tmp->childLabel, condition.node, nullptr);
                    test = make(kGo, tmp->parentLabel, test, then);
                    return make(kNop, 0, test, tmp);
                }
                Node* tmp = make(kNop, 0, then, nullptr);
                tmp->parentLabel = symbols_.nextLabel();
                Node* test = make(kGoFalse, tmp->parentLabel, condition.node, nullptr);
                return make(kNop, 0, test, tmp);
            }
            case Tok::While:
            {
                ++pos_;
                expectChar('(');
                Expr condition = parseExpression();
                use(condition, "the condition");
                expectChar(')');
                Node* body = parseStatement();
                Node* test = make(kGoFalse, 0, condition.node, nullptr);
                Node* loop = make(kGo, 0, test, body);
                loop->parentLabel = symbols_.nextLabel();
                loop->childLabel = symbols_.nextLabel();
                test->value = loop->parentLabel;
                loop->value = loop->childLabel;
                return loop;
            }
            case Tok::Do:
            {
                ++pos_;
                Node* body = parseStatement();
                if (peek().kind != Tok::While) throw SyntaxError{ peek() };
                ++pos_;
                expectChar('(');
                Expr condition = parseExpression();
                use(condition, "the condition");
                expectChar(')');
                expectChar(';');
                Node* loop = make(kGoTrue, 0, body, condition.node);
                loop->childLabel = symbols_.nextLabel();
                loop->value = loop->childLabel;
                return loop;
            }
            case Tok::For:
            {
                const std::int32_t line = t.line;
                ++pos_;
                expectChar('(');
                bool emptyInit = false;
                bool emptyCondition = false;
                Node* init = parseExpressionStatement(emptyInit, nullptr);
                Node* condition = parseExpressionStatement(emptyCondition, "the condition");
                Expr step = parseExpression();
                expectChar(')');
                Node* body = parseStatement();
                if (emptyCondition)
                {
                    diag(CogDiagnosticSeverity::warning, "forWithoutCondition", line,
                        "This for loop has no condition. The engine doesn't treat that as 'always': it tests "
                        "whatever is on the stack, usually nothing, and skips the loop.");
                }
                Node* tmp = make(kNop, 0, body, nullptr);
                tmp->parentLabel = symbols_.nextLabel();
                Node* test = make(kGoFalse, tmp->parentLabel, condition, nullptr);
                test->childLabel = symbols_.nextLabel();
                Node* loop = make(kGo, test->childLabel, tmp, step.node);
                test->value = symbols_.nextLabel();
                loop->parentLabel = test->value;
                Node* head = make(kNop, 0, init, test);
                return make(kNop, 0, head, loop);
            }
            case Tok::Goto:
            case Tok::Call:
            {
                const bool isCall = t.kind == Tok::Call;
                ++pos_;
                const Token target = peek();
                if (target.kind != Tok::Identifier) throw SyntaxError{ target };
                ++pos_;
                expectChar(';');
                const std::int32_t slot = labelSlot(target, false);
                jumps_.push_back(Jump{ slot, target.yylval, target.line, isCall });
                return leaf(isCall ? kCall : kGo, slot);
            }
            case Tok::Return:
                ++pos_;
                expectChar(';');
                return leaf(kRet);
            default:
                break;
        }
        if (startsExpression(t))
        {
            Expr e = parseExpression();
            expectChar(';');
            if (e.node->op != kAssign && e.node->op != kCallFunc && e.node->op != kNop)
            {
                diag(CogDiagnosticSeverity::warning, "noEffect", e.line,
                    "This statement computes a value and throws it away"
                        + std::string(e.node->op == kCmpEq ? "; did you mean '=' instead of '=='?" : "."));
            }
            return e.node;
        }
        throw SyntaxError{ t };
    }

    Node* parseExpressionStatement(bool& empty, const char* context)
    {
        if (isChar(peek(), ';'))
        {
            ++pos_;
            empty = true;
            return leaf(kNop);
        }
        Expr e = parseExpression();
        if (context) use(e, context);
        expectChar(';');
        return e.node;
    }

    static bool startsExpression(const Token& t)
    {
        return t.kind == Tok::Identifier || t.kind == Tok::Int || t.kind == Tok::Float || t.kind == Tok::String
            || t.kind == Tok::Vector || isChar(t, '(') || isChar(t, '-') || isChar(t, '!');
    }

    Expr parseExpression()
    {
        Expr e = parseAssignment();
        while (isChar(peek(), ','))
        {
            ++pos_;
            Expr next = parseAssignment();
            Expr joined;
            joined.node = make(kNop, 0, e.node, next.node);
            joined.unary = false;
            joined.line = e.line;
            e = joined;
        }
        return e;
    }

    Expr parseAssignment()
    {
        Expr left = parseBinary(0);
        if (isChar(peek(), '='))
        {
            if (!left.unary) throw SyntaxError{ peek() };
            ++pos_;
            Expr right = parseAssignment();
            use(right, "the assignment");
            assignTo(left);
            Expr e;
            e.node = make(kAssign, 0, left.node, right.node);
            e.unary = false;
            e.line = left.line;
            return e;
        }
        return left;
    }

    Expr parseBinary(const std::size_t level)
    {
        struct Binary
        {
            Tok tok;
            char ch;
            std::int32_t op;
        };
        static const std::array<std::vector<Binary>, 9> levels{ {
            { { Tok::OrOp, 0, kCmpOr } },
            { { Tok::AndOp, 0, kCmpAnd } },
            { { Tok::Char, '|', kOrI } },
            { { Tok::Char, '^', kXorI } },
            { { Tok::Char, '&', kAndI } },
            { { Tok::EqOp, 0, kCmpEq }, { Tok::NeOp, 0, kCmpNe } },
            { { Tok::Char, '<', kCmpLs }, { Tok::Char, '>', kCmpGt }, { Tok::LeOp, 0, kCmpLe }, { Tok::GeOp, 0, kCmpGe } },
            { { Tok::Char, '+', kAdd }, { Tok::Char, '-', kSub } },
            { { Tok::Char, '*', kMul }, { Tok::Char, '/', kDiv }, { Tok::Char, '%', kMod } },
        } };
        if (level == levels.size()) return parseUnary();
        Expr left = parseBinary(level + 1);
        for (;;)
        {
            const Token& t = peek();
            const Binary* match = nullptr;
            for (const Binary& b : levels[level])
            {
                if (t.kind == b.tok && (b.tok != Tok::Char || t.ch == b.ch)) match = &b;
            }
            if (!match) return left;
            ++pos_;
            Expr right = parseBinary(level + 1);
            use(left, "an operator");
            use(right, "an operator");
            Expr e;
            e.node = make(match->op, 0, left.node, right.node);
            e.unary = false;
            e.line = left.line;
            left = e;
        }
    }

    Expr parseUnary()
    {
        const Token& t = peek();
        if (isChar(t, '-') || isChar(t, '!'))
        {
            const std::int32_t op = isChar(t, '-') ? kNeg : kCmpFalse;
            const std::int32_t line = t.line;
            ++pos_;
            Node* opLeaf = leaf(op);
            Expr inner = parseUnary();
            use(inner, "an operator");
            Expr e;
            e.node = make(kNop, 0, inner.node, opLeaf);
            e.line = line;
            return e;
        }
        return parsePostfix();
    }

    Expr parsePostfix()
    {
        Expr e = parsePrimary();
        for (;;)
        {
            if (isChar(peek(), '['))
            {
                ++pos_;
                Expr index = parseExpression();
                use(index, "an array index");
                expectChar(']');
                if (e.shape == Shape::Symbol) noteValueUse(e);
                Expr indexed;
                indexed.node = make(kArrayIndex, 0, e.node, index.node);
                indexed.shape = Shape::Index;
                indexed.symbol = e.symbol;
                indexed.line = e.line;
                e = indexed;
            }
            else if (isChar(peek(), '('))
            {
                ++pos_;
                std::vector<Expr> args;
                if (!isChar(peek(), ')'))
                {
                    args.push_back(parseAssignment());
                    while (isChar(peek(), ','))
                    {
                        ++pos_;
                        args.push_back(parseAssignment());
                    }
                }
                expectChar(')');
                Node* argList = nullptr;
                for (Expr& a : args)
                {
                    use(a, "an argument");
                    argList = argList ? make(kNop, 0, argList, a.node) : a.node;
                }
                Expr call;
                call.node = args.empty() ? make(kCallFunc, 0, e.node, nullptr) : make(kCallFunc, 0, argList, e.node);
                call.shape = Shape::Call;
                call.line = e.line;
                checkCall(e, args, call);
                e = call;
            }
            else
            {
                return e;
            }
        }
    }

    Expr parsePrimary()
    {
        const Token& t = peek();
        Expr e;
        e.line = t.line;
        switch (t.kind)
        {
            case Tok::Identifier:
                e.node = leaf(kPushSymbol, t.yylval);
                e.shape = Shape::Symbol;
                e.symbol = t.yylval;
                ++pos_;
                return e;
            case Tok::Int:
                e.node = leaf(kPushInt, t.yylval);
                e.shape = Shape::Constant;
                ++pos_;
                return e;
            case Tok::Float:
                e.node = leaf(kPushFloat, t.yylval);
                e.shape = Shape::Constant;
                ++pos_;
                return e;
            case Tok::String:
                e.node = leaf(kPushSymbol, t.yylval);
                e.shape = Shape::StringLiteral;
                ++pos_;
                return e;
            case Tok::Vector:
                e.node = leaf(kPushVector);
                e.node->vector = t.vector;
                e.shape = Shape::VectorLiteral;
                ++pos_;
                return e;
            default:
                break;
        }
        if (isChar(t, '('))
        {
            ++pos_;
            Expr inner = parseExpression();
            expectChar(')');
            inner.unary = true;
            return inner;
        }
        throw SyntaxError{ t };
    }

    // ---- checks the engine doesn't make ----
    void noteValueUse(const Expr& e)
    {
        Symbol* s = symbols_.byId(e.symbol);
        if (!s) return;
        s->usedAsValue = true;
        if (!s->global && s->kind == "implicit" && !s->firstUseLine) s->firstUseLine = e.line;
        if (s->global && s->kind == "verb")
        {
            diag(CogDiagnosticSeverity::warning, "verbAsValue", e.line,
                "'" + s->name + "' is a verb used as a value; did you mean to call it, " + s->name + "()?");
        }
    }

    void use(const Expr& e, const char* context)
    {
        if (e.shape == Shape::Symbol) noteValueUse(e);
        if (e.returnsNothing)
        {
            diag(CogDiagnosticSeverity::error, "noResult", e.line,
                "'" + e.callee + "' doesn't return a value, so " + context
                    + " gets whatever happens to be on the stack.");
        }
    }

    void assignTo(const Expr& target)
    {
        if (target.shape == Shape::Index) return;
        if (target.shape != Shape::Symbol)
        {
            diag(CogDiagnosticSeverity::warning, "notAssignable", target.line,
                "The engine can only assign to a variable or an array element; it ignores this assignment.");
            return;
        }
        Symbol* s = symbols_.byId(target.symbol);
        if (!s) return;
        s->assigned = true;
        if (s->global && s->kind == "verb")
        {
            diag(CogDiagnosticSeverity::error, "assignToVerb", target.line,
                "This overwrites the verb '" + s->name + "' in the engine's global table, breaking it for every "
                "script until the game restarts.");
        }
        else if (s->global && s->kind == "message")
        {
            diag(CogDiagnosticSeverity::error, "assignToMessage", target.line,
                "This overwrites the global message number '" + s->name + "' for every script until the game restarts.");
        }
        else if (!s->global && s->kind == "implicit" && !s->firstUseLine)
        {
            s->firstUseLine = target.line;
        }
    }

    void checkCall(const Expr& callee, const std::vector<Expr>& args, Expr& call)
    {
        if (callee.shape != Shape::Symbol)
        {
            diag(CogDiagnosticSeverity::warning, "callNotByName", callee.line,
                "The engine only calls a verb named directly; this call does nothing.");
            return;
        }
        Symbol* s = symbols_.byId(callee.symbol);
        if (!s) return;
        call.callee = s->name;
        if (!s->global)
        {
            const bool here = cogVerbAvailable(target_, s->name);
            const bool elsewhere = cogVerbAvailable(target_ == CogPreflightTarget::jk ? CogPreflightTarget::mots
                                                                                      : CogPreflightTarget::jk, s->name);
            if (here)
            {
                diag(CogDiagnosticSeverity::error, "verbShadowed", callee.line,
                    "'" + s->name + "' is declared in this script, which hides the verb of that name; the engine "
                    "calls nothing.");
            }
            else
            {
                diag(CogDiagnosticSeverity::error, "unknownVerb", callee.line,
                    "'" + s->name + "' isn't a " + targetName() + " verb, so the engine calls nothing"
                        + (elsewhere ? " (it is a verb in " + otherTargetName() + ")." : "."));
                s->warnedUndeclared = true;
            }
            return;
        }
        if (s->kind != "verb")
        {
            diag(CogDiagnosticSeverity::error, "notAVerb", callee.line,
                "'" + s->name + "' is a " + (s->kind == "message" ? "message number" : "global variable")
                    + ", not a verb; the engine calls nothing.");
            return;
        }
        const std::optional<CogVerbInfo> sig = cogVerbSignature(s->name);
        if (!sig) return;
        call.returnsNothing = sig->result.empty();
        if (!sig->runs.empty())
        {
            diag(CogDiagnosticSeverity::error, "verbRunsAnother", callee.line,
                "In retail " + targetName() + ", '" + s->name + "' is registered to the code of '" + sig->runs
                    + "' (an engine bug), so this call runs " + sig->runs + "() instead.");
        }
        if (sig->args.size() == 1 && sig->args[0] == "?") return;
        if (args.size() != sig->args.size())
        {
            std::string list;
            for (const std::string& a : sig->args) list += (list.empty() ? "" : ", ") + a;
            const std::string what = args.size() < sig->args.size()
                ? "it takes the missing ones from whatever is on the stack"
                : "the verb takes the last " + std::to_string(sig->args.size()) + " and the rest stay on the stack";
            diag(CogDiagnosticSeverity::error, "argumentCount", callee.line,
                "'" + s->name + "' takes " + std::to_string(sig->args.size()) + " argument"
                    + (sig->args.size() == 1 ? "" : "s") + (list.empty() ? "" : " (" + list + ")") + " but gets "
                    + std::to_string(args.size()) + " here. The engine doesn't check: " + what + ".");
            return;
        }
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            const std::string& kind = sig->args[i];
            const Expr& a = args[i];
            const bool loose = kind == "any" || kind == "symbol";
            if (a.shape == Shape::StringLiteral && kind != "string" && !loose)
            {
                diag(CogDiagnosticSeverity::warning, "argumentKind", a.line,
                    "Argument " + std::to_string(i + 1) + " of '" + s->name + "' should be a " + kind
                        + ", not a string.");
            }
            else if (a.shape == Shape::VectorLiteral && kind != "vector" && !loose)
            {
                diag(CogDiagnosticSeverity::warning, "argumentKind", a.line,
                    "Argument " + std::to_string(i + 1) + " of '" + s->name + "' should be a " + kind
                        + ", not a vector.");
            }
            else if (a.shape == Shape::Constant && (kind == "vector" || kind == "string"))
            {
                diag(CogDiagnosticSeverity::warning, "argumentKind", a.line,
                    "Argument " + std::to_string(i + 1) + " of '" + s->name + "' should be a " + kind
                        + ", not a number.");
            }
        }
    }

    void syntaxError(const Token& t)
    {
        result_.loads = false;
        diag(CogDiagnosticSeverity::error, "syntax", t.line,
            "Syntax error at " + describe(t) + ". The engine refuses the whole script (its console says "
                "\"PARSER syntax error: line " + std::to_string(t.engineLine) + "\", counting from the line after "
                "'code').");
        if (t.kind == Tok::Break || t.kind == Tok::Continue)
        {
            diag(CogDiagnosticSeverity::note, "noBreak", t.line,
                "COG has no break or continue; use goto with a label.");
        }
        else if (t.kind == Tok::Eof && tokens_.size() == 1)
        {
            diag(CogDiagnosticSeverity::note, "emptyCode", t.line,
                "The engine needs at least one statement between 'code' and 'end'; a lone 'return;' will do.");
        }
    }

    // ---- code generation (sithCogParse_GenerateLabelTable / GenerateCode) ----
    void place(Node* node, std::int32_t& at)
    {
        if (node->childLabel) setLabel(node->childLabel, at);
        if (node->left) place(node->left, at);
        if (node->right) place(node->right, at);
        at += opSize(node->op);
        if (node->parentLabel) setLabel(node->parentLabel, at);
    }

    void setLabel(const std::int32_t slot, const std::int32_t at)
    {
        if (slot < 0) return;
        if (static_cast<std::size_t>(slot) >= labels_.size()) labels_.resize(static_cast<std::size_t>(slot) + 1, -1);
        labels_[static_cast<std::size_t>(slot)] = at;
    }

    [[nodiscard]] std::int32_t labelAt(const std::int32_t slot) const
    {
        return slot >= 0 && static_cast<std::size_t>(slot) < labels_.size() ? labels_[static_cast<std::size_t>(slot)] : -1;
    }

    void emit(Node* node)
    {
        if (node->left) emit(node->left);
        if (node->right) emit(node->right);
        if (!node->op) return;
        auto& code = result_.code;
        code.push_back(node->op);
        switch (node->op)
        {
            case kPushInt:
            case kPushFloat:
            case kPushSymbol:
                code.push_back(node->value);
                break;
            case kPushVector:
                for (const float f : node->vector)
                {
                    std::int32_t bits = 0;
                    std::memcpy(&bits, &f, sizeof bits);
                    code.push_back(bits);
                }
                break;
            case kGoFalse:
            case kGoTrue:
            case kGo:
            case kCall:
                code.push_back(labelAt(node->value));
                break;
            default:
                break;
        }
    }

    void generate(Node* root)
    {
        std::int32_t at = 0;
        place(root, at);
        emit(root);
        result_.code.push_back(kRet);
        for (const std::int32_t id : handlers_)
        {
            const Symbol* s = symbols_.byId(id);
            result_.handlers.push_back(CogCompiledHandler{ s->name, s->messageId, labelAt(s->label) });
        }
        disassemble();
    }

    void disassemble()
    {
        std::map<std::int32_t, std::vector<std::string>> marks;
        for (const auto& h : result_.handlers)
        {
            if (h.address >= 0) marks[h.address].push_back(h.message + " (message)");
        }
        for (const Symbol& s : symbols_.locals())
        {
            if (s.definedAsLabel && s.kind != "message")
            {
                const std::int32_t a = labelAt(s.label);
                if (a >= 0) marks[a].push_back(s.name);
            }
        }
        const auto& code = result_.code;
        std::ostringstream out;
        std::size_t i = 0;
        while (i < code.size())
        {
            const auto at = static_cast<std::int32_t>(i);
            if (const auto m = marks.find(at); m != marks.end())
            {
                for (const std::string& name : m->second) out << name << ":\n";
            }
            const std::int32_t op = code[i++];
            out << "  " << at << "\t" << opName(op);
            const auto operand = [&]() { return i < code.size() ? code[i++] : 0; };
            switch (op)
            {
                case kPushInt:
                    out << " " << operand();
                    break;
                case kPushFloat:
                {
                    const std::int32_t bits = operand();
                    float f = 0;
                    std::memcpy(&f, &bits, sizeof f);
                    out << " " << f;
                    break;
                }
                case kPushSymbol:
                {
                    const std::int32_t id = operand();
                    const Symbol* s = symbols_.byId(id);
                    out << " " << (s ? s->name : "?") << (id >= 0x100 ? " (global)" : "");
                    break;
                }
                case kPushVector:
                {
                    out << " '";
                    for (int k = 0; k < 3; ++k)
                    {
                        const std::int32_t bits = operand();
                        float f = 0;
                        std::memcpy(&f, &bits, sizeof f);
                        out << (k ? " " : "") << f;
                    }
                    out << "'";
                    break;
                }
                case kGoFalse:
                case kGoTrue:
                case kGo:
                case kCall:
                    out << " " << operand();
                    break;
                default:
                    break;
            }
            out << "\n";
        }
        result_.disassembly = out.str();
    }

    void checkAfterParse()
    {
        for (const Jump& j : jumps_)
        {
            if (labelAt(j.slot) < 0)
            {
                const Symbol* s = symbols_.byId(j.symbol);
                diag(CogDiagnosticSeverity::error, "undefinedLabel", j.line,
                    std::string(j.isCall ? "call" : "goto") + " " + (s ? s->name : "?")
                        + ": there is no such label, so the engine jumps to address -1 and runs whatever memory "
                          "is there.");
            }
        }
        for (const Symbol& s : symbols_.locals())
        {
            if (s.kind != "implicit" || !s.firstUseLine || s.warnedUndeclared) continue;
            const bool verbElsewhere = cogVerbAvailable(target_ == CogPreflightTarget::jk ? CogPreflightTarget::mots
                                                                                          : CogPreflightTarget::jk, s.name);
            if (s.assigned)
            {
                diag(CogDiagnosticSeverity::note, "undeclared", s.firstUseLine,
                    "'" + s.name + "' isn't declared; the engine makes it an int that starts at 0. Declaring it "
                    "('int " + s.name + " local') says so.");
            }
            else
            {
                diag(CogDiagnosticSeverity::warning, "undeclaredNeverSet", s.firstUseLine,
                    "'" + s.name + "' isn't declared or assigned anywhere, so it is always 0"
                        + (verbElsewhere ? " (it is a verb in " + otherTargetName() + "; did you mean to call it?)."
                                         : "; a misspelling?"));
            }
        }
        for (const std::int32_t id : handlers_)
        {
            const Symbol* s = symbols_.byId(id);
            if (!s->definedAsLabel)
            {
                diag(CogDiagnosticSeverity::note, "noHandler", s->line,
                    "The script declares the message '" + s->name + "' but has no '" + s->name
                        + ":' label, so nothing runs when it arrives.");
            }
        }
        const auto used = static_cast<std::size_t>(std::max(0, symbols_.labelsUsed()));
        if (used >= result_.limits.labelLimit)
        {
            diag(CogDiagnosticSeverity::error, "labelTableOverrun", 0,
                "The script needs " + std::to_string(used) + " label slots (one per symbol and per if, else, while "
                    "and for), more than the " + std::to_string(result_.limits.labelLimit) + " " + targetName()
                    + " has; the engine writes past its label table.");
        }
    }

    struct Jump
    {
        std::int32_t slot;
        std::int32_t symbol;
        std::int32_t line;
        bool isCall;
    };

    std::string_view text_;
    CogPreflightTarget target_;
    SymbolTable symbols_;
    CogCompileResult result_;
    std::size_t codeStart_{};
    std::size_t endOffset_{};
    std::int32_t endLine_{};
    std::vector<Token> tokens_;
    std::size_t pos_{};
    std::vector<std::unique_ptr<Node>> nodes_;
    std::vector<std::int32_t> handlers_;
    std::size_t links_{};
    std::vector<std::int32_t> labels_;
    std::map<std::int32_t, std::int32_t> globalLabels_;
    std::map<std::int32_t, std::int32_t> labelLines_;
    std::vector<Jump> jumps_;
    bool overflowReported_{ false };
};

} // namespace

std::size_t CogCompileResult::count(const CogDiagnosticSeverity severity) const noexcept
{
    return static_cast<std::size_t>(std::count_if(diagnostics.begin(), diagnostics.end(),
        [severity](const CogDiagnostic& d) { return d.severity == severity; }));
}

CogCompileResult compileCog(const std::string_view fileName, const std::string_view text, const CogPreflightTarget target)
{
    Compiler compiler(fileName, text, target);
    return compiler.run();
}

std::string_view cogDiagnosticSeverityName(const CogDiagnosticSeverity severity) noexcept
{
    switch (severity)
    {
        case CogDiagnosticSeverity::error: return "error";
        case CogDiagnosticSeverity::warning: return "warning";
        case CogDiagnosticSeverity::note: return "note";
    }
    return "error";
}

std::optional<CogVerbInfo> cogVerbSignature(const std::string_view name)
{
    const std::string key = lower(name);
    const auto& table = cog_verbs::kVerbSignatures;
    const auto it = std::lower_bound(table.begin(), table.end(), key,
        [](const cog_verbs::VerbSignature& entry, const std::string& k) { return entry.name < k; });
    if (it == table.end() || it->name != key) return std::nullopt;
    CogVerbInfo info;
    std::string_view args = it->args;
    while (!args.empty())
    {
        const auto comma = args.find(',');
        info.args.emplace_back(args.substr(0, comma));
        if (comma == std::string_view::npos) break;
        args.remove_prefix(comma + 1);
    }
    info.result = std::string(it->result);
    info.runs = std::string(it->runs);
    return info;
}

bool cogVerbSignaturesIntact()
{
    std::string canonical;
    const auto& table = cog_verbs::kVerbSignatures;
    for (std::size_t i = 0; i < table.size(); ++i)
    {
        if (i && !(table[i - 1].name < table[i].name)) return false;
        canonical += std::string(table[i].name) + "|" + std::string(table[i].args) + "|" + std::string(table[i].result)
            + "|" + std::string(table[i].runs) + "\n";
    }
    canonical += "jk-messages\n";
    for (const auto& m : cog_verbs::kJkMessageIds) canonical += std::string(m.name) + "|" + std::to_string(m.id) + "\n";
    canonical += "mots-messages\n";
    for (const auto& m : cog_verbs::kMotsMessageIds) canonical += std::string(m.name) + "|" + std::to_string(m.id) + "\n";
    return sed::update::sha256Hex(canonical) == cog_verbs::kVerbSignaturesSha256;
}

} // namespace sed::assets
