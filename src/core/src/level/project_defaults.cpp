#include "sed/level/project_defaults.hpp"

namespace sed::level
{

void applyNewProjectHeaderDefaults(Level& level, const ProjectType kind)
{
    level.kind = kind;
    level.header = LevelHeader{};
    level.header.version = (kind == ProjectType::IJIM) ? 3 : 1;
    level.header.gravity = (kind == ProjectType::IJIM) ? 2.0 : 4.0;
    level.header.ceilingSky.height = 15.0;
    level.header.horizonSky.distance = 100.0;
    level.header.horizonSky.pixelsPerRev = 768.0;
    level.header.mipmapDistances = { 1.0, 2.0, 3.0, 4.0 };
    level.header.lodDistances = { 0.3, 0.6, 0.9, 1.2 };
    level.header.perspectiveDistance = 2.0;
    level.header.gouraudDistance = 2.0;
    level.header.fog.enabled = false;
    level.header.fog.color = ColorWhite;
    level.header.fog.fogStart = 0.0;
    level.header.fog.fogEnd = 250.0;
}

LevelExportBufferExtras defaultNewProjectExportBufferExtras(const ProjectType kind) noexcept
{
    // These are explicit editable extras now, not hidden writer floors. The
    // values retain the original SED writer's new-project headroom while a
    // user-entered zero is honored exactly on export.
    return LevelExportBufferExtras{
        .sounds = kind == ProjectType::IJIM ? 80u : 0u,
        .materials = kind == ProjectType::IJIM ? 64u : 0u,
        .aiClasses = 64,
        .models = 1024,
        .sprites = 128,
        .keyframes = 512,
        .puppets = 64,
        .soundClasses = 128,
        .cogScripts = 128,
        .cogs = 0,
        .templates = 0,
        .things = 256,
    };
}

} // namespace sed::level
