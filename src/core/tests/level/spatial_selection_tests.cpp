
#include "test_common.hpp"

#include "sed/level/spatial_selection.hpp"

#include <string>

namespace sed::tests
{
namespace
{

using namespace sed::level;

// Original synthetic fixture: eight closed box rooms in a row sharing
// walls, in the spirit of a relay outpost (no retail assets). Room i
// spans x in [2i, 2i + 2], y and z in [-1, 1]; its centre is (2i+1, 0, 0).
sed::Level makeRelayOutpost()
{
    sed::Level level{};
    for (int room = 0; room < 8; ++room)
    {
        sed::Sector sector = makeTestCubeSector(1.0 + 2.0 * room);
        sector.num = room;
        level.sectors.push_back(std::move(sector));
    }

    sed::Thing& beacon = level.newThing();
    beacon.name = "relay beacon";
    beacon.templateName = "relay_beacon";
    beacon.x = 7.0; // room 3 centre
    beacon.y = 0.0;
    beacon.z = 0.0;
    beacon.sector = 3;

    sed::Thing& firstCrate = level.newThing();
    firstCrate.name = "supply crate";
    firstCrate.x = 11.0; // room 5 centre
    firstCrate.sector = 5;

    sed::Thing& secondCrate = level.newThing();
    secondCrate.name = "supply crate";
    secondCrate.x = 11.0;
    secondCrate.sector = 5;

    return level;
}

SelectionContext fixtureContext()
{
    return SelectionContext{ "fixture-input-sha", "fixture-revision-1" };
}

sed::Level makeTwinRooms()
{
    sed::Level level{};
    level.sectors.push_back(makeTestCubeSector(0.0));
    level.sectors.push_back(makeTestCubeSector(0.0));
    return level;
}

} // namespace

void runSpatialSelectionTests()
{
    const SelectionContext context = fixtureContext();

    // --- Sector by interior point -------------------------------------

    {
        const sed::Level level = makeRelayOutpost();

        const SpatialSelection unique = selectSectorAtPoint(
            level, context, sed::TVector{ 1.0, 0.0, 0.0 });
        check(unique.match == SpatialMatch::unique,
            "spatial: interior point selects its sector uniquely");
        check(unique.token.has_value() && !unique.token->text.empty(),
            "spatial: unique sector match mints a token");
        check(unique.target.kind == SelectionTargetKind::sector
                && unique.target.sector == 0,
            "spatial: interior point resolves to room 0");

        const SpatialSelection relay = selectSectorAtPoint(
            level, context, sed::TVector{ 7.0, 0.0, 0.0 });
        check(relay.match == SpatialMatch::unique && relay.target.sector == 3,
            "spatial: relay room centre selects room 3");

        const SpatialSelection again = selectSectorAtPoint(
            level, context, sed::TVector{ 1.0, 0.0, 0.0 });
        check(again.token.has_value() && unique.token.has_value()
                && again.token->text == unique.token->text,
            "spatial: re-selection of unchanged geometry is deterministic");

        const SpatialSelection none = selectSectorAtPoint(
            level, context, sed::TVector{ 100.0, 100.0, 100.0 });
        check(none.match == SpatialMatch::none && !none.token.has_value(),
            "spatial: point outside every room is a successful none");

        // Acceptance edge: a point on the wall shared by two rooms matches
        // both rooms and must report ambiguous, never the first match.
        const SpatialSelection wall = selectSectorAtPoint(
            level, context, sed::TVector{ 2.0, 0.0, 0.0 });
        check(wall.match == SpatialMatch::ambiguous && !wall.token.has_value()
                && wall.candidateCount == 2,
            "spatial: shared-wall point is ambiguous, not first-match");

        const SpatialSelection outerWall = selectSectorAtPoint(
            level, context, sed::TVector{ 0.0, 0.0, 0.0 });
        check(outerWall.match == SpatialMatch::unique
                && outerWall.target.sector == 0,
            "spatial: point on a single room's outer wall stays unique");
    }

    // Overlapping twin rooms: even an interior point is ambiguous.
    {
        const sed::Level level = makeTwinRooms();
        const SpatialSelection centre = selectSectorAtPoint(
            level, context, sed::TVector{ 0.0, 0.0, 0.0 });
        check(centre.match == SpatialMatch::ambiguous
                && centre.candidateCount == 2 && !centre.token.has_value(),
            "spatial: overlapping rooms make an interior point ambiguous");
    }

    // --- Surface by point + outward direction --------------------------

    {
        const sed::Level level = makeRelayOutpost();
        const sed::TVector wallPoint{ 2.0, 0.0, 0.0 };

        const SpatialSelection east = selectSurfaceByPointAndDirection(
            level, context, wallPoint, sed::TVector{ 1.0, 0.0, 0.0 });
        check(east.match == SpatialMatch::unique
                && east.target.kind == SelectionTargetKind::surface
                && east.target.sector == 0 && east.target.surface == 0
                && east.token.has_value(),
            "spatial: wall point facing +x selects room 0's east face");

        const SpatialSelection west = selectSurfaceByPointAndDirection(
            level, context, wallPoint, sed::TVector{ -1.0, 0.0, 0.0 });
        check(west.match == SpatialMatch::unique && west.target.sector == 1
                && west.target.surface == 1,
            "spatial: wall point facing -x selects room 1's west face");

        const SpatialSelection sideways = selectSurfaceByPointAndDirection(
            level, context, wallPoint, sed::TVector{ 0.0, 1.0, 0.0 });
        check(sideways.match == SpatialMatch::none && !sideways.token.has_value(),
            "spatial: direction matching no outward face is a successful none");

        const SpatialSelection interior = selectSurfaceByPointAndDirection(
            level, context, sed::TVector{ 1.0, 0.0, 0.0 }, sed::TVector{ 1.0, 0.0, 0.0 });
        check(interior.match == SpatialMatch::none,
            "spatial: interior point on no face selects no surface");
    }

    {
        const sed::Level level = makeTwinRooms();
        const SpatialSelection faces = selectSurfaceByPointAndDirection(
            level, context, sed::TVector{ 1.0, 0.0, 0.0 }, sed::TVector{ 1.0, 0.0, 0.0 });
        check(faces.match == SpatialMatch::ambiguous && faces.candidateCount == 2
                && !faces.token.has_value(),
            "spatial: coincident faces with one direction are ambiguous");
    }

    // --- Vertex by point -----------------------------------------------

    {
        const sed::Level level = makeRelayOutpost();

        const SpatialSelection corner = selectVertexAtPoint(
            level, context, sed::TVector{ 0.0, -1.0, -1.0 });
        check(corner.match == SpatialMatch::unique
                && corner.target.kind == SelectionTargetKind::vertex
                && corner.target.sector == 0 && corner.target.vertex == 0
                && corner.token.has_value(),
            "spatial: outer corner selects its vertex uniquely");

        const SpatialSelection shared = selectVertexAtPoint(
            level, context, sed::TVector{ 2.0, -1.0, -1.0 });
        check(shared.match == SpatialMatch::ambiguous
                && shared.candidateCount == 2 && !shared.token.has_value(),
            "spatial: corner shared by two rooms is ambiguous");

        const SpatialSelection nearCorner = selectVertexAtPoint(
            level, context, sed::TVector{ 0.0, -1.0 + 5.0e-8, -1.0 });
        check(nearCorner.match == SpatialMatch::unique,
            "spatial: vertex within the distance tolerance still matches");

        const SpatialSelection offCorner = selectVertexAtPoint(
            level, context, sed::TVector{ 0.0, -1.0 + 1.0e-6, -1.0 });
        check(offCorner.match == SpatialMatch::none && !offCorner.token.has_value(),
            "spatial: vertex beyond the distance tolerance is a successful none");

        const SpatialSelection centre = selectVertexAtPoint(
            level, context, sed::TVector{ 1.0, 0.0, 0.0 });
        check(centre.match == SpatialMatch::none,
            "spatial: room centre matches no vertex");
    }

    // --- Thing by point --------------------------------------------------

    {
        const sed::Level level = makeRelayOutpost();

        const SpatialSelection beacon = selectThingAtPoint(
            level, context, sed::TVector{ 7.0, 0.0, 0.0 });
        check(beacon.match == SpatialMatch::unique
                && beacon.target.kind == SelectionTargetKind::thing
                && beacon.target.thing == 0 && beacon.token.has_value(),
            "spatial: beacon position selects the beacon uniquely");

        const SpatialSelection crates = selectThingAtPoint(
            level, context, sed::TVector{ 11.0, 0.0, 0.0 });
        check(crates.match == SpatialMatch::ambiguous
                && crates.candidateCount == 2 && !crates.token.has_value(),
            "spatial: stacked things at one point are ambiguous");

        const SpatialSelection empty = selectThingAtPoint(
            level, context, sed::TVector{ 5.0, 0.0, 0.0 });
        check(empty.match == SpatialMatch::none && !empty.token.has_value(),
            "spatial: room without a thing is a successful none");
    }

    // --- Token replay: valid, stale and malformed ------------------------

    {
        const sed::Level level = makeRelayOutpost();
        const SpatialSelection picked = selectSectorAtPoint(
            level, context, sed::TVector{ 1.0, 0.0, 0.0 });
        check(picked.token.has_value(), "spatial: fixture mints a sector token");
        const std::string tokenText = picked.token->text;

        const SelectionResolution valid = resolveSelectionToken(level, context, tokenText);
        check(valid.status == SelectionTokenStatus::valid
                && valid.target == picked.target && valid.code.empty(),
            "spatial: fresh token replays to its stored address");

        // Geometry change in the bound sector invalidates the token, even
        // under the same input hash and revision.
        sed::Level moved = level;
        moved.sectors[0].vertices[1].x += 0.5;
        const SelectionResolution staleGeometry = resolveSelectionToken(moved, context, tokenText);
        check(staleGeometry.status == SelectionTokenStatus::stale
                && staleGeometry.code == kStaleSelectionTokenCode,
            "spatial: geometry change makes the token stale (distinct code)");

        // Re-selection against changed (still classifiable) geometry mints
        // a fresh token that both differs from and supersedes the stale one.
        sed::Level shifted = level;
        for (sed::Vertex& vertex : shifted.sectors[0].vertices)
        {
            vertex.x += 0.25;
        }
        const SpatialSelection reselected = selectSectorAtPoint(
            shifted, context, sed::TVector{ 1.25, 0.0, 0.0 });
        check(reselected.match == SpatialMatch::unique && reselected.token.has_value()
                && reselected.token->text != tokenText,
            "spatial: re-selection after geometry change mints a fresh token");
        check(resolveSelectionToken(shifted, context, reselected.token->text).status
                == SelectionTokenStatus::valid,
            "spatial: fresh token validates against the changed document");
        check(resolveSelectionToken(shifted, context, tokenText).status
                == SelectionTokenStatus::stale,
            "spatial: superseded token stays stale on the changed document");

        // A changed document revision invalidates the token without any
        // geometry edit.
        SelectionContext otherRevision = context;
        otherRevision.sourceRevision = "fixture-revision-2";
        check(resolveSelectionToken(level, otherRevision, tokenText).status
                == SelectionTokenStatus::stale,
            "spatial: changed sourceRevision makes the token stale");

        SelectionContext otherInput = context;
        otherInput.inputSha256 = "other-input-sha";
        check(resolveSelectionToken(level, otherInput, tokenText).status
                == SelectionTokenStatus::stale,
            "spatial: changed inputSha256 makes the token stale");

        // Metadata edits (material, ambient light) keep the token valid:
        // the binding covers geometry, not presentation data.
        sed::Level redecorated = level;
        redecorated.sectors[0].surfaces[0].material = "relay_wall.mat";
        redecorated.sectors[0].ambient = sed::ColorF{ 0.5f, 0.5f, 0.5f, 1.0f };
        check(resolveSelectionToken(redecorated, context, tokenText).status
                == SelectionTokenStatus::valid,
            "spatial: material and light metadata edits keep the token valid");

        const SelectionResolution garbage = resolveSelectionToken(level, context, "not-a-token");
        check(garbage.status == SelectionTokenStatus::malformed
                && garbage.code == kMalformedSelectionTokenCode,
            "spatial: unreadable token reports the malformed code");
        check(resolveSelectionToken(level, context, "").status
                == SelectionTokenStatus::malformed,
            "spatial: empty token is malformed, never valid");
    }

    // Surface and thing tokens follow the same staleness rules.
    {
        const sed::Level level = makeRelayOutpost();
        const SpatialSelection face = selectSurfaceByPointAndDirection(
            level, context, sed::TVector{ 2.0, 0.0, 0.0 }, sed::TVector{ 1.0, 0.0, 0.0 });
        check(face.token.has_value(), "spatial: fixture mints a surface token");

        sed::Level moved = level;
        moved.sectors[0].vertices[2].y += 0.25;
        check(resolveSelectionToken(moved, context, face.token->text).status
                == SelectionTokenStatus::stale,
            "spatial: moving the bound sector stales a surface token");

        const SpatialSelection beacon = selectThingAtPoint(
            level, context, sed::TVector{ 7.0, 0.0, 0.0 });
        check(beacon.token.has_value(), "spatial: fixture mints a thing token");
        sed::Level shifted = level;
        shifted.things[0].x += 1.0;
        check(resolveSelectionToken(shifted, context, beacon.token->text).status
                == SelectionTokenStatus::stale,
            "spatial: moving a thing stales its token");
        check(resolveSelectionToken(level, context, beacon.token->text).status
                == SelectionTokenStatus::valid,
            "spatial: unmoved thing token stays valid");
    }

    // --- Refusal edges: geometry that cannot classify safely -------------

    {
        // Open shell: the top face is missing, so containment cannot be
        // proven from the remaining planes.
        sed::Level open = makeRelayOutpost();
        open.sectors[0].surfaces.erase(open.sectors[0].surfaces.begin() + 4);
        const SpatialSelection result = selectSectorAtPoint(
            open, context, sed::TVector{ 1.0, 0.0, 0.0 });
        check(result.match == SpatialMatch::cannotClassify
                && !result.token.has_value()
                && result.code == kCannotClassifySpatialCode,
            "spatial: open sector refuses with cannotClassify, no guessed token");

        // The same broken room must not poison queries in healthy rooms.
        const SpatialSelection healthy = selectSectorAtPoint(
            open, context, sed::TVector{ 3.0, 0.0, 0.0 });
        check(healthy.match == SpatialMatch::unique && healthy.target.sector == 1,
            "spatial: unclassifiable room does not block healthy rooms");
    }

    {
        // Non-planar face: one top corner lifted out of its plane.
        sed::Level warped = makeRelayOutpost();
        warped.sectors[0].vertices[6].z += 0.5;
        const SpatialSelection result = selectSectorAtPoint(
            warped, context, sed::TVector{ 1.0, 0.0, 0.0 });
        check(result.match == SpatialMatch::cannotClassify
                && !result.token.has_value(),
            "spatial: non-planar sector refuses with cannotClassify");
    }
}

} // namespace sed::tests
