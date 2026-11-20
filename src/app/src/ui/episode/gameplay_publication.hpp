#pragma once

#include <QByteArray>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sed::ui::episode
{

// Campaign metadata is replaced only from its reviewed revision. A generated
// script is always create-only, with an identical existing file reusable.
struct GameplayFileChange
{
    std::filesystem::path relativePath;
    std::optional<QByteArray> previousSha256;
    std::optional<std::string> nextBytes;
};

std::optional<QByteArray> gameplayFileRevision(const std::filesystem::path& project,
                                               const std::filesystem::path& relativePath);

void publishGameplayFiles(const std::filesystem::path& project, const std::filesystem::path& recovery,
                          const std::vector<GameplayFileChange>& files);

// Replays the complete approved files from this user's recovery storage.
// A differing third-party revision is preserved and reported to the caller.
bool recoverGameplayFiles(const std::filesystem::path& project, const std::filesystem::path& recovery);

} // namespace sed::ui::episode
