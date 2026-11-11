#include "test_common.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/assets/resource_locator.hpp"
#include "sed/level.hpp"

#include <sstream>
#include <optional>
#include <string>
#include <string_view>

namespace sed::tests
{

namespace
{

// Original synthetic text, not a game-level excerpt. No resource database,
// game installation, geometry, or proprietary template parameters are needed.
// runtime_only represents a definition whose possible external script use is
// not visible to this export. Pruning it is existing behavior, not proof that
// the definition is unnecessary to a game at runtime.
std::string templateClosureSource()
{
    std::ostringstream source;
    source << "SECTION: HEADER\nVERSION 1\nEND\n"
           << "SECTION: TEMPLATES\nWORLD TEMPLATES 4\n"
           << "root_base none mod_root=opaque_root\n"
           << "actor_base root_base mod_actor=opaque_actor\n"
           << "walkplayer actor_base type=player mod_player=opaque_player\n"
           << "runtime_only none mod_runtime=opaque_runtime\n"
           << "END\nSECTION: THINGS\nWORLD THINGS 8\n";
    for ( int i = 0; i < 8; ++i )
    {
        source << i << ": walkplayer start_" << i << ' ' << i
               << " 0 0 0 0 0 -1 mod_instance=opaque_instance\n";
    }
    source << "END\n";
    return source.str();
}

const ThingTemplate* findTemplate(const Level& level, const std::string_view name)
{
    for ( const ThingTemplate& tpl : level.templates )
    {
        if ( tpl.name == name )
        {
            return &tpl;
        }
    }
    return nullptr;
}

bool hasOpaqueValue(const std::vector<TplValue>& values,
    const std::string_view name, const std::string_view value)
{
    for ( const TplValue& item : values )
    {
        if ( item.name == name && item.stringValue == value )
        {
            return true;
        }
    }
    return false;
}

void checkCogDependencyScan()
{
    Level level{};
    level.clear();
    level.kind = ProjectType::JKDF2;
    level.header.version = 1;
    check(assets::inspectJklExportBuffers(level).dependencyScanComplete,
        "JKL COG scan: no known external dependency needs no resolver");

    Cog& cog = level.newCog();
    cog.name = "synthetic_closure.cog";
    const auto unresolved = assets::inspectJklExportBuffers(level);
    check(!unresolved.dependencyScanComplete,
        "JKL COG scan: absent resolver cannot claim a scanned placed script");
    check(unresolved.cogScripts.collected == 1 && unresolved.cogs.collected == 1,
        "JKL COG scan: unavailable script still counts the known script and instance");

    assets::ResourceLocator missing{};
    assets::JklSaveOptions options{};
    options.resources = &missing;
    check(!assets::inspectJklExportBuffers(level, options).dependencyScanComplete,
        "JKL COG scan: configured resolver with missing script is incomplete");

    // An in-memory original script avoids filesystem and game-install inputs.
    // Complete here means the existing supported scan finished, not arbitrary
    // dynamic script execution or complete runtime dependency discovery.
    assets::ResourceLocator available{};
    available.setPrimaryResourceOverride("synthetic_closure.cog", []()
        -> std::optional<std::vector<std::byte>>
    {
        constexpr std::string_view script = "symbols\nend\ncode\nend\n";
        std::vector<std::byte> bytes;
        bytes.reserve(script.size());
        for ( const char value : script )
        {
            bytes.push_back(static_cast<std::byte>(value));
        }
        return bytes;
    });
    options.resources = &available;
    const auto resolved = assets::inspectJklExportBuffers(level, options);
    check(resolved.dependencyScanComplete && resolved.cogScripts.collected == 1
            && resolved.cogs.collected == 1,
        "JKL COG scan: available dependency-free script completes supported scan");
}

} // namespace

void runJklTemplateClosureTests()
{
    std::istringstream source(templateClosureSource());
    const Level imported = assets::loadFromJKL(source);
    check(imported.templates.size() == 4 && imported.things.size() == 8,
        "JKL template closure: import retains all four definitions and eight starts");

    for ( const bool compact : { false, true } )
    {
        assets::JklSaveOptions options{};
        options.compact = compact;
        check(options.templatesDb == nullptr && options.resources == nullptr,
            "JKL template closure: export uses no template database or resources");
        const assets::JklBuiltExport built = assets::buildJklExport(imported, options);
        std::istringstream output(built.text);
        const Level reopened = assets::loadFromJKL(output);

        check(reopened.templates.size() == 3,
            "JKL template closure: export emits exactly the reachable definitions");
        check(built.buffers.templates.collected == 3,
            "JKL template closure: buffer report counts emitted definitions");
        check(findTemplate(reopened, "runtime_only") == nullptr,
            "JKL template closure: unreferenced runtime-only definition is pruned");
        const ThingTemplate* player = findTemplate(reopened, "walkplayer");
        const ThingTemplate* actor = findTemplate(reopened, "actor_base");
        const ThingTemplate* root = findTemplate(reopened, "root_base");
        check(player != nullptr && player->parent == "actor_base",
            "JKL template closure: placed player definition and parent link survive");
        check(actor != nullptr && actor->parent == "root_base",
            "JKL template closure: intermediate ancestor and parent link survive");
        check(root != nullptr && root->parent == "none",
            "JKL template closure: root ancestor survives");
        check(player != nullptr && hasOpaqueValue(player->values, "mod_player", "opaque_player")
                && actor != nullptr && hasOpaqueValue(actor->values, "mod_actor", "opaque_actor")
                && root != nullptr && hasOpaqueValue(root->values, "mod_root", "opaque_root"),
            "JKL template closure: reachable unknown template parameters survive");

        check(reopened.things.size() == 8,
            "JKL template closure: all eight player starts survive");
        for ( std::size_t i = 0; i < reopened.things.size(); ++i )
        {
            const Thing& thing = reopened.things[i];
            check(thing.templateName == "walkplayer"
                    && thing.name == "start_" + std::to_string(i)
                    && thing.x == static_cast<double>(i)
                    && thing.sector == -1,
                "JKL template closure: start template, instance identity and position survive");
            check(hasOpaqueValue(thing.values, "mod_instance", "opaque_instance"),
                "JKL template closure: unknown instance parameter survives");
        }
        check(assets::buildJklExport(reopened, options).text == built.text,
            "JKL template closure: second export is stable after intentional pruning");
    }

    check(imported.templates.size() == 4 && findTemplate(imported, "runtime_only") != nullptr,
        "JKL template closure: export does not prune the editable input model");

    checkCogDependencyScan();
}

} // namespace sed::tests
