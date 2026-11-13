"""Synthetic native-report/PNG/process fixtures; no native GUI acceptance claim."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import uuid
import zipfile
import zlib

SCRIPT = Path(__file__).resolve().parents[1] / "accept_package_native.py"
SPEC = importlib.util.spec_from_file_location("accept_package_native", SCRIPT)
helper = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(helper)


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def native_png(second=False, color_type=6, filter_type=0, blank=False, width=64, height=64):
    channels = 4 if color_type == 6 else 3
    rows, previous = [], bytes(width * channels)
    for y in range(height):
        row = bytearray()
        for x in range(width):
            bright = not blank and 27 <= y <= 37 and (43 <= x <= 53 if second else 11 <= x <= 21)
            row.extend(([255 if bright else 0] * 3) + ([255] if channels == 4 else []))
        filtered = bytearray()
        for i, value in enumerate(row):
            left, above = row[i - channels] if i >= channels else 0, previous[i]
            upper_left = previous[i - channels] if i >= channels else 0
            if filter_type == 4:
                p = left + above - upper_left
                options = ((abs(p - left), left), (abs(p - above), above), (abs(p - upper_left), upper_left))
                predictor = min(enumerate(options), key=lambda item: (item[1][0], item[0]))[1][1]
            else:
                predictor = (0, left, above, (left + above) // 2)[filter_type]
            filtered.append((value - predictor) & 255)
        rows.append(bytes([filter_type]) + filtered)
        previous = row
    header = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b"")


class NativeAcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="red-native-acceptance-tests-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.deadline = time.monotonic() + 10
        self.probes = {"firstFace": [0.25, 0.5], "secondFace": [0.75, 0.5], "background": [0.5, 0.2]}
        self.executable = self.root / "red-editor"
        self.executable.write_bytes(b"synthetic executable identity")
        self.report = self.root / "report"
        self.report.mkdir()
        identifier = str(uuid.uuid4())
        self.receipt = {"schemaVersion": 1, "acceptanceKind": "native-installation", "status": "passed",
            "nativeFramesAccepted": True, "runId": identifier, "executable": str(self.executable),
            "reportDirectory": str(self.report), "child": {"schemaVersion": 1, "status": "passed",
                "runId": identifier, "executable": str(self.executable), "appVersion": "0.6.0", "qtVersion": "6.8.3",
                "nativePlatform": "windows", "gl": {"vendor": "synthetic", "renderer": "fixture", "version": "3.3"},
                "milestones": {key: True for key in ("shellExposed", "contextValid", "sceneShaderLinked", "runtimeDataParsed")},
                "probes": self.probes, "frames": [], "runtimeData": []}}
        for name, second in (("frame-before.png", False), ("frame-after.png", True)):
            raw = native_png(second)
            (self.report / name).write_bytes(raw)
            self.receipt["child"]["frames"].append({"file": name, "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()})
        (self.executable.parent / "data").mkdir()
        for name in ("df2jk.lst", "ijim.tpl", "master.tpl", "mats.jlh", "mats.jll", "mots.tpl", "mtpls.jlh",
                     "mtpls.jll", "shapes.tpl", "tpls.jlh", "tpls.jll"):
            raw = b"parsed synthetic data\n"
            (self.executable.parent / "data" / name).write_bytes(raw)
            self.receipt["child"]["runtimeData"].append({"file": name, "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()})
        (self.report / "request.json").write_text(json.dumps({"schemaVersion": 1, "runId": identifier,
            "executable": str(self.executable)}), encoding="utf-8")

    def validate(self, receipt=None, exit_code=0):
        receipt = self.receipt if receipt is None else receipt
        raw = json.dumps(receipt).encode()
        (self.report / "installation-report.json").write_bytes(raw)
        return helper.validate_native_report(self.executable, self.report,
            {"exitCode": exit_code, "stdout": raw, "stderr": b""}, "0.6.0", "6.8.3", "windows-x86_64", self.deadline)

    def test_rgb_rgba_and_every_png_filter_decode_exact_probe_regions(self):
        for color_type in (2, 6):
            for filter_type in range(5):
                with self.subTest(color_type=color_type, filter_type=filter_type):
                    result = helper.inspect_png_regions(native_png(color_type=color_type, filter_type=filter_type), self.probes, self.deadline)
                    self.assertEqual(result["dimensions"], [64, 64])
                    self.assertEqual(result["regions"], ["bright", "dark", "dark"])

    def test_matching_native_report_and_real_decoded_movement_are_admitted(self):
        result = self.validate()
        self.assertTrue(result["nativeInstallationAccepted"])
        self.assertTrue(result["independentPixelsVerified"])
        self.assertEqual(result["renderer"], "fixture")

    def test_bad_native_identity_status_and_milestones_refuse(self):
        variants = []
        for key, value in (("status", "failed"), ("nativeFramesAccepted", False), ("runId", str(uuid.uuid4()))):
            item = copy.deepcopy(self.receipt)
            item[key] = value
            variants.append(item)
        for key, value in (("nativePlatform", "offscreen"), ("appVersion", "0.5.0"), ("qtVersion", "6.10.0"),
                           ("executable", str(self.root / "other-editor"))):
            item = copy.deepcopy(self.receipt)
            item["child"][key] = value
            variants.append(item)
        item = copy.deepcopy(self.receipt)
        item["child"]["milestones"]["sceneShaderLinked"] = False
        variants.append(item)
        for item in variants:
            with self.subTest(item=item), self.assertRaises(helper.cli.AcceptanceError):
                self.validate(item)
        with self.assertRaises(helper.cli.AcceptanceError):
            self.validate(exit_code=1)

    def test_hash_correct_but_blank_and_stale_frames_fail_independent_pixels(self):
        for raw in (native_png(blank=True), native_png(False)):
            (self.report / "frame-after.png").write_bytes(raw)
            self.receipt["child"]["frames"][1].update(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
            with self.assertRaises(helper.cli.AcceptanceError):
                self.validate()

    def test_changed_frame_and_runtime_data_are_rejected(self):
        (self.report / "frame-after.png").write_bytes(native_png(False))
        with self.assertRaises(helper.cli.AcceptanceError):
            self.validate()
        raw = native_png(True)
        (self.report / "frame-after.png").write_bytes(raw)
        (self.executable.parent / "data" / "master.tpl").write_bytes(b"changed")
        with self.assertRaises(helper.cli.AcceptanceError):
            self.validate()

    def test_png_corruption_unsupported_format_and_bounded_inflate_refuse(self):
        raw = native_png()
        invalid = [raw[:-4], raw + b"extra", raw[:20] + bytes([raw[20] ^ 1]) + raw[21:]]
        header = struct.pack(">IIBBBBB", 64, 64, 8, 6, 0, 0, 0)
        invalid.append(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(b"\0" * (64 * 257 + 1))) + chunk(b"IEND", b""))
        for value in invalid:
            with self.subTest(size=len(value)), self.assertRaises(helper.cli.AcceptanceError):
                helper.inspect_png_regions(value, self.probes, self.deadline)

    def test_probe_edges_overlap_and_nonfinite_coordinates_refuse(self):
        for value in ([0.0, 0.5], [float("nan"), 0.5], [0.75, 0.5]):
            probes = dict(self.probes, firstFace=value)
            with self.subTest(value=value), self.assertRaises(helper.cli.AcceptanceError):
                helper.inspect_png_regions(native_png(), probes, self.deadline)

    def test_frame_path_cannot_escape_report_directory(self):
        self.receipt["child"]["frames"][0]["file"] = "../outside.png"
        with self.assertRaises(helper.cli.AcceptanceError):
            self.validate()

    def test_malformed_schema_and_runtime_names_fail_with_admission_diagnostics(self):
        for section in (self.receipt, self.receipt["child"]):
            original = section["schemaVersion"]
            section["schemaVersion"] = True
            with self.subTest(section="parent" if section is self.receipt else "child"), \
                 self.assertRaises(helper.cli.AcceptanceError):
                self.validate()
            section["schemaVersion"] = original
        self.receipt["child"]["runtimeData"][0]["file"] = []
        with self.assertRaises(helper.cli.AcceptanceError):
            self.validate()

    def test_persisted_and_stdout_receipts_must_agree(self):
        raw = json.dumps(self.receipt).encode()
        (self.report / "installation-report.json").write_bytes(raw)
        changed = copy.deepcopy(self.receipt)
        changed["child"]["gl"]["renderer"] = "other renderer"
        with self.assertRaisesRegex(helper.cli.AcceptanceError, "persisted receipt differ"):
            helper.validate_native_report(self.executable, self.report,
                {"exitCode": 0, "stdout": json.dumps(changed).encode()}, "0.6.0", "6.8.3",
                "windows-x86_64", self.deadline)

    def test_png_unsupported_headers_chunks_transparency_and_filter_are_refused(self):
        raw = native_png()
        header = raw[16:29]
        compressed = raw[41:-16]
        prefix = b"\x89PNG\r\n\x1a\n"
        ending = chunk(b"IEND", b"")
        invalid = [prefix + chunk(b"IHDR", struct.pack(">IIBBBBB", *values))
                   + chunk(b"IDAT", compressed) + ending for values in
                   ((4097, 64, 8, 6, 0, 0, 0), (64, 64, 16, 6, 0, 0, 0),
                    (64, 64, 8, 3, 0, 0, 0), (64, 64, 8, 6, 0, 0, 1))]
        for kind in (b"IHDR", b"tRNS", b"acTL", b"ABCD"):
            invalid.append(prefix + chunk(b"IHDR", header) + chunk(kind, header)
                           + chunk(b"IDAT", compressed) + ending)
        invalid.append(prefix + chunk(b"IHDR", header) + chunk(b"IDAT", compressed[:10])
                       + chunk(b"tEXt", b"key\0value") + chunk(b"IDAT", compressed[10:]) + ending)
        filtered = bytearray(zlib.decompress(compressed))
        filtered[0] = 5
        invalid.append(prefix + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(filtered)) + ending)
        for value in invalid:
            with self.subTest(size=len(value)), self.assertRaises(helper.cli.AcceptanceError):
                helper.inspect_png_regions(value, self.probes, self.deadline)
        filtered[0] = 0
        for y in range(64):
            for x in range(64):
                filtered[y * 257 + 1 + x * 4 + 3] = 0
        transparent = prefix + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(filtered)) + ending
        self.assertEqual(helper.inspect_png_regions(transparent, self.probes, self.deadline)["regions"],
                         ["other", "other", "other"])

    def command_fixture(self):
        (self.report / "installation-report.json").write_text(json.dumps(self.receipt), encoding="utf-8")
        script = self.root / "native_fixture.py"
        script.write_text('''import json,os,shutil,sys,time
from pathlib import Path
if os.environ.get("SYNTHETIC_NATIVE_MODE")=="stall": time.sleep(30)
assert len(sys.argv)==4 and sys.argv[1:3]==["--check-installation","--report-directory"]
destination=Path(sys.argv[3]); source=Path(__file__).parent/"report"
shutil.copytree(source,destination)
receipt=json.loads((destination/"installation-report.json").read_text())
receipt["reportDirectory"]=str(destination)
(destination/"installation-report.json").write_text(json.dumps(receipt))
print(json.dumps(receipt))
''', encoding="utf-8")
        return [sys.executable, str(script)]

    def test_actual_native_command_argument_vector_and_unicode_scope(self):
        records = []
        destination = self.root / "native 漢字 🧪"
        result = helper.exercise_native(self.command_fixture(), self.executable, destination,
            self.root, os.environ.copy(), self.deadline, "0.6.0", "6.8.3", "windows-x86_64", records)
        self.assertTrue(result["nativeInstallationAccepted"])
        self.assertEqual(records[0]["command"][-3:], ["--check-installation", "--report-directory", str(destination)])
        self.assertEqual(records[0]["exitCode"], 0)

    def test_actual_stalled_native_command_has_an_outer_deadline(self):
        started = time.monotonic()
        with self.assertRaisesRegex(helper.cli.AcceptanceError, "deadline"):
            helper.exercise_native(self.command_fixture(), self.executable, self.root / "never-created",
                self.root, dict(os.environ, SYNTHETIC_NATIVE_MODE="stall"), started + 0.2,
                "0.6.0", "6.8.3", "windows-x86_64", [])
        self.assertLess(time.monotonic() - started, 3)

    def test_explicit_storage_sentinels_preserve_bytes_and_absence(self):
        existing, absent = self.root / "red.ini", self.root / "missing.ini"
        existing.write_bytes(b"original preferences\n")
        snapshot = helper.capture_storage_sentinels([existing, absent], self.deadline)
        self.assertTrue(helper.verify_storage_sentinels(snapshot, self.deadline))
        existing.write_bytes(b"changed preferences\n")
        with self.assertRaises(helper.cli.AcceptanceError):
            helper.verify_storage_sentinels(snapshot, self.deadline)
        existing.write_bytes(b"original preferences\n")
        absent.write_bytes(b"unexpected creation")
        with self.assertRaises(helper.cli.AcceptanceError):
            helper.verify_storage_sentinels(snapshot, self.deadline)

    def package_request(self):
        files = {name: b"synthetic packaged payload" for name in
            ("red-editor.exe", "red-agent.exe", "red-mcp.exe", "LICENSE", "THIRD_PARTY_NOTICES.md",
             "tools/engine-capture/invoke_engine_capture.py", "tools/engine-capture/README.md")}
        files.update({"data/" + name: b"synthetic parsed data" for name in helper.RUNTIME_NAMES})
        manifest = {"format": 1, "platform": "Windows", "version": "0.6.0", "qt_version": "6.8.3",
                    "sourceCommit": "a" * 40, "files": [{"path": name, "bytes": len(data),
                    "sha256": hashlib.sha256(data).hexdigest()} for name, data in files.items()]}
        files["RELEASE_MANIFEST.json"] = json.dumps(manifest).encode()
        archive = self.root / "package.zip"
        with zipfile.ZipFile(archive, "w") as output:
            for name, data in files.items():
                output.writestr("RED/" + name, data)
        scratch = self.root / "worker"
        scratch.mkdir()
        request = {"archive": str(archive), "archiveSha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
            "version": "0.6.0", "platform": "windows-x86_64", "sourceCommit": "a" * 40,
            "timeoutSeconds": 10, "userStorageSentinels": []}
        path = scratch / "request.json"
        path.write_text(json.dumps(request), encoding="utf-8")
        return path, request

    def test_worker_accepts_only_unchanged_complete_package_and_owned_profile(self):
        path, _ = self.package_request()
        observed = {}
        def native(command, executable, report_directory, workspace, environment, *unused):
            observed.update(environment=environment, report=str(report_directory), executable=executable)
            return {"nativeInstallationAccepted": True, "independentPixelsVerified": True}
        with patch.object(helper, "native_host_platform", return_value="windows-x86_64"), \
             patch.object(helper, "exercise_native", side_effect=native):
            self.assertEqual(helper.perform_acceptance(path), 0)
        result = json.loads((path.parent / "worker-state.json").read_text())
        self.assertTrue(result["nativeInstallationAccepted"] and result["packageBytesUnchanged"])
        self.assertTrue(result["userStorageSentinelsUnchanged"])
        self.assertIn("漢字", observed["report"])
        self.assertTrue(Path(observed["environment"]["HOME"]).is_relative_to(path.parent))
        self.assertFalse(result["guiWorkflowAccepted"])

    def test_successful_native_claim_cannot_hide_package_mutation(self):
        path, _ = self.package_request()
        def mutate(command, executable, *unused):
            (executable.parent / "unlisted-file").write_bytes(b"mutation")
            return {"nativeInstallationAccepted": True}
        with patch.object(helper, "native_host_platform", return_value="windows-x86_64"), \
             patch.object(helper, "exercise_native", side_effect=mutate):
            self.assertEqual(helper.perform_acceptance(path), 1)
        result = json.loads((path.parent / "worker-state.json").read_text())
        self.assertEqual(result["status"], "failed")
        self.assertFalse(result["nativeInstallationAccepted"])

    def test_supervised_archive_failure_retains_honest_native_receipt(self):
        path, request = self.package_request()
        result = helper.run_acceptance(Path(request["archive"]), "0" * 64, "0.6.0",
            "windows-x86_64", "a" * 40, timeout=5, scratch_root=self.root)
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["acceptanceKind"], "package-native-installation")
        self.assertFalse(result["nativeInstallationAccepted"])
        self.assertFalse(result["guiRenderAccepted"])
        self.assertTrue(Path(result["workspace"]).is_dir())

    def test_native_claim_cannot_hide_explicit_storage_mutation(self):
        path, request = self.package_request()
        sentinel = self.root / "explicit-red.ini"
        sentinel.write_bytes(b"original user settings")
        request["userStorageSentinels"] = [str(sentinel)]
        path.write_text(json.dumps(request), encoding="utf-8")
        def mutate(command, executable, *unused):
            sentinel.write_bytes(b"changed user settings")
            return {"nativeInstallationAccepted": True}
        with patch.object(helper, "native_host_platform", return_value="windows-x86_64"), \
             patch.object(helper, "exercise_native", side_effect=mutate):
            self.assertEqual(helper.perform_acceptance(path), 1)
        result = json.loads((path.parent / "worker-state.json").read_text())
        self.assertFalse(result["nativeInstallationAccepted"])
        self.assertIn("sentinel changed", result["error"])

    def test_native_claim_cannot_hide_original_archive_mutation(self):
        path, request = self.package_request()
        def mutate(*unused):
            with Path(request["archive"]).open("ab") as stream:
                stream.write(b"changed original archive")
            return {"nativeInstallationAccepted": True}
        with patch.object(helper, "native_host_platform", return_value="windows-x86_64"), \
             patch.object(helper, "exercise_native", side_effect=mutate):
            self.assertEqual(helper.perform_acceptance(path), 1)
        result = json.loads((path.parent / "worker-state.json").read_text())
        self.assertFalse(result["nativeInstallationAccepted"])
        self.assertIn("Original archive changed", result["error"])

    def test_receipt_publication_is_create_only_and_failure_atomic(self):
        destination = self.root / "receipt-漢字-Δ.json"
        with patch.object(helper.os, "link", side_effect=OSError("synthetic link refusal")), \
             self.assertRaisesRegex(OSError, "link refusal"):
            helper.publish_receipt(destination, {"status": "failed"})
        self.assertFalse(destination.exists())
        self.assertFalse(list(self.root.glob(".red-native-receipt-*.tmp")))
        destination.write_bytes(b"another writer's receipt")
        with self.assertRaises(FileExistsError):
            helper.publish_receipt(destination, {"status": "passed"})
        self.assertEqual(destination.read_bytes(), b"another writer's receipt")
        self.assertFalse(list(self.root.glob(".red-native-receipt-*.tmp")))

    def test_cli_failure_receipt_and_existing_destination_refusal(self):
        _, request = self.package_request()
        receipt = self.root / "final-receipt.json"
        command = [sys.executable, "-B", str(SCRIPT), request["archive"], "--archive-sha256", "0" * 64,
            "--version", "0.6.0", "--platform", "windows-x86_64", "--commit", "a" * 40,
            "--receipt", str(receipt), "--scratch-root", str(self.root), "--timeout-seconds", "5"]
        result = subprocess.run(command, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 1)
        original = receipt.read_bytes()
        self.assertEqual(json.loads(original)["status"], "failed")
        result = subprocess.run(command, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(receipt.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
