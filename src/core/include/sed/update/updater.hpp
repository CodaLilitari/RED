#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sed::update
{

/** Feed document ceiling; a larger feed is refused, never truncated. */
inline constexpr std::size_t kMaxFeedBytes = 256 * 1024;
/** Release-manifest ceiling (entries). */
inline constexpr std::size_t kMaxManifestFiles = 20'000;
/** One package file ceiling; also bounds the in-memory hash read. */
inline constexpr std::uint64_t kMaxPackageFileBytes = 256ull * 1024 * 1024;

inline constexpr std::string_view kManifestFileName = "RELEASE_MANIFEST.json";
inline constexpr std::string_view kStagedReceiptFileName = "STAGED.json";

/** Stable refusal: `code` is machine-stable, `message` is for humans. */
struct UpdateFailure
{
    std::string code{};
    std::string message{};
};

struct FeedRelease
{
    std::string version{};
    std::string channel{};
    /** Package directory relative to the feed location. */
    std::string package{};
};

struct Feed
{
    std::vector<FeedRelease> releases{};
};

struct ManifestFile
{
    std::string path{};
    std::uint64_t bytes{};
    std::string sha256{};
};

struct ReleaseManifest
{
    std::string version{};
    std::vector<ManifestFile> files{};
};

/** Compares dotted versions; returns -1/0/1, or nullopt when malformed. */
[[nodiscard]] std::optional<int> compareVersions(
    std::string_view lhs, std::string_view rhs);

[[nodiscard]] std::optional<Feed> parseFeed(
    std::string_view text, UpdateFailure* failure);
[[nodiscard]] std::optional<ReleaseManifest> parseReleaseManifest(
    std::string_view text, UpdateFailure* failure);

[[nodiscard]] std::string sha256Hex(std::string_view bytes);
[[nodiscard]] std::optional<std::string> sha256FileHex(
    const std::filesystem::path& path, UpdateFailure* failure);

/** Injectable feed fetch: production may wrap HTTPS, tests use files. */
class IUpdateSource
{
public:
    virtual ~IUpdateSource() = default;
    [[nodiscard]] virtual std::optional<std::string> fetchFeedText(
        UpdateFailure* failure) = 0;
    [[nodiscard]] virtual std::optional<std::filesystem::path> packageDirectory(
        const FeedRelease& release, UpdateFailure* failure) = 0;
};

/** Local-file source: feed JSON plus package dirs beside the feed file. */
class FileUpdateSource final : public IUpdateSource
{
public:
    explicit FileUpdateSource(std::filesystem::path feedPath)
        : feedPath_(std::move(feedPath))
    {
    }
    [[nodiscard]] std::optional<std::string> fetchFeedText(
        UpdateFailure* failure) override;
    [[nodiscard]] std::optional<std::filesystem::path> packageDirectory(
        const FeedRelease& release, UpdateFailure* failure) override;

private:
    std::filesystem::path feedPath_{};
};

/** Highest release on `channel` strictly newer than `currentVersion`. */
[[nodiscard]] std::optional<FeedRelease> selectUpdate(
    const Feed& feed, std::string_view currentVersion,
    std::string_view channel, UpdateFailure* failure);

[[nodiscard]] std::filesystem::path stagingDirectoryFor(
    const std::filesystem::path& installDir, std::string_view version);
[[nodiscard]] std::filesystem::path activatedDirectoryFor(
    const std::filesystem::path& installDir, std::string_view version);

struct StagedUpdate
{
    std::filesystem::path stagingDir{};
    std::string version{};
    std::vector<ManifestFile> receipts{};
};

/**
 * Stages a verified package beside `installDir` (never inside or over it).
 * On any refusal the install tree is untouched; a staging directory left
 * by an interrupted run is reported, not silently reused or deleted.
 */
[[nodiscard]] std::optional<StagedUpdate> stageUpdate(
    IUpdateSource& source, const std::filesystem::path& installDir,
    std::string_view currentVersion, std::string_view channel,
    UpdateFailure* failure);

/** Rechecks every receipt in a staged or installed directory. */
[[nodiscard]] bool verifyDirectory(
    const std::filesystem::path& dir, bool requireStagedReceipt,
    UpdateFailure* failure);

/**
 * Explicit activation: rechecks all receipts, then renames the staging
 * directory to its activated sibling. The old install is never touched.
 */
[[nodiscard]] std::optional<std::filesystem::path> activateUpdate(
    const std::filesystem::path& stagingDir,
    const std::optional<std::filesystem::path>& targetDir,
    UpdateFailure* failure);

} // namespace sed::update
