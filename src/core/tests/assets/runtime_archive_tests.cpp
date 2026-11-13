#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "sed/assets/runtime_archive.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/jkl.hpp"
#include <cstring>

namespace
{
int failures{};
void check(const bool value, const char* message)
{
    if ( !value )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
}

void checkIncrementalAdmission()
{
    sed::assets::RuntimeArchiveBuilder builder(2);
    bool refusedAtAdd{};
    try
    {
        builder.add({"mat/bad.mat", "invalid"});
    }
    catch ( const sed::assets::RuntimeArchiveError& error )
    {
        refusedAtAdd = error.code() == sed::assets::RuntimeArchiveErrorCode::InvalidEntry;
    }
    check(refusedAtAdd, "incremental capture validates each entry before the next source read");
    sed::assets::RuntimeArchiveBuilder incomplete(2);
    incomplete.add({"misc/a.bin", "a"});
    bool refusedFinish{};
    try
    {
        (void)incomplete.finish();
    }
    catch ( const sed::assets::RuntimeArchiveError& error )
    {
        refusedFinish = error.code() == sed::assets::RuntimeArchiveErrorCode::InvalidRequest;
    }
    check(refusedFinish, "incomplete declared archive refuses publication");
    sed::assets::RuntimeArchiveBuilder full(1);
    full.add({"misc/a.bin", "a"});
    try
    {
        full.add({"misc/b.bin", "b"});
    }
    catch ( const sed::assets::RuntimeArchiveError& )
    {
    }
    bool poisoned{};
    try
    {
        (void)full.finish();
    }
    catch ( const sed::assets::RuntimeArchiveError& error )
    {
        poisoned = error.code() == sed::assets::RuntimeArchiveErrorCode::InvalidRequest;
    }
    check(poisoned, "over-count add refusal poisons builder and prevents later publication");

}

int main()
{
    using namespace sed::assets;
    checkIncrementalAdmission();
    const std::string payload("abc\0\xff", 5);
    const std::vector<RuntimeArchiveInput> inputs{{"Misc/ReadMe.BIN", payload}, {"episode.jk", "fixture\n"}};
    const auto result = buildRuntimeArchive(inputs);
    check(result.entries.size() == 2 && result.entries[0].name == "misc\\readme.bin"
            && result.entries[0].bytes == 5 && result.entries[0].readBack == "none",
        "runtime archive reports actual canonical entry and unvalidated type");
    check(result.entries.size() == 2 && result.entries[0].sha256 == jklSourceSha256Hex(payload),
        "runtime archive hashes unchanged captured payload");
    std::vector<std::byte> raw(result.outputData.size());
    std::memcpy(raw.data(), result.outputData.data(), raw.size());
    const auto reopened = GobArchive::fromBytes(std::move(raw));
    check(reopened.entries().size() == 2 && reopened.entries()[0].name == "misc\\readme.bin"
            && reopened.entries()[1].name == "episode.jk", "runtime archive preserves requested physical order");
    const auto bytes = reopened.read("misc/readme.bin");
    check(bytes.size() == payload.size() && std::memcmp(bytes.data(), payload.data(), bytes.size()) == 0,
        "runtime archive preserves binary payload exactly");
    const auto refuses = [](const std::vector<RuntimeArchiveInput>& entries, const RuntimeArchiveErrorCode code)
    {
        try
        {
            (void)buildRuntimeArchive(entries);
        }
        catch ( const RuntimeArchiveError& error )
        {
            return error.code() == code;
        }
        return false;
    };
    check(refuses({{"MAT/A.BIN", "a"}, {"mat/a.bin", "b"}}, RuntimeArchiveErrorCode::DuplicateEntry),
        "case-colliding canonical destinations refuse");
    check(refuses({{"../out.bin", "a"}}, RuntimeArchiveErrorCode::InvalidEntryName),
        "traversal logical destination refuses");
    check(refuses({{"mat/bad.mat", "invalid"}}, RuntimeArchiveErrorCode::InvalidEntry),
        "malformed known MAT refuses before archive publication");
    check(refuses({}, RuntimeArchiveErrorCode::InvalidRequest), "empty archive refuses");
    std::vector<RuntimeArchiveInput> tooMany(4097, {"a.bin", ""});
    check(refuses(tooMany, RuntimeArchiveErrorCode::InvalidRequest), "over-limit row count refuses before entries");
    bool dotNameUnchanged{};
    try
    {
        const auto dotName = buildRuntimeArchive(std::vector<RuntimeArchiveInput>{{".mat", "invalid"}});
        dotNameUnchanged = dotName.entries[0].readBack == "none";
    }
    catch ( const std::exception& )
    {
    }
    check(dotNameUnchanged, "dotfile without an extension retains the agent unknown-type classification");
    const auto warning = buildRuntimeArchive(std::vector<RuntimeArchiveInput>{{"root.bin", ""}, {"odd/a.bin", ""}});
    check(warning.warnings.size() == 2, "root and unusual folder are reported without changing payloads");
    const auto sizesRefuse = [](const std::vector<std::size_t>& sizes, const RuntimeArchiveErrorCode code)
    {
        try
        {
            validateRuntimeArchiveSizes(sizes);
        }
        catch ( const RuntimeArchiveError& error )
        {
            return error.code() == code;
        }
        return false;
    };
    constexpr std::size_t mib = 1024u * 1024u;
    check(sizesRefuse({256u * mib + 1}, RuntimeArchiveErrorCode::SourceTooLarge),
        "source byte admission refuses before allocation");
    check(sizesRefuse({256u * mib, 256u * mib}, RuntimeArchiveErrorCode::ArchiveTooLarge),
        "aggregate admission includes header and index");
    validateRuntimeArchiveSizes(std::vector<std::size_t>{256u * mib, 256u * mib - 288u});
    check(sizesRefuse({256u * mib, 256u * mib - 287u}, RuntimeArchiveErrorCode::ArchiveTooLarge),
        "aggregate exact fit and one-over differ by one byte");
    return failures == 0 ? 0 : 1;
}
