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

/** Legacy SED save backup name: `<file name>.bak`, e.g. `level.jkl.bak`. */
std::filesystem::path levelSaveBackupPath(const std::filesystem::path& levelPath);

struct LevelSaveBackupResult
{
    /** The `.bak` written, or empty when there was no previous file. */
    std::filesystem::path path{};
    std::string error{};
};

/** Keeps the file about to be overwritten as `<file name>.bak`.
 *
 * Legacy SED renamed the old file before writing. This copies it instead and
 * replaces any older `.bak`, so the level stays on disk until the caller's
 * atomic commit succeeds. A missing level file is not an error.
 */
LevelSaveBackupResult keepPreviousLevelAsBak(const std::filesystem::path& levelPath);

} // namespace sed::editor
