#include "agent_cog_links.hpp"

#include "agent_dependencies.hpp"
#include "agent_schema.hpp"
#include "agent_workspace.hpp"

#include <algorithm>

namespace sed_agent
{
namespace
{

[[noreturn]] void invalid(const char* message)
{
    throw AgentError{ kExitUsage, message, "cogLinksInvalidRequest" };
}

void checkFields(const Json& value, std::initializer_list<std::string_view> allowed)
{
    if ( !value.isObject() )
    {
        invalid("COG link request and archive entries must be objects");
    }
    for ( const auto& field : value.object )
    {
        if ( std::find(allowed.begin(), allowed.end(), field.first) == allowed.end() )
        {
            invalid("Unknown field in COG link request or archive entry");
        }
    }
}

std::size_t integer(const Json* value, std::size_t minimum, std::size_t maximum)
{
    if ( !value || !value->isInteger() || value->number < static_cast<double>(minimum)
        || value->number > static_cast<double>(maximum) )
    {
        invalid("COG link version or page value is missing or outside its integer range");
    }
    return static_cast<std::size_t>(value->number);
}

std::string digest(const Json* value)
{
    if ( !value || value->type != Json::Type::String || value->str.size() != 64
        || value->str.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos )
    {
        invalid("COG link request requires a 64-digit hexadecimal SHA-256 for every input");
    }
    return sed::text::lowerAscii(value->str);
}

Json count(std::size_t value)
{
    return Json::makeNumber(static_cast<double>(value));
}

Json bytes(const std::string& text)
{
    return Json::makeString(encodeDependencyText(text));
}

void addValues(Json& row, const sed::level::CogLinkInstance& instance)
{
    row.set("record", Json::makeString("instance"));
    row.set("script", bytes(instance.script));
    row.set("status", Json::makeString(instance.status));
    row.set("knownIncomingCogReferences", count(instance.knownIncomingCogReferences));
    row.set("knownOutgoingCogReferences", count(instance.knownOutgoingCogReferences));
    row.set("actualParameters", count(instance.actualParameters));
    row.set("expectedParameters", instance.expectedParameters ? count(*instance.expectedParameters) : Json{});
    row.set("scriptSha256", instance.sha256 ? Json::makeString(*instance.sha256) : Json{});
    row.set("archiveIndex", instance.archiveIndex ? count(*instance.archiveIndex) : Json{});
    row.set("lookupEntry", instance.lookupEntry ? bytes(*instance.lookupEntry) : Json{});
}

void addValues(Json& row, const sed::level::CogLinkMessage& message)
{
    row.set("record", Json::makeString("messageDeclaration"));
    row.set("symbol", bytes(message.name));
    row.set("line", Json::makeNumber(message.line));
}

void addValues(Json& row, const sed::level::CogLinkParameter& parameter)
{
    row.set("record", Json::makeString("binding"));
    row.set("parameter", count(parameter.parameter));
    row.set("symbol", bytes(parameter.symbol));
    row.set("line", Json::makeNumber(parameter.line));
    row.set("kind", Json::makeString(std::string(sed::level::cogReferenceKindName(parameter.target.kind))));
    row.set("status", Json::makeString(std::string(sed::level::cogBindingTargetStatusName(parameter.target.status))));
    if ( parameter.target.index ) row.set("index", Json::makeNumber(*parameter.target.index));
    if ( parameter.target.sectorIndex ) row.set("sector", Json::makeNumber(*parameter.target.sectorIndex));
    if ( parameter.target.surfaceIndex ) row.set("surface", Json::makeNumber(*parameter.target.surfaceIndex));
    if ( !parameter.target.detail.empty() ) row.set("detail", bytes(parameter.target.detail));
    row.set("rawPrefix", bytes(parameter.rawPrefix));
    row.set("rawTruncated", Json::makeBool(parameter.rawTruncated));
    row.set("eventLinkCandidate", Json::makeBool(parameter.eventCandidate));
    row.set("effectiveLinkId", Json::makeNumber(parameter.linkId));
    row.set("effectiveMask", Json::makeNumber(parameter.mask));
}

} // namespace

CogLinksQuery parseCogLinksRequest(const Json& request)
{
    checkFields(request, { "schemaVersion", "inputSha256", "resourceArchives", "offset", "limit" });
    (void)integer(request.find("schemaVersion"), 1, 1);
    CogLinksQuery query;
    query.inputSha256 = digest(request.find("inputSha256"));
    const auto* list = request.find("resourceArchives");
    if ( !list || list->type != Json::Type::Array || list->array.empty() || list->array.size() > 16 )
    {
        invalid("COG links require one to sixteen ordered resourceArchives");
    }
    for ( const auto& archive : list->array )
    {
        checkFields(archive, { "path", "sha256" });
        (void)digest(archive.find("sha256"));
        const auto* path = archive.find("path");
        if ( !path || path->type != Json::Type::String )
        {
            invalid("COG link archive requires a path string");
        }
        validateDependencyPath(path->str);
    }
    if ( const auto* value = request.find("offset") ) query.offset = integer(value, 0, 2147483647);
    if ( const auto* value = request.find("limit") ) query.limit = integer(value, 1, 256);
    return query;
}

void validateCogLinksRequest(const Json& request)
{
    (void)parseCogLinksRequest(request);
}

Json cogLinksReceipt(const sed::level::PlacedCogLinks& report, const CogLinksQuery& query)
{
    Json result = Json::makeObject();
    result.set("command", Json::makeString("cog-links"));
    result.set("ok", Json::makeBool(true));
    result.set("schemaVersion", count(1));
    result.set("inputSha256", Json::makeString(query.inputSha256));
    result.set("placedCogs", count(report.placedCogs));
    result.set("typedInstances", count(report.typedInstances));
    result.set("resolvedReferences", count(report.resolvedReferences));
    result.set("invalidReferences", count(report.invalidReferences));
    result.set("uncheckedReferences", count(report.uncheckedReferences));
    result.set("nullReferences", count(report.nullReferences));
    result.set("candidateEventLinks", count(report.candidateEventLinks));
    result.set("runtimeVerified", Json::makeBool(false));
    result.set("eventLinkSemantics", Json::makeString(std::string(sed::level::kCogEventLinkSemantics)));
    result.set("unverifiedScopes", parseJson(R"(["scriptControlFlow","dynamicSenders","engineEvents","templateAndStaticCogs","multiplayerDelivery"])"));
    result.set("textEncoding", Json::makeString("ascii-byte-escape-v1"));
    result.set("total", count(report.total));
    result.set("offset", count(query.offset));
    result.set("limit", count(query.limit));
    const auto next = query.offset + report.items.size();
    result.set("nextOffset", next < report.total ? count(next) : Json{});
    Json items = Json::makeArray();
    for ( const auto& item : report.items )
    {
        Json row = Json::makeObject();
        row.set("cog", count(item.cog));
        std::visit([&](const auto& value) { addValues(row, value); }, item.value);
        items.array.push_back(std::move(row));
    }
    result.set("items", std::move(items));
    return result;
}

} // namespace sed_agent
