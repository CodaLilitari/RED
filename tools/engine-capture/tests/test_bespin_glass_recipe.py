"""Synthetic Bespin template-inheritance, flag-edit and publication regressions."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / "recipes/bespin_glass/build.py"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def fixture(game, repaired=False, extra_thing=""):
    # Deliberately repeat flag-looking text in comments and outside the tables.
    rows = ["SECTION: TEMPLATES", "World templates 500", "_decor none type=cog move=path collide=1",
            "_structure    _decor    collide=3 thingflags=0x" + ("0" if repaired else "8"),
            "_walkstruct _structure thingflags=0x48",
            "_item _structure thingflags=0x88",
            "left _structure model3d=glass2.3do", "right _structure model3d=glass3.3do",
            "ignored _item model3d=sample.3do"]
    for i in range(219):
        rows.append(f"bp{i:03} _walkstruct model3d=bp{i:03}.3do")
    rows += ["end", "SECTION: THINGS", "World things 543"]
    for i in range(219):
        flag = (" thingflags=0x4000048" if repaired else " thingflags=0x4000040") if game == "jk" else ""
        rows.append(f"{i}: bp{i:03} bp{i:03} 1 2 3 0 0 0 0{flag}  # untouched thingflags=0x4000040")
    rows += ["269: left pane-a 0 0 0 0 0 0 0", "270: right pane-b 0 0 0 0 0 0 0",
             "271: ignored other 2 3 4 0 0 0 0"]
    if extra_thing:
        rows.append(extra_thing)
    rows += ["end", "# thingflags=0x4000040", "0: bp000 bp000 outside the world things table"]
    return ("\r\n".join(rows) + "\r\n").encode("ascii")


def packed(jkl):
    name = b"jkl\\bespin.jkl"
    return struct.pack("<4sII", b"GOB ", 20, 12 + len(jkl)) + jkl + struct.pack("<III128s", 1, 12, len(jkl), name)


class BespinGlass(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("tested_bespin_glass", SCRIPT)
        cls.recipe = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.recipe)

    def test_jk_edits_only_selected_flag_values_within_their_tables(self):
        self.assertEqual(self.recipe.repair_jkl(fixture("jk"), "jk"), fixture("jk", repaired=True))

    def test_mots_inherits_early_architecture_and_edits_one_byte(self):
        original = fixture("mots")
        result = self.recipe.repair_jkl(original, "mots")
        self.assertEqual(result, fixture("mots", repaired=True))
        self.assertEqual(sum(a != b for a, b in zip(original, result)), 1)

    def test_extra_placed_descendant_of_glass_parent_is_rejected(self):
        with self.assertRaises(ValueError):
            self.recipe.repair_jkl(fixture("jk", extra_thing="272: left extra 0 0 0 0 0 0 0"), "jk")

    def test_moving_architecture_owner_is_rejected(self):
        original = fixture("jk").replace(b"bp013.3do\r\n", b"bp013.3do numframes=2\r\n")
        with self.assertRaises(ValueError):
            self.recipe.repair_jkl(original, "jk")

    def test_unrelated_inheritor_without_model_is_still_detected(self):
        original = fixture("mots", extra_thing="272: _structure ghost 0 0 0 0 0 0 0")
        with self.assertRaises(ValueError):
            self.recipe.repair_jkl(original, "mots")

    def test_parent_gate_precedes_archive_parser_and_leaves_no_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            parent = root / "unknown.gob"
            parent.write_bytes(b"malformed unapproved archive")
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                self.recipe.build(parent, root / "fresh")
            self.assertFalse((root / "fresh").exists())

    def test_known_synthetic_parent_creates_only_the_verified_member(self):
        original = fixture("jk")
        expected = fixture("jk", repaired=True)
        archive = packed(original)
        with tempfile.TemporaryDirectory() as tmp, patch.dict(self.recipe.PARENTS, {digest(archive): "jk"}), \
                patch.dict(self.recipe.OUTPUT_DIGESTS, {"jk": digest(expected)}):
            root = Path(tmp)
            parent, output = root / "synthetic.gob", root / "overlay"
            parent.write_bytes(archive)
            self.recipe.build(parent, output)
            self.assertEqual([f.relative_to(output).as_posix() for f in output.rglob("*") if f.is_file()], ["jkl/bespin.jkl"])
            self.assertEqual((output / "jkl/bespin.jkl").read_bytes(), expected)
            self.assertEqual(parent.read_bytes(), archive)
            with self.assertRaises(FileExistsError):
                self.recipe.build(parent, output)
            self.assertEqual((output / "jkl/bespin.jkl").read_bytes(), expected)
            link = root / "symlink"
            link.symlink_to(output, target_is_directory=True)
            with self.assertRaises(FileExistsError):
                self.recipe.build(parent, link)

    def test_output_hash_gate_leaves_no_overlay(self):
        archive = packed(fixture("mots"))
        with tempfile.TemporaryDirectory() as tmp, patch.dict(self.recipe.PARENTS, {digest(archive): "mots"}):
            root = Path(tmp)
            parent = root / "synthetic.goo"
            parent.write_bytes(archive)
            with self.assertRaisesRegex(ValueError, "JKL"):
                self.recipe.build(parent, root / "new")
            self.assertEqual(set(root.iterdir()), {parent})
