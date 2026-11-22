#pragma once

#include "sed/level.hpp"

#include <string>
#include <vector>

namespace sed::assets
{

struct JoJaProjectileFinding
{
    std::string weapon, projectile, status, detail;
};

// Audit stock weapon roots and typed template dependencies in the serialized level.
// Names in a stock editor catalog are not a substitute for rows in that level.
std::vector<JoJaProjectileFinding> auditJoJaProjectiles(const Level& level, ProjectType target);

} // namespace sed::assets
