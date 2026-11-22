#pragma once

#include "agent_json.hpp"
#include "sed/level/cog_links.hpp"

namespace sed_agent
{

struct CogLinksQuery
{
    std::string inputSha256;
    std::size_t offset{};
    std::size_t limit{128};
};

CogLinksQuery parseCogLinksRequest(const Json& request);
Json cogLinksReceipt(const sed::level::PlacedCogLinks& report, const CogLinksQuery& query);

} // namespace sed_agent
