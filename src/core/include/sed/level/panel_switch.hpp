#pragma once

#include "sed/assets/3do.hpp"
#include "sed/level/cog_bindings.hpp"
#include "sed/level/workbench.hpp"

namespace sed::assets { class Templates; }

namespace sed::level
{

struct PanelSwitchRequest
{
    std::int32_t owner{ -1 };
    std::size_t mesh{};
    std::size_t face{};
    double u{}, v{}; // In-plane offsets in JKU from the face's vertex centroid.
    double radius{ 0.02 };
    std::int32_t cog{ -1 };
    std::string symbol;
};

struct PanelSwitchPlacement
{
    std::optional<WorkbenchFailure> failure;
    TVector center{}, anchor{}, uAxis{}, vAxis{};
    std::vector<TVector> boundary;
    std::int32_t button{ -1 };
    bool ok() const noexcept { return !failure; }
};

std::string panelOwnerModel(const Level& level, const assets::Templates* templates, std::int32_t owner);

// model is the owner's first physical geoset, loaded with loadCheckedModel3doCollision.
PanelSwitchPlacement locatePanelSwitch(const Level&, const assets::Templates*, const assets::Model3do&, const PanelSwitchRequest&);
PanelSwitchPlacement placePanelSwitch(Level&, const assets::Templates*, const assets::Model3do&, const PanelSwitchRequest&,
    const CogSchemaResolver&);
} // namespace sed::level
