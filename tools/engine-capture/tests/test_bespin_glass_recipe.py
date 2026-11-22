"""Synthetic Bespin template-inheritance, flag-edit and publication regressions."""
from contextlib import contextmanager
import hashlib
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
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

    def test_moving_architecture_override_with_zero_path_frames_is_rejected(self):
        for game in ("jk", "mots"):
            original = fixture(game)
            for prefix in (b"bp013.3do", b"13: bp013 bp013 1 2 3 0 0 0 0"):
                with self.subTest(game=game, owner=prefix):
                    changed = original.replace(prefix, prefix + b" move=physics vel=(1/0/0)", 1)
                    with self.assertRaisesRegex(ValueError, "architecture placement 13"):
                        self.recipe.repair_jkl(changed, game)

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

    def test_existing_symlink_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            link = root / "symlink"
            try:
                link.symlink_to(root / "absent", target_is_directory=True)
            except OSError as error:
                self.skipTest(f"Symlinks unavailable on this filesystem: {error}")
            with self.assertRaises(FileExistsError):
                self.recipe.build(root / "missing.gob", link)
            self.assertTrue(link.is_symlink())

    @contextmanager
    def synthetic_build(self):
        archive = packed(fixture("jk"))
        with tempfile.TemporaryDirectory() as tmp, patch.dict(self.recipe.PARENTS, {digest(archive): "jk"}), \
                patch.dict(self.recipe.OUTPUT_DIGESTS, {"jk": digest(fixture("jk", repaired=True))}):
            root = Path(tmp)
            parent = root / "source.gob"
            parent.write_bytes(archive)
            yield parent, root / "overlay", archive

    def test_competing_empty_directory_survives_publication(self):
        repair = self.recipe.repair_jkl
        with self.synthetic_build() as (parent, output, archive):
            owner = None

            def compete(source, game):
                nonlocal owner
                output.mkdir()
                owner = output.stat()
                return repair(source, game)

            with patch.object(self.recipe, "repair_jkl", side_effect=compete), self.assertRaises(FileExistsError):
                self.recipe.build(parent, output)
            self.assertTrue(os.path.samestat(owner, output.stat()))
            self.assertEqual(list(output.iterdir()), [])
            self.assertEqual(parent.read_bytes(), archive)
            self.assertEqual(set(parent.parent.iterdir()), {parent, output})

    def test_only_authenticated_bytes_reach_the_parser_after_parent_changes(self):
        real_index = self.recipe.archive_index
        original = fixture("jk")
        with self.synthetic_build() as (parent, output, archive):
            def change_before_index(stream):
                with parent.open("r+b") as writer:
                    writer.seek(12)
                    writer.write(b"changed")
                return real_index(stream)

            with patch.object(self.recipe, "archive_index", side_effect=change_before_index), \
                    patch.object(self.recipe, "repair_jkl", wraps=self.recipe.repair_jkl) as repair, \
                    self.assertRaisesRegex(ValueError, "(?i)parent changed"):
                self.recipe.build(parent, output)
            repair.assert_called_once_with(original, "jk")
            self.assertNotEqual(parent.read_bytes(), archive)
            self.assertEqual(set(parent.parent.iterdir()), {parent})

    def test_parent_size_limit_at_admission_and_final_verification(self):
        for grow_during_staging in (False, True):
            with self.subTest(grow_during_staging=grow_during_staging), \
                    self.synthetic_build() as (parent, output, archive):
                repair = self.recipe.repair_jkl

                def grow(source, game):
                    parent.write_bytes(archive + b"X")
                    return repair(source, game)

                if not grow_during_staging:
                    parent.write_bytes(archive + b"X")
                # The boundary is expressed in bytes so the fixture can stay small.
                with patch.object(self.recipe, "MAX_ARCHIVE", len(archive), create=True), \
                        patch.object(self.recipe, "repair_jkl", side_effect=grow), \
                        self.assertRaisesRegex(ValueError, "size limit"):
                    self.recipe.build(parent, output)
                self.assertEqual(set(parent.parent.iterdir()), {parent})

    def test_device_parent_is_refused_before_hashing(self):
        with tempfile.TemporaryDirectory() as tmp, self.assertRaisesRegex(ValueError, "regular"):
            self.recipe.build(Path(os.devnull), Path(tmp) / "overlay")

    def test_fifo_is_refused_without_blocking_at_admission_and_recheck(self):
        if not hasattr(os, "mkfifo"):
            return  # The device test also runs on native Windows.
        # A child-local audit hook catches blocking opens before the OS can wait.
        probe = """
import os, runpy, stat, sys
suite = runpy.run_path(sys.argv[1])['BespinGlass']
suite.setUpClass()
def guard(event, args):
    if event == 'open' and isinstance(args[0], str) and args[0].endswith('source.gob'):
        if os.path.exists(args[0]) and stat.S_ISFIFO(os.stat(args[0]).st_mode) and not args[2] & os.O_NONBLOCK:
            raise AssertionError('blocking FIFO open')
sys.addaudithook(guard)
case = suite()
for replace in (False, True):
    with case.synthetic_build() as (parent, output, archive):
        def replace_with_fifo():
            parent.unlink()
            os.mkfifo(parent)
        repair = case.recipe.repair_jkl
        def change(source, game):
            replace_with_fifo()
            return repair(source, game)
        if not replace:
            replace_with_fifo()
        from unittest.mock import patch
        with patch.object(case.recipe, 'repair_jkl', side_effect=change), case.assertRaisesRegex(ValueError, 'regular'):
            case.recipe.build(parent, output)
        case.assertEqual(set(parent.parent.iterdir()), {parent})
"""
        result = subprocess.run([sys.executable, "-B", "-c", probe, str(Path(__file__).resolve())],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_output_hash_gate_leaves_no_overlay(self):
        archive = packed(fixture("mots"))
        with tempfile.TemporaryDirectory() as tmp, patch.dict(self.recipe.PARENTS, {digest(archive): "mots"}):
            root = Path(tmp)
            parent = root / "synthetic.goo"
            parent.write_bytes(archive)
            with self.assertRaisesRegex(ValueError, "JKL"):
                self.recipe.build(parent, root / "new")
            self.assertEqual(set(root.iterdir()), {parent})
