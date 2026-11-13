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


class GraphicsBackendRequestTests(unittest.TestCase):
    def test_linux_backend_selection_is_additive_to_the_unchanged_capture_wire(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            document = request_document(root)
            legacy = runner.validate_request(document)
            self.assertNotIn("graphicsBackend", legacy)
            image = root / "capture.ppm"
            expected_wire = ("sed-engine-capture-v1\n64 64 0 3 30000\n"
                             f"1 2 3 0 90 0 90\n{image}\n")
            self.assertEqual(runner.request_text(legacy, image), expected_wire)
            for backend in ("xvfb", "sdl-offscreen"):
                with self.subTest(backend=backend):
                    document["graphicsBackend"] = backend
                    try:
                        selected = runner.validate_request(document)
                    except runner.CaptureError as exc:
                        self.fail(f"Explicit Linux backend was refused: {exc}")
                    self.assertEqual(selected.pop("graphicsBackend"), backend)
                    self.assertEqual(selected, legacy)
                    selected["graphicsBackend"] = backend
                    self.assertEqual(runner.request_text(selected, image), expected_wire)

    def test_invalid_backend_is_refused_before_creating_output(self):
        for backend in ("offscreen", "auto", "native", "Xvfb", "sdl-offscreen,x11",
                        "", None, False, 1, [], {}):
            with self.subTest(backend=backend), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                document = request_document(root)
                document["graphicsBackend"] = backend
                request_path = root / "request.json"
                request_path.write_text(json.dumps(document))
                output = root / "out"
                with self.assertRaisesRegex(runner.CaptureError, "graphicsBackend"):
                    runner._run_capture(request_path, output)
                self.assertFalse(output.exists())

    def test_windows_request_cannot_select_a_linux_graphics_backend(self):
        with tempfile.TemporaryDirectory() as tmp:
            document = request_document(Path(tmp), profile="windows")
            for backend in ("xvfb", "sdl-offscreen"):
                with self.subTest(backend=backend):
                    document["graphicsBackend"] = backend
                    with self.assertRaisesRegex(runner.CaptureError, "Linux"):
                        runner.validate_request(document)


def synthetic_graphics():
    return {"schemaVersion": 1, "requestedBackend": "sdl-offscreen",
            "sdlVideoDriver": "offscreen", "sdlVersion": "3.4.12",
            "glVendor": "synthetic vendor", "glRenderer": "synthetic renderer",
            "glVersion": "4.5 synthetic"}


def install_synthetic_child(document, body):
    engine = document["files"][0]
    source = Path(engine["source"])
    data = f"#!{sys.executable}\n{body}".encode("utf-8")
    source.write_bytes(data)
    source.chmod(0o600)
    engine["sha256"] = sha(data)


def synthetic_capture_program(report):
    # A real child process that reports its actual environment but supplies only
    # synthetic pixels/graphics metadata. It is never renderer acceptance evidence.
    return """import json, os
from pathlib import Path
lines = Path(os.environ['OPENJKDF2_CAPTURE_REQUEST']).read_text().splitlines()
width, height = map(int, lines[1].split()[:2])
image = Path(lines[3])
image.write_bytes(f'P6\\n{width} {height}\\n255\\n'.encode() + bytes(width * height * 3))
names = ['DISPLAY', 'WAYLAND_DISPLAY', 'SDL_VIDEO_DRIVER', 'SDL_VIDEODRIVER',
         'SDL_AUDIO_DRIVER', 'SDL_AUDIODRIVER', 'ALSOFT_DRIVERS',
         'OPENJKDF2_CAPTURE_BACKEND', 'OPENJKDF2_CAPTURE_REQUEST', 'HOME',
         'XDG_RUNTIME_DIR', 'LIBGL_ALWAYS_SOFTWARE', 'GALLIUM_DRIVER',
         'LP_NUM_THREADS', 'MESA_SHADER_CACHE_DISABLE']
print(json.dumps({'environment': {key: os.environ.get(key) for key in names},
                  'cwd': str(Path.cwd()), 'pid': os.getpid(), 'sessionId': os.getsid(0)}))
""" + ("" if report is None else
           f"Path(str(image) + '.json').write_text(json.dumps({report!r}))\n")


class GraphicsReceiptTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.request = runner.validate_request(request_document(self.root))
        # This is the normalized boundary consumed by validate_capture, so its
        # receipt tests do not depend on the separate request-selector feature.
        self.request["graphicsBackend"] = "sdl-offscreen"
        (self.root / "capture.ppm").write_bytes(b"P6\n64 64\n255\n" + bytes(64 * 64 * 3))
        self.report_path = self.root / "capture.ppm.json"

    def write_report(self, report):
        self.report_path.write_text(json.dumps(report), encoding="utf-8")

    def test_offscreen_requires_graphics_even_when_the_ppm_is_valid(self):
        with self.assertRaisesRegex(runner.CaptureError, "receipt|graphics"):
            runner.validate_capture(self.root, self.request)
        self.write_report({"width": 64, "height": 64})
        with self.assertRaisesRegex(runner.CaptureError, "graphics"):
            runner.validate_capture(self.root, self.request)

    def test_valid_observations_are_returned_separately_from_requested_settings(self):
        graphics = synthetic_graphics()
        graphics["glVendor"] = 'synthetic "quoted" vendor\\device'.ljust(255, "~")
        graphics["glRenderer"] = " " + "R" * 253 + "~"
        graphics["glVersion"] = "V" * 255
        graphics["sdlVersion"] = "3." + "4" * 27 + ".1"
        report = {"width": 64, "height": 64, "graphics": graphics,
                  "existingEngineField": "preserved"}
        self.write_report(report)
        result = runner.validate_capture(self.root, self.request)
        self.assertTrue(result["validPpm"])
        self.assertEqual(result.get("engineReportedGraphics"), graphics)
        self.assertEqual(result["engineReportedReceipt"], report)
        self.assertEqual(result["engineReceiptSha256"], sha(self.report_path.read_bytes()))

    def test_graphics_object_requires_exact_fields(self):
        reports = [None, [], "offscreen", True, {}]
        for key in synthetic_graphics():
            report = synthetic_graphics()
            del report[key]
            reports.append(report)
        reports.append(dict(synthetic_graphics(), extra="unsupported"))
        for graphics in reports:
            with self.subTest(graphics=graphics):
                self.write_report({"graphics": graphics})
                with self.assertRaises(runner.CaptureError):
                    runner.validate_capture(self.root, self.request)

    def test_graphics_schema_and_observed_driver_must_match_offscreen(self):
        cases = [("schemaVersion", value) for value in (True, 1.0, "1", 0, 2)]
        cases += [("requestedBackend", value) for value in
                  ("native", "xvfb", "offscreen", "sdl-offscreen,xvfb", None)]
        cases += [("sdlVideoDriver", value) for value in
                  ("x11", "wayland", "dummy", "Offscreen", "offscreen,x11")]
        for key, value in cases:
            with self.subTest(key=key, value=value):
                graphics = synthetic_graphics()
                graphics[key] = value
                self.write_report({"graphics": graphics})
                with self.assertRaises(runner.CaptureError):
                    runner.validate_capture(self.root, self.request)

    def test_graphics_strings_are_nonempty_bounded_printable_ascii(self):
        limits = {"sdlVideoDriver": 31, "sdlVersion": 31,
                  "glVendor": 255, "glRenderer": 255, "glVersion": 255}
        for key, limit in limits.items():
            for value in (None, 3, True, [], "", "line\nbreak", "nul\0byte",
                          "tab\tstop", "delete\x7f", "nonascii\u00e9", "x" * (limit + 1)):
                with self.subTest(key=key, value=value):
                    graphics = synthetic_graphics()
                    graphics[key] = value
                    self.write_report({"graphics": graphics})
                    with self.assertRaises(runner.CaptureError):
                        runner.validate_capture(self.root, self.request)

    def test_sdl_version_is_a_runtime_major_minor_micro_version(self):
        for version in ("3", "3.4", "3.4.12.0", "3.4.12-dev", "SDL 3.4.12",
                        "3.-4.12", " 3.4.12", "3.4.12 "):
            with self.subTest(version=version):
                graphics = synthetic_graphics()
                graphics["sdlVersion"] = version
                self.write_report({"graphics": graphics})
                with self.assertRaises(runner.CaptureError):
                    runner.validate_capture(self.root, self.request)

    def test_legacy_sidecars_remain_optional_and_preserve_unknown_graphics(self):
        legacy_graphics = {"schemaVersion": "future", "other": "preserved"}
        for profile, backend in (("linux", None), ("linux", "xvfb"), ("windows", None)):
            with self.subTest(profile=profile, backend=backend):
                request = dict(self.request, engineProfile=profile)
                request.pop("graphicsBackend")
                if backend is not None:
                    request["graphicsBackend"] = backend
                self.report_path.unlink(missing_ok=True)
                result = runner.validate_capture(self.root, request)
                self.assertTrue(result["validPpm"])
                self.assertNotIn("engineReportedGraphics", result)
                report = {"width": 64, "height": 64, "graphics": legacy_graphics}
                self.write_report(report)
                result = runner.validate_capture(self.root, request)
                self.assertEqual(result["engineReportedReceipt"], report)
                self.assertNotIn("engineReportedGraphics", result)

    def test_receipt_size_json_and_dimensions_still_fail_closed(self):
        cases = [b"{", b"[]", b"\xff", b" " * 16385,
                 json.dumps({"width": 65, "graphics": synthetic_graphics()}).encode()]
        for data in cases:
            with self.subTest(data=data[:80]):
                self.report_path.write_bytes(data)
                with self.assertRaises(runner.CaptureError):
                    runner.validate_capture(self.root, self.request)


class FakeEngine:
    """Writes the requested PPM where the runner's capture-request
    points, then exits 0. Stands in for a capture-v1 engine."""

    def __init__(self, width, height):
        self.width, self.height = width, height
        self.calls = []

    def __call__(self, command, cwd, env, timeout_ms, stdout_path, stderr_path, **kwargs):
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
            # No native engine is involved; host-profile admission is covered
            # separately, including a real CLI refusal on unsupported hosts.
            with mock.patch.object(runner, "profile_runs_here", return_value=True), \
                    mock.patch.object(runner, "execute_engine", engine), \
                    mock.patch.object(runner, "PrivateDisplay", return_value=display), \
                    mock.patch.object(runner.subprocess, "Popen",
                                      side_effect=AssertionError("unexpected live process")), \
                    mock.patch.object(runner.shutil, "which",
                                      side_effect=AssertionError("unexpected Xvfb lookup")):
                receipt = runner._run_capture(request_path, output)
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
                self.assertEqual(env.get("SDL_VIDEO_DRIVER"), "x11")
                self.assertEqual(env["SDL_AUDIODRIVER"], "dummy")
                self.assertEqual(env.get("SDL_AUDIO_DRIVER"), "dummy")
                self.assertEqual(env["ALSOFT_DRIVERS"], "null")
                self.assertEqual(env.get("OPENJKDF2_CAPTURE_BACKEND"), "xvfb")
                self.assertEqual(receipt.get("requestedGraphicsBackend"), "xvfb")
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
                with mock.patch.object(runner, "profile_runs_here", return_value=True), \
                        mock.patch.object(runner, "PrivateDisplay", return_value=display), \
                        mock.patch.object(runner, "execute_engine", return_value=result):
                    receipt = runner._run_capture(request_path, root / "out")
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

                def execute(*args, **kwargs):
                    result = engine(*args, **kwargs)
                    image = Path(args[1]) / "capture.ppm"
                    if failure == "missing":
                        image.unlink()
                    elif failure == "wrong-size":
                        image.write_bytes(b"P6\n64 64\n255\nshort")
                    else:
                        (Path(args[1]) / "registry.json").write_bytes(b"changed")
                    return result

                with mock.patch.object(runner, "profile_runs_here", return_value=True), \
                        mock.patch.object(runner, "PrivateDisplay", return_value=FakeDisplay()), \
                        mock.patch.object(runner, "execute_engine", side_effect=execute):
                    receipt = runner._run_capture(request_path, root / "out")
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
                receipt = runner._run_capture(request_path, root / "out")
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
                receipt = runner._run_capture(request_path, output)
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
                receipt = runner._run_capture(request_path, root / "out")
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
            with mock.patch.object(runner, "profile_runs_here", return_value=True):
                receipt = runner._run_capture(request_path, root / "out")
            self.assertEqual(receipt["status"], "failed")
            self.assertEqual(receipt["failureStage"], "staging")
            self.assertIn("hash mismatch", receipt["error"])

    def test_windows_profile_does_not_inherit_linux_backend_intent(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            document = request_document(root, profile="windows")
            request_path = root / "request.json"
            request_path.write_text(json.dumps(document))
            engine = FakeEngine(64, 64)
            with mock.patch.dict(os.environ, {"OPENJKDF2_CAPTURE_BACKEND": "sdl-offscreen"}), \
                    mock.patch.object(runner, "profile_runs_here", return_value=True), \
                    mock.patch.object(runner, "PrivateDisplay", side_effect=AssertionError("Windows started Xvfb")), \
                    mock.patch.object(runner, "execute_engine", engine):
                receipt = runner._run_capture(request_path, root / "out")
                self.assertEqual(os.environ["OPENJKDF2_CAPTURE_BACKEND"], "sdl-offscreen")
            self.assertEqual(receipt["status"], "completed", receipt.get("error"))
            self.assertTrue("OPENJKDF2_CAPTURE_BACKEND" not in engine.calls[0][2],
                            "Windows child inherited Linux backend intent")
            self.assertNotIn("requestedGraphicsBackend", receipt)
            self.assertNotIn("privateDisplay", receipt)

    def test_wrong_os_profile_is_refused_before_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            foreign = "windows" if sys.platform.startswith("linux") else "linux"
            document = request_document(root, profile=foreign)
            request_path = root / "request.json"
            request_path.write_text(json.dumps(document), encoding="utf-8")
            output = root / "out"
            with self.assertRaises(runner.CaptureError):
                runner._run_capture(request_path, output)
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


@unittest.skipUnless(sys.platform.startswith("linux"), "Linux graphics profiles")
class LinuxGraphicsExecutionTests(unittest.TestCase):
    def test_offscreen_runs_a_real_staged_child_without_a_display_or_parent_env_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            document = request_document(root)
            document["graphicsBackend"] = "sdl-offscreen"
            graphics = synthetic_graphics()
            install_synthetic_child(document, synthetic_capture_program(
                {"width": 64, "height": 64, "graphics": graphics}))
            request_path = root / "request.json"
            request_path.write_text(json.dumps(document))
            output = root / "out"
            inherited = {"DISPLAY": ":parent", "WAYLAND_DISPLAY": "parent-wayland",
                         "SDL_VIDEO_DRIVER": "wayland", "SDL_VIDEODRIVER": "x11",
                         "SDL_AUDIO_DRIVER": "pulse", "SDL_AUDIODRIVER": "alsa",
                         "ALSOFT_DRIVERS": "alsa", "OPENJKDF2_CAPTURE_BACKEND": "native",
                         "LIBGL_ALWAYS_SOFTWARE": "0", "GALLIUM_DRIVER": "parent-hardware",
                         "LP_NUM_THREADS": "99", "MESA_SHADER_CACHE_DISABLE": "false"}
            with mock.patch.dict(os.environ, inherited), \
                    mock.patch.object(runner, "PrivateDisplay",
                                      side_effect=AssertionError("offscreen started a display")), \
                    mock.patch.object(runner.shutil, "which",
                                      side_effect=AssertionError("offscreen searched for Xvfb")):
                parent_environment = dict(os.environ)
                receipt = runner._run_capture(request_path, output)
                self.assertEqual(dict(os.environ), parent_environment)
            self.assertEqual(receipt["status"], "completed", receipt.get("error"))
            child = json.loads((output / "engine-stdout.log").read_text())
            self.assertEqual(child["environment"], {
                "DISPLAY": None, "WAYLAND_DISPLAY": None,
                "SDL_VIDEO_DRIVER": "offscreen", "SDL_VIDEODRIVER": "offscreen",
                "SDL_AUDIO_DRIVER": "dummy", "SDL_AUDIODRIVER": "dummy",
                "ALSOFT_DRIVERS": "null", "OPENJKDF2_CAPTURE_BACKEND": "sdl-offscreen",
                "OPENJKDF2_CAPTURE_REQUEST": str(output / "capture-request.txt"),
                "HOME": str(output / "session" / "home"),
                "XDG_RUNTIME_DIR": str(output / "session" / "runtime"),
                "LIBGL_ALWAYS_SOFTWARE": "1", "GALLIUM_DRIVER": "llvmpipe",
                "LP_NUM_THREADS": "1", "MESA_SHADER_CACHE_DISABLE": "true"})
            self.assertEqual(child["cwd"], str(output))
            self.assertEqual(child["pid"], child["sessionId"])
            self.assertEqual(child["pid"], receipt["processId"])
            self.assertEqual(receipt.get("requestedGraphicsBackend"), "sdl-offscreen")
            self.assertEqual(receipt["engineReportedGraphics"], graphics)
            self.assertNotIn("privateDisplay", receipt)
            self.assertTrue(receipt["validPpm"])
            self.assertTrue(receipt["inputsUnchanged"])
            self.assertFalse(receipt["effectiveCameraObserved"])
            self.assertFalse(receipt["runtimeResourceResolutionObserved"])
            self.assertEqual(receipt["acceptance"], "unverified")
            self.assertEqual((output / "engine-stderr.log").read_bytes(), b"")
            saved_request = json.loads((output / "request.json").read_text())
            self.assertEqual(saved_request["graphicsBackend"], "sdl-offscreen")
            self.assertEqual(json.loads((output / "launcher-receipt.json").read_text()), receipt)
            self.assertEqual((output / "capture-request.txt").read_text(),
                             "sed-engine-capture-v1\n64 64 0 3 30000\n"
                             f"1 2 3 0 90 0 90\n{output / 'capture.ppm'}\n")
            self.assertEqual(stat.S_IMODE(Path(document["files"][0]["source"]).stat().st_mode),
                             0o600)

    def test_explicit_xvfb_keeps_private_display_and_overrides_both_driver_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            document = request_document(root)
            document["graphicsBackend"] = "xvfb"
            request_path = root / "request.json"
            request_path.write_text(json.dumps(document))
            display, engine = FakeDisplay(), FakeEngine(64, 64)
            with mock.patch.dict(os.environ, {"SDL_VIDEO_DRIVER": "offscreen",
                                              "SDL_VIDEODRIVER": "wayland",
                                              "OPENJKDF2_CAPTURE_BACKEND": "sdl-offscreen"}), \
                    mock.patch.object(runner, "PrivateDisplay", return_value=display), \
                    mock.patch.object(runner, "execute_engine", engine):
                receipt = runner._run_capture(request_path, root / "out")
            self.assertEqual(receipt["status"], "completed", receipt.get("error"))
            env = engine.calls[0][2]
            self.assertEqual(env["DISPLAY"], ":471")
            self.assertEqual(env.get("SDL_VIDEO_DRIVER"), "x11")
            self.assertEqual(env["SDL_VIDEODRIVER"], "x11")
            self.assertEqual(env.get("OPENJKDF2_CAPTURE_BACKEND"), "xvfb")
            self.assertEqual(receipt.get("requestedGraphicsBackend"), "xvfb")
            self.assertEqual(receipt["privateDisplay"]["display"], ":471")
            self.assertTrue(display.closed)
            self.assertNotIn("engineReportedGraphics", receipt)

    def test_offscreen_child_failure_and_timeout_do_not_retry_or_start_a_display(self):
        for failure in ("exit", "timeout"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                document = request_document(root)
                document.update(graphicsBackend="sdl-offscreen", engineTimeoutMs=1000)
                program = ("from pathlib import Path\nimport sys, time\n"
                           "with Path('attempts.txt').open('a') as attempts:\n"
                           "    attempts.write('attempt\\n')\n")
                program += ("sys.exit(6)\n" if failure == "exit" else "time.sleep(60)\n")
                install_synthetic_child(document, program)
                request_path = root / "request.json"
                request_path.write_text(json.dumps(document))
                output = root / "out"
                with mock.patch.object(runner, "PrivateDisplay",
                                       side_effect=AssertionError("offscreen fell back to a display")), \
                        mock.patch.object(runner.shutil, "which",
                                          side_effect=AssertionError("offscreen searched for Xvfb")):
                    receipt = runner._run_capture(request_path, output)
                self.assertEqual(receipt["status"], "failed")
                self.assertEqual(receipt["failureStage"], "execution", receipt.get("error"))
                self.assertTrue(receipt["engineStarted"])
                self.assertEqual(receipt["processExitCode"], 6 if failure == "exit" else None)
                self.assertEqual(receipt["timedOut"], failure == "timeout")
                self.assertEqual((output / "attempts.txt").read_text(), "attempt\n")
                self.assertNotIn("privateDisplay", receipt)
                self.assertNotIn("engineReportedGraphics", receipt)
                self.assertTrue(receipt["inputsUnchanged"])
                self.assertEqual(receipt["acceptance"], "unverified")

    def test_offscreen_image_without_matching_report_remains_a_failed_capture(self):
        for report in (None, {"width": 64, "height": 64},
                       {"graphics": dict(synthetic_graphics(), sdlVideoDriver="x11")}):
            with self.subTest(report=report), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                document = request_document(root)
                document["graphicsBackend"] = "sdl-offscreen"
                install_synthetic_child(document, synthetic_capture_program(report))
                request_path = root / "request.json"
                request_path.write_text(json.dumps(document))
                output = root / "out"
                with mock.patch.object(runner, "PrivateDisplay",
                                       side_effect=AssertionError("offscreen fell back to a display")):
                    receipt = runner._run_capture(request_path, output)
                self.assertEqual(receipt["status"], "failed")
                self.assertEqual(receipt["failureStage"], "capture-validation", receipt.get("error"))
                self.assertEqual(receipt["processExitCode"], 0)
                self.assertTrue((output / "capture.ppm").is_file())
                self.assertNotIn("engineReportedGraphics", receipt)
                self.assertTrue(receipt["inputsUnchanged"])
                self.assertFalse(receipt["effectiveCameraObserved"])
                self.assertFalse(receipt["runtimeResourceResolutionObserved"])
                self.assertEqual(receipt["acceptance"], "unverified")


if __name__ == "__main__":
    unittest.main()
