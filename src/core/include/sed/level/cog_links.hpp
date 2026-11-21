#pragma once

#include "sed/level/cog_bindings.hpp"

#include <variant>

namespace sed::level
{

// A snapshot of authored instances and their initial parameter values.
struct CogLinkInstance
{
    std::string script;
    std::string status;
    std::optional<std::string> sha256;
    std::optional<std::size_t> archiveIndex;
    std::optional<std::string> lookupEntry;
    std::size_t knownIncomingCogReferences{};
    std::size_t knownOutgoingCogReferences{};
    std::size_t actualParameters{};
    std::optional<std::size_t> expectedParameters;
};

struct CogLinkMessage
{
    std::string name;
    std::int32_t line{};
};

struct CogLinkParameter
{
    std::size_t parameter{};
    std::string symbol;
    std::int32_t line{};
    CogBindingTarget target;
    std::string rawPrefix;
    bool rawTruncated{};
    bool eventCandidate{};
    std::int32_t linkId{};
    std::uint32_t mask{};
};

struct PlacedCogLinkRow
{
    std::size_t cog{};
    std::variant<CogLinkInstance, CogLinkMessage, CogLinkParameter> value;
};

struct PlacedCogLinks
{
    std::size_t placedCogs{};
    std::size_t typedInstances{};
    std::size_t resolvedReferences{};
    std::size_t invalidReferences{};
    std::size_t uncheckedReferences{};
    std::size_t nullReferences{};
    std::size_t candidateEventLinks{};
    std::size_t total{};
    std::vector<PlacedCogLinkRow> items;
};

/** Inspect placed COGs in document order using the regular ordered archive
 * resolver. The offset/limit select detail only. Work limits throw length_error
 * before a partial graph could be mistaken for a completed audit. */
PlacedCogLinks inspectPlacedCogLinks(const Level& level,
    const std::vector<assets::CogScriptArchive>& archives, std::size_t offset, std::size_t limit);

} // namespace sed::level
