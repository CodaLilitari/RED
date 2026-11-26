"""Synthetic v46 BSP, RED model packet and episode. No game assets are used."""
import json
import hashlib
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

TOOL = Path(__file__).resolve().parents[1] / "quake_lightmap_atlas.py"


def fixture(root):
    from PIL import Image
    points = [(0, 0), (320, 0), (0, 320)]
    lumps = [b""] * 17
    lumps[1] = struct.pack("<64sii", b"stone", 0, 1)
    lumps[10] = b"".join(struct.pack("<10f4B", x, y, 0, x/320, y/320,
                                      (.5+x/320)/128, (.5+y/320)/128, 0, 0, 1, 255, 255, 255, 255)
                         for x, y in points)
    lumps[13] = struct.pack("<8i72x", 0, 0, 1, 0, 3, 0, 0, 0)
    lumps[14] = bytes((120, 60, 30)) * (128 * 128)
    bsp = bytearray(8 + 17 * 8)
    struct.pack_into("<4si", bsp, 0, b"IBSP", 46)
    for i, lump in enumerate(lumps):
        struct.pack_into("<ii", bsp, 8 + i*8, len(bsp), len(lump))
        bsp.extend(lump)
    with zipfile.ZipFile(root / "source.pk3", "w") as z:
        z.writestr("maps/test.bsp", bsp)
    text = """SECTION: MODELRESOURCE
MATERIALS 1
0: rock.mat

GEOSETS 1
MESHES 1
VERTICES 3
0: 0 0 0 0.5
1: 1 0 0 0.5
2: 0 1 0 0.5

TEXTURE VERTICES 3
0: 0 0
1: 2 0
2: 0 2

FACES 1
0: 0 0x1 4 3 3 0 3 0, 0 1, 1 2, 2

FACE NORMALS
0: 0 0 1
"""
    model = text.replace("\n", "\r\n").encode("ascii")
    (root / "episode.gob").write_bytes(struct.pack("<4sII", b"GOB ", 20, 12+len(model))
                                     + model + struct.pack("<III128s", 1, 12, len(model), b"3do\\rock.3do"))
    (root / "plan.json").write_text(json.dumps({"models": [
        {"name": "rock.3do", "position": [0, 0, 0], "sourceSurfaces": [0]}]}))
    im = Image.new("RGB", (2, 2), (128, 128, 128))
    im.save(root / "diffuse.png")
    Image.new("RGB", (2, 2), (4, 8, 16)).save(root / "glow.png")
    row = {"chosenStage": "diffuse.png", "mat": "rock.mat", "dimensions": [2, 2],
           "additiveStages": [{"image": "glow.png", "gain": 0.5}]}
    (root / "materials.json").write_text(json.dumps({"shaders": {"stone": row}}))


@unittest.skipUnless(all(importlib.util.find_spec(package) for package in ("PIL", "numpy", "scipy")),
                     "The optional RGB atlas authoring tool requires Pillow, NumPy and SciPy")
class QuakeLightmapAtlasTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        fixture(self.root)
        self.out = self.root / "new"

    def invoke(self, *opts):
        return subprocess.run([sys.executable, str(TOOL), "source.pk3", "plan.json",
                               "episode.gob", "materials.json", "new", "--member", "maps/test.bsp",
                               "--lightmap-shift", "0", "--display-gain", "1", *opts],
                              cwd=self.root, capture_output=True, text=True, timeout=45)

    def test_source_texel_times_rgb_light_plus_additive_stage(self):
        from PIL import Image
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt = json.loads((self.out / "receipt.json").read_text())
        self.assertEqual((receipt["pageCount"], receipt["mapped"], receipt["untouched"]), (1, 1, 0))
        self.assertEqual(receipt["pageSha256"]["ql000.png"],
                         hashlib.sha256((self.out / "rgb/ql000.png").read_bytes()).hexdigest())
        self.assertEqual(receipt["modelSha256"]["rock.3do"],
                         hashlib.sha256((self.out / "models/rock.3do").read_bytes()).hexdigest())
        with Image.open(self.out / "rgb/ql000.png") as image:
            self.assertEqual(image.getpixel((5, 5)), (62, 34, 23))
        text = (self.out / "models/rock.3do").read_text()
        self.assertIn("1: ql000.mat\n", text)
        self.assertIn("0: 1 0x1 4 0 3 0 3 0, 0 1, 1 2, 2", text)
        self.assertIn("FACE NORMALS\n0: 0 0 1\n", text)
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertEqual(receipt, json.loads((self.out / "receipt.json").read_text()))

    def test_mismatched_plan_fails_without_publication(self):
        (self.root / "plan.json").write_text(json.dumps({"models": [
            {"name": "rock.3do", "position": [0, 80, 0], "sourceSurfaces": [0]}]}))
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertFalse(self.out.exists())

    def test_transparency_is_never_flattened_to_opaque(self):
        p = self.root / "materials.json"
        art = json.loads(p.read_text())
        art["shaders"]["stone"]["cutout"] = True
        p.write_text(json.dumps(art))
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertFalse(self.out.exists())

    def test_model_name_cannot_escape_output_tree(self):
        (self.root / "plan.json").write_text(json.dumps({"models": [
            {"name": "../other.3do", "position": [0, 0, 0], "sourceSurfaces": [0]}]}))
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertFalse(self.out.exists())

    def test_plan_reference_to_missing_surface_is_rejected(self):
        path = self.root / "plan.json"
        plan = json.loads(path.read_text())
        plan["models"][0]["sourceSurfaces"].append(80)
        path.write_text(json.dumps(plan))
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("source surface", result.stderr)
        self.assertFalse(self.out.exists())

    def test_patch_budget_precedes_tessellation(self):
        sys.path.insert(0, str(TOOL.parent))
        self.addCleanup(sys.path.pop, 0)
        import quake_lightmap_atlas as atlas
        with zipfile.ZipFile(self.root / "source.pk3") as z:
            raw = bytearray(z.read("maps/test.bsp"))
        vertex, length = struct.unpack_from("<ii", raw, 8+10*8)
        vertices = raw[vertex:vertex+length] * 3
        struct.pack_into("<ii", raw, 8+10*8, len(raw), len(vertices))
        raw.extend(vertices)
        face = struct.pack("<8i64x2i", 0, 0, 2, 0, 9, 0, 0, 0, 3, 3)
        struct.pack_into("<ii", raw, 8+13*8, len(raw), len(face)*2)
        raw.extend(face*2)
        with mock.patch.object(atlas, "MAX_CHART_VERTICES", 17, create=True), \
                mock.patch.object(atlas.np, "einsum", wraps=atlas.np.einsum) as tessellate:
            with self.assertRaisesRegex(ValueError, "expanded vertex"):
                atlas.source_charts(raw, {"stone": {}}, 3, [{"sourceSurfaces": [0, 1]}])
            self.assertEqual(tessellate.call_count, 1)

    def test_extensionless_art_requires_a_unique_name(self):
        from PIL import Image
        Image.new("RGB", (2, 2), (255, 0, 0)).save(self.root / "diffuse.tga")
        p = self.root / "materials.json"
        materials = json.loads(p.read_text())
        materials["shaders"]["stone"]["chosenStage"] = "diffuse"
        p.write_text(json.dumps(materials))
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("extension", result.stderr)
        self.assertFalse(self.out.exists())


if __name__ == "__main__":
    unittest.main()
