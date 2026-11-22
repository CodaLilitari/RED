#include "sed/assets/jo_ja_projectile_audit.hpp"

#include "sed/text.hpp"

#include <map>
#include <set>
#include <string_view>

namespace sed::assets
{
namespace
{
using Roots = std::map<std::string, std::vector<std::string>>;

// Ordinary stock firing-mode roots from JK weap_*.cog and MotS weap_*_m.cog.
// Inventory overrides, force powers, special MotS malfunction effects and custom
// firing COGs are not inferred from a pickup name.
const Roots kCommon{
    {"bryarpistol", {"+bryarbolt"}}, {"strifle", {"+stlaser"}},
    {"detonator", {"+grenade1", "+grenade2"}}, {"singledetonator", {"+grenade1", "+grenade2"}},
    {"crossbow", {"+crossbowbolt", "+crossbowbolt2", "+crossbowbolt3"}},
    {"repeatergun", {"+repeaterball"}}, {"railgun", {"+raildet", "+raildet2"}},
    {"seqcharge", {"+seqchrg", "+seqchrg2"}}, {"singleseqcharge", {"+seqchrg", "+seqchrg2"}},
    {"concrifle", {"+concbullet", "+concblast2"}}, {"lightsaber", {}}
};

using Index = std::map<std::string, const ThingTemplate*>;

JoJaProjectileFinding inspect(const Index& index, const std::string& weapon, const std::string& root)
{
    JoJaProjectileFinding result{weapon, root, "bound-static", "Stock root and template references present; firing not tested."};
    std::vector<std::string> pending{root};
    std::set<std::string> visited;
    while ( !pending.empty() )
    {
        const auto name = pending.back();
        pending.pop_back();
        if ( !visited.insert(name).second )
        {
            continue;
        }
        const auto it = index.find(name);
        if ( it == index.end() )
        {
            result.status = name == root ? "missing-template" : "unresolved-dependency";
            result.detail = "Missing level template: " + name;
            return result;
        }
        if ( !it->second )
        {
            return {weapon, root, "unverified", "Duplicate level template: " + name};
        }
        const auto add = [&](std::string_view dep)
        {
            const auto key = text::lowerAscii(dep);
            if ( !key.empty() && key != "none" )
            {
                pending.push_back(key);
            }
        };
        add(it->second->parent);
        for ( const auto& value : it->second->values )
        {
            if ( value.adjType == AdjType::tpl )
            {
                add(value.stringValue);
            }
        }
    }
    // A cycle/forward reference cannot be certified as a resolved runtime binding.
    for ( const auto& name : visited )
    {
        const auto* row = index.at(name);
        const auto earlier = [&](std::string_view dep)
        {
            const auto key = text::lowerAscii(dep);
            return key.empty() || key == "none" || index.at(key) < row;
        };
        if ( !earlier(row->parent) )
        {
            return {weapon, root, "unverified", "Parent not declared earlier than " + name};
        }
        for ( const auto& value : row->values )
        {
            if ( value.adjType == AdjType::tpl && !earlier(value.stringValue) )
            {
                return {weapon, root, "unverified", "Template reference not declared earlier than " + name};
            }
        }
    }
    for ( const ThingTemplate* row = index.at(root); row != nullptr; )
    {
        for ( auto it = row->values.rbegin(); it != row->values.rend(); ++it )
        {
            if ( text::iequalsAscii(it->name, "type") )
            {
                if ( !text::iequalsAscii(it->stringValue, "weapon") )
                {
                    return {weapon, root, "unverified", "Root does not inherit type=weapon."};
                }
                return result;
            }
        }
        const auto parent = text::lowerAscii(row->parent);
        row = parent.empty() || parent == "none" ? nullptr : index.at(parent);
    }
    return {weapon, root, "unverified", "No effective type=weapon found."};
}
} // namespace

std::vector<JoJaProjectileFinding> auditJoJaProjectiles(const Level& level, ProjectType target)
{
    if ( target != ProjectType::JKDF2 && target != ProjectType::MOTS )
    {
        return {{"", "", "unverified", "No stock projectile profile for target."}};
    }
    Index index;
    for ( const auto& row : level.templates )
    {
        auto [it, inserted] = index.emplace(text::lowerAscii(row.name), &row);
        if ( !inserted )
        {
            it->second = nullptr;
        }
    }
    Roots roots = kCommon;
    roots["stock-start/bryar"] = {"+bryarbolt"};
    roots["stock-start/fists"] = {"+punch"};
    std::set<std::string> weapons{"stock-start/bryar", "stock-start/fists"};
    if ( target == ProjectType::MOTS )
    {
        roots["stock-start/fists"].push_back("+bigpunch");
        roots["blastechpistol"] = {"+bryarbolt"};
        roots["scope"] = {"+sclaser"};
        roots["eweb"] = {"+ewebround"};
        roots["seqcharge"] = roots["singleseqcharge"] = {"+seqchrg", "+seqchrg2", "+seqchrg3", "+seqchrg4"};
    }
    std::vector<JoJaProjectileFinding> result;
    for ( const auto& thing : level.things )
    {
        const auto name = text::lowerAscii(thing.templateName);
        if ( roots.contains(name) )
        {
            weapons.insert(name);
        }
        else if ( target == ProjectType::MOTS && (name == "railseek" || name == "carbgun") )
        {
            if ( weapons.insert(name).second )
            {
                result.push_back({name, "", "unverified", "Special weapon profile not modeled."});
            }
        }
    }
    for ( const auto& weapon : weapons )
    {
        const auto found = roots.find(weapon);
        if ( found == roots.end() )
        {
            continue;
        }
        if ( found->second.empty() )
        {
            result.push_back({weapon, "", "not-applicable", "Stock saber has no fired projectile template."});
        }
        for ( const auto& root : found->second )
        {
            result.push_back(inspect(index, weapon, root));
        }
    }
    return result;
}

} // namespace sed::assets
