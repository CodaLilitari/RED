#include "agent_campaign.hpp"
#include "agent_workspace.hpp"
#include "utf8_path.hpp"
#include "sed/level/campaign_contracts.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <set>
#include <sstream>
#include <stdexcept>

namespace sed_agent
{
namespace
{
namespace fs = std::filesystem;
using namespace sed::level;
constexpr std::size_t kFileLimit = 64u << 20;
constexpr std::size_t kTotalLimit = 256u << 20;

[[noreturn]] void invalid(const std::string& message)
{
    throw AgentError{kExitUsage, message, "campaignContractInvalid"};
}

void fields(const Json& object, std::initializer_list<std::string_view> allowed)
{
    if ( !object.isObject() )
    {
        invalid("Expected a campaign contract object");
    }
    for ( const auto& [key, value] : object.object )
    {
        if ( std::find(allowed.begin(), allowed.end(), key) == allowed.end() )
        {
            invalid("Unknown campaign contract field");
        }
    }
}

const Json& field(const Json& object, const char* key, Json::Type type)
{
    const auto* value = object.find(key);
    if ( !value || value->type != type )
    {
        invalid(std::string("Missing or invalid campaign field: ") + key);
    }
    return *value;
}

std::string text(const Json& object, const char* key, std::size_t maximum = 64)
{
    const auto& value = field(object, key, Json::Type::String).str;
    if ( value.empty() || value.size() > maximum || value.find('\0') != std::string::npos
        || !isValidJsonUtf8(value) )
    {
        invalid(std::string("Empty, oversized or invalid campaign string: ") + key);
    }
    return value;
}

std::size_t integer(const Json& object, const char* key, std::size_t maximum, std::size_t fallback)
{
    const auto* value = object.find(key);
    if ( !value )
    {
        return fallback;
    }
    if ( !value->isInteger() || value->number < 0 || value->number > static_cast<double>(maximum) )
    {
        invalid(std::string("Invalid campaign integer: ") + key);
    }
    return static_cast<std::size_t>(value->number);
}

const std::vector<Json>& array(const Json& object, const char* key, std::size_t maximum)
{
    const auto& rows = field(object, key, Json::Type::Array).array;
    if ( rows.size() > maximum )
    {
        throw AgentError{kExitRefusal, std::string(key) + " exceeds the campaign entry budget", "campaignContractTooLarge"};
    }
    return rows;
}

std::string id(const Json& object, const char* key)
{
    const auto value = text(object, key);
    if ( !std::all_of(value.begin(), value.end(), [](unsigned char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                || c == '.' || c == '_' || c == ':' || c == '-';
        }) )
    {
        invalid("Campaign input IDs require ASCII letters, digits, dot, underscore, colon or hyphen");
    }
    return value;
}

std::string hash(const Json& value)
{
    if ( value.type != Json::Type::String || value.str.size() != 64
        || !std::all_of(value.str.begin(), value.str.end(), [](unsigned char c)
            {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            }) )
    {
        invalid("Expected a campaign SHA-256 digest");
    }
    return sed::text::lowerAscii(value.str);
}

std::optional<std::string> optionalHash(const Json& object, const char* key)
{
    const auto* value = object.find(key);
    return value ? std::optional{hash(*value)} : std::nullopt;
}

fs::path filePath(const Json& object)
{
    const auto value = text(object, "path", 4096);
    return pathFromUtf8(value);
}

std::string resourceName(std::string value)
{
    if ( value.empty() || value.size() > 255 || !isUtf8PathText(value) )
    {
        invalid("Campaign resource names require 1..255 UTF-8 bytes without NUL");
    }
    std::replace(value.begin(), value.end(), '\\', '/');
    return sed::text::lowerAscii(value);
}

std::string captureRequired(const Json& descriptor, std::size_t limit, bool hashRequired = false)
{
    fields(descriptor, {"path", "sha256"});
    const auto expected = optionalHash(descriptor, "sha256");
    if ( hashRequired && !expected )
    {
        invalid("A campaign baseline requires its SHA-256 digest");
    }
    const auto bytes = readFileBytes(filePath(descriptor), limit);
    if ( expected && *expected != sha256Hex(bytes) )
    {
        throw AgentError{kExitConflict, "Campaign file differs from its expected digest", "campaignInputChanged"};
    }
    return bytes;
}

CampaignState state(const Json& object)
{
    if ( !object.isObject() || object.object.size() > 64 )
    {
        invalid("A campaign state requires an object with at most 64 facts");
    }
    CampaignState result;
    for ( const auto& [key, value] : object.object )
    {
        if ( !isValidJsonUtf8(key) || (value.type == Json::Type::String && !isValidJsonUtf8(value.str)) )
        {
            invalid("Campaign state keys and string values require valid UTF-8");
        }
        if ( value.type == Json::Type::Bool )
        {
            result.emplace(key, value.boolean);
        }
        else if ( value.type == Json::Type::String )
        {
            result.emplace(key, value.str);
        }
        else if ( value.isInteger() && std::abs(value.number) <= 9007199254740991.0 )
        {
            result.emplace(key, static_cast<std::int64_t>(value.number));
        }
        else
        {
            invalid("Campaign facts require booleans, exact integers or short strings");
        }
    }
    return result;
}

Json stateValue(const CampaignStateValue& value)
{
    if ( const auto* boolean = std::get_if<bool>(&value) )
    {
        return Json::makeBool(*boolean);
    }
    if ( const auto* number = std::get_if<std::int64_t>(&value) )
    {
        return Json::makeNumber(static_cast<double>(*number));
    }
    return Json::makeString(std::get<std::string>(value));
}

struct Input
{
    fs::path path;
    std::string resource;
    std::optional<std::string> expected, digest;
    std::string status{"unavailable"};
    std::size_t bytes{};
};

struct Sequence
{
    CampaignSequenceContract contract;
    Json rules;
    std::string input;
    std::set<std::string> dependencies;
};

struct BaselineRow
{
    std::optional<std::string> revision;
    std::string rules;
    std::set<std::string> inputs;
};

struct Baseline
{
    struct InputBinding
    {
        std::string resource;
        std::optional<std::string> digest;
    };
    std::string digest, episode;
    std::map<std::string, InputBinding> inputs;
    std::map<int, BaselineRow> rows;
};

Baseline baseline(const Json& descriptor)
{
    const auto bytes = captureRequired(descriptor, kMaxRequestBytes, true);
    Json report;
    try
    {
        report = parseJson(bytes);
    }
    catch ( const std::exception& )
    {
        invalid("Malformed campaign baseline JSON");
    }
    if ( !campaignNumbersAreIntegers(report) )
    {
        invalid("Campaign baseline numbers must be exact integers");
    }
    if ( !field(report, "ok", Json::Type::Bool).boolean
        || integer(report, "schemaVersion", 1, 0) != 1 || text(report, "command") != "campaign-check"
        || text(report, "scope") != "declaredCampaignContracts" )
    {
        invalid("Unsupported campaign baseline");
    }
    Baseline out;
    out.digest = sha256Hex(bytes);
    out.episode = hash(field(report, "episodeSha256", Json::Type::String));
    bool inputsComplete = true;
    for ( const auto& item : array(report, "inputs", 256) )
    {
        const auto name = id(item, "id");
        const auto* digest = item.find("sha256");
        const auto resource = resourceName(text(item, "resource", 255));
        const auto status = text(item, "status");
        const auto* size = item.find("bytes");
        if ( !digest || !size || (status != "available" && status != "unavailable" && status != "inputTooLarge") )
        {
            invalid("Invalid campaign baseline input status");
        }
        if ( status == "available" )
        {
            if ( digest->type == Json::Type::Null || !size->isInteger() || size->number < 0
                || size->number > static_cast<double>(kFileLimit) )
            {
                invalid("Campaign baseline availability disagrees with its input evidence");
            }
        }
        else
        {
            inputsComplete = false;
            if ( digest->type != Json::Type::Null || size->type != Json::Type::Null )
            {
                invalid("An unavailable campaign baseline input cannot have a digest or byte count");
            }
        }
        if ( !digest || !out.inputs.emplace(name, Baseline::InputBinding{resource,
            digest->type == Json::Type::Null ? std::nullopt : std::optional{hash(*digest)}}).second )
        {
            invalid("Invalid or repeated campaign baseline input");
        }
    }
    std::set<int> unmodeled;
    for ( const auto& line : array(report, "unmodeledSequences", 128) )
    {
        if ( !line.isInteger() || line.number < 0 || line.number > 2147483647
            || !unmodeled.insert(static_cast<int>(line.number)).second )
        {
            invalid("Invalid campaign baseline unmodeled sequence");
        }
    }
    std::size_t associations = 0;
    for ( const auto& item : array(report, "sequences", 128) )
    {
        const auto line = static_cast<int>(integer(item, "line", 2147483647, 2147483647));
        (void)field(item, "line", Json::Type::Number);
        BaselineRow row;
        row.rules = hash(field(item, "rulesSha256", Json::Type::String));
        const auto* revision = item.find("revision");
        if ( !revision )
        {
            invalid("Missing campaign baseline revision");
        }
        if ( revision->type != Json::Type::Null )
        {
            row.revision = hash(*revision);
        }
        for ( const auto& source : array(item, "inputs", 33) )
        {
            if ( source.type != Json::Type::String || !out.inputs.contains(source.str)
                || !row.inputs.insert(source.str).second || ++associations > 1024 )
            {
                invalid("Invalid campaign baseline sequence inputs");
            }
        }
        const auto type = text(item, "type");
        const auto status = text(item, "inputStatus");
        const auto file = resourceName(text(item, "file", 31));
        if ( (type != "LEVEL" && type != "CUT" && type != "DECIDE")
            || (status != "available" && status != "unavailable" && status != "unbound"
                && status != "nameMismatch" && status != "not-required")
            || (status == "not-required" && type != "DECIDE")
            || ((status == "available" || status == "unavailable" || status == "nameMismatch") && row.inputs.empty()) )
        {
            invalid("Invalid campaign baseline sequence availability");
        }
        if ( status == "available" || status == "unavailable" || status == "nameMismatch" )
        {
            const bool possiblePrimary = std::any_of(row.inputs.begin(), row.inputs.end(), [&](const auto& name)
            {
                const auto& source = out.inputs.at(name);
                return status == "nameMismatch" ? source.resource != file
                    : source.resource == file && source.digest.has_value() == (status == "available");
            });
            if ( !possiblePrimary )
            {
                invalid("Campaign baseline primary input status contradicts its associated resources");
            }
        }
        const bool modeled = !unmodeled.contains(line);
        if ( !modeled && (!row.inputs.empty() || status != (type == "DECIDE" ? "not-required" : "unbound")) )
        {
            invalid("Campaign baseline unmodeled row has authored input bindings");
        }
        const bool available = modeled && (status == "available" || status == "not-required")
            && std::all_of(row.inputs.begin(), row.inputs.end(), [&](const auto& name)
                {
                    return out.inputs.at(name).digest.has_value();
                });
        if ( row.revision.has_value() != available )
        {
            invalid("Campaign baseline revision presence disagrees with its source evidence");
        }
        inputsComplete &= available;
        if ( row.revision )
        {
            Json bindings = Json::makeArray();
            for ( const auto& name : row.inputs )
            {
                const auto& source = out.inputs.at(name);
                if ( !source.digest )
                {
                    invalid("Campaign baseline revision references unavailable input bytes");
                }
                Json binding = Json::makeObject();
                binding.set("id", Json::makeString(name));
                binding.set("resource", Json::makeString(source.resource));
                binding.set("sha256", Json::makeString(*source.digest));
                bindings.array.push_back(std::move(binding));
            }
            if ( *row.revision != sha256Hex(out.episode + row.rules + toJsonString(bindings)) )
            {
                invalid("Campaign baseline revision is inconsistent with its declared inputs");
            }
        }
        if ( !out.rows.emplace(line, std::move(row)).second )
        {
            invalid("Repeated campaign baseline line");
        }
    }
    if ( out.rows.empty() || field(report, "inputsComplete", Json::Type::Bool).boolean != inputsComplete
        || !std::all_of(unmodeled.begin(), unmodeled.end(), [&](const auto line)
            {
                return out.rows.contains(line);
            }) )
    {
        invalid("Campaign baseline coverage disagrees with its sequence evidence");
    }
    return out;
}

Json transition(const CampaignTransition& result)
{
    Json row = Json::makeObject();
    row.set("from", Json::makeNumber(result.from));
    row.set("to", Json::makeNumber(result.to));
    row.set("branch", Json::makeString(result.branchB ? "b" : "a"));
    row.set("status", Json::makeString(result.status == CampaignTransitionStatus::matches ? "matches"
        : result.status == CampaignTransitionStatus::mismatch ? "mismatch" : "incomplete"));
    row.set("reason", result.reason.empty() ? Json{} : Json::makeString(result.reason));
    row.set("requirements", Json::makeNumber(static_cast<double>(result.requirements)));
    row.set("matched", Json::makeNumber(static_cast<double>(result.matched)));
    row.set("mismatched", Json::makeNumber(static_cast<double>(result.mismatched)));
    row.set("unknown", Json::makeNumber(static_cast<double>(result.unknown)));
    Json details = Json::makeArray();
    for ( const auto& finding : result.unmet )
    {
        if ( details.array.size() == 8 )
        {
            break;
        }
        Json item = Json::makeObject();
        item.set("state", Json::makeString(finding.key));
        item.set("required", stateValue(finding.required));
        item.set("provided", finding.provided ? stateValue(*finding.provided) : Json{});
        details.array.push_back(std::move(item));
    }
    row.set("detailsTruncated", Json::makeBool(details.array.size() < result.unmet.size()));
    row.set("details", std::move(details));
    return row;
}

Json check(const Json& request)
{
    if ( !campaignNumbersAreIntegers(request) )
    {
        invalid("Campaign numbers must be exact integers");
    }
    fields(request, {"schemaVersion", "episode", "inputs", "sequences", "baseline", "offset", "limit"});
    if ( integer(request, "schemaVersion", 1, 0) != 1 )
    {
        invalid("campaign-check requires schemaVersion 1");
    }
    const auto offset = integer(request, "offset", 2147483647, 0);
    const auto limit = integer(request, "limit", 32, 16);
    if ( !limit )
    {
        invalid("Campaign transition limit must be positive");
    }
    std::map<std::string, Input> inputs;
    for ( const auto& item : array(request, "inputs", 256) )
    {
        fields(item, {"id", "path", "sha256", "resource"});
        const auto name = id(item, "id");
        Input source;
        source.path = filePath(item);
        const auto filename = source.path.filename().u8string();
        source.resource = resourceName(item.find("resource") ? text(item, "resource", 255)
            : std::string(filename.begin(), filename.end()));
        source.expected = optionalHash(item, "sha256");
        if ( !inputs.emplace(name, std::move(source)).second )
        {
            invalid("Repeated campaign input ID");
        }
    }
    std::map<int, Sequence> sequences;
    std::vector<CampaignSequenceContract> contracts;
    std::size_t associations = 0;
    for ( const auto& item : array(request, "sequences", 128) )
    {
        fields(item, {"line", "input", "dependsOn", "entry", "exits"});
        Sequence row;
        (void)field(item, "line", Json::Type::Number);
        row.contract.line = static_cast<int>(integer(item, "line", 2147483647, 0));
        row.contract.entry = state(field(item, "entry", Json::Type::Object));
        const auto& exits = field(item, "exits", Json::Type::Object);
        fields(exits, {"a", "b"});
        if ( const auto* value = exits.find("a") )
        {
            row.contract.exitA = state(*value);
        }
        if ( const auto* value = exits.find("b") )
        {
            row.contract.exitB = state(*value);
        }
        if ( item.find("input") )
        {
            row.input = id(item, "input");
            row.dependencies.insert(row.input);
        }
        if ( item.find("dependsOn") )
        {
            std::set<std::string> declared;
            for ( const auto& value : array(item, "dependsOn", 32) )
            {
                if ( value.type != Json::Type::String || !declared.insert(value.str).second )
                {
                    invalid("Campaign dependencies require distinct input IDs");
                }
                row.dependencies.insert(value.str);
            }
        }
        associations += row.dependencies.size();
        if ( associations > 1024 )
        {
            throw AgentError{kExitRefusal, "Campaign dependencies exceed 1024 associations", "campaignContractTooLarge"};
        }
        for ( const auto& name : row.dependencies )
        {
            if ( !inputs.contains(name) )
            {
                invalid("Campaign sequence references an undeclared input");
            }
        }
        row.rules = item;
        Json dependencies = Json::makeArray();
        for ( const auto& name : row.dependencies )
        {
            dependencies.array.push_back(Json::makeString(name));
        }
        row.rules.set("dependsOn", std::move(dependencies));
        contracts.push_back(row.contract);
        if ( !sequences.emplace(row.contract.line, std::move(row)).second )
        {
            invalid("Repeated campaign sequence line");
        }
    }
    const auto episodeBytes = captureRequired(field(request, "episode", Json::Type::Object), kMaxRequestBytes);
    const auto episodeHash = sha256Hex(episodeBytes);
    std::istringstream stream(episodeBytes);
    const auto episode = sed::assets::loadFromEpisodeJk(stream, 128);
    for ( const auto& row : episode.sequences )
    {
        if ( !isValidJsonUtf8(row.fileName) )
        {
            throw AgentError{kExitRefusal, "Campaign episode resource names require UTF-8 for JSON receipts",
                "campaignEpisodeEncoding"};
        }
    }
    const auto audit = auditCampaignContracts(episode, contracts);
    std::optional<Baseline> prior;
    if ( const auto* previous = request.find("baseline") )
    {
        prior = baseline(*previous);
    }
    std::size_t remaining = kTotalLimit;
    bool inputsComplete = true;
    Json inputRows = Json::makeArray();
    Json inputModel = Json::makeArray();
    for ( auto& [name, source] : inputs )
    {
        try
        {
            const auto bytes = readFileBytes(source.path, std::min(remaining, kFileLimit));
            source.bytes = bytes.size();
            remaining -= bytes.size();
            source.digest = sha256Hex(bytes);
            source.status = "available";
            if ( source.expected && source.expected != source.digest )
            {
                throw AgentError{kExitConflict, "Campaign input differs from its expected digest: " + name, "campaignInputChanged"};
            }
        }
        catch ( const AgentError& error )
        {
            if ( error.code != kExitInput )
            {
                throw;
            }
            source.status = error.errorCode == "inputTooLarge" ? "inputTooLarge" : "unavailable";
            inputsComplete = false;
        }
        Json record = Json::makeObject();
        record.set("id", Json::makeString(name));
        record.set("resource", Json::makeString(source.resource));
        inputModel.array.push_back(record);
        record.set("sha256", source.digest ? Json::makeString(*source.digest) : Json{});
        record.set("bytes", source.digest ? Json::makeNumber(static_cast<double>(source.bytes)) : Json{});
        record.set("status", Json::makeString(source.status));
        inputRows.array.push_back(std::move(record));
    }
    Json sequenceRows = Json::makeArray();
    Json contractModel = Json::makeObject();
    contractModel.set("inputs", std::move(inputModel));
    Json declarations = Json::makeArray();
    for ( const auto& [line, sequence] : sequences )
    {
        declarations.array.push_back(sequence.rules);
    }
    contractModel.set("sequences", std::move(declarations));
    for ( const auto& row : episode.sequences )
    {
        const auto declaration = sequences.find(row.lineNumber);
        const Sequence* declared = declaration == sequences.end() ? nullptr : &declaration->second;
        const auto type = sed::text::upperAscii(row.type);
        std::string resourceStatus = type == "DECIDE" ? "not-required" : "unbound";
        if ( declared && !declared->input.empty() )
        {
            const auto& source = inputs.at(declared->input);
            resourceStatus = source.resource != resourceName(row.fileName) ? "nameMismatch"
                : source.digest ? "available" : "unavailable";
        }
        bool available = declared && (resourceStatus == "available" || resourceStatus == "not-required");
        Json rules = Json::makeObject();
        rules.set("sequence", declared ? declared->rules : Json{});
        rules.set("resource", declared && !declared->input.empty()
            ? Json::makeString(inputs.at(declared->input).resource) : Json{});
        Json successors = Json::makeArray();
        for ( const auto line : {row.gotoA, row.gotoB} )
        {
            Json next = Json::makeObject();
            next.set("line", Json::makeNumber(line));
            const auto target = sequences.find(line);
            next.set("entry", target == sequences.end() ? Json{} : target->second.rules.object.at("entry"));
            successors.array.push_back(std::move(next));
        }
        rules.set("successors", std::move(successors));
        const auto rulesHash = sha256Hex(toJsonString(rules));
        Json bindings = Json::makeArray();
        Json ids = Json::makeArray();
        const std::set<std::string> names = declared ? declared->dependencies : std::set<std::string>{};
        for ( const auto& name : names )
        {
            ids.array.push_back(Json::makeString(name));
            Json binding = Json::makeObject();
            binding.set("id", Json::makeString(name));
            const auto& source = inputs.at(name);
            available &= source.digest.has_value();
            binding.set("resource", Json::makeString(source.resource));
            binding.set("sha256", source.digest ? Json::makeString(*source.digest) : Json{});
            bindings.array.push_back(std::move(binding));
        }
        inputsComplete &= available;
        const auto revision = available ? std::optional{sha256Hex(episodeHash + rulesHash + toJsonString(bindings))}
            : std::nullopt;
        Json changed = Json::makeArray();
        bool rulesChanged = false;
        const BaselineRow* old = nullptr;
        if ( prior )
        {
            const auto found = prior->rows.find(row.lineNumber);
            old = found == prior->rows.end() ? nullptr : &found->second;
        }
        if ( old )
        {
            rulesChanged = old->rules != rulesHash;
            auto compared = names;
            compared.insert(old->inputs.begin(), old->inputs.end());
            for ( const auto& name : compared )
            {
                if ( !names.contains(name) || !old->inputs.contains(name)
                    || inputs.at(name).digest != prior->inputs.at(name).digest
                    || inputs.at(name).resource != prior->inputs.at(name).resource )
                {
                    changed.array.push_back(Json::makeString(name));
                }
            }
        }
        const char* freshness = !revision ? "unavailable" : !prior ? "uncompared" : !old ? "untracked"
            : old->revision == revision ? "current" : "stale";
        Json result = Json::makeObject();
        result.set("line", Json::makeNumber(row.lineNumber));
        result.set("type", Json::makeString(type));
        result.set("file", Json::makeString(row.fileName));
        result.set("inputStatus", Json::makeString(resourceStatus));
        result.set("rulesSha256", Json::makeString(rulesHash));
        result.set("revision", revision ? Json::makeString(*revision) : Json{});
        result.set("inputs", std::move(ids));
        result.set("freshness", Json::makeString(freshness));
        result.set("changedInputs", std::move(changed));
        result.set("rulesChanged", old ? Json::makeBool(rulesChanged) : Json{});
        result.set("episodeChanged", prior ? Json::makeBool(prior->episode != episodeHash) : Json{});
        sequenceRows.array.push_back(std::move(result));
    }
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("campaign-check"));
    out.set("schemaVersion", Json::makeNumber(1));
    out.set("scope", Json::makeString("declaredCampaignContracts"));
    out.set("runtimeVerified", Json::makeBool(false));
    out.set("scriptsEvaluated", Json::makeBool(false));
    out.set("initialStateVerified", Json::makeBool(false));
    out.set("episodeSha256", Json::makeString(episodeHash));
    out.set("contractSha256", Json::makeString(sha256Hex(toJsonString(contractModel))));
    out.set("baselineSha256", prior ? Json::makeString(prior->digest) : Json{});
    out.set("inputsComplete", Json::makeBool(inputsComplete));
    out.set("inputs", std::move(inputRows));
    out.set("sequences", std::move(sequenceRows));
    out.set("declaredStateCompatible", audit.compatible ? Json::makeBool(*audit.compatible) : Json{});
    Json unmodeled = Json::makeArray();
    for ( const auto line : audit.unmodeledSequences )
    {
        unmodeled.array.push_back(Json::makeNumber(line));
    }
    out.set("unmodeledSequences", std::move(unmodeled));
    Json rows = Json::makeArray();
    for ( std::size_t i = offset; i < audit.transitions.size() && rows.array.size() < limit; ++i )
    {
        rows.array.push_back(transition(audit.transitions[i]));
    }
    out.set("totalTransitions", Json::makeNumber(static_cast<double>(audit.transitions.size())));
    out.set("offset", Json::makeNumber(static_cast<double>(offset)));
    const auto end = offset + rows.array.size();
    out.set("nextOffset", end < audit.transitions.size() ? Json::makeNumber(static_cast<double>(end)) : Json{});
    out.set("transitions", std::move(rows));
    if ( toJsonString(out).size() > nativeReceiptLimit() )
    {
        throw AgentError{kExitRefusal, "Campaign receipt exceeds its byte budget", "campaignReportTooLarge"};
    }
    return out;
}
}

Json campaignCheck(const std::vector<std::string>& args)
{
    if ( args.size() != 1 )
    {
        invalid("Usage: red-agent campaign-check <request.json|->");
    }
    if ( !isUtf8PathText(args[0]) )
    {
        invalid("Campaign request paths require valid UTF-8 without NUL");
    }
    const auto request = readRequestFile(pathFromUtf8(args[0]));
    try
    {
        return check(request);
    }
    catch ( const std::length_error& error )
    {
        throw AgentError{kExitRefusal, error.what(), "campaignContractTooLarge"};
    }
    catch ( const std::exception& error )
    {
        invalid(isValidJsonUtf8(error.what()) ? error.what() : "Invalid campaign input with legacy byte names");
    }
}
}
