import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import compare_pk3_maps as maps


class ComparePk3Maps(unittest.TestCase):
    def test_duplicate_bytes_and_different_maps_at_one_installed_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            packages = []
            for i, data in enumerate((b"older", b"older", b"redux")):
                path = root / f"version{i}.pk3"
                with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
                    z.writestr("Maps/Arena.bsp", data)
                    z.writestr("textures/arena/wall.jpg", b"ignore")
                packages.append(path)
            inspected = []
            def inspect(path):
                data = path.read_bytes()
                inspected.append(data)
                return {"sourceSha256": hashlib.sha256(data).hexdigest(), "worldSurfaces": len(data),
                        "entityClasses": {"worldspawn": 1, "info_player_deathmatch": 2, "trigger_once": 3}}
            digest = hashlib.sha256(b"redux").hexdigest()
            output = root / "selected.bsp"
            report = maps.compare(packages, inspect, digest, output)
            self.assertEqual(len(report["paths"]["maps/arena.bsp"]), 2)
            self.assertEqual(sorted(inspected), [b"older", b"redux"])
            self.assertEqual(len(report["variants"][hashlib.sha256(b"older").hexdigest()]["members"]), 2)
            self.assertEqual(report["variants"][digest]["playerStarts"], 2)
            self.assertEqual(report["variants"][digest]["triggers"], 3)
            self.assertEqual(report["selectedSource"], {"path": str(output.resolve()), "sha256": digest})
            self.assertEqual(output.read_bytes(), b"redux")
            with self.assertRaises((ValueError, FileExistsError)):
                maps.compare(packages, inspect, digest, output)
            missing = root / "missing.bsp"
            with self.assertRaises(ValueError):
                maps.compare(packages, inspect, "0" * 64, missing)
            self.assertFalse(missing.exists())


if __name__ == "__main__":
    unittest.main()
