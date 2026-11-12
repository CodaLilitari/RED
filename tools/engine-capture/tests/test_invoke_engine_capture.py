"""Stdlib tests for the D-14 engine-capture runner pattern.

Synthetic fixtures only: no retail assets, no real engine binary.
The full-capture test substitutes the engine and display boundaries.
It writes a well-formed PPM and proves runner orchestration, not engine
acceptance (which stays unverified -- see ../README.md).
"""

import hashlib
import json
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

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
        self.calls = []

    def __call__(self, command, cwd, env, timeout_ms, stdout_path, stderr_path):
        self.calls.append((command, cwd, dict(env), timeout_ms))
        Path(stdout_path).write_bytes(b"fake engine\n")
        Path(stderr_path).write_bytes(b"")
        header = f"P6\n{self.width} {self.height}\n255\n".encode()
        (Path(cwd) / "capture.ppm").write_bytes(header + bytes(self.width * self.height * 3))
        return {"processId": 4242, "processExitCode": 0, "timedOut": False}


class FakeDisplay:
    """External display stand-in; staging, environment and receipts stay real."""

    display = ":471"

    def __init__(self):
        self.closed = False

    def __enter__(self):
        return self

    def receipt(self):
        return {"display": self.display, "tcpListening": False,
                "userVisible": False}

    def close(self):
        self.closed = True


class RunCaptureTests(unittest.TestCase):
    def test_full_capture_with_synthetic_engine(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            profile = "linux" if sys.platform.startswith("linux") else "windows"
            document = request_document(root, profile=profile)
            request_path = root / "request.json"
            request_path.write_text(json.dumps(document), encoding="utf-8")
            output = root / "out"
            engine, display = FakeEngine(64, 64), FakeDisplay()
            with mock.patch.object(runner, "execute_engine", engine), \
                    mock.patch.object(runner, "PrivateDisplay", return_value=display), \
                    mock.patch.object(runner.subprocess, "Popen",
                                      side_effect=AssertionError("unexpected live process")), \
                    mock.patch.object(runner.shutil, "which",
                                      side_effect=AssertionError("unexpected Xvfb lookup")):
                receipt = runner.run_capture(request_path, output)
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
            self.assertEqual(len(engine.calls), 1)
            command, cwd, env, timeout = engine.calls[0]
            executable = "openjkdf2" if profile == "linux" else "openjkdf2-64.exe"
            self.assertEqual(command, [str(output / executable), "-capture", "-autostart",
                                       "-sp", "-motsCompat", "-noHUD", "-episode",
                                       "test", "-map", "level.jkl"])
            self.assertEqual(cwd, output)
            self.assertEqual(timeout, 30000)
            self.assertEqual(env["OPENJKDF2_CAPTURE_REQUEST"],
                             str((output / "capture-request.txt").resolve()))
            self.assertEqual((output / ".openjkdf2-capture-session").read_text(),
                             "red-engine-capture-runner\n")
            self.assertEqual((output / "capture-request.txt").read_text().splitlines()[:3],
                             ["sed-engine-capture-v1", "64 64 0 3 30000",
                              "1 2 3 0 90 0 90"])
            if profile == "linux":
                self.assertEqual(env["DISPLAY"], ":471")
                self.assertEqual(env["SDL_VIDEODRIVER"], "x11")
                self.assertEqual(env["SDL_AUDIODRIVER"], "dummy")
                self.assertEqual(env["ALSOFT_DRIVERS"], "null")
                self.assertEqual(env["HOME"], str(output / "session" / "home"))
                self.assertEqual(env["XDG_RUNTIME_DIR"], str(output / "session" / "runtime"))
                self.assertTrue(display.closed)
                self.assertEqual(receipt["privateDisplay"],
                                 {"display": ":471", "tcpListening": False,
                                  "userVisible": False})

    def test_engine_failure_and_timeout_keep_unverified_failed_receipts(self):
        for exit_code, timed_out in ((9, False), (None, True)):
            with self.subTest(exit_code=exit_code, timed_out=timed_out), \
                    tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                profile = "linux" if sys.platform.startswith("linux") else "windows"
                request_path = root / "request.json"
                request_path.write_text(json.dumps(request_document(root, profile=profile)))
                display = FakeDisplay()
                result = {"processId": 4242, "processExitCode": exit_code,
                          "timedOut": timed_out}
                with mock.patch.object(runner, "PrivateDisplay", return_value=display), \
                        mock.patch.object(runner, "execute_engine", return_value=result):
                    receipt = runner.run_capture(request_path, root / "out")
                self.assertEqual(receipt["status"], "failed")
                self.assertEqual(receipt["failureStage"], "execution")
                self.assertEqual(receipt["processExitCode"], exit_code)
                self.assertEqual(receipt["timedOut"], timed_out)
                self.assertTrue(receipt["inputsUnchanged"])
                self.assertEqual(receipt["acceptance"], "unverified")
                if profile == "linux":
                    self.assertTrue(display.closed)

    def test_invalid_capture_and_changed_inputs_cannot_complete(self):
        for failure in ("missing", "wrong-size", "changed-input"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                profile = "linux" if sys.platform.startswith("linux") else "windows"
                document = request_document(root, profile=profile)
                request_path = root / "request.json"
                request_path.write_text(json.dumps(document))
                engine = FakeEngine(64, 64)

                def execute(*args):
                    result = engine(*args)
                    image = Path(args[1]) / "capture.ppm"
                    if failure == "missing":
                        image.unlink()
                    elif failure == "wrong-size":
                        image.write_bytes(b"P6\n64 64\n255\nshort")
                    else:
                        (Path(args[1]) / "registry.json").write_bytes(b"changed")
                    return result

                with mock.patch.object(runner, "PrivateDisplay", return_value=FakeDisplay()), \
                        mock.patch.object(runner, "execute_engine", side_effect=execute):
                    receipt = runner.run_capture(request_path, root / "out")
                self.assertEqual(receipt["status"], "failed")
                self.assertEqual(receipt["acceptance"], "unverified")
                self.assertEqual(receipt["failureStage"], "post-run-identity"
                                 if failure == "changed-input" else "capture-validation")
                self.assertEqual(receipt["inputsUnchanged"], failure != "changed-input")

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux display profile")
    def test_display_start_failure_never_launches_engine(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            request_path = root / "request.json"
            request_path.write_text(json.dumps(request_document(root)))
            display = mock.Mock(spec=FakeDisplay)
            display.__enter__ = mock.Mock(side_effect=runner.CaptureError("display unavailable"))
            with mock.patch.object(runner, "PrivateDisplay", return_value=display), \
                    mock.patch.object(runner, "execute_engine") as execute:
                receipt = runner.run_capture(request_path, root / "out")
            execute.assert_not_called()
            self.assertEqual(receipt["status"], "failed")
            self.assertFalse(receipt["engineStarted"])
            self.assertTrue(receipt["inputsUnchanged"])
            self.assertIn("display unavailable", receipt["error"])

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux display profile")
    def test_setup_failure_after_display_start_closes_display(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            request_path = root / "request.json"
            request_path.write_text(json.dumps(request_document(root)))
            output, display = root / "out", FakeDisplay()
            original_mkdir = Path.mkdir

            def fail_session_home(path, *args, **kwargs):
                if path == output / "session" / "home":
                    raise OSError("synthetic session setup failure")
                return original_mkdir(path, *args, **kwargs)

            with mock.patch.object(runner, "PrivateDisplay", return_value=display), \
                    mock.patch.object(Path, "mkdir", fail_session_home), \
                    mock.patch.object(runner, "execute_engine") as execute:
                receipt = runner.run_capture(request_path, output)
            self.assertEqual(receipt["status"], "failed")
            self.assertIn("session setup failure", receipt["error"])
            self.assertFalse(receipt["engineStarted"])
            execute.assert_not_called()
            self.assertTrue(display.closed, "started display leaked after setup failure")

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux executable permissions")
    def test_staged_linux_engine_executes_without_changing_data_permissions(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            document = request_document(root)
            engine_item = document["files"][0]
            source = Path(engine_item["source"])
            source.write_bytes(b"#!/bin/sh\nprintf 'synthetic child executed\\n'\n")
            source.chmod(0o755)
            engine_item["sha256"] = sha(source.read_bytes())
            request = runner.validate_request(document)
            output = root / "stage"
            output.mkdir()
            before = runner.stage_files(request, output)
            staged = output / "openjkdf2"
            self.assertTrue(staged.stat().st_mode & stat.S_IXUSR,
                            "staged Linux engine is not executable")
            self.assertEqual(staged.stat().st_mode & (stat.S_IXGRP | stat.S_IXOTH), 0)
            for item in request["files"][1:]:
                self.assertEqual((output / item["destination"]).stat().st_mode & 0o111, 0)
            result = runner.execute_engine([str(staged)], output, dict(os.environ), 1000,
                                           output / "stdout.log", output / "stderr.log")
            self.assertEqual(result["processExitCode"], 0)
            self.assertFalse(result["timedOut"])
            self.assertEqual((output / "stdout.log").read_bytes(), b"synthetic child executed\n")
            self.assertEqual((output / "stderr.log").read_bytes(), b"")
            self.assertEqual(stat.S_IMODE(source.stat().st_mode), 0o755)
            after = runner.after_manifest(before, output)
            self.assertTrue(all(item["sourceUnchanged"] and item["stagedUnchanged"]
                                for item in after))

    @unittest.skipUnless(os.name == "posix", "POSIX permission metadata")
    def test_windows_profile_staging_does_not_add_execute_permissions(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            request = runner.validate_request(request_document(root, profile="windows"))
            output = root / "stage"
            output.mkdir()
            runner.stage_files(request, output)
            for item in request["files"]:
                self.assertEqual((output / item["destination"]).stat().st_mode & 0o111, 0)

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux executable permissions")
    def test_engine_permission_failure_is_a_staging_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            request_path = root / "request.json"
            request_path.write_text(json.dumps(request_document(root)))
            with mock.patch.object(Path, "chmod", side_effect=PermissionError("chmod denied")), \
                    mock.patch.object(runner, "execute_engine") as execute:
                receipt = runner.run_capture(request_path, root / "out")
            self.assertEqual(receipt["status"], "failed")
            self.assertEqual(receipt["failureStage"], "staging")
            self.assertFalse(receipt["engineStarted"])
            self.assertIn("chmod denied", receipt["error"])
            execute.assert_not_called()

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
