#pragma once

#include "sed/assets/resource_locator.hpp"

#include <algorithm>

namespace sed::assets
{

/** Refresh file-backed archive indexes before checking a reviewed operation's inputs.
 * In-memory archives and primary overrides remain pinned to their captured bytes. */
inline ResourceLocator currentArchiveSnapshot(const ResourceLocator& source)
{
    ResourceLocator current = source;
    if (std::ranges::any_of(source.gobs(), [](const auto& archive) { return !archive.capturedBytes(); }))
    {
        current.clearGobs();
        for (const auto& archive : source.gobs())
            current.mountGob(archive.capturedBytes() ? archive : GobArchive::open(archive.path()));
    }
    return current;
}

} // namespace sed::assets
