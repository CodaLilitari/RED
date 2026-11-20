#pragma once

#include "sed/level.hpp"

namespace sed::level
{

/** A storage-slot edit against the specific Level used to review the operation. */
struct SurfaceMaterialEdit
{
    int sector{};
    int surface{};
    int materialIndex{};

    bool operator==(const SurfaceMaterialEdit&) const = default;

    bool validFor(const Level& level) const noexcept
    {
        return sector >= 0 && static_cast<std::size_t>(sector) < level.sectors.size()
            && surface >= 0 && static_cast<std::size_t>(surface) < level.sectors[sector].surfaces.size()
            && materialIndex >= 0 && static_cast<std::size_t>(materialIndex) < level.materials.size();
    }

    /** Returns false for a refused or unchanged edit. No other fields are touched. */
    bool apply(Level& level) const
    {
        if (!validFor(level)) return false;
        auto& face = level.sectors[sector].surfaces[surface];
        const auto& name = level.materials[materialIndex];
        if (face.nMat == materialIndex && face.material == name) return false;
        face.material = name;
        face.nMat = materialIndex;
        return true;
    }
};

} // namespace sed::level
