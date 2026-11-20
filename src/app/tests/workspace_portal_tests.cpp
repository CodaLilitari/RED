#include "workspace_portal_fixture.hpp"
#include "ui/shortcuts/workspace_selection.hpp"
#include "sed/assets/sed.hpp"
#include "sed/editor/document.hpp"
#include "sed/level/cleave.hpp"
#include "sed/level/merge.hpp"
#include <QTemporaryDir>

using namespace sed;
using namespace sed::workspace_test;
using sed::tests::check;

static void verifyPortals(const Level& current, const Level& original, int expectedPerRoom)
{
    check(current.kind == ProjectType::MOTS, "retains the MotS target");
    for (int side : {0,1})
    {
        const auto& room = current.sectors.at(side);
        const auto& reference = original.sectors.at(side).surfaces.at(side);
        int fragments = 0;
        for (int index=0; index < static_cast<int>(room.surfaces.size()); ++index)
        {
            const auto& face = room.surfaces[index];
            if (!face.adjoin) continue;
            ++fragments;
            check(face.adjoin->sector == 1-side &&
                current.sectors.at(1-side).surfaces.at(face.adjoin->surface).adjoin == SurfaceRef{side,index},
                "portal links return to the exact owning fragment");
            check(face.material == reference.material && face.faceFlags == reference.faceFlags &&
                face.surfaceFlags == reference.surfaceFlags && face.adjoinFlags == reference.adjoinFlags &&
                face.uScale == reference.uScale && face.vScale == reference.vScale, "each side keeps its own settings");
            for (std::size_t i=0; i<face.vertices.size(); ++i)
            {
                const auto wanted = portalCorner(side,room.vertices.at(face.vertices[i]));
                const auto& stored = face.texVertices.at(i);
                check(stored.u == wanted.u && stored.v == wanted.v && stored.color == wanted.color,
                    "authored and interpolated corners keep wrapped texture coordinates and baked colors");
            }
        }
        check(fragments == expectedPerRoom, "all expected reciprocal fragments present");
    }
    check(current.cogs.size() == 1 && current.cogs.at(0).name == "switch.cog" &&
        current.cogs.at(0).values.at(0).cogType == CogType::surface &&
        current.cogs.at(0).values.at(0).intValue == 0, "placed switch script still targets its door");
}

int main()
{
    const auto original = twoRoomPortal();
    editor::DocumentState start;
    start.level = original;
    start.selection.surfaces.insert(front);
    editor::Document document;
    document.clear(start);
    const auto identity = ui::inspectSelection(original, start.selection);
    check(!identity.identity.isEmpty() && identity.sectorIds.contains(100) && identity.hasBounds, "selected portal has stable identity and bounds");
    check(document.applyUndoable("Portal cleave", [](auto& state)
        { return level::levelCleaveSurface(state.level, front, {0,0,1}, 0,0,0); }), "cleave reciprocal portal");
    verifyPortals(document.level(), original, 2);
    check(ui::inspectSelection(document.level(),start.selection).identity != identity.identity,
        "saved face selection refuses a changed boundary");
    const std::array<SurfaceRef,2> pair{front, SurfaceRef{0,6}};
    check(document.applyUndoable("Merge portal", [&](auto& state)
    {
        const auto result = level::levelMergeSelectedSurfaces(state.level, pair, level::SurfaceMergePolicy::keepSurvivorMapping);
        return result && result->mergeCount == 1;
    }), "explicitly merge selected portal faces");
    verifyPortals(document.level(), original, 1);
    check(document.undo(), "undo portal merge");
    verifyPortals(document.level(), original, 2);
    check(document.redo(), "redo portal merge");
    QTemporaryDir directory;
    const auto path = std::filesystem::path(directory.path().toStdString())/"wrapped.sed";
    assets::saveToSED(document.level(),path);
    verifyPortals(assets::loadFromSED(path), original, 1);

    auto broken = original;
    broken.sectors[back.sector].surfaces[back.surface].adjoin.reset();
    check(ui::missingAdjoinBacklink(broken,front) == back, "diagnostics recognizes one missing return link");
    broken.sectors[0].surfaces[2].adjoin = back;
    check(!ui::missingAdjoinBacklink(broken,front), "diagnostics refuses a second claimant");
    return tests::failures == 0 ? 0 : 1;
}
