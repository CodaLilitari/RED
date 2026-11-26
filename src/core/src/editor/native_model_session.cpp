#include "sed/editor/native_model_session.hpp"

#include "sed/update/updater.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace sed::editor
{
namespace
{
void require(const bool condition, const char* message)
{
    if ( !condition )
    {
        throw std::runtime_error(std::string("Native model session: ") + message);
    }
}

void validateSelection(const assets::NativeModelDocument& document,
    const std::optional<NativeModelSelection>& selection)
{
    if ( !selection )
    {
        return;
    }
    const auto models = document.geosets();
    require(selection->geoset < models.size()
            && selection->sourceMesh < models[selection->geoset].meshes.size(),
        "selection mesh address is outside the document.");
    require(!selection->textureVertex
            || *selection->textureVertex < models[selection->geoset].meshes[selection->sourceMesh].texVertices.size(),
        "selection texture vertex is outside the document.");
}
} // namespace

struct NativeModelSession::Data
{
    struct Snapshot
    {
        std::shared_ptr<const std::string> source;
        NativeModelRevision revision;
    };

    struct Transition
    {
        std::optional<NativeModelSelection> beforeSelection, afterSelection;
    };

    struct Interaction
    {
        NativeModelInteractionKind kind;
        NativeModelSelection selection;
    };

    Data(assets::NativeModelDocument document, const NativeModelHistoryLimits configured,
        std::shared_ptr<const void> identity, const NativeModelRevision& initial)
        : current(std::move(document)), limits(configured), owner(std::move(identity)), clean(initial),
          originalHash(update::sha256Hex(current.source())), originalBytes(current.source().size())
    {
        snapshots.push_back({ std::make_shared<const std::string>(current.source()), initial });
        retainedBytes = originalBytes;
    }

    assets::NativeModelDocument current;
    std::optional<assets::NativeModelDocument> proposal;
    std::optional<NativeModelSelection> selection;
    std::optional<Interaction> interaction;
    NativeModelHistoryLimits limits;
    std::shared_ptr<const void> owner;
    NativeModelRevision clean;
    std::string originalHash;
    std::size_t originalBytes{};
    std::vector<Snapshot> snapshots;
    std::vector<Transition> transitions;
    std::size_t cursor{}, retainedBytes{};
    std::uint64_t lastOrdinal{};
};

NativeModelRevision::NativeModelRevision(std::shared_ptr<const void> owner, const std::uint64_t ordinal)
    : owner_(std::move(owner)), ordinal_(ordinal)
{
}

std::uint64_t NativeModelRevision::ordinal() const noexcept
{
    return ordinal_;
}

NativeModelSession::NativeModelSession(std::unique_ptr<Data> data)
    : data_(std::move(data))
{
}

NativeModelSession::~NativeModelSession() = default;
NativeModelSession::NativeModelSession(NativeModelSession&&) noexcept = default;
NativeModelSession& NativeModelSession::operator=(NativeModelSession&&) noexcept = default;

NativeModelSession NativeModelSession::open(const std::string_view source, const NativeModelHistoryLimits limits)
{
    require(limits.transitions <= NativeModelHistoryLimits::kMaximumTransitions && limits.sourceBytes > 0
            && limits.sourceBytes <= NativeModelHistoryLimits::kMaximumSourceBytes,
        "history limit is outside the supported range.");
    require(source.size() <= limits.sourceBytes, "initial source exceeds the history payload budget.");
    auto document = assets::NativeModelDocument::open(source);
    // Revision tokens keep this identity alive even after a session or timeline slot retires.
    auto owner = std::make_shared<const char>(0);
    const NativeModelRevision initial(owner, 0);
    return NativeModelSession(std::make_unique<Data>(std::move(document), limits, std::move(owner), initial));
}

assets::NativeModelDocument NativeModelSession::committed() const noexcept
{
    return data_->current;
}

assets::NativeModelDocument NativeModelSession::displayed() const noexcept
{
    return data_->proposal ? *data_->proposal : data_->current;
}

std::optional<NativeModelSelection> NativeModelSession::selection() const noexcept
{
    return data_->selection;
}

void NativeModelSession::setSelection(const std::optional<NativeModelSelection> selection)
{
    require(!data_->interaction, "selection cannot change during an interaction.");
    validateSelection(data_->current, selection);
    data_->selection = selection;
}

void NativeModelSession::begin(const NativeModelInteractionKind kind)
{
    require(!data_->interaction, "another interaction is already active.");
    require(data_->selection.has_value(), "an explicit source mesh selection is required.");
    require(kind != NativeModelInteractionKind::TextureVertex || data_->selection->textureVertex.has_value(),
        "an explicit texture vertex selection is required.");
    data_->interaction = Data::Interaction{ kind, *data_->selection };
}

void NativeModelSession::beginMeshTransform()
{
    begin(NativeModelInteractionKind::MeshTransform);
}

void NativeModelSession::beginTextureVertexEdit()
{
    begin(NativeModelInteractionKind::TextureVertex);
}

void NativeModelSession::beginInsertOffsetEdit()
{
    begin(NativeModelInteractionKind::InsertOffset);
}

void NativeModelSession::previewInsertOffset(TVector offset)
{
    data_->proposal.reset();
    require(data_->interaction && data_->interaction->kind == NativeModelInteractionKind::InsertOffset,
            "placement proposal requires an insertion-offset interaction.");
    data_->proposal = data_->current.applyInsertOffset(offset);
}

void NativeModelSession::previewMeshTransform(const double scale, const TVector translation)
{
    data_->proposal.reset();
    require(data_->interaction && data_->interaction->kind == NativeModelInteractionKind::MeshTransform,
        "mesh proposal requires an active interaction of the same family.");
    const auto& selected = data_->interaction->selection;
    const assets::NativeMeshTransform operation[]{ { selected.geoset, selected.sourceMesh, scale, translation } };
    data_->proposal = data_->current.applyMeshTransforms(operation);
}

void NativeModelSession::previewTextureVertex(const float u, const float v)
{
    data_->proposal.reset();
    require(data_->interaction && data_->interaction->kind == NativeModelInteractionKind::TextureVertex,
        "texture proposal requires an active interaction of the same family.");
    const auto& selected = data_->interaction->selection;
    const assets::NativeTextureVertexEdit operation[]{
        { selected.geoset, selected.sourceMesh, *selected.textureVertex, u, v } };
    data_->proposal = data_->current.applyTextureVertexEdits(operation);
}

std::optional<NativeModelInteractionKind> NativeModelSession::interaction() const noexcept
{
    return data_->interaction ? std::optional(data_->interaction->kind) : std::nullopt;
}

bool NativeModelSession::hasProposal() const noexcept
{
    return data_->proposal.has_value();
}

bool NativeModelSession::apply()
{
    require(data_->interaction && data_->proposal, "a valid current proposal is required before Apply.");
    if ( data_->proposal->source() == data_->current.source() )
    {
        cancel();
        return false;
    }
    const auto sourceBytes = data_->proposal->source().size();
    require(sourceBytes <= data_->limits.sourceBytes, "candidate source exceeds the history payload budget.");
    require(data_->lastOrdinal != std::numeric_limits<std::uint64_t>::max(), "authored revision identity exhausted.");

    // Determine retention without modifying the live Redo branch. At most 64 MiB plus one admitted source is summed.
    std::size_t retainedBytes = sourceBytes;
    for ( std::size_t i = 0; i <= data_->cursor; ++i )
    {
        retainedBytes += data_->snapshots[i].source->size();
    }
    std::size_t first = 0;
    while ( data_->cursor + 1 - first > data_->limits.transitions || retainedBytes > data_->limits.sourceBytes )
    {
        retainedBytes -= data_->snapshots[first].source->size();
        ++first;
    }
    const auto kept = data_->cursor + 1 - first;
    const NativeModelRevision nextRevision(data_->owner, data_->lastOrdinal + 1);
    auto nextSource = std::make_shared<const std::string>(data_->proposal->source());
    std::vector<Data::Snapshot> nextSnapshots;
    std::vector<Data::Transition> nextTransitions;
    nextSnapshots.reserve(kept + 1);
    nextTransitions.reserve(kept);
    for ( std::size_t i = first; i <= data_->cursor; ++i )
    {
        nextSnapshots.push_back(data_->snapshots[i]);
        if ( i < data_->cursor )
        {
            nextTransitions.push_back(data_->transitions[i]);
        }
    }
    nextSnapshots.push_back({ std::move(nextSource), nextRevision });
    if ( kept > 0 )
    {
        nextTransitions.push_back({ data_->interaction->selection, data_->selection });
    }

    // Core preview already reparsed the exact candidate. Every throwing preparation precedes this publication.
    static_assert(std::is_nothrow_move_assignable_v<assets::NativeModelDocument>);
    data_->current = std::move(*data_->proposal);
    data_->snapshots.swap(nextSnapshots);
    data_->transitions.swap(nextTransitions);
    data_->cursor = kept;
    data_->retainedBytes = retainedBytes;
    ++data_->lastOrdinal;
    cancel();
    return true;
}

void NativeModelSession::cancel() noexcept
{
    data_->proposal.reset();
    data_->interaction.reset();
}

bool NativeModelSession::canUndo() const noexcept
{
    return !data_->interaction && data_->cursor > 0;
}

bool NativeModelSession::canRedo() const noexcept
{
    return !data_->interaction && data_->cursor + 1 < data_->snapshots.size();
}

bool NativeModelSession::undo()
{
    require(!data_->interaction, "Undo requires an idle interaction state.");
    if ( !canUndo() )
    {
        return false;
    }
    auto candidate = assets::NativeModelDocument::open(*data_->snapshots[data_->cursor - 1].source);
    data_->current = std::move(candidate);
    data_->selection = data_->transitions[data_->cursor - 1].beforeSelection;
    --data_->cursor;
    return true;
}

bool NativeModelSession::redo()
{
    require(!data_->interaction, "Redo requires an idle interaction state.");
    if ( !canRedo() )
    {
        return false;
    }
    auto candidate = assets::NativeModelDocument::open(*data_->snapshots[data_->cursor + 1].source);
    data_->current = std::move(candidate);
    data_->selection = data_->transitions[data_->cursor].afterSelection;
    ++data_->cursor;
    return true;
}

NativeModelHistoryStats NativeModelSession::historyStats() const noexcept
{
    return { data_->cursor, data_->snapshots.size() - data_->cursor - 1, data_->retainedBytes,
        data_->current.source().size(), data_->proposal ? data_->proposal->source().size() : 0 };
}

NativeModelHistoryLimits NativeModelSession::historyLimits() const noexcept
{
    return data_->limits;
}

NativeModelRevision NativeModelSession::revision() const noexcept
{
    return data_->snapshots[data_->cursor].revision;
}

NativeModelRevision NativeModelSession::cleanRevision() const noexcept
{
    return data_->clean;
}

bool NativeModelSession::dirty() const noexcept
{
    return revision() != data_->clean;
}

bool NativeModelSession::acknowledgeSaved(const NativeModelRevision& expected) noexcept
{
    if ( expected != revision() )
    {
        return false;
    }
    data_->clean = expected;
    return true;
}

std::string_view NativeModelSession::originalSourceSha256() const noexcept
{
    return data_->originalHash;
}

std::size_t NativeModelSession::originalSourceBytes() const noexcept
{
    return data_->originalBytes;
}

} // namespace sed::editor
