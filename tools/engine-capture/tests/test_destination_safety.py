"""Portable staging admission; all files and links are synthetic and test-owned."""

import copy
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from test_invoke_engine_capture import runner, request_document


class DestinationSafetyTests(unittest.TestCase):
    def test_portable_relative_paths_are_preserved_exactly(self):
        for value in ("registry.json", "resource/3do/walk-01.3do",
                      "resource/mat/Stone texture.MAT", "resource/.notes/data"):
            with self.subTest(value=value):
                self.assertEqual(runner.safe_destination(value), value)

    def test_runner_namespace_does_not_reserve_unrelated_nested_paths(self):
        self.assertEqual(runner.safe_destination("resource/session/data"), "resource/session/data")
        with tempfile.TemporaryDirectory() as temporary:
            document = request_document(Path(temporary))
            document["episode"] = "session"
            for item in document["files"]:
                item["destination"] = item["destination"].replace("episode/test/", "episode/session/")
            self.assertEqual(runner.validate_request(document)["episode"], "session")

    def test_unsafe_raw_components_are_refused_without_normalization(self):
        values = ("C:outside.txt", "C:/outside.txt", "resource/name:stream",
                  "/absolute", "//server/share", "resource\\outside",
                  "resource/../outside", "resource/./image", "resource//image",
                  "resource/", ".", "", "registry.json.", "registry.json ",
                  "resource/dir./file", "resource/dir /file", "bad\0name",
                  "bad\nname", "bad\x1fname", "resource/<name>", "bad?name",
                  "bad*name", 'bad"name', "bad|name")
        for value in values:
            with self.subTest(value=repr(value)):
                with self.assertRaises(runner.CaptureError):
                    runner.safe_destination(value)

    def test_windows_devices_are_refused_on_every_host(self):
        for name in ("NUL", "con", "PrN.txt", "aux.jpeg", "COM1", "lpt9.cfg",
                     "COM\u00b9", "LPT\u00b2.txt", "COM\u00b3", "CON .txt", "CONIN$",
                     "CONOUT$", "CLOCK$"):
            for value in (name, "resource/" + name + "/data"):
                with self.subTest(value=value):
                    with self.assertRaises(runner.CaptureError):
                        runner.safe_destination(value)

    def test_runner_files_and_directories_cannot_be_aliased(self):
        for value in ("CAPTURE.PPM", ".Admitted-Request.json",
                      "CAPTURE-REQUEST.TXT/child", ".openjkdf2-capture-session",
                      "SESSION", "session/home/injected"):
            with self.subTest(value=value):
                with self.assertRaises(runner.CaptureError):
                    runner.safe_destination(value)

    def test_case_aliases_and_file_directory_conflicts_are_refused(self):
        pairs = (("registry.json", "REGISTRY.JSON"),
                 ("resource/item", "resource/ITEM"),
                 ("resource/tree", "resource/TREE/leaf"),
                 ("resource/tree/leaf", "resource/TREE"))
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for first, second in pairs:
                with self.subTest(first=first, second=second):
                    document = request_document(root)
                    template = document["files"][1]
                    for destination in (first, second):
                        if any(item["destination"] == destination for item in document["files"]):
                            continue
                        document["files"].append({**template, "destination": destination,
                                                 "role": "resource" if "/" in destination else "config"})
                    with self.assertRaises(runner.CaptureError):
                        runner.validate_request(document)

    def test_malformed_cli_request_creates_no_output_or_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            document = request_document(root, "windows" if os.name == "nt" else "linux")
            document["files"].append({**document["files"][1],
                                      "destination": "C:outside.txt", "role": "config"})
            request = root / "request.json"
            request.write_text(json.dumps(document), encoding="utf-8")
            output = root / "output"
            # Even the red phase must never execute an unsafe staged request.
            with mock.patch.object(runner, "WorkerTree", side_effect=RuntimeError("No child allowed")):
                with self.assertRaises(runner.CaptureError):
                    runner.run_capture(request, output)
            self.assertFalse(output.exists())

    def test_stage_files_rechecks_raw_destination_before_any_write(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            request = runner.validate_request(request_document(root))
            item = copy.deepcopy(request["files"][1])
            item["destination"] = "../outside.txt"
            request["files"] = [item]
            output = root / "output"
            output.mkdir()
            with self.assertRaises(runner.CaptureError):
                runner.stage_files(request, output)
            self.assertEqual(list(output.iterdir()), [])
            self.assertFalse((root / "outside.txt").exists())

    def test_existing_parent_link_cannot_escape_staging(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            request = runner.validate_request(request_document(root))
            output, outside = root / "output", root / "outside"
            output.mkdir()
            outside.mkdir()
            link = output / "resource"
            if os.name == "nt":
                completed = subprocess.run(["cmd", "/d", "/c", "mklink", "/J",
                                            str(link), str(outside)],
                                           capture_output=True, text=True, timeout=10)
                self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            else:
                link.symlink_to(outside, target_is_directory=True)
            try:
                item = {**request["files"][1], "destination": "resource/new/note.txt", "role": "resource"}
                request["files"] = [item]
                with self.assertRaises(runner.CaptureError):
                    runner.stage_files(request, output)
                self.assertEqual(list(outside.iterdir()), [])
            finally:
                if os.name == "nt":
                    link.rmdir()
                else:
                    link.unlink()


if __name__ == "__main__":
    unittest.main()
