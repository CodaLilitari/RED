#pragma once

#include "sed/assets/native_model_document.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace sed::editor
{

struct NativeModelSelection
{
    std::size_t geoset{}, sourceMesh{};
    std::optional<std::size_t> textureVertex;
    bool operator==(const NativeModelSelection&) const = default;
};

enum class NativeModelInteractionKind
{
    MeshTransform,
    TextureVertex,
    InsertOffset,
};

struct NativeModelHistoryLimits
{
    static constexpr std::size_t kMaximumTransitions = 64;
    static constexpr std::size_t kMaximumSourceBytes = 64 * 1024 * 1024;
    std::size_t transitions{ kMaximumTransitions };
    std::size_t sourceBytes{ kMaximumSourceBytes };
};

// An authored identity, never a history cursor or a content-equivalence claim.
class NativeModelRevision
{
public:
    NativeModelRevision() = default;
    std::uint64_t ordinal() const noexcept;
    bool operator==(const NativeModelRevision&) const = default;

private:
    friend class NativeModelSession;
    NativeModelRevision(std::shared_ptr<const void> owner, std::uint64_t ordinal);
    std::shared_ptr<const void> owner_;
    std::uint64_t ordinal_{};
};

struct NativeModelHistoryStats
{
    std::size_t undoTransitions{}, redoTransitions{};
    // Timeline strings, including the current slot; excludes parsed snapshots and allocation capacity.
    std::size_t retainedSourceBytes{};
    std::size_t committedSourceBytes{}, previewSourceBytes{};
};

// One thread owns mutations. Copy immutable document handles for independently owned read-only snapshots.
class NativeModelSession
{
public:
    static NativeModelSession open(std::string_view source, NativeModelHistoryLimits limits = {});
    ~NativeModelSession();
    NativeModelSession(NativeModelSession&&) noexcept;
    NativeModelSession& operator=(NativeModelSession&&) noexcept;
    NativeModelSession(const NativeModelSession&) = delete;
    NativeModelSession& operator=(const NativeModelSession&) = delete;

    assets::NativeModelDocument committed() const noexcept;
    assets::NativeModelDocument displayed() const noexcept;
    std::optional<NativeModelSelection> selection() const noexcept;
    void setSelection(std::optional<NativeModelSelection> selection);

    void beginMeshTransform();
    void beginTextureVertexEdit();
    void beginInsertOffsetEdit();
    void previewInsertOffset(TVector offset);
    void previewMeshTransform(double scale, TVector translation);
    void previewTextureVertex(float u, float v);
    std::optional<NativeModelInteractionKind> interaction() const noexcept;
    bool hasProposal() const noexcept;
    // Returns false only for a valid, byte-identical proposal; missing/invalid proposals refuse.
    bool apply();
    void cancel() noexcept;

    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    bool undo();
    bool redo();
    NativeModelHistoryStats historyStats() const noexcept;
    NativeModelHistoryLimits historyLimits() const noexcept;

    NativeModelRevision revision() const noexcept;
    NativeModelRevision cleanRevision() const noexcept;
    bool dirty() const noexcept;
    // This only records an acknowledgement; the session never writes a file.
    bool acknowledgeSaved(const NativeModelRevision& revision) noexcept;
    std::string_view originalSourceSha256() const noexcept;
    std::size_t originalSourceBytes() const noexcept;

private:
    struct Data;
    explicit NativeModelSession(std::unique_ptr<Data> data);
    void begin(NativeModelInteractionKind kind);
    std::unique_ptr<Data> data_;
};

} // namespace sed::editor
