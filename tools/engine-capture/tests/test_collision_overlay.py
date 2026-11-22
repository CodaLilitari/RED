"""The native-face layer requires a receipt that declares its coordinate basis."""

from pathlib import Path
from types import SimpleNamespace
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import collision_overlay


class CollisionBasisTests(unittest.TestCase):
    def render(self, basis, second_basis=None):
        triangle = [[0, 0, 0], [1, 0, 0], [0, 1, 0]]
        model = {"shape": "faces", "faces": [{"polygon": triangle}], "nextOffset": None}
        if basis is not None:
            model["geometryBasis"] = basis
        target = {
            "archiveSha256": "a" * 64,
            "levelSha256": "b" * 64,
            "native": {
                "surfaces": [{"polygon": triangle, "boundary": "sectorExterior"}],
                "nextOffset": None,
            },
            "model": model,
        }
        source = {"sourceSha256": "c" * 64, "collisionBrush": {
            "sides": [], "nextOffset": None, "shaderName": "synthetic_clip",
            "solid": False, "water": False, "playerClip": True, "shotClip": False,
            "noShot": False, "hasLinkedDrawSurface": False,
        }}
        args = SimpleNamespace(
            agent=Path("red-agent"), source=Path("synthetic.bsp"), archive=Path("synthetic.gob"),
            level="jkl/synthetic.jkl", sector=0, thing=0, brush=0, axis="z", depth=0,
            scale=1, player=None, projectile=None, projectile_origin=None, decision=None,
        )
        pages = [target]
        if second_basis is not None:
            model["nextOffset"] = 1
            pages.append({**target, "model": {
                "shape": "faces", "faces": [{"polygon": triangle}], "nextOffset": None,
                "geometryBasis": second_basis,
            }})
        with patch.object(collision_overlay, "capture", side_effect=[*pages, source]):
            return collision_overlay.render(args)

    def test_legacy_receipt_cannot_claim_native_faces(self):
        with self.assertRaisesRegex(ValueError, "rawGeoset0"):
            self.render(None)

    def test_visual_geometry_cannot_claim_native_faces(self):
        with self.assertRaisesRegex(ValueError, "rawGeoset0"):
            self.render("hierarchyRestPose")

    def test_native_receipt_draws_the_model_layer(self):
        html = self.render("rawGeoset0")
        self.assertIn('class="model"><polyline', html)
        self.assertIn("Native model faces (geoset 0)", html)
        self.assertIn("Model polygon basis: rawGeoset0", html)

    def test_later_model_pages_require_native_coordinates(self):
        with self.assertRaisesRegex(ValueError, "rawGeoset0"):
            self.render("rawGeoset0", "hierarchyRestPose")


if __name__ == "__main__":
    unittest.main()
