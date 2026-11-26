"""Synthetic convex pad and teleport fixtures. No game files are required."""
import copy
import math
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from quake_trigger_routes import add_routes


def fixture(mots=False):
    def table(name, rows):
        return "\r\n".join([f"Section: {name}", f"World {name} 5", *rows, "END"])
    lev = {name: table(name, rows) for name, rows in {
        "things": ["0: walkplayer player 0 0 0 0 0 0 0" + (" -1" if mots else "")],
        "cogscripts": [], "cogs": [], "templates": [
            "walkplayer _actor movesize=.065 airdrag=.5",
            "ghost none type=ghost move=path"]}.items()}
    lev["sectors"] = "Section: SECTORS\nWorld sectors 1\nSector 0"
    lev["header"] = "Version 1\nWorld Gravity 4"
    if mots:
        lev["archlighting"] = "Section: ARCHLIGHTING"
    # Chamfer the +X,+Y corner; a model AABB cannot represent this trigger.
    h = 2**-.5
    planes = [(1,0,0,64), (-1,0,0,0), (0,1,0,64), (0,-1,0,0),
              (0,0,1,4), (0,0,-1,4), (h,h,0,72*h)]
    lumps = [b""] * 18
    lumps[2] = b"".join(struct.pack("<4f", *p) for p in planes)
    lumps[8] = struct.pack("<3i", 0,len(planes),0)
    lumps[9] = b"".join(struct.pack("<2i", i,0) for i in range(len(planes)))
    header = bytearray(b"IBSP/\x00\x00\x00")
    offset = 8 + 8*len(lumps)
    for data in lumps:
        header.extend(struct.pack("<2i", offset,len(data)))
        offset += len(data)
    data = bytes(header) + b"".join(lumps)
    models = [(), (0,0,-4,64,64,4,0,0,0,1)]
    entities = [{"classname":"trigger_push","model":"*1","target":"top"},
                {"classname":"target_position","targetname":"top","origin":"64 32 320"},
                {"classname":"trigger_teleport","model":"*1","target":"end"},
                {"classname":"target_teleporter","targetname":"end","origin":"48 32 24","angle":"90"}]
    return lev, entities, models, data


class QuakeRoutesTests(unittest.TestCase):
    def test_complete_brush_apex_and_ghost(self):
        for mots in (False, True):
            lev, entities, models, source = fixture(mots)
            files = {}
            result = add_routes(lev,files,entities,models,1/320,source_bsp=source)
            jump,warp = result["routes"]
            self.assertEqual(len(jump["sourceHulls"][0]["planes"]), 7)
            code = files["cog/qlroutes.cog"].decode()
            self.assertIn("VectorDot(pos, '0.707106",code)
            self.assertIn("GetLocalPlayerThing()",code)
            self.assertEqual(warp["ghostThing"],1)
            dst = next(s for s in lev["things"].splitlines() if "qlexit" in s)
            self.assertEqual(dst.split()[-2:],["0","-1"] if mots else ["0","0"])
            self.assertEqual(warp["nativeGhostYaw"],0)
            for actual, wanted in zip(warp["nativeGhostPosition"], [.15,.1,25/320]):
                self.assertAlmostEqual(actual, wanted)
            # Integrate independently from the source trigger center to its apex.
            xyz = list(jump["nativeTriggerCenter"])
            v = list(jump["launchVelocity"])
            for _ in range(200):
                v = [n * .99 for n in v]
                v[2] -= .08
                if v[2] <= 0:
                    break
                xyz = [p + n*.02 for p,n in zip(xyz,v)]
            for measured,wanted in zip(xyz,[.2,.1,1.]):
                self.assertTrue(math.isclose(measured,wanted,abs_tol=1e-7))

    def test_invalid_second_route_preserves_jkl_and_archive(self):
        lev, entities, models, source = fixture()
        del lev["cogscripts"]
        before = copy.deepcopy(lev)
        files = {"episode.jk": b"existing"}
        with self.assertRaises((ValueError,KeyError)):
            add_routes(lev,files,entities,models,1/320,source_bsp=source)
        self.assertEqual(lev,before)
        self.assertEqual(files,{"episode.jk": b"existing"})

    def test_reject_multisector_scene_and_preexisting_script(self):
        lev, entities, models, source = fixture()
        existing = {"COG\\QLROUTES.COG": b"old"}
        with self.assertRaisesRegex(ValueError, "already present"):
            add_routes(lev, existing, entities, models, 1/320, source_bsp=source)
        self.assertEqual(existing, {"COG\\QLROUTES.COG": b"old"})
        lev["sectors"] = "World sectors 2\nSector 0\nSector 1\n"
        with self.assertRaisesRegex(ValueError, "single-sector"):
            add_routes(lev, {}, entities, models, 1/320, source_bsp=source)

    def test_unresolved_source_target_is_reported(self):
        lev, entities, models, source = fixture()
        entities[0]["target"] = "missing"
        with self.assertRaisesRegex(ValueError,"destination"):
            add_routes(lev,{},entities,models,1/320,source_bsp=source)


if __name__ == "__main__":
    unittest.main()
