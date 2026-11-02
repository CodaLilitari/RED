#include "test_common.hpp"

#include "sed/update/updater.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace sed::tests
{
namespace
{
namespace fs = std::filesystem;
using namespace sed::update;

fs::path makeTempRoot(const char* name)
{
    const fs::path root = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    return root;
}

void writeText(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

std::string readText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Builds a feed directory holding one package release.
fs::path makeFeed(const fs::path& root, const std::string& version, bool corruptHash)
{
    const fs::path package = root / "pkg";
    writeText(package / "sed.exe", "new editor binary");
    writeText(package / "data" / "needed.dat", "runtime data");
    const std::string exeHash = corruptHash ? std::string(64, '0') : sha256Hex("new editor binary");
    writeText(package / "RELEASE_MANIFEST.json",
        "{\n  \"format\": 1,\n  \"version\": \"" + version + "\",\n  \"files\": [\n"
        "    {\"path\":\"sed.exe\",\"bytes\":17,\"sha256\":\"" + exeHash + "\"},\n"
        "    {\"path\":\"data/needed.dat\",\"bytes\":12,\"sha256\":\"" + sha256Hex("runtime data") + "\"}\n  ]\n}\n");
    writeText(root / "feed.json",
        "{\"format\":1,\"releases\":[{\"version\":\"" + version
            + "\",\"channel\":\"stable\",\"package\":\"pkg\"}]}\n");
    return root / "feed.json";
}

fs::path makeInstall(const fs::path& root)
{
    const fs::path install = root / "SED";
    writeText(install / "sed.exe", "old editor binary");
    return install;
}

} // namespace

void runUpdaterTests()
{
    // SHA-256 known-answer vectors (FIPS 180-4 examples).
    check(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "updater: sha256 empty vector");
    check(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "updater: sha256 abc vector");

    // Version ordering: equal and downgrades are never updates.
    check(compareVersions("0.4.0", "0.4.0") == 0, "updater: equal versions compare equal");
    check(compareVersions("0.5.0", "0.4.0") == 1, "updater: newer compares greater");
    check(compareVersions("0.3.9", "0.4.0") == -1, "updater: older compares smaller");
    check(compareVersions("1.0", "1.0.0") == 0, "updater: missing components are zero");
    check(compareVersions("0.5.0-dev", "0.5.0") == -1, "updater: prerelease precedes release");
    check(!compareVersions("x.y", "0.4.0").has_value(), "updater: malformed version refused");

    UpdateFailure failure;

    // Malformed feed and unsafe package path refuse.
    check(!parseFeed("not json", &failure) && failure.code == "malformed-feed", "updater: malformed feed refused");
    check(!parseFeed("{\"releases\":[{\"version\":\"0.5.0\",\"package\":\"../escape\"}]}", &failure)
            && failure.code == "unsafe-path",
        "updater: unsafe feed package path refused");

    // Equal / downgrade selection refusals carry stable codes.
    {
        const auto feed = parseFeed("{\"releases\":[{\"version\":\"0.4.0\",\"channel\":\"stable\",\"package\":\"pkg\"}]}", &failure);
        check(feed.has_value(), "updater: equal feed parses");
        check(!selectUpdate(*feed, "0.4.0", "stable", &failure) && failure.code == "equal-version", "updater: equal version refused");
        const auto older = parseFeed("{\"releases\":[{\"version\":\"0.3.0\",\"channel\":\"stable\",\"package\":\"pkg\"}]}", &failure);
        check(!selectUpdate(*older, "0.4.0", "stable", &failure) && failure.code == "downgrade-refused", "updater: downgrade refused");
    }

    // Happy path: stage beside, activate as a separate step, old intact.
    {
        const fs::path root = makeTempRoot("sed-updater-happy");
        const fs::path feed = makeFeed(root, "0.5.0", false);
        const fs::path install = makeInstall(root);
        FileUpdateSource source(feed);
        const auto staged = stageUpdate(source, install, "0.4.0", "stable", &failure);
        check(staged.has_value(), "updater: newer release stages");
        if (staged)
        {
            check(readText(install / "sed.exe") == "old editor binary", "updater: staging leaves the old install intact");
            check(verifyDirectory(staged->stagingDir, true, &failure), "updater: staged receipts verify");
            const auto activated = activateUpdate(staged->stagingDir, std::nullopt, &failure);
            check(activated.has_value(), "updater: activation succeeds as a separate step");
            if (activated)
            {
                check(readText(*activated / "sed.exe") == "new editor binary", "updater: activated tree holds the new binary");
                check(verifyDirectory(*activated, true, &failure), "updater: activated receipts recheck at launch");
            }
            check(readText(install / "sed.exe") == "old editor binary", "updater: activation leaves the old install runnable");
        }
        fs::remove_all(root);
    }

    // Corrupt manifest hash: staging refuses and removes its staging dir.
    {
        const fs::path root = makeTempRoot("sed-updater-corrupt");
        const fs::path feed = makeFeed(root, "0.5.0", true);
        const fs::path install = makeInstall(root);
        FileUpdateSource source(feed);
        check(!stageUpdate(source, install, "0.4.0", "stable", &failure) && failure.code == "hash-mismatch", "updater: corrupt file hash refused");
        check(!fs::exists(stagingDirectoryFor(install, "0.5.0")), "updater: failed staging is cleaned up");
        check(readText(install / "sed.exe") == "old editor binary", "updater: corrupt feed leaves the old install intact");
        fs::remove_all(root);
    }

    // Interrupted staging: a leftover staging dir blocks, never overwrites.
    {
        const fs::path root = makeTempRoot("sed-updater-interrupted");
        const fs::path feed = makeFeed(root, "0.5.0", false);
        const fs::path install = makeInstall(root);
        writeText(stagingDirectoryFor(install, "0.5.0") / "sed.exe", "partial copy");
        FileUpdateSource source(feed);
        check(!stageUpdate(source, install, "0.4.0", "stable", &failure) && failure.code == "interrupted-staging", "updater: interrupted staging refused");
        check(readText(install / "sed.exe") == "old editor binary", "updater: interrupted staging leaves the old install intact");
        fs::remove_all(root);
    }

    // Tampering after staging: activation rechecks receipts and refuses.
    {
        const fs::path root = makeTempRoot("sed-updater-tamper");
        const fs::path feed = makeFeed(root, "0.5.0", false);
        const fs::path install = makeInstall(root);
        FileUpdateSource source(feed);
        const auto staged = stageUpdate(source, install, "0.4.0", "stable", &failure);
        check(staged.has_value(), "updater: tamper fixture stages");
        if (staged)
        {
            writeText(staged->stagingDir / "sed.exe", "tampered binary!!");
            check(!activateUpdate(staged->stagingDir, std::nullopt, &failure) && failure.code == "hash-mismatch", "updater: staged tampering refused at activation");
            check(!fs::exists(activatedDirectoryFor(install, "0.5.0")), "updater: tampered staging is not activated");
        }
        check(readText(install / "sed.exe") == "old editor binary", "updater: tamper refusal leaves the old install intact");
        fs::remove_all(root);
    }
}

} // namespace sed::tests
