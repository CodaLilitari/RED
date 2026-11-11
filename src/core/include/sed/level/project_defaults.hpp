#pragma once

#include "sed/level.hpp"

namespace sed::level
{

/**
 * Engine-facing header values for a model-created project.
 *
 * A zero-initialized LevelHeader is a valid document but a poor level: zero
 * MIPMAP, LOD, PERSPECTIVE and GOURAUD distances make the engines pick the
 * coarsest texture and model detail everywhere. These are the editor's
 * new-project values, shared with the headless agent so both start from the
 * same header. The header is replaced; no other Level state changes except
 * `kind`.
 */
void applyNewProjectHeaderDefaults(Level& level, ProjectType kind);

/**
 * Spare resource slots a new project declares beyond its collected records.
 *
 * The engines size several runtime tables from the level's declared counts,
 * and scripts and templates create resources the writer cannot enumerate.
 * These are the editor's defaults; a stored zero is still honored exactly.
 */
[[nodiscard]] LevelExportBufferExtras defaultNewProjectExportBufferExtras(ProjectType kind) noexcept;

} // namespace sed::level
