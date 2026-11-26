"""A static model contains a texture-baked face and a residual Gouraud face."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mots_baked_draw_pass import switch_draw_pass, write_fresh


MODEL = b"""GEOSETS 1
GEOSET 0
MESHES 1
MESH 0
LIGHTINGMODE 3
VERTICES 4
0: 0 0 0 .25
1: 1 0 0 .25
2: 0 1 0 .25
3: 1 1 0 .25
FACES 2
0: 0 0x0 4 3 1 0 3 0,0 1,1 2,2
1: 1 0x0 4 0 1 0 3 1,1 2,2 3,3
"""
JKL = b"""Section: Templates
World templates 3
world none type=cog move=path thingflags=0x48 collide=3
wall world model3d=wall.3do thingflags=0x48 size=1
unrelated none thingflags=0x48
end
Section: Things
World things 3
0: wall placement 0 0 0 0 0 0 0 0
end
Section: ARCHLIGHTING
Num ArchObjects 1
archobject 0
nummeshes: 1
mesh 0
numvertices: 4
0: .25 .25 .25 .25
1: .25 .25 .25 .25
2: .25 .25 .25 .25
3: .25 .7 .3 .1
END
""".replace(b"\n", b"\r\n")


class DrawPassTests(unittest.TestCase):
    def apply(self, jkl=JKL, model=MODEL):
        return switch_draw_pass(jkl, {"wall.3do": model}, "world")

    def test_preserves_all_but_owned_flags_and_checks_only_live_corners(self):
        after, report = self.apply()
        self.assertEqual(after, JKL.replace(b"wall.3do thingflags=0x48",
                                            b"wall.3do thingflags=0x4000040"))
        self.assertEqual(report["residualGouraudVertices"], 3)
        self.assertEqual(report["residualGouraudFaces"], 1)
        self.assertEqual(report["staticPlacements"], 1)
        self.assertNotEqual(report["inputSha256"], report["outputSha256"])

    def test_rejects_colored_or_nonfinite_live_light(self):
        for value in (b".4", b"nan", b"inf"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.apply(JKL.replace(b"0: .25 .25 .25 .25", b"0: .25 .25 .25 " + value))

    def test_rejects_inherited_effects_and_placement_overrides(self):
        for before, after in ((b"unrelated none", b"unrelated wall"),
                              (b"0 0\r\nend", b"0 0 thingflags=0x48\r\nend"),
                              (b"wall.3do thingflags", b"wall.3do cog=rotate.cog thingflags"),
                              (b"move=path", b"move=physics"),
                              (b"0 0\r\nend", b"0 0\r\n0: unrelated d 0 0 0 0 0 0 0 -1\r\nend"),
                              (b"archobject 0\r", b"archobject 2\r")):
            with self.subTest(after=after), self.assertRaises(ValueError):
                self.apply(JKL.replace(before, after))

    def test_rejects_bad_mesh_index_or_unsupported_light_mode(self):
        for before, after in ((b"MESHES 1", b"MESHES 2"), (b"2,2", b"4,2"),
                              (b"4 3 1", b"4 17 1"), (b"0: 0 0 0 .25", b"3: 0 0 0 .25")):
            with self.subTest(after=after), self.assertRaises(ValueError):
                self.apply(model=MODEL.replace(before, after))

    def test_checks_every_placement_of_the_same_template(self):
        with self.assertRaises(ValueError):
            self.apply(JKL.replace(b"0 0\r\nend", b"0 0\r\n1: wall second 0 0 0 0 0 0 0 1\r\nend"))

    def test_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            dest = Path(directory) / "candidate.jkl"
            write_fresh(dest, b"original")
            with self.assertRaises(FileExistsError):
                write_fresh(dest, b"replacement")
            self.assertEqual(dest.read_bytes(), b"original")


if __name__ == "__main__":
    unittest.main()
