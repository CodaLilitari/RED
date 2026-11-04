#include "sed/editor/level_backup.hpp"

#include <cstdio>
#include <system_error>

namespace sed::editor
{

namespace fs = std::filesystem;

fs::path levelBackupDirectory(const fs::path& levelPath)
{
    return levelPath.parent_path() / "backup";
}

fs::path levelBackupCopyPath(const fs::path& levelPath, const int slot)
{
    // Legacy SED formats the name as '%s%s%-.2d%s': stem, two-digit slot, extension.
    char number[8]{};
    std::snprintf(number, sizeof(number), "%02d", slot);
    fs::path name = levelPath.stem();
    name += number;
    name += levelPath.extension();
    return levelBackupDirectory(levelPath) / name;
}

LevelBackupResult makeLevelBackupCopy(const fs::path& levelPath)
{
    if ( levelPath.empty() )
    {
        return { {}, "The level hasn't been saved yet." };
    }

    std::error_code error{};
    if ( !fs::is_regular_file(levelPath, error) )
    {
        return { {}, "The saved level file " + levelPath.string() + " was not found." };
    }

    const fs::path directory = levelBackupDirectory(levelPath);
    const bool directoryExists = fs::exists(directory, error);
    if ( error )
    {
        return { {}, "Could not inspect " + directory.string() + ": " + error.message() };
    }
    if ( directoryExists && !fs::is_directory(directory, error) )
    {
        return { {}, directory.string() + " exists but is not a folder." };
    }
    if ( !directoryExists && !fs::create_directories(directory, error) && error )
    {
        return { {}, "Could not create " + directory.string() + ": " + error.message() };
    }

    for ( int slot = 0; slot < kMaxLevelBackupCopies; ++slot )
    {
        const fs::path target = levelBackupCopyPath(levelPath, slot);
        const bool occupied = fs::exists(fs::symlink_status(target, error));
        if ( error && error != std::errc::no_such_file_or_directory )
        {
            return { {}, "Could not inspect " + target.string() + ": " + error.message() };
        }
        error.clear();
        if ( occupied )
        {
            continue;
        }
        // copy_options::none refuses to replace a file created since the check.
        if ( !fs::copy_file(levelPath, target, fs::copy_options::none, error) )
        {
            return { {}, "Could not copy the level to " + target.string() + ": " + error.message() };
        }
        return { target, {} };
    }
    return { {}, "You have " + std::to_string(kMaxLevelBackupCopies) + " backup copies! Remove some." };
}

} // namespace sed::editor
