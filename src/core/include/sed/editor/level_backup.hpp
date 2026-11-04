#pragma once

#include <filesystem>
#include <string>

namespace sed::editor
{

/** Number of numbered backup slots the legacy editor allowed per level. */
inline constexpr int kMaxLevelBackupCopies = 100;

/** Directory that receives numbered copies: `<level dir>/backup`. */
std::filesystem::path levelBackupDirectory(const std::filesystem::path& levelPath);

/** Legacy SED slot name `<level dir>/backup/<stem><NN><ext>` for slot 0-99. */
std::filesystem::path levelBackupCopyPath(const std::filesystem::path& levelPath, int slot);

struct LevelBackupResult
{
    std::filesystem::path path{};
    std::string error{};

    bool ok() const noexcept
    {
        return error.empty() && !path.empty();
    }
};

/** Copies the saved level file into the first unused backup slot.
 *
 * The copy is made from disk, not from in-memory editor state, matching the
 * legacy Make a Backup Copy command. Existing files are never overwritten;
 * when all slots are occupied the call fails without touching any file.
 */
LevelBackupResult makeLevelBackupCopy(const std::filesystem::path& levelPath);

} // namespace sed::editor
