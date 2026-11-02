
#include "test_common.hpp"

#include "sed/assets/cog_preflight.hpp"
#include "sed/assets/cog_registry_tables.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace sed::tests
{
namespace
{

bool hasAbsentCall(const sed::assets::CogPreflightResult& result, const std::string_view name)
{
    for (const sed::assets::CogPreflightAbsentCall& absent : result.absentCalls)
    {
        if (absent.name == name)
        {
            return true;
        }
    }
    return false;
}

const sed::assets::CogPreflightAbsentCall* findAbsentCall(
    const sed::assets::CogPreflightResult& result, const std::string_view name)
{
    for (const sed::assets::CogPreflightAbsentCall& absent : result.absentCalls)
    {
        if (absent.name == name)
        {
            return &absent;
        }
    }
    return nullptr;
}

bool hasCall(const sed::assets::CogPreflightResult& result, const std::string_view name)
{
    for (const sed::assets::CogPreflightCall& call : result.calls)
    {
        if (call.name == name)
        {
            return true;
        }
    }
    return false;
}

bool contains(const std::vector<std::string>& values, const std::string_view needle)
{
    return std::find(values.begin(), values.end(), needle) != values.end();
}

bool anyContains(const std::vector<std::string>& values, const std::string_view needle)
{
    for (const std::string& value : values)
    {
        if (value.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

std::string scriptWithCalls(const std::string& calls)
{
    return "symbols\nmessage startup\nend\ncode\nstartup:\n" + calls + "  return;\nend\n";
}

} // namespace

void runCogPreflightTests()
{
    using sed::assets::CogPreflightTarget;
    const auto jk = CogPreflightTarget::jk;
    const auto mots = CogPreflightTarget::mots;

    // Registry generation (COG_SLICE_SPEC 3.6.1): the tables are generated
    // from the measured JSON, keep JK and MotS distinct, and carry the
    // source hashes that make hand-patching detectable.
    {
        check(sed::assets::cogRegistryTablesIntact(), "CogPreflight.registry.intact");
        const sed::assets::CogRegistryInfo jkInfo = sed::assets::cogRegistryInfo(jk);
        const sed::assets::CogRegistryInfo motsInfo = sed::assets::cogRegistryInfo(mots);
        check(jkInfo.measured && jkInfo.verbCount == 451 && jkInfo.messageCount == 41
                && jkInfo.globalFloatCount == 16,
            "CogPreflight.registry.jkCounts");
        check(motsInfo.measured && motsInfo.verbCount == 538 && motsInfo.messageCount == 47
                && motsInfo.globalFloatCount == 16,
            "CogPreflight.registry.motsCounts");
        check(jkInfo.retailBinarySha256
                == "4509cd690634ba20e132bd16e713f91e144b17e2beaaad5e678f1ddb189b51d9",
            "CogPreflight.registry.jkBinaryHash");
        check(motsInfo.retailBinarySha256
                == "23cd064217f904d36d896f52d3cdd17635bb85441a12e0415c0637504191d974",
            "CogPreflight.registry.motsBinaryHash");
        check(sed::assets::cog_registry::kJkRegistrySourceJsonSha256
                == "8a9465f69ec16a26744a3b3ed882302e1d1776eebb56dcb5fb6999ffa55748b6",
            "CogPreflight.registry.jkSourceHash");
        check(sed::assets::cog_registry::kMotsRegistrySourceJsonSha256
                == "009907d89770279f398873ae5f7bff3f9104c2ad5e285d676957bacce5ecdf30",
            "CogPreflight.registry.motsSourceHash");
        check(sed::assets::cog_registry::kGlobalFloatsSourceJsonSha256
                == "a2f04a49d5f78c2617a1cdcaae49f84379f96cf7cb02de5515013d16b58d05a0",
            "CogPreflight.registry.globalsSourceHash");

        // Exactly 1 JK-only and 94 MotS-only spellings across verbs+messages.
        std::size_t jkOnly = 0;
        std::size_t motsOnly = 0;
        for (const std::string_view name : sed::assets::cog_registry::kJkVerbs)
        {
            if (!sed::assets::cogVerbAvailable(mots, name)) ++jkOnly;
        }
        for (const std::string_view name : sed::assets::cog_registry::kJkMessages)
        {
            if (!sed::assets::cogMessageAvailable(mots, name)) ++jkOnly;
        }
        for (const std::string_view name : sed::assets::cog_registry::kMotsVerbs)
        {
            if (!sed::assets::cogVerbAvailable(jk, name)) ++motsOnly;
        }
        for (const std::string_view name : sed::assets::cog_registry::kMotsMessages)
        {
            if (!sed::assets::cogMessageAvailable(jk, name)) ++motsOnly;
        }
        check(jkOnly == 1, "CogPreflight.registry.jkOnlyCount");
        check(motsOnly == 94, "CogPreflight.registry.motsOnlyCount");
        check(sed::assets::cogVerbAvailable(jk, "GETCURINVWEAPON"),
            "CogPreflight.registry.caseInsensitive");
        check(!sed::assets::cogVerbAvailable(jk, "TeleportPlayerHome"),
            "CogPreflight.registry.inventedAbsent");
    }

    // A-31: GetCurInvWeapon is JK-only; its MotS mirrors stay MotS-only.
    {
        const std::string jkScript = scriptWithCalls("  GetCurInvWeapon(0);\n");
        const sed::assets::CogPreflightResult asJk =
            sed::assets::runCogPreflight("a31.cog", jkScript, jk);
        const sed::assets::CogPreflightResult asMots =
            sed::assets::runCogPreflight("a31.cog", jkScript, mots);
        check(!hasAbsentCall(asJk, "getcurinvweapon"), "CogPreflight.A31.jkAvailable");
        const sed::assets::CogPreflightAbsentCall* absent =
            findAbsentCall(asMots, "getcurinvweapon");
        check(absent != nullptr && absent->presentInOtherRetailTable,
            "CogPreflight.A31.motsAbsentOther");

        const std::string motsScript = scriptWithCalls(
            "  GetCurInvWeapon2(0);\n  GetWeaponBin(0);\n  FireProjectileData(0);\n");
        const sed::assets::CogPreflightResult motsAsMots =
            sed::assets::runCogPreflight("a31.cog", motsScript, mots);
        const sed::assets::CogPreflightResult motsAsJk =
            sed::assets::runCogPreflight("a31.cog", motsScript, jk);
        check(motsAsMots.absentCalls.empty(), "CogPreflight.A31.motsAvailable");
        for (const char* name : { "getcurinvweapon2", "getweaponbin", "fireprojectiledata" })
        {
            const sed::assets::CogPreflightAbsentCall* mirror = findAbsentCall(motsAsJk, name);
            check(mirror != nullptr && mirror->presentInOtherRetailTable,
                "CogPreflight.A31.jkAbsentOther");
        }
    }

    // A-32: MotS-only spellings are absent for JK and clean for MotS.
    {
        const std::string text =
            "symbols\nmessage startup\nmessage enterbubble\nend\ncode\n"
            "startup:\n  jkSyncForcePowers(0);\n  FindSectorAtPos(0);\n"
            "  SendMessageExRadius(0);\n  return;\nenterbubble:\n  return;\nend\n";
        const sed::assets::CogPreflightResult asJk =
            sed::assets::runCogPreflight("a32.cog", text, jk);
        const sed::assets::CogPreflightResult asMots =
            sed::assets::runCogPreflight("a32.cog", text, mots);
        check(hasAbsentCall(asJk, "jksyncforcepowers")
                && hasAbsentCall(asJk, "findsectoratpos")
                && hasAbsentCall(asJk, "sendmessageexradius"),
            "CogPreflight.A32.jkAbsentCalls");
        check(contains(asJk.absentMessages, "enterbubble"), "CogPreflight.A32.jkAbsentMessage");
        check(asMots.absentCalls.empty() && asMots.absentMessages.empty()
                && asMots.availabilityFindings == 0,
            "CogPreflight.A32.motsClean");
    }

    // A-33: an invented spelling is absent on both targets; strict fails,
    // non-strict only reports. The record is labelled not-a-compiler and
    // pins the correct binary hash per target. Source text is unmodified.
    {
        std::string text = scriptWithCalls("  TeleportPlayerHome(0);\n");
        const std::string original = text;
        for (const auto target : { jk, mots })
        {
            const sed::assets::CogPreflightResult relaxed =
                sed::assets::runCogPreflight("a33.cog", text, target);
            const sed::assets::CogPreflightResult strict =
                sed::assets::runCogPreflight("a33.cog", text, target, { true });
            const sed::assets::CogPreflightAbsentCall* absent =
                findAbsentCall(relaxed, "teleportplayerhome");
            check(absent != nullptr && !absent->presentInOtherRetailTable,
                "CogPreflight.A33.absentBoth");
            check(!relaxed.strictFailure && strict.strictFailure,
                "CogPreflight.A33.strictMode");
            check(relaxed.method == "lexical-preflight-v3-not-a-compiler",
                "CogPreflight.A33.methodLabel");
            check(anyContains(relaxed.notes, "not an argument/type"),
                "CogPreflight.A33.notes");
            check(relaxed.retailBinarySha256
                    && *relaxed.retailBinarySha256
                        == std::string(sed::assets::cogRegistryInfo(target).retailBinarySha256),
                "CogPreflight.A33.binaryHash");
        }
        check(text == original, "CogPreflight.A33.sourceUnmodified");

        // Source identity is the SHA-256 of the supplied bytes ("abc" vector).
        const sed::assets::CogPreflightResult abc =
            sed::assets::runCogPreflight("abc.cog", "abc", jk);
        check(abc.sourceSha256
                == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "CogPreflight.A33.sourceSha256");
    }

    // A-34: cogext and dynamic-POV spellings are never retail-available.
    {
        const std::string text = scriptWithCalls("  HeapLoad();\n  jkSetPOVSprite(0);\n");
        for (const auto target : { jk, mots })
        {
            const sed::assets::CogPreflightResult result =
                sed::assets::runCogPreflight("a34.cog", text, target);
            check(hasAbsentCall(result, "heapload")
                    && hasAbsentCall(result, "jksetpovsprite"),
                "CogPreflight.A34.extensionAbsent");
            check(!sed::assets::cogVerbAvailable(target, "HeapLoad")
                    && !sed::assets::cogVerbAvailable(target, "jkSetPOVSprite"),
                "CogPreflight.A34.neverAvailable");
        }
    }

    // A-35: message aliases, global-float bindings, and handler inventory.
    {
        const std::string text =
            "symbols\nmessage startup\nmessage damaged\nmessage entered\n"
            "message onStart=startup\nmessage global0\nint counter local\nend\n"
            "code\nstartup:\n  return;\ndamaged:\n  return;\ndamaged:\n  return;\n"
            "onStart:\n  return;\nexited:\n  return;\nend\n";
        const sed::assets::CogPreflightResult result =
            sed::assets::runCogPreflight("a35.cog", text, jk);
        check(!contains(result.absentMessages, "startup"),
            "CogPreflight.A35.aliasValid");
        bool aliasRecorded = false;
        for (const sed::assets::CogPreflightDeclaration& declaration : result.declarations)
        {
            if (declaration.name == "onStart" && declaration.registeredMessage
                && *declaration.registeredMessage == "startup")
            {
                aliasRecorded = true;
            }
        }
        check(aliasRecorded, "CogPreflight.A35.aliasResolved");
        check(result.nonstandardMessageBindings.size() == 1
                && result.nonstandardMessageBindings[0].name == "global0"
                && !contains(result.absentMessages, "global0"),
            "CogPreflight.A35.globalFloatBinding");
        check(contains(result.missingMessageHandlers, "entered"),
            "CogPreflight.A35.missingHandler");
        check(result.duplicateLabels.size() == 1
                && result.duplicateLabels[0].name == "damaged"
                && result.duplicateLabels[0].lines.size() == 2
                && !contains(result.missingMessageHandlers, "damaged"),
            "CogPreflight.A35.duplicateSeparate");
        check(contains(result.undeclaredMessageHandlers, "exited"),
            "CogPreflight.A35.undeclaredHandler");
    }

    // A-36: a header apostrophe opens no phantom literal, block-comment
    // markers warn without removing their contents, and lines stay true.
    {
        const std::string text =
            "symbols\nmessage startup\nflex speed desc=where's\nend\ncode\n"
            "startup:\n  /* TeleportPlayerHome(0); */\n  SetTimer(0);\nend\n";
        const sed::assets::CogPreflightResult result =
            sed::assets::runCogPreflight("a36.cog", text, jk);
        check(result.hasSymbols && result.hasCode, "CogPreflight.A36.sections");
        bool speedDeclared = false;
        for (const sed::assets::CogPreflightDeclaration& declaration : result.declarations)
        {
            if (declaration.name == "speed" && declaration.type == "flex"
                && declaration.line == 3)
            {
                speedDeclared = true;
            }
        }
        check(speedDeclared, "CogPreflight.A36.apostropheDeclaration");
        check(hasCall(result, "settimer") && hasCall(result, "teleportplayerhome"),
            "CogPreflight.A36.callsInventoried");
        const sed::assets::CogPreflightAbsentCall* absent =
            findAbsentCall(result, "teleportplayerhome");
        check(absent != nullptr && absent->lines == std::vector<std::int32_t>{ 7 },
            "CogPreflight.A36.lineNumbers");
        check(anyContains(result.warnings, "block-comment"),
            "CogPreflight.A36.blockCommentWarning");
        check(!hasAbsentCall(result, "settimer"), "CogPreflight.A36.knownCallClean");
    }

    // A-37: IJIM has no measured registry; nothing is substituted for it.
    {
        const std::string text = scriptWithCalls("  TeleportPlayerHome(0);\n");
        const sed::assets::CogPreflightResult result = sed::assets::runCogPreflight(
            "a37.cog", text, CogPreflightTarget::ijim, { true });
        check(result.status == sed::assets::CogPreflightStatus::targetUnmeasured
                && !result.measured && result.targetName == "ijim",
            "CogPreflight.A37.targetUnmeasured");
        check(result.absentCalls.empty() && result.absentMessages.empty()
                && result.availabilityFindings == 0 && !result.strictFailure,
            "CogPreflight.A37.noSubstitutedTable");
        check(!result.retailBinarySha256 && !result.registrySourceJsonSha256,
            "CogPreflight.A37.noHashes");
        check(anyContains(result.warnings, "no measured registry"),
            "CogPreflight.A37.warning");
        check(sed::assets::parseCogPreflightTarget("IJIM")
                == CogPreflightTarget::ijim,
            "CogPreflight.A37.targetParse");
    }
}

} // namespace sed::tests
