"""Synthetic cloud-colour and cubemap edge checks; no game data."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from quake_cloud_sky import bake_faces

class CloudTests(unittest.TestCase):
    def test_additive_colour_and_undrawn_bottom(self):
        base = (2, 2, bytes([210, 5, 8]) * 4, 2, 2)
        glow = (2, 2, bytes([90, 40, 5]) * 4, 3, 2)
        faces = bake_faces([base, glow], height=384, size=8)
        self.assertEqual(set(faces), {'rt', 'lf', 'bk', 'ft', 'up', 'dn'})
        self.assertEqual(faces['up'], bytes([255, 45, 13]) * 64)
        self.assertEqual(faces['dn'], bytes(3 * 64))

    def test_asymmetric_art_agrees_on_adjacent_edges(self):
        data = bytes(range(4 * 4 * 3))
        faces = bake_faces([(4, 4, data, 2, 3)], height=384, size=9)
        def pixel(face, x, y):
            offset = 3 * (9 * y + x)
            return faces[face][offset:offset + 3]
        self.assertGreater(len(set(faces['rt'])), 5)
        for y in range(9):
            self.assertEqual(pixel('rt', 0, y), pixel('bk', 8, y))
        for x in range(9):
            self.assertEqual(pixel('rt', x, 0), pixel('up', x, 8))

    def test_invalid_geometry_and_image(self):
        layer = (1, 1, bytes(3), 1., 1.)
        for height, size in [(0, 8), (float("nan"), 8), (384, 1)]:
            with self.assertRaises(ValueError):
                bake_faces([layer], height, size)
        with self.assertRaises(ValueError):
            bake_faces([(2, 2, b'', 1., 1.)], 384, 8)

if __name__ == '__main__':
    unittest.main()
