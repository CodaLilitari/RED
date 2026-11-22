#include "sed/level/campaign_contracts.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
using namespace sed;
using namespace sed::level;

void check(bool condition, const char* message)
{
    if ( !condition )
    {
        throw std::runtime_error(message);
    }
}

assets::EpisodeFile episode()
{
    return {"Two levels and a cutscene", 1, {
        {10, 0, 1, "LEVEL", "arrival.jkl", 0, 0, 20, 30},
        {20, 0, 1, "CUT", "relay.smk", 0, 0, 30, -1},
        {30, 0, 2, "LEVEL", "vault.jkl", 0, 0, -1, -1}}};
}

std::vector<CampaignSequenceContract> contracts()
{
    const CampaignState ready{{"relay", true}, {"keys", std::int64_t{2}}};
    return {{10, {}, ready, ready}, {20, ready, ready, {}}, {30, ready, {}, {}}};
}

void transitions()
{
    auto declared = contracts();
    const auto good = auditCampaignContracts(episode(), declared);
    check(good.compatible == true && good.transitions.size() == 3 && good.unmodeledSequences.empty(),
        "both actual episode branches and the cutscene handoff are checked");
    check(good.transitions[1].from == 10 && good.transitions[1].to == 30 && good.transitions[1].branchB,
        "a branch targets the episode line identifier, not an array index");
    declared[0].exitB->at("relay") = false;
    const auto bad = auditCampaignContracts(episode(), declared);
    check(bad.compatible == false && bad.transitions[1].status == CampaignTransitionStatus::mismatch
        && bad.transitions[1].mismatched == 1 && bad.transitions[1].unmet[0].key == "relay",
        "a known incompatible exit guarantee retains its concrete counterexample");
    declared[0].exitB->erase("keys");
    const auto mixed = auditCampaignContracts(episode(), declared);
    check(mixed.compatible == false && mixed.transitions[1].unknown == 1,
        "a known contradiction remains false when another guarantee is missing");
    declared[0].exitB = CampaignState{{"relay", true}};
    const auto missing = auditCampaignContracts(episode(), declared);
    check(!missing.compatible && missing.transitions[1].status == CampaignTransitionStatus::incomplete
        && !missing.transitions[1].unmet[0].provided,
        "missing guarantees are unknown instead of false or silently inherited");
}

void typesAndUnknowns()
{
    auto declared = contracts();
    declared[0].exitA->at("relay") = std::int64_t{1};
    check(auditCampaignContracts(episode(), declared).compatible == false,
        "boolean and integer state contracts are distinct");
    declared = contracts();
    declared[1].exitA.reset();
    const auto noCutExit = auditCampaignContracts(episode(), declared);
    check(!noCutExit.compatible && noCutExit.transitions[2].reason == "exitContractMissing",
        "cutscenes do not implicitly preserve declared state");
    check(noCutExit.transitions[2].requirements == 2 && noCutExit.transitions[2].unknown == 2
        && noCutExit.transitions[2].unmet.size() == 2 && !noCutExit.transitions[2].unmet[0].provided,
        "a missing exit retains every known successor requirement as unknown evidence");
    declared = contracts();
    declared.erase(declared.begin() + 1);
    const auto unmodeled = auditCampaignContracts(episode(), declared);
    check(!unmodeled.compatible && unmodeled.unmodeledSequences == std::vector<int>{20}
        && unmodeled.transitions[0].reason == "targetContractMissing"
        && unmodeled.transitions[2].reason == "sourceContractMissing",
        "undeclared intermediate rows leave both adjacent handoffs unknown");
    check(unmodeled.transitions[2].requirements == 2 && unmodeled.transitions[2].unknown == 2
        && unmodeled.transitions[2].unmet.size() == 2,
        "a missing source does not erase the known target's requirements");
    auto cyclic = episode();
    cyclic.sequences[2].gotoA = 10;
    declared = contracts();
    declared[2].exitA = CampaignState{};
    check(auditCampaignContracts(cyclic, declared).compatible == true,
        "cycles are checked edge by edge without implying termination");
}

void refuses()
{
    const auto refuse = [](auto change)
    {
        auto e = episode();
        auto c = contracts();
        change(e, c);
        try
        {
            (void)auditCampaignContracts(e, c);
        }
        catch ( const std::invalid_argument& )
        {
            return;
        }
        catch ( const std::length_error& )
        {
            return;
        }
        throw std::runtime_error("invalid campaign contracts must refuse");
    };
    refuse([](auto& e, auto&) { e.sequences[0].gotoA = 99; });
    refuse([](auto&, auto& c) { c.push_back(c.front()); });
    refuse([](auto&, auto& c) { c[0].line = 99; });
    refuse([](auto&, auto& c) { c[0].entry.emplace("", true); });
    refuse([](auto&, auto& c) { c[0].entry.emplace("long", std::string(129, 'x')); });
    refuse([](auto& e, auto&) { e.sequences.resize(129); });
    refuse([](auto&, auto& c)
    {
        for ( int i = 0; i < 65; ++i )
        {
            c[0].entry.emplace("state" + std::to_string(i), true);
        }
    });
}
}

int main()
{
    try
    {
        transitions();
        typesAndUnknowns();
        refuses();
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
