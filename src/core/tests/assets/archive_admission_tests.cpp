#include "sed/assets/archive_admission.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace
{
int failures{};

void check(bool condition, const char* name)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

void put32(std::string& bytes, std::size_t offset, std::uint32_t value)
{
    for ( unsigned i = 0; i < 4; ++i )
    {
        bytes[offset + i] = static_cast<char>((value >> (8u * i)) & 0xffu);
    }
}

std::string v1(std::uint32_t count)
{
    std::string bytes(12 + 21u * count, '\0');
    bytes.replace(0, 4, "GOB\n");
    put32(bytes, 4, 8);
    put32(bytes, 8, count);
    return bytes;
}

std::string v2(std::uint32_t count)
{
    std::string bytes(16 + 136u * count, '\0');
    bytes.replace(0, 4, "GOB ");
    put32(bytes, 12, count);
    return bytes;
}
}

int main()
{
    using namespace sed::assets;
    const auto admit = [](const std::string& bytes, const std::size_t limit)
    {
        return admitCapturedArchiveIndex(bytes, limit);
    };
    for ( const auto& bytes : {v1(0), v2(0)} )
    {
        const auto result = admit(bytes, 0);
        check(result.status == ArchiveIndexAdmissionStatus::Accepted && result.entries == 0,
            "empty v1/v2 archives fit zero entry allowance");
    }
    for ( const auto& bytes : {v1(2), v2(2)} )
    {
        const auto result = admit(bytes, 2);
        check(result.status == ArchiveIndexAdmissionStatus::Accepted && result.entries == 2,
            "exact index allowance returns physical entry count for v1/v2");
        check(admit(bytes, 1).status == ArchiveIndexAdmissionStatus::EntryLimitExceeded,
            "one over index allowance is distinguished from malformed input");
        check(admit(bytes.substr(0, bytes.size() - 1), 2).status == ArchiveIndexAdmissionStatus::Malformed,
            "truncated final physical row is refused before allocation");
    }
    for ( std::size_t length = 0; length < 12; ++length )
    {
        check(admit(v1(0).substr(0, length), 0).status == ArchiveIndexAdmissionStatus::Malformed,
            "every truncated v1 header/index boundary is refused");
    }
    for ( std::size_t length = 0; length < 16; ++length )
    {
        check(admit(v2(0).substr(0, length), 0).status == ArchiveIndexAdmissionStatus::Malformed,
            "every truncated v2 header boundary is refused");
    }
    auto badOffset = v1(1);
    put32(badOffset, 4, 0xffffffffu);
    check(admit(badOffset, 1).status == ArchiveIndexAdmissionStatus::Malformed,
        "out of range v1 index offset does not overflow");
    auto badCount = v2(1);
    put32(badCount, 12, 0xffffffffu);
    check(admit(badCount, 1).status == ArchiveIndexAdmissionStatus::Malformed,
        "malformed oversized count retains precedence over entry limit");
    check(admit("INVALID ARCHIVE", 100000).status == ArchiveIndexAdmissionStatus::Malformed,
        "unknown archive signature is refused");
    auto payload = v2(1);
    put32(payload, 16, 0xffffffffu);
    check(admit(payload, 1).status == ArchiveIndexAdmissionStatus::Accepted,
        "index admission leaves payload validity to the authoritative GOB parser");
    return failures == 0 ? 0 : 1;
}
