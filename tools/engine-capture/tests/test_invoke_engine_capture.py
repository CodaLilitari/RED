"""Stdlib tests for the D-14 engine-capture runner pattern.

Synthetic fixtures only: no retail assets, no real engine binary.
The full-capture test substitutes a fake engine launcher that writes
a well-formed PPM; it proves runner orchestration, not engine
acceptance (which stays unverified -- see ../README.md).
"""

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import invoke_engine_capture as runner


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def make_source(root: Path, name: str, data: bytes) -> Path:
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def request_document(root: Path, profile="linux", width=64, height=64):
    names = (["openjkdf2"] if profile == "linux" else
             ["openjkdf2-64.exe", "OpenAL32.dll", "libGameNetworkingSockets.dll"])
    files = []
    for name in names:
        data = f"synthetic {name}".encode()
        role = "engine" if name in ("openjkdf2", "openjkdf2-64.exe") else "runtime"
        files.append({"source": str(make_source(root, f"src/{name}", data)),
                      "destination": name, "sha256": sha(data), "role": role})
    for name in ("registry.json", "openjkdf2_cvars.json"):
        data = b"{}"
        files.append({"source": str(make_source(root, f"src/{name}", data)),
                      "destination": name, "sha256": sha(data), "role": "config"})
    data = b"episode"
    files.append({"source": str(make_source(root, "src/episode.jk", data)),
                  "destination": "episode/test/episode.jk",
                  "sha256": sha(data), "role": "scenario"})
    data = b"level"
    files.append({"source": str(make_source(root, "src/level.jkl", data)),
                  "destination": "episode/test/jkl/level.jkl",
                  "sha256": sha(data), "role": "scenario"})
    return {"schemaVersion": 1, "episode": "test", "map": "level.jkl",
            "camera": {"width": width, "height": height, "sector": 0,
                       "position": [1, 2, 3], "pitchYawRoll": [0, 90, 0],
                       "horizontalFov": 90},
            "files": files}


class ProfileValidationTests(unittest.TestCase):
    def test_linux_profile_validates_and_selects_its_executable(self):
        with tempfile.TemporaryDirectory() as tmp:
            request = runner.validate_request(request_document(Path(tmp)))
            self.assertEqual(request["engineProfile"], "linux")
            self.assertEqual(runner.engine_executable(request), "openjkdf2")

    def test_windows_profile_validates(self):
        with tempfile.TemporaryDirectory() as tmp:
            request = runner.validate_request(
                request_document(Path(tmp), profile="windows"))
            self.assertEqual(request["engineProfile"], "windows")
            self.assertEqual(runner.engine_executable(request), "openjkdf2-64.exe")

    def test_mixed_or_incomplete_profiles_are_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            document = request_document(Path(tmp))
            document["files"] = [f for f in document["files"]
                                 if f["destination"] != "openjkdf2"]
            with self.assertRaises(runner.CaptureError):
                runner.validate_request(document)

    def test_reserved_destination_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            document = request_document(Path(tmp))
            document["files"][0]["destination"] = "capture.ppm"
            with self.assertRaises(runner.CaptureError):
                runner.validate_request(document)

    def test_request_text_matches_capture_v1_layout(self):
        with tempfile.TemporaryDirectory() as tmp:
            request = runner.validate_request(request_document(Path(tmp)))
            text = runner.request_text(request, Path(tmp) / "capture.ppm")
            lines = text.splitlines()
            self.assertEqual(lines[0], "sed-engine-capture-v1")
            self.assertEqual(lines[1], "64 64 0 3 30000")
            self.assertEqual(lines[2], "1 2 3 0 90 0 90")
            self.assertTrue(lines[3].endswith("capture.ppm"))


class CommandProfileTests(unittest.TestCase):
    def test_mots_compat_flag_defaults_on_and_can_be_disabled(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            request = runner.validate_request(request_document(root, profile="windows"))
            self.assertIn("-motsCompat", runner.command_for(request, root))
            document = request_document(root, profile="windows")
            document["motsCompat"] = False
            jk_request = runner.validate_request(document)
            self.assertNotIn("-motsCompat", runner.command_for(jk_request, root))
            document["motsCompat"] = "yes"
            with self.assertRaises(runner.CaptureError):
                runner.validate_request(document)


class FakeEngine:
    """Writes the requested PPM where the runner's capture-request
    points, then exits 0. Stands in for a capture-v1 engine."""

    def __init__(self, width, height):
        self.width, self.height = width, height

    def __call__(self, command, cwd, env, timeout_ms, stdout_path, stderr_path):
        Path(stdout_path).write_bytes(b"fake engine\n")
        Path(stderr_path).write_bytes(b"")
        header = f"P6\n{self.width} {self.height}\n255\n".encode()
        (Path(cwd) / "capture.ppm").write_bytes(header + bytes(self.width * self.height * 3))
        return {"processId": 4242, "processExitCode": 0, "timedOut": False}


class RunCaptureTests(unittest.TestCase):
    def test_full_capture_with_synthetic_engine(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            profile = "linux" if sys.platform.startswith("linux") else "windows"
            document = request_document(root, profile=profile)
            request_path = root / "request.json"
            request_path.write_text(json.dumps(document), encoding="utf-8")
            output = root / "out"
            original = runner.execute_engine
            runner.execute_engine = FakeEngine(64, 64)
            try:
                receipt = runner.run_capture(request_path, output)
            finally:
                runner.execute_engine = original
            self.assertEqual(receipt["status"], "completed", receipt.get("error"))
            self.assertTrue(receipt["validPpm"])
            self.assertFalse(receipt["effectiveCameraObserved"])
            self.assertFalse(receipt["runtimeResourceResolutionObserved"])
            self.assertEqual(receipt["acceptance"], "unverified")
            self.assertTrue(receipt["inputsUnchanged"])
            on_disk = json.loads((output / "launcher-receipt.json").read_text())
            self.assertEqual(on_disk["status"], "completed")
            self.assertTrue((output / "manifest-before.json").is_file())
            self.assertTrue((output / "manifest-after.json").is_file())

    def test_hash_mismatch_fails_with_receipt_not_refusal(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            profile = "linux" if sys.platform.startswith("linux") else "windows"
            document = request_document(root, profile=profile)
            document["files"][0]["sha256"] = "0" * 64
            request_path = root / "request.json"
            request_path.write_text(json.dumps(document), encoding="utf-8")
            receipt = runner.run_capture(request_path, root / "out")
            self.assertEqual(receipt["status"], "failed")
            self.assertEqual(receipt["failureStage"], "staging")
            self.assertIn("hash mismatch", receipt["error"])

    def test_wrong_os_profile_is_refused_before_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            foreign = "windows" if sys.platform.startswith("linux") else "linux"
            document = request_document(root, profile=foreign)
            request_path = root / "request.json"
            request_path.write_text(json.dumps(document), encoding="utf-8")
            output = root / "out"
            with self.assertRaises(runner.CaptureError):
                runner.run_capture(request_path, output)
            self.assertFalse(output.exists())

    @unittest.skipUnless(sys.platform.startswith("linux"), "POSIX process groups")
    def test_process_group_kill_terminates_engine(self):
        import subprocess as sp
        process = sp.Popen([sys.executable, "-c", "import time; time.sleep(60)"],
                           start_new_session=True)
        try:
            runner.kill_process_tree(process)
            self.assertIsNotNone(process.poll())
        finally:
            if process.poll() is None:
                process.kill()


if __name__ == "__main__":
    unittest.main()
