#include "../archive_admission.hpp"

#include "sed/assets/gob.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
int failures{};

void check(const bool condition, const char* message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void append32(std::string& bytes, const std::uint32_t value)
{
    for ( unsigned shift = 0; shift < 32u; shift += 8u )
    {
        bytes.push_back(static_cast<char>((value >> shift) & 0xffu));
    }
}

std::string fixture(const bool versionOne, const std::uint32_t count)
{
    std::string bytes = versionOne ? "GOB\n" : "GOB ";
    if ( versionOne )
    {
        append32(bytes, 8u);
    }
    else
    {
        append32(bytes, 0x14u);
        append32(bytes, 0x0cu);
    }
    append32(bytes, count);
    for ( std::uint32_t index = 0; index < count; ++index )
    {
        append32(bytes, 0u);
        append32(bytes, 0u);
        std::string name = "entry" + std::to_string(index);
        name.resize(versionOne ? 13u : 128u, '\0');
        bytes += name;
    }
    return bytes;
}

sed::assets::GobArchive payloadFixture(const bool versionOne, const bool alias)
{
    std::string bytes = versionOne ? "GOB\n" : "GOB ";
    if ( versionOne )
    {
        append32(bytes, 8u);
    }
    else
    {
        append32(bytes, 0x14u);
        append32(bytes, 0x0cu);
    }
    append32(bytes, 2u);
    const std::uint32_t payloadOffset = (versionOne ? 12u : 16u) + 2u * (versionOne ? 21u : 136u);
    for ( unsigned index = 0; index < 2u; ++index )
    {
        append32(bytes, index == 0u ? payloadOffset : 0u);
        append32(bytes, index == 0u ? 8u : 0u);
        std::string name = index == 0u ? "dir/a.cog" : alias ? "/DIR\\A.COG" : "dir/b.cog";
        name.resize(versionOne ? 13u : 128u, '\0');
        bytes += name;
    }
    bytes += "12345678";
    std::vector<std::byte> captured;
    for ( const char value : bytes )
    {
        captured.push_back(static_cast<std::byte>(value));
    }
    return sed::assets::GobArchive::fromBytes(std::move(captured));
}

void expectError(const std::string& bytes, const std::size_t limit,
    const sed_agent::detail::InputReadError::Kind expected, const char* message)
{
    bool matched{};
    try
    {
        (void)sed_agent::detail::admitArchiveIndex(bytes, limit);
    }
    catch ( const sed_agent::detail::InputReadError& error )
    {
        matched = error.kind == expected;
    }
    check(matched, message);
}

void runTests()
{
    using Kind = sed_agent::detail::InputReadError::Kind;
    using sed_agent::detail::admitArchiveIndex;
    for ( const bool versionOne : { true, false } )
    {
        const auto threeEntries = fixture(versionOne, 3u);
        check(admitArchiveIndex(threeEntries, 3u) == 3u,
            "archive index accepts an exact configured entry limit");
        expectError(threeEntries, 2u, Kind::LimitExceeded,
            "valid archive index exceeding the small limit refuses before parsing");
        const auto zeroEntries = fixture(versionOne, 0u);
        check(admitArchiveIndex(zeroEntries, 0u) == 0u,
            "zero-entry archive is valid with no remaining entries");
        expectError(fixture(versionOne, 1u), 0u, Kind::LimitExceeded,
            "nonempty archive refuses an exhausted entry budget");
        std::vector<std::byte> parsedBytes;
        for ( const char value : threeEntries )
        {
            parsedBytes.push_back(static_cast<std::byte>(value));
        }
        check(sed::assets::GobArchive::fromBytes(std::move(parsedBytes)).entries().size() == 3u,
            "small-limit budget fixture is a valid complete core archive");
        auto truncated = threeEntries;
        truncated.pop_back();
        expectError(truncated, 100u, Kind::ReadFailure,
            "truncated index is rejected separately from a budget limit");
        std::size_t remaining = 4u;
        remaining -= admitArchiveIndex(threeEntries, remaining);
        expectError(fixture(versionOne, 2u), remaining, Kind::LimitExceeded,
            "successive archive admissions respect the command's remaining count");
        check(remaining == 1u, "refused archive admission leaves caller accounting unchanged");

        const auto aliases = payloadFixture(versionOne, true);
        check(aliases.entries()[0].size == 8u && aliases.entries()[1].size == 0u
                && aliases.read(aliases.entries()[0].name).size() == 8u
                && aliases.read(aliases.entries()[1].name).size() == 8u,
            "case/slash alias fixture resolves both physical rows to the first eight-byte payload");
        std::size_t payloadBudget = 8u;
        std::size_t entryBudget = 2u;
        bool refused{};
        try
        {
            sed_agent::detail::admitArchivePayloads(aliases, payloadBudget, entryBudget);
        }
        catch ( const sed_agent::detail::InputReadError& error )
        {
            refused = error.kind == Kind::LimitExceeded;
        }
        check(refused && payloadBudget == 8u && entryBudget == 2u,
            "resolved duplicate payloads cannot bypass expansion admission or partially consume counters");
        payloadBudget = 16u;
        sed_agent::detail::admitArchivePayloads(aliases, payloadBudget, entryBudget);
        check(payloadBudget == 0u && entryBudget == 0u,
            "exact alias budget charges the resolved bytes once for every materialized row");
        payloadBudget = 16u;
        entryBudget = 1u;
        refused = false;
        try
        {
            sed_agent::detail::admitArchivePayloads(aliases, payloadBudget, entryBudget);
        }
        catch ( const sed_agent::detail::InputReadError& error )
        {
            refused = error.kind == Kind::LimitExceeded;
        }
        check(refused && payloadBudget == 16u && entryBudget == 1u,
            "resolved payload entry exhaustion retains both caller budgets");
        const auto unique = payloadFixture(versionOne, false);
        payloadBudget = 8u;
        entryBudget = 2u;
        sed_agent::detail::admitArchivePayloads(unique, payloadBudget, entryBudget);
        check(payloadBudget == 0u && entryBudget == 0u,
            "a unique zero-byte row remains admissible with an exhausted byte budget");
    }
    auto invalidOffset = fixture(true, 0u);
    for ( std::size_t index = 4u; index < 8u; ++index )
    {
        invalidOffset[index] = static_cast<char>(0xff);
    }
    expectError(invalidOffset, 100u, Kind::ReadFailure,
        "v1 index offset cannot overflow or escape the captured bytes");
    expectError("GOB", 100u, Kind::ReadFailure, "short archive header is rejected before field reads");
    expectError("not an archive", 100u, Kind::ReadFailure, "foreign magic cannot bypass index admission");
}

} // namespace

int main()
{
    try
    {
        runTests();
    }
    catch ( const std::exception& error )
    {
        ++failures;
        std::cerr << "Unexpected archive admission error: " << error.what() << '\n';
    }
    std::cout << "archive admission failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
