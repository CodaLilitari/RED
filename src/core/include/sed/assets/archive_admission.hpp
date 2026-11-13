#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sed::assets
{
enum class ArchiveIndexAdmissionStatus
{
    Accepted,
    Malformed,
    EntryLimitExceeded
};
struct ArchiveIndexAdmission
{
    ArchiveIndexAdmissionStatus status{ArchiveIndexAdmissionStatus::Malformed};
    std::uint32_t entries{};
};

/** Allocation-free captured index admission, before GobArchive::fromBytes.
 * The authoritative parser still validates payload bounds and entry names.
 * Charge the returned physical count only when status is Accepted. */
inline ArchiveIndexAdmission admitCapturedArchiveIndex(const std::string_view bytes,
    const std::size_t maxEntries) noexcept
{
    const auto read32 = [&bytes](const std::size_t offset)
    {
        std::uint32_t value{};
        for ( unsigned index = 0; index < 4u; ++index )
        {
            value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + index]))
                << (index * 8u);
        }
        return value;
    };
    std::uint32_t count{};
    if ( bytes.size() >= 8u && bytes.substr(0u, 4u) == "GOB\n" )
    {
        const std::size_t indexOffset = read32(4u);
        if ( indexOffset > bytes.size() || bytes.size() - indexOffset < 4u )
        {
            return {};
        }
        count = read32(indexOffset);
        if ( count > (bytes.size() - indexOffset - 4u) / 21u )
        {
            return {};
        }
    }
    else if ( bytes.size() >= 16u && bytes.substr(0u, 4u) == "GOB " )
    {
        count = read32(12u);
        if ( count > (bytes.size() - 16u) / 136u )
        {
            return {};
        }
    }
    else
    {
        return {};
    }
    if ( count > maxEntries )
    {
        return {ArchiveIndexAdmissionStatus::EntryLimitExceeded, 0};
    }
    return {ArchiveIndexAdmissionStatus::Accepted, count};
}
} // namespace sed::assets
