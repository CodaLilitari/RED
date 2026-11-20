#pragma once

#include "../../core/tests/test_common.hpp"

namespace sed::workspace_test
{
inline constexpr SurfaceRef front{0, 0}, back{1, 1};

// Each side has its own affine mapping. Coordinates cross both MotS tile boundaries.
inline TexVertex portalCorner(const int side, const Vertex& point)
{
    const float y = static_cast<float>(point.y), z = static_cast<float>(point.z);
    const float u = side == 0 ? 2*y + 3 : -3*y - 2;
    const float v = side == 0 ? z - 2 : 2*z + 4;
    return {u, v, {0.375f + y/8, 0.5f + z/8, side == 0 ? 0.125f : 0.75f, 0.625f}};
}

inline Level twoRoomPortal()
{
    Level level;
    level.kind = ProjectType::MOTS;
    level.layers = {"Rooms"};
    for (int side : {0,1})
    {
        auto room = tests::makeLitTestCubeSector(side*2, ColorWhite);
        room.id = 100 + side;
        auto& face = room.surfaces[side];
        face.material = side == 0 ? "bend.mat" : "landing.mat";
        face.surfaceFlags = 0x2 | (side << 4);
        face.faceFlags = 0x2 | (side << 2);
        face.adjoinFlags = side == 0 ? 0x5 : 0x7;
        face.uScale = side == 0 ? 0.5f : 2.f;
        face.vScale = side == 0 ? 4.f : 0.25f;
        face.texVertices.clear();
        for (const auto id : face.vertices) face.texVertices.push_back(portalCorner(side,room.vertices[id]));
        level.sectors.push_back(std::move(room));
    }
    level::levelSysAdjoinSurfaces(level, front, back, false);
    Cog placed;
    placed.id = 17;
    placed.name = "switch.cog";
    CogValue target;
    target.name = "door";
    target.cogType = CogType::surface;
    target.type = ValueType::integer;
    target.intValue = 0;
    placed.values.push_back(target);
    level.cogs.push_back(placed);
    return level;
}
} // namespace sed::workspace_test
