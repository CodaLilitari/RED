#pragma once

// Original synthetic geometry and authored lighting. No game assets.
inline constexpr const char* kModelLightingFixture = R"MODEL(# Synthetic model-lighting contract fixture.
SECTION: HEADER
3DO 2.1

SECTION: MODELRESOURCE
MATERIALS 1
0: fixture.mat

SECTION: GEOMETRYDEF
RADIUS 2
INSERT OFFSET 0 0 0
GEOSETS 1
GEOSET 0
MESHES 1
MESH 0
NAME fixture
RADIUS 2
GEOMETRYMODE 4
LIGHTINGMODE 3
TEXTUREMODE 1
VERTICES 3
0: 0 0 0 0.25 0.25 0.25 0.375 # first alpha
1: 1 0 0 0.25 0.25 0.25 0.625
2: 0 1 0 0.25 0.25 0.25 0.875
TEXTURE VERTICES 3
0: 0 0
1: 16 0
2: 0 16
VERTEX NORMALS
0: 0 0 1
1: 1 0 0
2: 0 0 -1
FACES 1
0: 0 0x0 4 3 1 0 3 0, 0 1, 1 2, 2
FACE NORMALS
0: 0 0 1

SECTION: HIERARCHYDEF
HIERARCHY NODES 1
0: 0x0 0x1 0 -1 -1 -1 0 0 0 0 0 0 0 0 0 0 fixture

SECTION: KEEP_OPAQUE
VERTICES 1
0: do not change this opaque record
)MODEL";
