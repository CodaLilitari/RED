"""Synthetic inputs for pairing opaque baked materials with existing episode resources."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from quake_lightmap_package import append_pages, verify_files, check_model_materials


class AtlasPackagingTests(unittest.TestCase):
    def setUp(self):
        self.jkl = (b"SECTION: MATERIALS\r\nWORLD MATERIALS 5  # capacity\r\n"
                    b"0: brick.mat 1 1\r\n# keep placement\r\nEND\r\nSECTION: SOUNDS\r\nEND\r\n")

    def test_adds_pages_preserving_comments_order_and_reserve(self):
        expected = self.jkl.replace(b" 5 ", b" 6 ").replace(
            b"# keep placement\r\nEND", b"# keep placement\r\n1: bake000.mat 1.000000 1.000000\r\nEND")
        self.assertEqual(append_pages(self.jkl, ["bake000.mat"]), expected)

    def test_refuses_conflicting_material_or_ambiguous_table(self):
        for raw, pages in [(self.jkl, ["brick.mat"]), (self.jkl + self.jkl, ["a.mat"]),
                           (self.jkl.replace(b"0: brick", b"2: brick"), ["a.mat"])]:
            with self.subTest(raw=raw, pages=pages), self.assertRaises(ValueError):
                append_pages(raw, pages)

    def test_model_referencing_missing_page_refuses_even_in_both_editions(self):
        parent = b"MATERIALS 1\n0: brick.mat\n"
        model = b"MATERIALS 2\n0: brick.mat\n1: ql000.mat\n"
        with self.assertRaisesRegex(ValueError, "ql000.mat"):
            check_model_materials(parent, model, set())
        check_model_materials(parent, model, {"ql000.mat"})

    def test_receipt_never_resolves_path_outside_atlas(self):
        with self.assertRaises(ValueError):
            verify_files(Path("."), {"../escape.3do": "0" * 64}, ".3do", Path("."))


if __name__ == "__main__":
    unittest.main()
