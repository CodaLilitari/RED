#pragma once

#include "bounded_input.hpp"

#include "sed/assets/archive_admission.hpp"
#include "sed/assets/gob.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sed_agent::detail
{

// Inspect only the captured header and index extent, without allocating index
// entries. Call before GobArchive::fromBytes and charge the returned count to
// the command's remaining entry budget. The core parser still validates each
// entry's payload bounds and interprets its name.
inline std::uint32_t admitArchiveIndex(const std::string_view bytes, const std::size_t maxEntries)
{
    const auto admission = sed::assets::admitCapturedArchiveIndex(bytes, maxEntries);
    if ( admission.status == sed::assets::ArchiveIndexAdmissionStatus::Malformed )
    {
        throw InputReadError(InputReadError::Kind::ReadFailure, "Invalid or truncated archive index.");
    }
    if ( admission.status == sed::assets::ArchiveIndexAdmissionStatus::EntryLimitExceeded )
    {
        throw InputReadError(InputReadError::Kind::LimitExceeded, "Archive index exceeds its entry limit.");
    }
    return admission.entries;
}

// Current consumers iterate physical rows but materialize/hash by name. GOB
// lookup selects the first normalized-name match, so aliases must be charged
// for that resolved payload rather than their own possibly smaller row size.
// Commit caller accounting only after the complete archive is admitted.
inline void admitArchivePayloads(const sed::assets::GobArchive& archive,
    std::size_t& remainingBytes, std::size_t& remainingEntries)
{
    std::size_t bytes = remainingBytes;
    std::size_t entries = remainingEntries;
    for ( const auto& row : archive.entries() )
    {
        const auto resolved = archive.find(row.name);
        if ( !resolved )
        {
            throw InputReadError(InputReadError::Kind::ReadFailure, "Archive index contains an unresolvable entry name.");
        }
        if ( entries == 0u || resolved->size > bytes )
        {
            throw InputReadError(InputReadError::Kind::LimitExceeded, "Archive entries exceed the aggregate expansion budget.");
        }
        --entries;
        bytes -= resolved->size;
    }
    remainingBytes = bytes;
    remainingEntries = entries;
}

} // namespace sed_agent::detail
