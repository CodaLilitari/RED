#pragma once

#include "sed/assets/jo_ja_hybrid_geometry.hpp"

#include <memory>
#include <span>
#include <vector>

namespace sed::assets
{

struct JoJaStructuralPlane
{
    RavenBspPlane plane;                // Canonically oriented unit plane in source coordinates.
    std::vector<std::size_t> triangles; // Indices into the input triangle span.
    double area{};                      // Sum of the source triangle areas, before overlap removal.
};

// The source BSP must remain alive and unchanged for the index's lifetime.
// All queries share one bounded work budget.
class JoJaStructuralBrushIndex
{
  public:
    explicit JoJaStructuralBrushIndex(const RavenBsp& bsp);
    ~JoJaStructuralBrushIndex();
    std::vector<JoJaStructuralPlane> planes(std::span<const JoJaHybridTriangle> sourceTriangles);
    // Convex masks in source units, with the winding of the convex aperture.
    // Each mask belongs to a static opaque brush of positive thickness.
    std::vector<std::vector<TVector>> crossSections(const RavenBspPlane& interfacePlane,
                                                    std::span<const TVector> convexInterface, std::size_t maxFragments);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Identify planar world triangles wholly supported by a solid, opaque world
// brush with six verifiable axial bounds. Draw geometry supplies the partition
// plane. Inconclusive support leaves the triangle available as model detail.
std::vector<JoJaStructuralPlane> findJoJaStructuralPlanes(const RavenBsp& bsp,
                                                          std::span<const JoJaHybridTriangle> sourceTriangles);

} // namespace sed::assets
