#pragma once

#include "sed/assets/native_model_document.hpp"

namespace sed::assets
{

struct ModelCollisionFaceCounts
{
    std::size_t meshes{};
    std::size_t faces{};
    // Directly authored material -1 and face geometry mode 0, in geoset zero.
    std::size_t untexturedModeZeroFaces{};
};

struct ModelCollisionComparison
{
    ModelCollisionFaceCounts source;
    ModelCollisionFaceCounts candidate;
    std::size_t matchedFaces{};
    std::size_t missingFaces{};
    std::size_t extraFaces{};
    bool sameFaceSequence{};
    bool sameMeshFaceCounts{};
};

// Compare raw first-geoset faces across mesh boundaries. Equality requires
// every ordered corner XYZ and the authored face normal, including duplicates.
// No Thing placement, runtime contact, face flags or visual payload is compared.
ModelCollisionComparison compareModelCollision(const NativeModelDocument& source, const NativeModelDocument& candidate);

} // namespace sed::assets
