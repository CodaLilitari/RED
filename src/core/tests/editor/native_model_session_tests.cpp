#include "sed/editor/native_model_session.hpp"
#include "sed/update/updater.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

using namespace sed::editor;

namespace
{
void require(const bool condition, const char* message)
{
    if ( !condition )
    {
        throw std::runtime_error(message);
    }
}

template <class Fn>
void refuses(Fn attempt, const std::string_view diagnostic = {})
{
    try
    {
        attempt();
    }
    catch ( const std::runtime_error& error )
    {
        require(std::string_view(error.what()).find(diagnostic) != std::string_view::npos,
            "Unexpected refusal diagnostic.");
        return;
    }
    throw std::runtime_error("Expected refusal: " + std::string(diagnostic));
}

void replace(std::string& text, const std::string_view before, const std::string_view after)
{
    const auto at = text.find(before);
    require(at != text.npos, "Fixture anchor missing.");
    text.replace(at, before.size(), after);
}

std::string fixture()
{
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
        .parent_path().parent_path() / "tests/fixtures/relay-sentry/relay_sentry.3do";
    std::ifstream stream(path, std::ios::binary);
    require(stream.good(), "Owned relay-sentry fixture missing.");
    std::string source{ std::istreambuf_iterator<char>(stream), {} };
    replace(source, "1: 0x0000 0x00001 1 0", "1: 0x0000 0x00001 0 0");
    replace(source, "NAME boom", "NAME base");
    replace(source, "0: 0 0\n1: 1 0", "0: -0.000 0.00 # exact origin\r\n1: 1 0");
    return source;
}

void commitUv(NativeModelSession& session, const NativeModelSelection selected, const float u, const float v)
{
    session.setSelection(selected);
    session.beginTextureVertexEdit();
    session.previewTextureVertex(u, v);
    require(session.apply(), "Expected effective UV commit.");
}

void checkSelectionAndPreview(const std::string& source)
{
    auto session = NativeModelSession::open(source);
    require(session.committed().source() == source && session.displayed().source() == source,
        "Opening changed source bytes.");
    require(session.originalSourceSha256() == sed::update::sha256Hex(source)
            && session.originalSourceBytes() == source.size(), "Original capture identity missing.");
    require(!session.selection() && !session.dirty() && !session.canUndo() && !session.canRedo(),
        "Initial session state is not clean and empty.");
    const auto& lod = session.committed().geosets()[0];
    require(lod.meshes[0].name == lod.meshes[1].name && lod.nodes[0].mesh == 0 && lod.nodes[1].mesh == 0,
        "Fixture must contain duplicate mesh names, shared mesh 0 and orphan mesh 1.");
    refuses([&] { session.beginMeshTransform(); }, "selection");
    for ( const auto bad : { NativeModelSelection{ 2, 0, {} }, NativeModelSelection{ 0, 2, {} },
             NativeModelSelection{ 0, 0, 999 } } )
    {
        refuses([&] { session.setSelection(bad); }, "selection");
        require(!session.selection(), "Bad selection mutated the current selection.");
    }
    const NativeModelSelection selected{ 1, 1, {} };
    session.setSelection(selected);
    refuses([&] { session.beginTextureVertexEdit(); }, "texture vertex");
    require(!session.interaction() && session.selection() == selected, "Failed Begin changed the session.");
    session.beginMeshTransform();
    session.previewMeshTransform(1, { 1, 0, 0 });
    require(session.hasProposal() && session.displayed().geosets()[1].meshes[1].vertices[0].x == 1,
        "First mesh proposal was not displayed.");
    require(session.committed().source() == source && !session.dirty(), "Preview authored committed state.");
    session.previewMeshTransform(1, { 2, 0, 0 });
    const auto pinnedPreview = session.displayed();
    require(session.displayed().geosets()[1].meshes[1].vertices[0].x == 2,
        "Preview compounded instead of recomputing from the committed baseline.");
    refuses([&] { session.setSelection(NativeModelSelection{ 0, 0, {} }); }, "interaction");
    refuses([&] { session.undo(); }, "interaction");
    refuses([&] { session.redo(); }, "interaction");
    refuses([&] { session.beginTextureVertexEdit(); }, "interaction");
    require(session.selection() == selected && session.hasProposal(), "Refused control changed the frozen target.");
    session.cancel();
    require(!session.interaction() && !session.hasProposal() && session.displayed().source() == source
            && session.selection() == selected && !session.canUndo(), "Cancel failed to restore the exact baseline.");
    require(pinnedPreview.geosets()[1].meshes[1].vertices[0].x == 2,
        "A caller-owned preview snapshot did not survive Cancel.");

    session.beginMeshTransform();
    session.previewMeshTransform(1, { 1, 0, 0 });
    refuses([&] { session.previewMeshTransform(0, {}); });
    require(!session.hasProposal() && session.displayed().source() == source, "Invalid input left a stale proposal.");
    refuses([&] { session.apply(); }, "proposal");
    require(session.committed().source() == source && session.interaction().has_value(),
        "Invalid Apply committed or ended the interaction.");
    session.previewMeshTransform(1, { 3, 0, 0 });
    refuses([&] { session.previewTextureVertex(2, 2); }, "family");
    require(!session.hasProposal(), "Wrong-family input retained the earlier proposal.");
    session.previewMeshTransform(1, { 4, 0, 0 });
    require(session.apply() && !session.interaction() && session.dirty(), "Valid Apply did not commit once.");
    require(session.historyStats().undoTransitions == 1 && session.committed().geosets()[1].meshes[1].vertices[0].x == 4,
        "Preview samples became separate history transitions.");
}

void checkExactHistory(const std::string& source)
{
    auto session = NativeModelSession::open(source);
    const auto initial = session.revision();
    const NativeModelSelection uv{ 0, 0, 0 }, mesh{ 1, 1, {} };
    commitUv(session, uv, 0, -32.5f);
    const auto uvRevision = session.revision();
    const std::string uvSource(session.committed().source());
    auto expectedUv = source;
    replace(expectedUv, "-0.000 0.00 # exact origin", "-0.000 -32.5 # exact origin");
    require(uvSource == expectedUv, "UV commit changed unrelated source spellings.");
    require(session.acknowledgeSaved(uvRevision) && !session.dirty(), "Exact save acknowledgement failed.");
    session.setSelection(mesh);
    session.beginMeshTransform();
    session.previewMeshTransform(1.25, { 0.125, -0.25, 0.5 });
    require(session.apply(), "Mesh edit was not committed.");
    const auto meshRevision = session.revision();
    const std::string meshSource(session.committed().source());
    require(meshSource.find("-0.000 -32.5 # exact origin") != std::string::npos,
        "A later mesh commit discarded the earlier UV commit.");
    require(meshRevision.ordinal() > uvRevision.ordinal() && uvRevision.ordinal() > initial.ordinal(),
        "Authored revision ordinals did not increase.");
    require(!session.acknowledgeSaved(uvRevision) && session.dirty(), "A stale save acknowledgement cleaned new work.");
    session.setSelection(std::nullopt);
    require(session.undo() && session.committed().source() == uvSource && session.selection() == mesh
            && session.revision() == uvRevision && !session.dirty(), "Undo failed exact bytes, selection or clean revision.");
    require(session.undo() && session.committed().source() == source && session.selection() == uv
            && session.revision() == initial && session.dirty(), "Undo did not restore the original numeric spellings.");
    require(!session.undo(), "Undo moved beyond the retained timeline.");
    require(session.redo() && session.committed().source() == uvSource && session.selection() == uv && !session.dirty(),
        "Redo failed to restore the saved UV revision.");
    session.setSelection(std::nullopt);
    require(session.redo() && session.committed().source() == meshSource && session.selection() == mesh
            && session.revision() == meshRevision, "Redo used the current selection instead of the transition selection.");
    require(!session.redo(), "Redo moved beyond the retained timeline.");
    require(session.originalSourceSha256() == sed::update::sha256Hex(source), "Editing changed original source identity.");
}

void checkRedoAndCheckpoints(const std::string& source)
{
    auto session = NativeModelSession::open(source);
    const NativeModelSelection uv{ 0, 0, 0 };
    commitUv(session, uv, 1, 2);
    const auto saved = session.revision();
    require(session.acknowledgeSaved(saved), "Could not acknowledge current revision.");
    commitUv(session, uv, 3, 4);
    const auto discarded = session.revision();
    require(session.undo(), "Could not create a Redo branch.");
    const auto checkpoint = session.revision();
    const std::string baseline(session.committed().source());
    const auto retained = session.historyStats().retainedSourceBytes;
    session.beginTextureVertexEdit();
    session.previewTextureVertex(5, 6);
    session.cancel();
    require(session.canRedo() && session.historyStats().retainedSourceBytes == retained,
        "Cancel truncated Redo or changed retained payload.");
    session.beginTextureVertexEdit();
    session.previewTextureVertex(7, 8);
    refuses([&] { session.previewTextureVertex(std::numeric_limits<float>::infinity(), 0); });
    refuses([&] { session.apply(); }, "proposal");
    session.cancel();
    require(session.canRedo() && session.revision() == checkpoint && session.committed().source() == baseline,
        "Refusal altered the timeline.");
    session.beginTextureVertexEdit();
    session.previewTextureVertex(1, 2);
    require(!session.apply() && !session.interaction() && session.canRedo() && session.revision() == checkpoint,
        "No-op Apply changed revision or truncated Redo.");
    commitUv(session, uv, 9, 10);
    require(!session.canRedo() && session.revision() != saved && session.revision() != discarded
            && session.revision().ordinal() > discarded.ordinal() && session.dirty(),
        "Branching reused an authored revision or retained stale Redo.");
    require(!session.acknowledgeSaved(discarded) && session.dirty(), "Discarded revision cleaned the new branch.");
    auto foreign = NativeModelSession::open(source);
    require(!session.acknowledgeSaved(foreign.revision()) && session.dirty(), "Foreign session token was accepted.");
    require(!foreign.acknowledgeSaved(NativeModelRevision{}), "Empty revision token was accepted.");
    require(session.undo() && session.revision() == saved && !session.dirty(), "Branch Undo lost the saved revision.");

    auto recipient = NativeModelSession::open(source);
    commitUv(recipient, uv, 1, 1);
    commitUv(foreign, uv, 2, 2);
    const auto clean = recipient.cleanRevision();
    require(recipient.revision().ordinal() == foreign.revision().ordinal() && recipient.dirty(),
        "Foreign revision fixture must have equal ordinals and a dirty recipient.");
    require(!recipient.acknowledgeSaved(foreign.revision()) && recipient.dirty() && recipient.cleanRevision() == clean,
        "An equal-ordinal token from another session cleaned the recipient.");
    require(recipient.acknowledgeSaved(recipient.revision()) && !recipient.dirty(), "The recipient rejected its own token.");
}

void checkRetention(const std::string& source)
{
    const NativeModelSelection uv{ 0, 0, 0 };
    auto countLimited = NativeModelSession::open(source, { 2, NativeModelHistoryLimits::kMaximumSourceBytes });
    const auto original = countLimited.revision();
    commitUv(countLimited, uv, 1, 1);
    const auto evictedClean = countLimited.revision();
    require(countLimited.acknowledgeSaved(evictedClean), "Could not set clean checkpoint before eviction.");
    commitUv(countLimited, uv, 2, 2);
    commitUv(countLimited, uv, 3, 3);
    commitUv(countLimited, uv, 4, 4);
    require(countLimited.historyStats().undoTransitions == 2 && countLimited.dirty(), "Count retention exceeded its bound.");
    require(countLimited.undo() && countLimited.undo() && !countLimited.undo()
            && countLimited.revision() != original && countLimited.revision() != evictedClean && countLimited.dirty(),
        "Eviction reused the clean cursor identity or kept too many transitions.");
    require(!countLimited.acknowledgeSaved(evictedClean), "Evicted revision acknowledgement cleaned newer work.");

    // All edited UV tokens become one digit, so this budget holds two states but cannot hold three.
    const auto twoStateBudget = source.size() * 2;
    auto payloadLimited = NativeModelSession::open(source, { 64, twoStateBudget });
    commitUv(payloadLimited, uv, 1, 1);
    const auto first = payloadLimited.revision();
    const std::string firstText(payloadLimited.committed().source());
    commitUv(payloadLimited, uv, 2, 2);
    const auto stats = payloadLimited.historyStats();
    require(stats.undoTransitions == 1 && stats.redoTransitions == 0
            && stats.retainedSourceBytes == firstText.size() + payloadLimited.committed().source().size()
            && stats.retainedSourceBytes <= twoStateBudget,
        "Payload retention did not include exactly the current and preceding source strings.");
    require(payloadLimited.undo() && payloadLimited.revision() == first && !payloadLimited.undo(),
        "Payload eviction lost the wrong edge of the timeline.");

    auto noHistory = NativeModelSession::open(source, { 0, source.size() });
    commitUv(noHistory, uv, 1, 1);
    require(!noHistory.canUndo() && !noHistory.canRedo() && noHistory.historyStats().undoTransitions == 0
            && noHistory.historyStats().retainedSourceBytes == noHistory.committed().source().size(),
        "Zero-transition retention did not retain exactly the current source slot.");
    refuses([&] { NativeModelSession::open(source, { 64, source.size() - 1 }); }, "budget");
    refuses([&] { NativeModelSession::open(source, { 65, NativeModelHistoryLimits::kMaximumSourceBytes }); }, "limit");
    refuses([&] { NativeModelSession::open(source, { 1, NativeModelHistoryLimits::kMaximumSourceBytes + 1 }); }, "limit");
    refuses([&] { NativeModelSession::open(source, { 1, 0 }); }, "limit");

    // Admit two normal states; a very long finite float spelling exceeds the current-slot budget.
    auto constrained = NativeModelSession::open(source, { 64, source.size() });
    commitUv(constrained, uv, 1, 1);
    const auto compact = constrained.revision();
    const std::string compactText(constrained.committed().source());
    constrained.beginTextureVertexEdit();
    constrained.previewTextureVertex(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    require(constrained.displayed().source().size() > source.size(), "Budget fixture did not grow its proposed source.");
    refuses([&] { constrained.apply(); }, "budget");
    require(constrained.revision() == compact && constrained.committed().source() == compactText
            && constrained.selection() == uv && constrained.hasProposal(), "Over-budget commit partially changed state.");
    constrained.cancel();
}

void checkBudgetRefusalPreservesRedo(std::string source)
{
    // A repeated loose position is admissible. Expanding its numeric spelling can exceed a two-state payload budget.
    const auto begin = source.find("VERTICES ", source.rfind("MESH 1\n"));
    const auto end = source.find("SECTION: HIERARCHYDEF", begin);
    require(begin != source.npos && end != source.npos, "Loose-mesh fixture anchors missing.");
    std::string records = "VERTICES 512\n";
    for ( int i = 0; i < 512; ++i )
    {
        records += std::to_string(i) + ": 0 0 0 1\n";
    }
    records += "TEXTURE VERTICES 1\n0: 0 0\nVERTEX NORMALS\n";
    for ( int i = 0; i < 512; ++i )
    {
        records += std::to_string(i) + ": 0 0 1\n";
    }
    records += "FACES 0\nFACE NORMALS\n\n";
    source.replace(begin, end - begin, records);
    const auto budget = source.size() * 2;
    auto session = NativeModelSession::open(source, { 64, budget });
    const NativeModelSelection uv{ 1, 1, 0 }, mesh{ 1, 1, {} };
    commitUv(session, uv, 1, 1);
    const auto beforeRevision = session.revision();
    require(session.acknowledgeSaved(beforeRevision), "Budget fixture could not record its clean checkpoint.");
    commitUv(session, uv, 2, 2);
    const auto redoRevision = session.revision();
    const std::string redoSource(session.committed().source());
    require(session.undo() && session.canRedo(), "Budget fixture did not retain a Redo branch.");
    session.setSelection(mesh);
    const auto beforeStats = session.historyStats();
    const std::string beforeSource(session.committed().source());
    session.beginMeshTransform();
    session.previewMeshTransform(1, { 1.234567890123456e200, -9.87654321098765e190, 4.3210987654321e180 });
    require(session.displayed().source().size() > budget, "Valid expansion did not exceed the current-source budget.");
    refuses([&] { session.apply(); }, "budget");
    require(session.hasProposal() && session.selection() == mesh && session.revision() == beforeRevision
            && session.committed().source() == beforeSource && session.cleanRevision() == beforeRevision && !session.dirty()
            && session.historyStats().retainedSourceBytes == beforeStats.retainedSourceBytes
            && session.historyStats().undoTransitions == beforeStats.undoTransitions
            && session.historyStats().redoTransitions == beforeStats.redoTransitions,
        "Over-budget effective Apply altered committed state, selection, checkpoint or history.");
    session.cancel();
    require(session.canRedo() && session.redo() && session.revision() == redoRevision
            && session.committed().source() == redoSource && session.selection() == uv,
        "Failed commit destroyed the exact pre-existing Redo destination.");
}
}

int main()
{
    try
    {
        const auto source = fixture();
        checkSelectionAndPreview(source);
        checkExactHistory(source);
        checkRedoAndCheckpoints(source);
        checkRetention(source);
        checkBudgetRefusalPreservesRedo(source);
        refuses([] { NativeModelSession::open("not a model"); });
        refuses([] { NativeModelSession::open(std::string(16 * 1024 * 1024 + 1, 'x')); });
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
