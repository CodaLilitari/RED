import json
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL))
import walkway_clearance as walkway
import walkway_geometry as geometry


def level(tmp, faces, things=None):
    """Write the native RED row layout; capacities intentionally exceed occupied Things."""
    vertices = []
    rows = []
    for flag, adjoin, points in faces:
        first = len(vertices)
        vertices.extend(points)
        pairs = " ".join(f"{i},0" for i in range(first,len(vertices)))
        rows.append(f"0 {flag} 0 4 3 3 {adjoin} 0 {len(points)} {pairs}")
    def table(name, records):
        return f"{name} {len(records)}\n" + "".join(f"{i}: {r}\n" for i,r in enumerate(records))
    geo = (table("World vertices",[" ".join(map(str,v)) for v in vertices]) +
           table("World adjoins",["0x7 0 0","0x17 1 0"]) + table("World surfaces",rows))
    src = ("SECTION: GEORESOURCE\n" + geo + "END\nSECTION: SECTORS\nWorld sectors 1\n"
           f"SECTOR 0\nSURFACES 0 {len(rows)}\nEND\nSECTION: TEMPLATES\nWorld templates 99\n"
           "player none type=player collide=1 size=.065\nitem none type=item collide=1 size=.1\n"
           "solid none collide=3 model3d=wall.3do\nsphere none collide=1 size=.15\n"
           "child solid\nEND\nSECTION: THINGS\nWorld things 99\n"
           "0: player Start .3 .3 .05 0 0 0 0\n1: item Prize 1.7 .3 .05 0 0 0 0\n" +
           (things or "") + "END\n")
    dst=tmp/"room.jkl"
    dst.write_text(src,encoding="ascii")
    return dst


def rect(x1,y1,x2,y2):
    return (5,-1,[(x1,y1,0),(x2,y1,0),(x2,y2,0),(x1,y2,0)])


class NativeGeometryTests(unittest.TestCase):
    def test_native_corner_pairs_and_unprefixed_hex_masks(self):
        with tempfile.TemporaryDirectory() as temp:
            wall = [(1,0,.001), (1,.6,.001), (1,.6,.21), (1,0,.21)]
            file = level(Path(temp), [rect(0,0,2,.6), (0,0,wall)])
            original = file.read_text(encoding="ascii")
            # 0x22 allows crossing. Decimal 22 would incorrectly block the player.
            text = original.replace("0x7 0 0", "22 0 0").replace(
                "0 5 0 4 3 3 -1", "0 000000a5 0 4 3 3 -1").replace(
                "0,0 1,0 2,0 3,0", "0 3 1, 0 2,0 3 1")
            file.write_text(text, encoding="ascii")
            floors, walls, _, _ = geometry.load_level(file, 0, 0)
            self.assertEqual(floors, [rect(0,0,2,.6)[2]])
            self.assertEqual(walls, [])
            file.write_text(text.replace("22 0 0", "32 0 0"), encoding="ascii")
            self.assertEqual(geometry.load_level(file, 0, 0)[1], [wall])

    def test_nonadjoined_faces_require_collision_to_support_or_block(self):
        with tempfile.TemporaryDirectory() as temp:
            footprint = rect(0, 0, 2, .6)[2]
            wall = [(1,0,0), (1,.6,0), (1,.6,.2), (1,0,.2)]
            file = level(Path(temp), [(5,-1,footprint), (1,-1,footprint), (0,-1,wall)])
            floors, walls, _, _ = geometry.load_level(file, 0, 0)
            self.assertEqual(floors, [footprint])
            self.assertEqual(walls, [])

    def test_3do_separate_and_joined_vertex_uv_fields(self):
        model = """SECTION: GEOMETRYDEF
GEOSETS 1
GEOSET 0
MESH 0
VERTICES 3
0: 0 0 0
1: 1 0 0
2: 0 0 .2
FACES 1
0: 0 0 4 3 1 0 3 0 2 1, 0 2,1
SECTION: HIERARCHYDEF
HIERARCHY NODES 1
0: 0x0 0x0 0 -1 -1 -1 0 0 0 0 0 0 0 0 0 0 wall
"""
        with tempfile.TemporaryDirectory() as temp:
            file = Path(temp) / "wall.3do"
            file.write_text(model, encoding="ascii")
            self.assertEqual(geometry.native_mesh(file)[0], [[(0,0,0),(1,0,0),(0,0,.2)]])
            file.write_text(model.replace("1, 0", "1, broken"), encoding="ascii")
            with self.assertRaisesRegex(ValueError, "texture vertex"):
                geometry.native_mesh(file)


class HelpTests(unittest.TestCase):
    def test_standard_library_help_and_missing_extras(self):
        cmd=[sys.executable,"-B","-S",str(TOOL/"walkway_clearance.py")]
        run=subprocess.run(cmd+["--help"],capture_output=True,check=True,timeout=15)
        self.assertIn(b"--sector",run.stdout)
        missing=subprocess.run(cmd+["unused.jkl","--sector","0","--floor-z","0","--radius",".1",
                                    "--height",".2","--anchor","A","--anchor","B","--out","unused"],
                               capture_output=True,timeout=15)
        self.assertEqual(missing.returncode,2)
        self.assertIn(b"pip install numpy scipy Pillow",missing.stderr)


@unittest.skipUnless(all(importlib.util.find_spec(p) for p in ("numpy","scipy","PIL")),
                     "optional walkway analysis dependencies are not installed")
class WalkwayTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.tmp = Path(self.temp.name)

    def run_tool(self, src, *extra, output="analysis"):
        out = self.tmp/output
        args = [str(src),"--out",str(out),"--radius",".065","--height",".2","--step",".01",
                "--sector","0","--floor-z","0","--anchor","Start","--anchor","Prize",*extra]
        return walkway.main(args), out

    def test_pinched_connected_path_and_overlay(self):
        path=level(self.tmp,[rect(0,0,.8,.6),rect(.8,.19,1.2,.41),rect(1.2,0,2,.6)])
        status,out=self.run_tool(path)
        self.assertEqual(status,0)
        report=json.loads((out/"report.json").read_text())
        self.assertTrue((out/"overlay.png").read_bytes().startswith(b"\x89PNG"))
        self.assertTrue(report["sampledConnected"])
        self.assertGreater(report["widestSampledRadius"],.075)
        self.assertLess(report["widestSampledRadius"],.1)
        self.assertLess(abs(report["anchorPathPinch"]["x"]-1),.25)
        self.assertEqual(report["transientThingsExcluded"],["Start","Prize"])
        self.assertEqual(self.run_tool(path)[0],2)
        status,out=self.run_tool(path,"--radius",".14",output="blocked")
        self.assertEqual(status,1)
        self.assertFalse(json.loads((out/"report.json").read_text())["passes"])

    def test_player_blocked_adjoin_and_low_ceiling(self):
        solid=[(1,0,.001),(1,.61,.001),(1,.61,.21),(1,0,.21)]
        for name,adjoin,expected in [("open",0,0),("player-block",1,1),("wall",-1,1)]:
            src=level(self.tmp,[rect(0,0,2,.6),(4,adjoin,solid)])
            self.assertEqual(self.run_tool(src,output=name)[0],expected)
        ceiling=[(.95,0,.12),(1.05,0,.12),(1.05,.61,.12),(.95,.61,.12)]
        src=level(self.tmp,[rect(0,0,2,.6),(4,-1,ceiling)])
        self.assertEqual(self.run_tool(src,output="ceiling")[0],1)

    def test_model_collision_inheritance_yaw_and_slab(self):
        model = ("SECTION: GEOMETRYDEF\nGEOSETS 1\nGEOSET 0\nMESH 0\nVERTICES 4\n"
                 "0: -.5 0 0\n1: .5 0 0\n2: .5 0 .2\n3: -.5 0 .2\n"
                 "FACES 1\n0: 0 0 4 3 1 0 4 0, 0 1, 0 2, 0 3, 0\n"
                 "SECTION: HIERARCHYDEF\nHIERARCHY NODES 1\n"
                 "0: 0x0 0x0 0 -1 -1 -1 0 0 0 0 0 0 0 0 0 0 wall\n")
        (self.tmp/"WALL.3DO").write_text(model)
        placement="2: child Gate 1 .3 {z} 0 90 0 0\n"
        low=level(self.tmp,[rect(0,0,2,.6)],placement.format(z=0))
        self.assertEqual(self.run_tool(low,output="no-models")[0],2)
        self.assertEqual(self.run_tool(low,"--models",str(self.tmp))[0],1)
        high=level(self.tmp,[rect(0,0,2,.6)],placement.format(z=1))
        self.assertEqual(self.run_tool(high,"--models",str(self.tmp),output="above")[0],0)
        (self.tmp/"WALL.3DO").write_text(model.replace("0 0 0 wall","0 .25 0 wall"))
        self.assertEqual(self.run_tool(high,"--models",str(self.tmp),output="unsupported")[0],2)

    def test_loop_around_excluded_point(self):
        floor=[rect(0,0,2,.6),rect(0,.6,.4,2),rect(1.6,.6,2,2),rect(.4,1.6,1.6,2)]
        src=level(self.tmp,floor)
        status,out=self.run_tool(src,"--around","1","1")
        self.assertEqual(status,0)
        self.assertTrue(json.loads((out/"report.json").read_text())["surroundsAtRadius"])
        src=level(self.tmp,floor[:-1])
        status,out=self.run_tool(src,"--around","1","1",output="openring")
        self.assertEqual(status,1)
        self.assertTrue(json.loads((out/"report.json").read_text())["sampledConnected"])
        self.assertFalse(json.loads((out/"report.json").read_text())["surroundsAtRadius"])

    def test_unknown_duplicate_and_grid_budget(self):
        src=level(self.tmp,[rect(0,0,2,.6)])
        for args in [("--anchor","missing"),("--anchor","start"),("--floor-z","nan"),("--step",".00001")]:
            self.assertEqual(self.run_tool(src,*args)[0],2)
        big=level(self.tmp,[rect(0,0,1000,1000)])
        self.assertEqual(self.run_tool(big)[0],2)
        self.assertFalse((self.tmp/"analysis").exists())

    def test_moving_props_explicitly_scoped(self):
        for move in ("physics","path numframes=2","path frame=(0/0/0:0/0/0)"):
            src=level(self.tmp,[rect(0,0,2,.6)],f"2: sphere Gate 1 .3 .1 0 0 0 0 move={move}\n")
            self.assertEqual(self.run_tool(src,"--stationary-paths")[0],2)
        src=level(self.tmp,[rect(0,0,2,.6)],"2: sphere Post 1 .51 .1 0 0 0 0 move=path\n")
        self.assertEqual(self.run_tool(src)[0],2)
        status,out=self.run_tool(src,"--stationary-paths")
        self.assertEqual(status,0)
        self.assertEqual(json.loads((out/"report.json").read_text())["stationaryPathPoses"],["Post"])


if __name__ == "__main__":
    unittest.main()
