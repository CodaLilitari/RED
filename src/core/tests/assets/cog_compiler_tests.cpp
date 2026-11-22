#include "test_common.hpp"

#include "sed/assets/cog_compiler.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace sed::tests
{
namespace
{

using sed::assets::CogCompileResult;
using sed::assets::CogDiagnosticSeverity;
using sed::assets::CogPreflightTarget;
using sed::assets::compileCog;

CogCompileResult jk(const std::string& text)
{
    return compileCog("test.cog", text, CogPreflightTarget::jk);
}

CogCompileResult mots(const std::string& text)
{
    return compileCog("test.cog", text, CogPreflightTarget::mots);
}

const sed::assets::CogDiagnostic* find(const CogCompileResult& r, const std::string_view code)
{
    for (const auto& d : r.diagnostics)
    {
        if (d.code == code) return &d;
    }
    return nullptr;
}

bool has(const CogCompileResult& r, const std::string_view code, const CogDiagnosticSeverity severity)
{
    const auto* d = find(r, code);
    return d && d->severity == severity;
}

std::size_t countOf(const CogCompileResult& r, const std::string_view code)
{
    return static_cast<std::size_t>(std::count_if(r.diagnostics.begin(), r.diagnostics.end(),
        [code](const sed::assets::CogDiagnostic& d) { return d.code == code; }));
}

std::string script(const std::string& symbols, const std::string& code)
{
    return "# test\nsymbols\n" + symbols + "end\ncode\n" + code + "end\n";
}

void testCodeGeneration()
{
    const CogCompileResult r = jk(script("message startup\nthing door\nint count=3 local\n",
        "startup:\n"
        "  if (count > 1) count = count - 1; else count = 0;\n"
        "  while (count) { count = count - 1; }\n"
        "  return;\n"));
    check(r.loads && r.count(CogDiagnosticSeverity::error) == 0 && r.count(CogDiagnosticSeverity::warning) == 0,
        "cog compiler: a plain script loads with no errors or warnings");
    const std::vector<std::int32_t> expected{ 3, 2, 1, 1, 15, 26, 17, 3, 2, 3, 2, 1, 1, 9, 6, 28, 22, 3, 2, 1, 0, 6,
        3, 2, 26, 36, 3, 2, 3, 2, 1, 1, 9, 6, 28, 22, 29, 29 };
    check(r.code == expected,
        "cog compiler: if/else, while and assignments compile to the engine's opcodes, jump targets and final return");
    check(r.handlers.size() == 1 && r.handlers[0].message == "startup" && r.handlers[0].messageId == 3
            && r.handlers[0].address == 0,
        "cog compiler: the startup handler starts at its label");
    check(r.symbols.size() == 3 && r.symbols[2].name == "count" && r.symbols[2].local && r.symbols[2].value == "3"
            && r.limits.links == 1,
        "cog compiler: symbols in engine order; a local int doesn't take a link");
    check(r.codeLine == 8, "cog compiler: the code section starts on the line after 'code'");
    check(r.disassembly.find("startup (message):") != std::string::npos
            && r.disassembly.find("gofalse 17") != std::string::npos,
        "cog compiler: the listing marks the handler and shows jump targets");

    const CogCompileResult loop = jk(script("message activated\nint i local\n",
        "activated:\n  for (i = 0; i < 3; i = i + 1) call add;\n  do i = i - 1; while (i > 0);\n  return;\nadd:\n  stop;\n"));
    check(loop.loads && loop.count(CogDiagnosticSeverity::error) == 0, "cog compiler: for, do-while, call and stop compile");
    check(std::find(loop.code.begin(), loop.code.end(), 31) != loop.code.end()
            && std::find(loop.code.begin(), loop.code.end(), 27) != loop.code.end(),
        "cog compiler: call and the do-while's gotrue are emitted");
}

void testSyntax()
{
    const CogCompileResult r = jk(script("message startup\n", "startup:\n  Sleep(1.0)\n  return;\n"));
    const auto* d = find(r, "syntax");
    check(!r.loads && d && d->line == 8 && d->message.find("'return'") != std::string::npos
            && d->message.find("line 3") != std::string::npos,
        "cog compiler: a missing semicolon refuses the script, at the next token, with the engine's own line count");
    check(!jk(script("", "")).loads && find(jk(script("", "")), "emptyCode"),
        "cog compiler: an empty code section is a syntax error in the engine");
    const CogCompileResult brk = jk(script("message startup\n", "startup:\n  while (1) break;\n  return;\n"));
    check(!brk.loads && find(brk, "noBreak"), "cog compiler: break is a syntax error in COG");
    check(!jk(script("message startup\n", "startup:\n  a + b = 3;\n  return;\n")).loads,
        "cog compiler: only a unary expression can be assigned to");
    check(!jk(script("message startup\n", "startup:\n  return 1;\n")).loads, "cog compiler: return takes no value");
    check(!jk(script("message startup\nint i local\n", "startup:\n  for (i = 0; i < 2; ) i = i + 1;\n  return;\n")).loads,
        "cog compiler: a for loop needs its third expression");
    const CogCompileResult caps = jk(script("message startup\nint X local\n", "Startup:\n  IF (x == 1) STOP; Else x = 1;\n  Return;\n"));
    check(caps.loads && caps.count(CogDiagnosticSeverity::error) == 0,
        "cog compiler: keywords and names are case-insensitive");
}

void testHeader()
{
    check(!jk("code\nend\n").loads && find(jk("code\nend\n"), "noSymbols"), "cog compiler: no symbols section is refused");
    check(!jk("symbols\nend\nend\n").loads && find(jk("symbols\nend\nend\n"), "noCode"),
        "cog compiler: 'code' must follow the symbols");
    const CogCompileResult flags = jk("flags=0x40\nsymbols\nmessage startup\nend\ncode\nstartup:\nreturn;\nend\n");
    check(flags.loads && flags.flags == 0x40, "cog compiler: the flags line is read as hexadecimal");
    const CogCompileResult header = jk(
        "; comment\n# comment\nsymbols\nthing a, thing2 # trailing\nsound s=Door.WAV local desc=Opens\n"
        "int n=0x10\nvector v='1 2 3'\nthing m mask=-1\nwidget w\nend\ncode\nreturn;\nend\n");
    check(header.loads && header.symbols.size() == 5 && header.symbols[1].value == "door.wav",
        "cog compiler: header comments, commas and lowercasing follow the engine's reader");
    check(has(header, "intDefaultHex", CogDiagnosticSeverity::warning)
            && has(header, "vectorDefaultIgnored", CogDiagnosticSeverity::warning)
            && has(header, "unknownType", CogDiagnosticSeverity::warning) && !find(header, "maskUnreadable"),
        "cog compiler: a hex int default is 0, a vector default is ignored, an unknown type is skipped, mask=-1 reads");
    const CogCompileResult zero = jk("symbols\nvector a='0 0 0' local\nvector b=(0/0/0)\nend\ncode\nreturn;\nend\n");
    check(zero.loads && !find(zero, "vectorDefaultIgnored"),
        "cog compiler: a zero vector default isn't reported, since every vector starts at zero");
    const CogCompileResult cont = jk("symbols\nthing \\\n door\nend\ncode\nreturn;\nend\n");
    check(cont.loads && cont.symbols.size() == 1 && cont.symbols[0].name == "door",
        "cog compiler: a backslash continues a header line");
}

void testLexer()
{
    const CogCompileResult r = jk(script("message startup\nint i local\nflex f local\nvector v local\n",
        "startup:\n  i = 010;\n  i = 0x1F;\n  f = .5 + 5. + 1e2;\n  v = '1 2 3';\n  return;\n"));
    check(r.loads && has(r, "octalNumber", CogDiagnosticSeverity::warning),
        "cog compiler: numbers lex as the engine reads them, and a leading 0 is octal");
    const std::vector<std::int32_t> eight{ 1, 8 };
    const std::vector<std::int32_t> thirtyOne{ 1, 31 };
    check(std::search(r.code.begin(), r.code.end(), eight.begin(), eight.end()) != r.code.end()
            && std::search(r.code.begin(), r.code.end(), thirtyOne.begin(), thirtyOne.end()) != r.code.end(),
        "cog compiler: 010 is 8 and 0x1F is 31");
    const CogCompileResult after = jk(script("message startup\n", "startup:\n  return;\n") + "stray text\n");
    check(after.loads && find(after, "textAfterEnd"), "cog compiler: code after 'end' is ignored");
    const CogCompileResult quote = jk(script("message startup\n", "startup:\n  Print(\"open);\n  return;\n"));
    check(find(quote, "unterminatedQuote") != nullptr, "cog compiler: an unclosed string is reported");
    const CogCompileResult vec = jk(script("message startup\nvector v local\n", "startup:\n  v = '(1/2/3)';\n  return;\n"));
    check(find(vec, "vectorUnreadable") != nullptr, "cog compiler: a vector the engine can't read is reported");
}

void testVerbs()
{
    const CogCompileResult unknown = jk(script("message startup\n", "startup:\n  GetWeaponBin(1);\n  return;\n"));
    const auto* d = find(unknown, "unknownVerb");
    check(unknown.loads && d && d->severity == CogDiagnosticSeverity::error && d->message.find("MotS") != std::string::npos,
        "cog compiler: a MotS-only verb in a JK script is an unknown verb");
    check(!find(mots(script("message startup\n", "startup:\n  GetWeaponBin(1);\n  return;\n")), "unknownVerb"),
        "cog compiler: the same verb is fine for MotS");
    const CogCompileResult count = jk(script("message startup\nthing a\nvector v local\n",
        "startup:\n  v = VectorSub(GetThingPos(GetLocalPlayerThing(), a));\n  return;\n"));
    check(countOf(count, "argumentCount") == 2, "cog compiler: wrong argument counts are reported for each verb");
    const CogCompileResult value = jk(script("message startup\nint i local\n", "startup:\n  i = Sleep(1);\n  return;\n"));
    check(has(value, "noResult", CogDiagnosticSeverity::error), "cog compiler: using a verb that returns nothing is an error");
    const CogCompileResult alias = jk(script("message startup\n", "startup:\n  ModifyColorEffect(1,0,0,0,1,1,1,0,0,0,1);\n  return;\n"));
    check(has(alias, "verbRunsAnother", CogDiagnosticSeverity::error) && has(alias, "argumentCount", CogDiagnosticSeverity::error),
        "cog compiler: ModifyColorEffect runs FreeColorEffect in retail");
    const CogCompileResult assign = jk(script("message startup\n", "startup:\n  randvec = 1;\n  return;\n"));
    check(has(assign, "assignToVerb", CogDiagnosticSeverity::error), "cog compiler: assigning to a verb's name is an error");
    const CogCompileResult shadow = jk(script("message startup\nint sleep local\n", "startup:\n  Sleep(1);\n  return;\n"));
    check(has(shadow, "shadowsVerb", CogDiagnosticSeverity::warning) && has(shadow, "verbShadowed", CogDiagnosticSeverity::error),
        "cog compiler: a symbol named after a verb hides it");
    const CogCompileResult kinds = jk(script("message startup\nthing a\n", "startup:\n  SetThingPos(a, 1);\n  return;\n"));
    check(has(kinds, "argumentKind", CogDiagnosticSeverity::warning), "cog compiler: a number passed for a vector is reported");
}

void testLabelsAndMessages()
{
    const CogCompileResult r = jk(script("message startup\nmessage activated\nmessage escaped\n",
        "startup:\n  goto nowhere;\n  return;\nstartup:\n  return;\npulse:\n  return;\n"));
    check(has(r, "undefinedLabel", CogDiagnosticSeverity::error), "cog compiler: goto to a missing label is an error");
    check(has(r, "duplicateLabel", CogDiagnosticSeverity::warning), "cog compiler: a repeated label is reported");
    check(has(r, "noHandler", CogDiagnosticSeverity::note), "cog compiler: a declared message without a label is noted");
    check(has(r, "unknownMessage", CogDiagnosticSeverity::error), "cog compiler: a MotS message in a JK script is reported");
    check(has(r, "labelOnGlobalName", CogDiagnosticSeverity::warning),
        "cog compiler: a label named after an undeclared message is reported");
    check(!find(mots(script("message escaped\n", "escaped:\n  return;\n")), "unknownMessage"),
        "cog compiler: MotS knows the escaped message");
    const CogCompileResult undeclared = jk(script("message startup\n", "startup:\n  if (pleyer == 1) return;\n  x = 2;\n  return;\n"));
    check(has(undeclared, "undeclaredNeverSet", CogDiagnosticSeverity::warning) && has(undeclared, "undeclared", CogDiagnosticSeverity::note),
        "cog compiler: an undeclared name that is never set is a warning; one that is set is a note");
    const CogCompileResult loop = jk(script("message startup\nint i local\n", "startup:\n  for (i = 0; ; i = i + 1) i = 1;\n  i == 2;\n  return;\n"));
    check(has(loop, "forWithoutCondition", CogDiagnosticSeverity::warning) && has(loop, "noEffect", CogDiagnosticSeverity::warning),
        "cog compiler: a for loop without a condition and a statement with no effect are reported");
}

void testLimits()
{
    std::string many;
    for (int i = 0; i < 129; ++i) many += "thing t" + std::to_string(i) + "\n";
    check(has(jk(script(many, "return;\n")), "linkTableFull", CogDiagnosticSeverity::error),
        "cog compiler: a 129th linked resource is skipped by the engine");
    std::string ints;
    for (int i = 0; i < 129; ++i) ints += "int v" + std::to_string(i) + "\n";
    check(has(jk(script(ints, "return;\n")), "linkTableOverrun", CogDiagnosticSeverity::error),
        "cog compiler: a 129th linked variable overruns the engine's link table");
    std::string locals;
    for (int i = 0; i < 256; ++i) locals += "int l" + std::to_string(i) + " local\n";
    const CogCompileResult full = jk(script(locals, "x = 1;\nreturn;\n"));
    check(has(full, "symbolTableFull", CogDiagnosticSeverity::error) && full.limits.symbols == 256,
        "cog compiler: the 257th symbol doesn't fit");
    std::string messages;
    const char* names[] = { "activated", "startup", "timer", "blocked", "entered", "exited", "crossed", "sighted",
        "damaged", "arrived", "killed", "pulse", "touched", "created", "loading", "selected", "deselected", "changed",
        "deactivated", "shutdown", "respawn", "removed", "autoselect", "aievent", "skill", "taken", "user0", "user1",
        "user2", "user3", "user4", "user5", "user6" };
    for (const char* n : names) messages += std::string("message ") + n + "\n";
    check(has(jk(script(messages, "return;\n")), "handlerTableFull", CogDiagnosticSeverity::error),
        "cog compiler: a 33rd message is skipped");
    std::string branches = "startup:\n";
    for (int i = 0; i < 260; ++i) branches += "if (i) i = 0; else i = 1;\n";
    branches += "return;\n";
    const CogCompileResult deep = jk(script("message startup\nint i local\n", branches));
    check(has(deep, "labelTableOverrun", CogDiagnosticSeverity::error), "cog compiler: JK's 512 label slots run out");
    check(!find(mots(script("message startup\nint i local\n", branches)), "labelTableOverrun"),
        "cog compiler: MotS has 1024 label slots");
}

void testValidationBudgets()
{
    const auto inspect = [](const std::string& text)
    {
        return sed::assets::compileCogForValidation("fixture.cog", text, CogPreflightTarget::jk);
    };
    std::string source = script("", "return;\n");
    source.resize(256u * 1024u, ' ');
    const auto atLimit = inspect(source);
    check(atLimit && atLimit->loads && atLimit->disassembly.empty(), "placed compiler admits the byte limit without a listing");
    source.push_back(' ');
    check(!inspect(source), "placed compiler omits a load verdict beyond the source budget");
    const auto treeAtLimit = inspect(script("", std::string(1024, ';') + "\n"));
    check(treeAtLimit && treeAtLimit->loads, "placed compiler admits the code tree at its depth limit");
    check(!inspect(script("", std::string(1025, ';') + "\n")), "placed compiler omits a load verdict beyond the tree budget");
}

} // namespace

void runCogCompilerTests()
{
    check(sed::assets::cogVerbSignaturesIntact(), "cog compiler: the generated verb signature table is intact");
    const auto sleep = sed::assets::cogVerbSignature("Sleep");
    check(sleep && sleep->args == std::vector<std::string>{ "flex" } && sleep->result.empty(),
        "cog compiler: Sleep takes one flex and returns nothing");
    const auto pos = sed::assets::cogVerbSignature("getthingpos");
    check(pos && pos->args == std::vector<std::string>{ "thing" } && pos->result == "vector",
        "cog compiler: GetThingPos takes a thing and returns a vector");
    check(!sed::assets::cogVerbSignature("notaverb"), "cog compiler: unknown names have no signature");
    const auto ijim = compileCog("a.cog", script("", "return;\n"), CogPreflightTarget::ijim);
    check(!ijim.loads && has(ijim, "targetUnmeasured", CogDiagnosticSeverity::error),
        "cog compiler: Infernal Machine scripts are refused as unmeasured");
    testCodeGeneration();
    testSyntax();
    testHeader();
    testLexer();
    testVerbs();
    testLabelsAndMessages();
    testLimits();
    testValidationBudgets();
}

} // namespace sed::tests
