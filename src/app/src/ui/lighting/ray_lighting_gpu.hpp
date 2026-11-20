#pragma once

#include "sed/level/ray_lighting.hpp"

#include <QString>

#include <memory>

namespace sed::ui::lighting
{

struct RayLightingGpuInput;

// Prepare one native context and offscreen surface on the GUI thread for a single worker.
[[nodiscard]] std::shared_ptr<RayLightingGpuInput> createRayLightingGpuInput();

// Construct, use and destroy on one worker thread, for one immutable bake job.
// Scene arrays must remain immutable and address-stable for the object's
// lifetime. No viewport context is used.
class RayLightingGpu final
{
public:
    explicit RayLightingGpu(RayLightingGpuInput* input);
    ~RayLightingGpu();
    RayLightingGpu(const RayLightingGpu&) = delete;
    RayLightingGpu& operator=(const RayLightingGpu&) = delete;

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] QString description() const;
    [[nodiscard]] bool intersect(
        std::span<const level::RayLightTriangle> triangles,
        std::span<const level::RayLightNode> nodes,
        std::span<const level::RayLightRay> rays,
        std::span<level::RayLightHit> hits);

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace sed::ui::lighting
