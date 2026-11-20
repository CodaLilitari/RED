#pragma once
#include "sed/level/shadow_cuts.hpp"
#include <QByteArray>
#include <optional>

namespace sed::ui::lighting
{
struct OwnedCuts
{
    QByteArray base, generated;
    std::vector<std::vector<SurfaceRef>> families;
};

QByteArray cutTopology(const Level& level);
QByteArray encodeOwnedCuts(const OwnedCuts& cuts);
std::optional<OwnedCuts> decodeOwnedCuts(const QByteArray& bytes);
} // namespace sed::ui::lighting
