"""Synthetic glass geometry and create-only recipe contracts. No game assets."""
from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
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

    def run_synthetic_overlay(self, root, after_readback):
        recipe = self.recipe()
        parent = root / "source.gob"
        # One-byte payload followed by a valid one-entry GOB directory.
        data = struct.pack("<4sII", b"GOB ", 20, 13) + b"X" + struct.pack("<III128s", 1, 12, 1, b"item")
        parent.write_bytes(data)
        output = root / "result"
        overlay = {"3do/pane.3do": b"model", "mat/glass.mat": b"material"}
        spec = {"parentSha256": hashlib.sha256(data).hexdigest(),
                "overlay": {name: hashlib.sha256(payload).hexdigest() for name, payload in overlay.items()}}
        check_overlay = recipe.check_overlay

        def inspect_stage(stage, expected):
            check_overlay(stage, expected)
            after_readback(stage, output)

        # Substitute just the proprietary recipe inputs and generated resources.
        # Exercise the normal CLI from admission through real staging, publication and cleanup.
        with patch.object(recipe, "load_recipe", return_value=spec), \
                patch.object(recipe, "create_overlay", return_value=overlay), \
                patch.object(recipe, "check_overlay", side_effect=inspect_stage), \
                patch.object(sys, "argv", [str(RECIPE), "--parent", str(parent), "--overlay", str(output)]), \
                redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            recipe.main()
        return output

    def test_publishes_entire_verified_overlay_directory_in_one_step(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            verified_directory = None

            def observe(stage, output):
                nonlocal verified_directory
                self.assertFalse(output.exists())
                self.assertEqual((stage / "3do/pane.3do").read_bytes(), b"model")
                self.assertEqual((stage / "mat/glass.mat").read_bytes(), b"material")
                verified_directory = stage.stat()

            output = self.run_synthetic_overlay(root, observe)
            self.assertTrue(os.path.samestat(verified_directory, output.stat()),
                            "the complete verified directory must become the destination atomically")
            self.assertEqual((output / "3do/pane.3do").read_bytes(), b"model")
            self.assertEqual((output / "mat/glass.mat").read_bytes(), b"material")
            self.assertEqual(set(root.iterdir()), {root / "source.gob", output})

    def test_preserves_every_competing_destination_entry_at_publication(self):
        kinds = ["file", "empty directory", "nonempty directory"]
        if os.name == "posix":
            kinds.append("symlink")
        for kind in kinds:
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as scratch:
                root = Path(scratch)
                output = root / "result"
                identity = None

                def compete(stage, target):
                    nonlocal identity
                    if kind == "file":
                        target.write_bytes(b"competing file")
                    elif kind == "symlink":
                        target.symlink_to(root / "absent")
                    else:
                        target.mkdir()
                        if kind == "nonempty directory":
                            (target / "owned").write_bytes(b"competing contents")
                    identity = target.lstat()

                with self.assertRaises(SystemExit) as refusal:
                    self.run_synthetic_overlay(root, compete)
                self.assertEqual(refusal.exception.code, 1)
                self.assertTrue(os.path.samestat(identity, output.lstat()))
                if kind == "file":
                    self.assertEqual(output.read_bytes(), b"competing file")
                elif kind == "symlink":
                    self.assertEqual(os.readlink(output), str(root / "absent"))
                elif kind == "empty directory":
                    self.assertEqual(list(output.iterdir()), [])
                else:
                    self.assertEqual((output / "owned").read_bytes(), b"competing contents")
                    self.assertEqual(list(output.iterdir()), [output / "owned"])
                self.assertEqual(set(root.iterdir()), {root / "source.gob", output})

    def test_overlay_fails_closed_without_atomic_directory_publication(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            with patch.object(sys, "platform", "unsupported-platform"), self.assertRaises(SystemExit) as refused:
                self.run_synthetic_overlay(root, lambda stage, target: None)
            self.assertEqual(refused.exception.code, 1)
            self.assertEqual(set(root.iterdir()), {root / "source.gob"})

    def test_oversized_parent_is_refused_by_size_at_admission_and_recheck(self):
        recipe = self.recipe()
        with tempfile.TemporaryDirectory() as scratch:
            parent = Path(scratch) / "source.gob"
            parent.write_bytes(b"initial")
            expected = hashlib.sha256(b"initial").hexdigest()
            recipe.check_parent(parent, expected)
            with parent.open("wb") as replacement:
                replacement.truncate(53_592_642)
            # Even its correct digest cannot admit this oversized replacement.
            expected = "1d4fc1448f0e79dd5f17e6749d53de30b65dec57a32c640fce203fda79da3629"
            for check in (recipe.capture_parent, recipe.check_parent):
                with self.subTest(check=check.__name__), self.assertRaisesRegex(ValueError, "size|limit"):
                    check(parent, expected)

    def test_nonregular_parent_is_refused_at_admission_and_recheck(self):
        recipe = self.recipe()
        for check in (recipe.capture_parent, recipe.check_parent):
            with self.subTest(check=check.__name__), self.assertRaisesRegex(ValueError, "regular"):
                check(Path(os.devnull), hashlib.sha256(b"").hexdigest())
        if not hasattr(os, "mkfifo"):
            return  # Native Windows has no filesystem FIFOs; its device check runs above.

        with tempfile.TemporaryDirectory() as scratch:
            fifo = Path(scratch) / "source.gob"
            os.mkfifo(fifo)
            # A child-local audit guard rejects a blocking open before it can hang the test.
            # Both real calls must reject the unopened-for-writing FIFO as a nonregular file.
            probe = """
import os, runpy, sys
recipe, path = sys.argv[1:]
functions = runpy.run_path(recipe)
def guard(event, args):
    if event == 'open' and args[0] == path and not args[2] & os.O_NONBLOCK:
        raise AssertionError("blocking FIFO open")
sys.addaudithook(guard)
for check in ('capture_parent', 'check_parent'):
    try:
        functions[check](functions['Path'](path), '0' * 64)
    except ValueError as error:
        if 'regular' in str(error):
            continue
        raise
    raise AssertionError("accepted a FIFO")
"""
            result = subprocess.run([sys.executable, "-B", "-c", probe, str(RECIPE), str(fifo)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
