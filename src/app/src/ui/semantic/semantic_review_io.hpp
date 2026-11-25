#pragma once

#include "sed/level/semantic_preview.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace sed::ui::semantic_review
{

// Paths are copied into a request before background work starts. No request or
// result contains a live Document, a resource locator, or writable Level state.
struct SnapshotRequest
{
    QString baselinePath;
    QString candidatePath;
    QString outputPath;
};

struct ReceiptRequest
{
    QString receiptPath;
};

using Request = std::variant<SnapshotRequest, ReceiptRequest>;

enum class SourceKind
{
    SavedSnapshots,
    ImportedReceipt
};

enum class ErrorCode
{
    None,
    Cancelled,
    InvalidRequest,
    UnsupportedFormat,
    InputTooLarge,
    ReadFailed,
    InvalidNative,
    InvalidReceipt,
    ReceiptTooComplex,
    OutputExists,
    WriteFailed
};

inline constexpr std::uint64_t kMaxSnapshotBytes = 64ull * 1024ull * 1024ull;
inline constexpr std::uint64_t kMaxReceiptBytes = 4ull * 1024ull * 1024ull;
inline constexpr std::size_t kMaxReceiptDepth = 32;
inline constexpr std::size_t kMaxReceiptNodes = 65536;
inline constexpr std::size_t kMaxPathCharacters = 32768;

struct FileIdentity
{
    QString role;
    QString path;
    QString sha256;
    std::uint64_t byteCount{};
    bool nativeHadCorruption{};
    std::uint32_t nativeRepairedChunks{};
    QStringList nativeRepairedSections;
};

struct Review
{
    SourceKind sourceKind{SourceKind::SavedSnapshots};
    QString provenance;
    QStringList warnings;
    QString receiptCommand;
    QString inputSha256;
    QString outputSha256;
    semantic_preview::Limits limits;
    semantic_preview::Comparison authored;
    semantic_preview::Comparison persisted;
    // Saved snapshots have baseline/candidate/output entries. Imported receipts
    // have one receipt entry; their asserted input/output hashes are not verified.
    std::vector<FileIdentity> files;
    // Exact imported bytes are retained for byte-preserving create-only export.
    // Snapshot reports leave this empty and serialize an explicit provenance wrapper.
    QByteArray originalReceipt;
};

struct Result
{
    std::shared_ptr<const Review> review;
    QString error;
    ErrorCode code{ErrorCode::None};
};

struct ExportResult
{
    bool success{};
    QString error;
    ErrorCode code{ErrorCode::None};
};

// Only native .sed snapshot files are admitted. Existing complete plan/apply
// receipts remain inspectable regardless of their original level file format.
// Reads, hashes and comparisons use captured bytes. This function never writes.
[[nodiscard]] Result loadReview(const Request& request, sed::StopToken stop = {});

// Creates a new report only. Existing files, directories and symlinks are never
// replaced. Imported receipts are exported with exactly their original bytes.
[[nodiscard]] ExportResult exportReviewCreateOnly(const Review& review,
    const QString& destination, sed::StopToken stop = {});

} // namespace sed::ui::semantic_review
