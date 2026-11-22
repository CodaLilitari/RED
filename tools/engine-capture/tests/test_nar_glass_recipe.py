"""Synthetic glass geometry and create-only recipe contracts. No game assets."""
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


RECIPE = Path(__file__).resolve().parents[1] / "recipes" / "nar_glass" / "build.py"


class NarGlassRecipeTests(unittest.TestCase):
    def recipe(self):
        self.assertTrue(RECIPE.is_file(), "hash-pinned Nar recipe is missing")
        spec = importlib.util.spec_from_file_location("nar_glass_recipe_under_test", RECIPE)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_wrong_parent_rejected_before_gob_parsing_and_output(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            archive = root / "unrelated.gob"
            archive.write_bytes(b"not a GOB")
            result = subprocess.run([sys.executable, "-B", str(RECIPE), "--parent", str(archive),
                                     "--overlay", str(root / "new")], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("parent SHA-256", result.stderr)
            self.assertFalse((root / "new").exists())

    def test_existing_output_directory_is_never_used_even_if_empty(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            output = root / "existing"
            output.mkdir()
            result = subprocess.run([sys.executable, "-B", str(RECIPE), "--parent", str(root / "input"),
                                     "--overlay", str(output)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("destination already exists", result.stderr)
            self.assertEqual(list(output.iterdir()), [])

    def test_alpha_key_is_disabled_and_visible_straight_rims_fade_at_distance(self):
        recipe = self.recipe()
        mat = recipe.glass_material()
        self.assertEqual(struct.unpack_from("<6I", mat, 116), (256, 256, 0, 0, 0, 4))
        self.assertEqual(struct.unpack_from("<2I", mat, 20), (1, 16))
        offset, peaks = 140, []
        for level in range(4):
            width = 256 >> level
            tile = width // 2
            scale = 1 << level
            def pixel(x, y):
                return struct.unpack_from("<H", mat, offset + 2 * (y * width + x))[0]
            base = pixel(0, 0)
            self.assertEqual(base, (3 << 11) | (8 << 5) | 5)
            self.assertEqual(pixel(tile + 50 // scale, 50 // scale), base)
            self.assertEqual(pixel(tile + 63 // scale, 64 // scale), base)
            peak = pixel(tile + 47 // scale, 8 // scale)
            self.assertGreater(peak, base)
            peaks.append(peak)
            offset += width * width * 2
        self.assertEqual(len(mat), offset)
        self.assertEqual(peaks, sorted(peaks, reverse=True))

    def test_split_keeps_hidden_collision_and_preserves_corner_attributes(self):
        self.recipe()
        from nar_glass.model import extract_mesh
        # Corner slots intentionally differ from vertex ordinals. A face with
        # the glass slot but geometry mode 0 represents invisible collision.
        mesh = """MESH 0
NAME test_panel
RADIUS 2
GEOMETRYMODE 4
LIGHTINGMODE 3
TEXTUREMODE 1
VERTICES 4
0: 0 0 0 .25
1: 1 0 0 .5
2: 1 1 0 .75
3: 0 1 0 1
TEXTURE VERTICES 4
0: 0 0
1: 2 0
2: 2 2
3: 0 2
VERTEX NORMALS
0: 0 0 1
1: 0 1 0
2: 1 0 0
3: 0 0 1
FACES 3
0: 1 0x2 4 3 1 0 3 3, 2 0, 3 2, 1
1: 0 0x0 4 3 1 0 3 0, 0 1, 1 2, 2
2: 1 0x0 0 3 1 0 3 3, 2 0, 3 2, 1
FACE NORMALS
0: 0 0 1
1: 0 0 1
2: 0 0 -1
"""
        donor, glass = extract_mesh(mesh, {1}, 0)
        self.assertIn("1: 1 0x0 0 3 1 0 3 3, 2 0, 3 2, 1", donor)
        self.assertIn("0: 0 0x0 4 3 1 0 3 0, 0 1, 1 2, 2", donor)
        self.assertIn("2: 0 1 0 1", glass)
        self.assertIn("0: 0 0x2 4 3 1 0 3 2, 1 0, 2 1, 0", glass)
        self.assertIn("TEXTURE VERTICES 3\n0: 2 0\n1: 2 2\n2: 0 2", glass)

    def test_reordering_moves_only_declared_ids_without_rewriting_placement_payloads(self):
        recipe = self.recipe()
        rows = ["0: wall w 0 0 0 0 0 0 0", "2: pane p 1 2 3 0 0 0 0", "5: lift l 0 0 0 0 0 0 0"]
        self.assertEqual(recipe.order_panes(rows, {2}), [rows[1], rows[0], rows[2]])
        with self.assertRaises(ValueError):
            recipe.order_panes(rows, {3})

    def test_modified_manifest_cannot_admit_an_unapproved_parent_or_missing_member(self):
        recipe = self.recipe()
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / "recipe.json"
            source = json.loads((RECIPE.parent / "recipe.json").read_text())
            for edited in ({**source, "schemaVersion": 2}, {**source, "parentSha256": "0" * 64},
                           {**source, "overlay": {}}, {**source, "extra": True}):
                with self.subTest(fields=edited.keys()):
                    path.write_text(json.dumps(edited))
                    with self.assertRaisesRegex(ValueError, "recipe manifest"):
                        recipe.load_recipe(path)

    def test_parent_replaced_during_staging_blocks_publication(self):
        recipe = self.recipe()
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / "source"
            path.write_bytes(b"original")
            expected = hashlib.sha256(path.read_bytes()).hexdigest()
            replacement = Path(scratch) / "replacement"
            replacement.write_bytes(b"tampered")
            replacement.replace(path)
            with self.assertRaisesRegex(ValueError, "parent changed"):
                recipe.check_parent(path, expected)

    def test_incorrect_disk_readback_refused_before_overlay_publication(self):
        recipe = self.recipe()
        with tempfile.TemporaryDirectory() as scratch:
            stage = Path(scratch)
            (stage / "item").write_bytes(b"bad")
            with self.assertRaisesRegex(ValueError, "disk readback"):
                recipe.check_overlay(stage, {"item": hashlib.sha256(b"expected").hexdigest()})

    def test_existing_dangling_symlink_is_refused(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            out = root / "target"
            try:
                out.symlink_to(root / "missing")
            except OSError as error:
                self.skipTest(f"Symlinks unavailable on this filesystem: {error}")
            result = subprocess.run([sys.executable, "-B", str(RECIPE), "--parent", str(root / "parent"),
                                     "--overlay", str(out)], capture_output=True, text=True)
            self.assertIn("destination already exists", result.stderr)
            self.assertTrue(out.is_symlink())

    def test_red_failure_and_wrong_output_remove_stage_and_publish_nothing(self):
        recipe = self.recipe()
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "source"
            source.write_bytes(b"untouched")
            red = root / "red"
            spec = {"archiveMembers": 1, "unchangedMembers": 0,
                    "archiveSha256": "0" * 64, "parentSha256": hashlib.sha256(b"untouched").hexdigest()}
            for command_fails in (True, False):
                def red_boundary(command, **kwargs):
                    if command[1] == "validate":
                        return subprocess.CompletedProcess(command, 0, stdout='{"valid":true}')
                    self.assertEqual(command[1], "archive-create")
                    if command_fails:
                        raise subprocess.CalledProcessError(1, command)
                    Path(command[-1]).write_bytes(b"incorrect archive")
                    return subprocess.CompletedProcess(command, 0)
                with self.subTest(command_fails=command_fails), patch.object(recipe.subprocess, "run", red_boundary):
                    with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                        recipe.pack_archive(root / "new.gob", red, source, b"", {},
                                            {"jkl/nsstreets.jkl": b"synthetic"}, spec)
                    self.assertEqual(set(root.iterdir()), {source})
                    self.assertEqual(source.read_bytes(), b"untouched")

    def test_partial_link_failure_rolls_back_only_the_files_this_publisher_linked(self):
        recipe = self.recipe()
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            stage = root / "stage"
            (stage / "group").mkdir(parents=True)
            (stage / "group/first").write_bytes(b"staged")
            with self.assertRaises(FileNotFoundError):
                recipe.publish_overlay(root / "result", stage, ["group/first", "group/missing"])
            self.assertFalse((root / "result").exists())
            self.assertEqual((stage / "group/first").read_bytes(), b"staged")

    def test_rollback_preserves_replaced_file_symlink_and_directory(self):
        recipe = self.recipe()
        real_link = os.link
        for replacement in ("file", "symlink", "directory"):
            with self.subTest(replacement=replacement), tempfile.TemporaryDirectory() as scratch:
                root = Path(scratch)
                stage = root / "stage"
                (stage / "group").mkdir(parents=True)
                (stage / "group/first").write_bytes(b"staged")
                (stage / "group/second").write_bytes(b"second")
                out = root / "result"
                first = out / "group/first"
                if replacement == "symlink":
                    probe = root / "probe"
                    try:
                        probe.symlink_to(stage / "group/first")
                    except OSError as error:
                        self.skipTest(f"Symlinks unavailable on this filesystem: {error}")
                    probe.unlink()

                def replace_then_fail(source, target):
                    if target == first:
                        return real_link(source, target)
                    if replacement == "directory":
                        first.parent.rename(root / "moved-group")
                        first.parent.mkdir()
                    else:
                        first.unlink()
                        if replacement == "file":
                            first.write_bytes(b"another writer")
                        else:
                            first.symlink_to(stage / "group/first")
                    raise OSError("second link failed")

                with patch.object(recipe.os, "link", replace_then_fail):
                    with self.assertRaisesRegex(OSError, "second link failed"):
                        recipe.publish_overlay(out, stage, ["group/first", "group/second"])
                if replacement == "directory":
                    self.assertTrue(first.parent.is_dir())
                elif replacement == "symlink":
                    self.assertTrue(first.is_symlink())
                else:
                    self.assertEqual(first.read_bytes(), b"another writer")
