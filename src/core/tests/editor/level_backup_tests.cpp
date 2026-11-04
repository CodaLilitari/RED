#include "test_common.hpp"

#include "sed/editor/level_backup.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace sed::tests
{
namespace
{

namespace fs = std::filesystem;

class ScratchDirectory
{
public:
    ScratchDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = fs::temp_directory_path() / ("sed-level-backup-tests-" + std::to_string(stamp));
        std::error_code ignored{};
        fs::remove_all(root_, ignored);
        fs::create_directories(root_);
    }
    ~ScratchDirectory()
    {
        std::error_code ignored{};
        fs::remove_all(root_, ignored);
    }
    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    const fs::path& root() const noexcept
    {
        return root_;
    }

private:
    fs::path root_{};
};

void writeFile(const fs::path& path, const std::string& text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

std::string readFile(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void testBackupNamingFollowsLegacyPattern()
{
    const fs::path level = fs::path("levels") / "01narshadda.jkl";
    check(editor::levelBackupDirectory(level) == fs::path("levels") / "backup",
        "backup directory is the sibling 'backup' folder of the saved level");
    check(editor::levelBackupCopyPath(level, 0) == fs::path("levels") / "backup" / "01narshadda00.jkl",
        "backup slot 0 uses the legacy <stem>00<ext> name");
    check(editor::levelBackupCopyPath(level, 7) == fs::path("levels") / "backup" / "01narshadda07.jkl",
        "single-digit backup slots are zero padded to two digits");
    check(editor::levelBackupCopyPath(level, 99) == fs::path("levels") / "backup" / "01narshadda99.jkl",
        "slot 99 is the final legacy backup name");
    check(editor::kMaxLevelBackupCopies == 100, "the legacy backup limit is 100 copies");
}

void testBackupCopyUsesFirstFreeSlotAndPreservesSavedBytes()
{
    ScratchDirectory scratch{};
    const fs::path level = scratch.root() / "test.sed";
    writeFile(level, "saved-bytes");

    const editor::LevelBackupResult first = editor::makeLevelBackupCopy(level);
    check(first.ok() && first.path == scratch.root() / "backup" / "test00.sed",
        "first backup creates backup/test00.sed");
    check(readFile(first.path) == "saved-bytes", "backup copies the saved file's exact bytes");

    // Slot 01 is occupied by an unrelated user file and must not be replaced.
    writeFile(scratch.root() / "backup" / "test01.sed", "user-file");
    const editor::LevelBackupResult second = editor::makeLevelBackupCopy(level);
    check(second.ok() && second.path == scratch.root() / "backup" / "test02.sed",
        "backup skips occupied slots and uses the first free name");
    check(readFile(scratch.root() / "backup" / "test01.sed") == "user-file",
        "backup never overwrites an existing backup slot");

    std::error_code error{};
    fs::remove(scratch.root() / "backup" / "test00.sed", error);
    const editor::LevelBackupResult refill = editor::makeLevelBackupCopy(level);
    check(refill.ok() && refill.path == scratch.root() / "backup" / "test00.sed",
        "a freed lower slot is reused, matching legacy first-unused numbering");
}

void testBackupRefusesWhenAllSlotsAreUsed()
{
    ScratchDirectory scratch{};
    const fs::path level = scratch.root() / "full.jkl";
    writeFile(level, "level");
    fs::create_directories(scratch.root() / "backup");
    for ( int slot = 0; slot < editor::kMaxLevelBackupCopies; ++slot )
    {
        writeFile(editor::levelBackupCopyPath(level, slot), "old");
    }

    const editor::LevelBackupResult result = editor::makeLevelBackupCopy(level);
    check(!result.ok() && result.error.find("100 backup copies") != std::string::npos,
        "backup refuses with the legacy limit message when 100 copies exist");
    check(readFile(editor::levelBackupCopyPath(level, 0)) == "old",
        "a refused backup leaves every existing copy untouched");
}

void testBackupRequiresSavedLevelFile()
{
    ScratchDirectory scratch{};
    const editor::LevelBackupResult unsaved = editor::makeLevelBackupCopy(fs::path{});
    check(!unsaved.ok() && !unsaved.error.empty(), "backup requires a saved level path");

    const editor::LevelBackupResult missing =
        editor::makeLevelBackupCopy(scratch.root() / "missing.sed");
    check(!missing.ok() && !missing.error.empty(), "backup refuses a level file that is not on disk");
    check(!fs::exists(scratch.root() / "backup"),
        "a refused backup of a missing file does not create the backup directory");

    fs::create_directories(scratch.root() / "folder.sed");
    const editor::LevelBackupResult directory =
        editor::makeLevelBackupCopy(scratch.root() / "folder.sed");
    check(!directory.ok(), "backup refuses a directory in place of a level file");
}

void testBackupReportsBlockedBackupDirectory()
{
    ScratchDirectory scratch{};
    const fs::path level = scratch.root() / "blocked.sed";
    writeFile(level, "level");
    writeFile(scratch.root() / "backup", "not-a-directory");
    const editor::LevelBackupResult result = editor::makeLevelBackupCopy(level);
    check(!result.ok() && !result.error.empty(),
        "backup reports an error when 'backup' exists as a file");
    check(readFile(scratch.root() / "backup") == "not-a-directory",
        "a blocked backup directory is never replaced");
}

void testSaveBackupPathAppendsBakToTheExtension()
{
    check(editor::levelSaveBackupPath(fs::path("levels") / "01narshadda.jkl")
            == fs::path("levels") / "01narshadda.jkl.bak",
        "save backup appends .bak to the full file name like legacy BackupFile");
    check(editor::levelSaveBackupPath(fs::path("work") / "level.sed")
            == fs::path("work") / "level.sed.bak",
        "native documents get a .sed.bak backup");
}

void testSaveBackupReplacesPreviousBakWithCurrentFile()
{
    ScratchDirectory scratch{};
    const fs::path level = scratch.root() / "level.jkl";
    writeFile(level, "previous-save");
    writeFile(scratch.root() / "level.jkl.bak", "older-save");

    const editor::LevelSaveBackupResult result = editor::keepPreviousLevelAsBak(level);
    check(result.error.empty() && result.path == scratch.root() / "level.jkl.bak",
        "save backup reports the .bak path it wrote");
    check(readFile(scratch.root() / "level.jkl.bak") == "previous-save",
        "save backup replaces the older .bak with the file about to be overwritten");
    check(readFile(level) == "previous-save",
        "save backup copies rather than moves, so the level stays on disk until the atomic commit");
}

void testSaveBackupSkipsNewFilesAndReportsBlockedTargets()
{
    ScratchDirectory scratch{};
    const editor::LevelSaveBackupResult fresh =
        editor::keepPreviousLevelAsBak(scratch.root() / "new.jkl");
    check(fresh.error.empty() && fresh.path.empty()
            && !fs::exists(scratch.root() / "new.jkl.bak"),
        "save backup does nothing when the level has not been written yet");

    const editor::LevelSaveBackupResult unsaved = editor::keepPreviousLevelAsBak(fs::path{});
    check(unsaved.error.empty() && unsaved.path.empty(), "save backup ignores an empty path");

    const fs::path level = scratch.root() / "blocked.jkl";
    writeFile(level, "level");
    fs::create_directories(scratch.root() / "blocked.jkl.bak");
    const editor::LevelSaveBackupResult blocked = editor::keepPreviousLevelAsBak(level);
    check(!blocked.error.empty() && blocked.path.empty(),
        "save backup reports an error when the .bak name is a folder");
    check(fs::is_directory(scratch.root() / "blocked.jkl.bak"),
        "a blocked save backup never removes the conflicting folder");
}

} // namespace

void runLevelBackupTests()
{
    testBackupNamingFollowsLegacyPattern();
    testBackupCopyUsesFirstFreeSlotAndPreservesSavedBytes();
    testBackupRefusesWhenAllSlotsAreUsed();
    testBackupRequiresSavedLevelFile();
    testBackupReportsBlockedBackupDirectory();
    testSaveBackupPathAppendsBakToTheExtension();
    testSaveBackupReplacesPreviousBakWithCurrentFile();
    testSaveBackupSkipsNewFilesAndReportsBlockedTargets();
}

} // namespace sed::tests
