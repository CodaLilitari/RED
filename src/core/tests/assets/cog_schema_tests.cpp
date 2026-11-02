
#include "test_common.hpp"

#include "sed/assets/cog_schema.hpp"
#include "sed/assets/cog_script.hpp"

#include <string>
#include <utility>
#include <vector>

namespace sed::tests
{
namespace
{

bool hasDiagnostic(const sed::assets::CogSchema& schema, const std::string_view code)
{
    for (const sed::assets::CogSchemaDiagnostic& diagnostic : schema.diagnostics)
    {
        if (diagnostic.code == code)
        {
            return true;
        }
    }
    return false;
}

std::vector<std::string> levelValueNames(const sed::assets::CogSchema& schema)
{
    std::vector<std::string> names{};
    for (const sed::assets::CogSymbol* symbol : schema.levelValues())
    {
        names.push_back(symbol->name);
    }
    return names;
}

} // namespace

void runCogSchemaTests()
{
    // A-01 (derived from Coda CogScriptTests door-shaped script): tabs,
    // defaults, locals, desc= and trailing comments.
    {
        const std::string text =
            "# Jedi Knight Cog Script\n"
            "#\n"
            "# 00_door.cog\n"
            "\n"
            "symbols\n"
            "\n"
            "message\tstartup\n"
            "message\tactivate\n"
            "\n"
            "thing\t\tdoor0\n"
            "thing\t\tdoor1\n"
            "thing\t\tplayer\t\tlocal\n"
            "sector\tinside\t\t\t// The sector that opens the door\n"
            "sound\t\tlocked=i00ky73t.wav\tlocal\n"
            "flex\t\tmoveSpeed=8.0\t\tdesc=how_fast\n"
            "int\t\tcount=3\n"
            "\n"
            "end\n"
            "\n"
            "code\n"
            "\n"
            "startup:\n"
            "\treturn;\n"
            "\n"
            "end\n";
        const sed::assets::CogSchema schema = sed::assets::parseCogSchema("00_door.cog", text);
        check(schema.isComplete(), "CogSchema.A01.complete");
        check(schema.name == "00_door.cog", "CogSchema.A01.name");
        check(schema.sourceText == text, "CogSchema.A01.sourceText");
        check(schema.symbols.size() == 9, "CogSchema.A01.symbolCount");

        const sed::assets::CogSymbol* door0 = nullptr;
        const sed::assets::CogSymbol* player = nullptr;
        const sed::assets::CogSymbol* locked = nullptr;
        const sed::assets::CogSymbol* moveSpeed = nullptr;
        const sed::assets::CogSymbol* inside = nullptr;
        for (const sed::assets::CogSymbol& symbol : schema.symbols)
        {
            if (symbol.name == "door0") door0 = &symbol;
            if (symbol.name == "player") player = &symbol;
            if (symbol.name == "locked") locked = &symbol;
            if (symbol.name == "moveSpeed") moveSpeed = &symbol;
            if (symbol.name == "inside") inside = &symbol;
        }
        check(door0 != nullptr && door0->kind == sed::assets::CogSchemaSymbolKind::thing
                && !door0->local && !door0->defaultText,
            "CogSchema.A01.door0");
        check(player != nullptr && player->local, "CogSchema.A01.playerLocal");
        check(locked != nullptr && locked->local
                && locked->defaultText && *locked->defaultText == "i00ky73t.wav",
            "CogSchema.A01.lockedDefault");
        check(moveSpeed != nullptr && moveSpeed->kind == sed::assets::CogSchemaSymbolKind::flex
                && moveSpeed->defaultText && *moveSpeed->defaultText == "8.0"
                && moveSpeed->description && *moveSpeed->description == "how_fast",
            "CogSchema.A01.moveSpeed");
        check(inside != nullptr && inside->kind == sed::assets::CogSchemaSymbolKind::sector
                && inside->description && *inside->description == "The sector that opens the door",
            "CogSchema.A01.commentDescription");
        check(levelValueNames(schema)
                == std::vector<std::string>{ "door0", "door1", "inside", "moveSpeed", "count" },
            "CogSchema.A01.levelValues");
        bool codeLabelIsSymbol = false;
        for (const sed::assets::CogSymbol& symbol : schema.symbols)
        {
            if (symbol.name.find(':') != std::string::npos)
            {
                codeLabelIsSymbol = true;
            }
        }
        check(!codeLabelIsSymbol, "CogSchema.A01.codeIgnored");
    }

    // A-02: raw declaration/modifier retention; a later linkid wins over nolink.
    {
        const std::string text =
            "symbols\n  thing door=-1 nolink,linkid=7 mask=0xFFFFFFFF desc=door # note\nend\n";
        const sed::assets::CogSchema schema = sed::assets::parseCogSchema("original.cog", text);
        check(schema.isComplete(), "CogSchema.A02.complete");
        check(schema.sourceText == text, "CogSchema.A02.sourceText");
        const std::vector<const sed::assets::CogSymbol*> values = schema.levelValues();
        check(values.size() == 1, "CogSchema.A02.levelValues");
        if (values.size() == 1)
        {
            const sed::assets::CogSymbol& symbol = *values[0];
            check(symbol.rawDeclaration
                    == "  thing door=-1 nolink,linkid=7 mask=0xFFFFFFFF desc=door # note",
                "CogSchema.A02.rawDeclaration");
            check(symbol.rawModifiers
                    == std::vector<std::string>{ "nolink", "linkid=7", "mask=0xFFFFFFFF", "desc=door" },
                "CogSchema.A02.rawModifiers");
            check(symbol.noLink, "CogSchema.A02.noLink");
            check(symbol.explicitLinkId && *symbol.explicitLinkId == 7, "CogSchema.A02.explicitLinkId");
            check(symbol.effectiveLinkId == 7, "CogSchema.A02.effectiveLinkId");
            check(symbol.explicitMask && *symbol.explicitMask == 0xFFFFFFFFu, "CogSchema.A02.explicitMask");
            check(symbol.effectiveMask == 0xFFFFFFFFu, "CogSchema.A02.effectiveMask");
            check(symbol.lineNumber == 2, "CogSchema.A02.lineNumber");
        }
    }

    // A-03: explicit modifiers stay distinct from engine defaults, both in
    // the schema and in the CogValue link fields populated by the loader.
    {
        const sed::assets::CogSchema schema = sed::assets::parseCogSchema(
            "x.cog", "symbols\nthing a\nthing b linkid=3 nolink mask=0\nend");
        check(schema.isComplete(), "CogSchema.A03.complete");
        const std::vector<const sed::assets::CogSymbol*> values = schema.levelValues();
        check(values.size() == 2, "CogSchema.A03.levelValues");
        if (values.size() == 2)
        {
            check(!values[0]->explicitLinkId && !values[0]->explicitMask
                    && values[0]->effectiveLinkId == 0 && values[0]->effectiveMask == 0x401u
                    && !values[0]->noLink,
                "CogSchema.A03.defaults");
            check(values[1]->explicitLinkId && *values[1]->explicitLinkId == 3
                    && values[1]->effectiveLinkId == -1
                    && values[1]->explicitMask && *values[1]->explicitMask == 0u
                    && values[1]->effectiveMask == 0u,
                "CogSchema.A03.explicit");
        }

        const sed::CogValue plain{};
        check(!plain.explicitLinkId && !plain.explicitMask && !plain.noLink
                && plain.effectiveLinkId() == 0 && plain.effectiveMask() == 0x401u,
            "CogSchema.A03.valueDefaults");

        const sed::assets::CogSymbolLoadResult loaded = sed::assets::loadCogSymbolsFromTextResult(
            "symbols\nthing door=-1 nolink,linkid=7 mask=0x1F\nthing plain\nend\n", true);
        check(loaded.loaded() && loaded.symbols.size() == 2, "CogSchema.A03.loaderLoaded");
        if (loaded.symbols.size() == 2)
        {
            const sed::CogValue& door = loaded.symbols[0];
            check(door.noLink && door.explicitLinkId && *door.explicitLinkId == 7
                    && door.explicitMask && *door.explicitMask == 0x1Fu && door.mask == 0x1F,
                "CogSchema.A03.loaderLinkFields");
            const sed::CogValue& unlinked = loaded.symbols[1];
            check(!unlinked.noLink && !unlinked.explicitLinkId && !unlinked.explicitMask
                    && unlinked.effectiveLinkId() == 0 && unlinked.effectiveMask() == 0x401u,
                "CogSchema.A03.loaderDefaults");
        }
    }

    // A-04: every incomplete/unsupported layout is a diagnostic, never an
    // editable schema. Codes are Coda's strings verbatim.
    {
        const std::vector<std::pair<std::string, std::string>> cases{
            { "code\nend", "missingSymbolsBlock" },
            { "symbols\nthing a", "missingSymbolsEnd" },
            { "symbols\nthing\nend", "unsupportedDeclaration" },
            { "symbols\nunknown a\nend", "unknownDeclaration" },
            { "symbols\nthing a b\nend", "unknownModifier" },
            { "symbols\nthing a,b\nend", "unsupportedDeclaration" },
            { "symbols\nthing a[2]\nend", "unsupportedDeclaration" },
            { "symbols\nthing a=\nend", "unsupportedDeclaration" },
            { "symbols\nthing a linkid=1tail\nend", "invalidModifier" },
            { "symbols\nthing a mask=0x100000000\nend", "invalidModifier" },
            { "symbols\nthing a mask=xyz\nend", "invalidModifier" },
            { "symbols\nthing a newflag=1\nend", "unknownModifier" },
            { "symbols\nthing a desc=\"unfinished\nend", "unsupportedDeclaration" },
            { "symbols\nthing a\nTHING A\nend", "duplicateSymbol" },
            { "symbols\nthing a\nend\nsymbols\nthing b\nend", "multipleSymbolBlocks" },
        };
        for (const auto& [text, code] : cases)
        {
            const sed::assets::CogSchema schema = sed::assets::parseCogSchema("x.cog", text);
            const std::string label = "CogSchema.A04." + code;
            check(!schema.isComplete(), label.c_str());
            check(hasDiagnostic(schema, code), label.c_str());
            check(schema.sourceText == text, label.c_str());
        }
    }

    // A-05: quoted defaults and comments do not shift the positional schema.
    {
        const std::string text =
            "symbols\nvector origin='1 2 3' local\nthing a desc=\"a,# // b\" # real comment\nend\n"
            "code\nthing ignored;\nend\n";
        const sed::assets::CogSchema schema = sed::assets::parseCogSchema("x.cog", text);
        check(schema.isComplete(), "CogSchema.A05.complete");
        check(schema.symbols.size() == 2, "CogSchema.A05.symbolCount");
        if (schema.symbols.size() == 2)
        {
            check(schema.symbols[0].defaultText && *schema.symbols[0].defaultText == "'1 2 3'"
                    && schema.symbols[0].local,
                "CogSchema.A05.quotedDefault");
        }
        const std::vector<const sed::assets::CogSymbol*> values = schema.levelValues();
        check(values.size() == 1 && values[0]->name == "a", "CogSchema.A05.levelValues");
        if (values.size() == 1)
        {
            check(values[0]->description && *values[0]->description == "\"a,# // b\"",
                "CogSchema.A05.quotedDescription");
        }
    }

    // A-06: unknown declarations stay available as raw declarations even
    // though no symbol is produced for them.
    {
        const sed::assets::CogSchema schema = sed::assets::parseCogSchema(
            "x.cog", "symbols\nthing a\nfuturetype b metadata=1\nend");
        check(schema.symbols.size() == 1, "CogSchema.A06.symbols");
        check(schema.rawSymbolDeclarations.size() == 2, "CogSchema.A06.rawDeclarations");
        check(schema.diagnostics.size() == 1
                && schema.diagnostics[0].code == "unknownDeclaration"
                && schema.diagnostics[0].rawDeclaration
                && *schema.diagnostics[0].rawDeclaration == "futuretype b metadata=1",
            "CogSchema.A06.diagnostic");
        check(!schema.isComplete(), "CogSchema.A06.incomplete");
    }

    // Schema-inspection budget: oversized captured text is a diagnostic,
    // not an exception and never a complete schema.
    {
        std::string text = "symbols\n";
        text.append(1024u * 1024u, 'x');
        const sed::assets::CogSchema schema = sed::assets::parseCogSchema("x.cog", text);
        check(!schema.isComplete() && hasDiagnostic(schema, "schemaTooLarge"),
            "CogSchema.budget.refused");
    }
}

} // namespace sed::tests
