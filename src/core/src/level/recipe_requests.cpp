#include "sed/level/recipe_requests.hpp"

namespace sed::level
{

std::optional<WorkbenchFailure> validateStairParameters(const StairParameters&)
{
    return std::nullopt;
}

std::optional<WorkbenchFailure> validateSectorLightIntensity(double)
{
    return std::nullopt;
}

std::optional<WorkbenchFailure> validateUnderwaterSettings(const UnderwaterSettings&)
{
    return std::nullopt;
}

std::optional<WorkbenchFailure> validateBreakableGlassSettings(const BreakableGlassSettings&)
{
    return std::nullopt;
}

UnderwaterSettings defaultUnderwaterSettings()
{
    return {};
}

double makeDoorHeight(const Level&, const assets::Templates*, std::size_t) noexcept
{
    return 0.0;
}

BreakableGlassSettings breakableGlassDefaults(const Level&, SurfaceRef, std::span<const CogValue>)
{
    return {};
}

std::vector<SurfaceRef> planarizeTargets(const Level&, std::span<const SurfaceRef>)
{
    return {};
}

PlanarizeResult levelPlanarizeSurfaces(Level&, std::span<const SurfaceRef>)
{
    return {};
}

std::vector<SurfaceRef> surfaceFlagRepairTargets(
    const Level&,
    std::span<const std::int32_t>,
    std::span<const SurfaceRef>)
{
    return {};
}

} // namespace sed::level
