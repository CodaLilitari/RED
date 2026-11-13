"""Synthetic SDK fixtures: no downloaded Qt or proprietary assets required."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "stage_notices.py"


class ReleaseNoticesTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="red-release-notices-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.qt = self.root / "Qt"
        self.kit = self.qt / "6.8.3" / "kit"
        self.package = self.root / "RED"
        self.package.mkdir()
        (self.package / "red-editor").write_bytes(b"abc")
        self.licenses = self.qt / "6.8.3" / "Src" / "LICENSES"
        self.licenses.mkdir(parents=True)
        for name in ("GPL-2.0-only.txt", "GPL-3.0-only.txt", "LGPL-3.0-only.txt"):
            (self.licenses / name).write_text("Synthetic license fixture\n", encoding="utf-8")
        for module in ("qtcore", "qtgui", "qtnetwork", "qtmultimedia", "qtsvg"):
            directory = self.qt / "Docs" / "Qt-6.8.3" / module
            directory.mkdir(parents=True)
            (directory / (module + "-attribution-synthetic.html")).write_bytes(b"<html>fixture</html>")
        (self.kit / "sbom").mkdir(parents=True)
        for module in ("qtbase", "qtmultimedia", "qtsvg"):
            (self.kit / "sbom" / (module + "-6.8.3.spdx.json")).write_text(
                json.dumps({"spdxVersion": "SPDX-2.3", "packages": [{"name": module}]}), encoding="utf-8")

    def run_script(self, command, *extra):
        return subprocess.run(
            [sys.executable, "-B", str(SCRIPT), command, "--package", str(self.package),
             "--qt-version", "6.8.3", *extra],
            capture_output=True, text=True, timeout=5)

    def stage(self):
        return self.run_script("stage", "--qt-kit", str(self.kit))

    def manifest(self):
        return self.run_script("manifest", "--version", "0.6.0", "--platform", "linux-x86_64",
                               "--commit", "a" * 40)

    def test_copies_exact_notice_bytes_and_hashes_package(self):
        result = self.stage()
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.manifest()
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads((self.package / "RELEASE_MANIFEST.json").read_text())
        self.assertEqual(manifest["qtVersion"], "6.8.3")
        self.assertEqual(manifest["sourceCommit"], "a" * 40)
        files = {entry["path"]: entry for entry in manifest["files"]}
        self.assertEqual(files["red-editor"]["sha256"],
                         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
        self.assertEqual(files["red-editor"]["size"], 3)
        notice = "licenses/qt/6.8.3/LICENSES/LGPL-3.0-only.txt"
        self.assertEqual((self.package / notice).read_bytes(), (self.licenses / "LGPL-3.0-only.txt").read_bytes())
        self.assertEqual(len(files), 12)
        self.assertNotIn(str(self.root), json.dumps(manifest))
        self.assertNotIn("RELEASE_MANIFEST.json", files)

    def test_missing_sbom_refuses_before_creating_partial_notices(self):
        (self.kit / "sbom" / "qtsvg-6.8.3.spdx.json").unlink()
        result = self.stage()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("qtsvg-6.8.3.spdx.json", result.stderr)
        self.assertFalse((self.package / "licenses").exists())

    def test_empty_license_refuses_before_staging(self):
        (self.licenses / "LGPL-3.0-only.txt").write_bytes(b"")
        result = self.stage()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("empty", result.stderr)
        self.assertFalse((self.package / "licenses").exists())

    def test_missing_attribution_module_refuses_before_staging(self):
        path = self.qt / "Docs/Qt-6.8.3/qtgui/qtgui-attribution-synthetic.html"
        path.unlink()
        result = self.stage()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("qtgui", result.stderr)
        self.assertFalse((self.package / "licenses").exists())

    def test_invalid_spdx_refuses_before_staging(self):
        (self.kit / "sbom" / "qtsvg-6.8.3.spdx.json").write_text("{}")
        result = self.stage()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SPDX", result.stderr)
        self.assertFalse((self.package / "licenses").exists())

    def test_manifest_requires_staged_notices(self):
        result = self.manifest()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Release package error: required Qt notices", result.stderr)
        self.assertFalse((self.package / "RELEASE_MANIFEST.json").exists())

    def test_external_symlink_cannot_leak_files_into_manifest(self):
        self.assertEqual(self.stage().returncode, 0)
        outside = self.root / "private-file"
        outside.write_bytes(b"unrelated bytes")
        (self.package / "escape").symlink_to(outside)
        result = self.manifest()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside", result.stderr)
        self.assertFalse((self.package / "RELEASE_MANIFEST.json").exists())

    def test_internal_framework_symlink_is_recorded_without_following_directory_twice(self):
        self.assertEqual(self.stage().returncode, 0)
        (self.package / "framework").mkdir()
        (self.package / "framework" / "Current").symlink_to("../red-editor")
        result = self.manifest()
        self.assertEqual(result.returncode, 0, result.stderr)
        entries = json.loads((self.package / "RELEASE_MANIFEST.json").read_text())["files"]
        link = next(entry for entry in entries if entry["path"] == "framework/Current")
        self.assertEqual(link, {"path": "framework/Current", "type": "symlink", "target": "../red-editor"})


if __name__ == "__main__":
    unittest.main()
