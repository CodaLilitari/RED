#include "sed/assets/jo_ja_projectile_audit.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
using namespace sed;
using namespace sed::assets;

std::string status(const Level& level, std::string_view weapon, std::string_view root)
{
    for ( const auto& finding : auditJoJaProjectiles(level, level.kind) )
    {
        if ( finding.weapon == weapon && finding.projectile == root )
        {
            return finding.status;
        }
    }
    return "absent";
}

void check(bool condition)
{
    if ( !condition )
    {
        throw std::runtime_error("Unexpected projectile audit result");
    }
}
}

int main()
{
    Level level;
    const auto bryar = [&] { return status(level, "stock-start/bryar", "+bryarbolt"); };
    check(bryar() == "missing-template");
    TplValue type;
    type.name = "type";
    type.stringValue = "weapon";
    level.templates.push_back({"_weapon", "none", {type}});
    level.templates.push_back({"+BRYARBOLT", "_weapon", {}});
    check(bryar() == "bound-static");
    level.templates[1].parent = "_missing";
    check(bryar() == "unresolved-dependency");
    level.templates[1].parent = "_weapon";
    TplValue explode;
    explode.name = "explode";
    explode.adjType = AdjType::tpl;
    explode.stringValue = "+impact";
    level.templates[1].values.push_back(explode);
    check(bryar() == "unresolved-dependency");
    level.templates.push_back({"+impact", "none", {}});
    check(bryar() == "unverified"); // forward reference
    std::swap(level.templates[1], level.templates[2]);
    check(bryar() == "bound-static");
    level.templates[0].values[0].stringValue = "item";
    check(bryar() == "unverified");
    level.things.emplace_back().templateName = "CROSSBOW";
    check(status(level, "crossbow", "+crossbowbolt3") == "missing-template");
    level.things.emplace_back().templateName = "lightsaber";
    check(status(level, "lightsaber", "") == "not-applicable");
    level.kind = ProjectType::MOTS;
    check(status(level, "stock-start/fists", "+bigpunch") == "missing-template");
    level.templates.push_back({"+impact", "none", {}});
    check(bryar() == "unverified");
    std::cout << "Projectile audit tests passed\n";
}
