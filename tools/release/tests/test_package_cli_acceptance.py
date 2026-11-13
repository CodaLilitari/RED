"""Synthetic archives/processes; no game data, SDK download, or native GUI."""
import hashlib
import importlib.util
import io
import json
import os
import subprocess
from pathlib import Path
import sys
import tarfile
import stat
import tempfile
import time
import unittest
import zipfile
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "accept_package_cli.py"
SPEC = importlib.util.spec_from_file_location("accept_package_cli", SCRIPT)
helper = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(helper)


class PackageAcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="red-package-acceptance-tests-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.deadline = time.monotonic() + 10

    def zip(self, entries):
        archive = self.root / "candidate.zip"
        with zipfile.ZipFile(archive, "w") as stream:
            for name, data in entries:
                stream.writestr(name, data)
        return archive

    def manifest(self, package, entry):
        document = {"schemaVersion": 1, "version": "0.6.0", "platform": "linux-x86_64",
                    "sourceCommit": "a" * 40, "files": [entry]}
        (package / "RELEASE_MANIFEST.json").write_text(json.dumps(document), encoding="utf-8")
        return document

    def test_portable_path_admission_rejects_escape_and_aliases(self):
        for name in ("../escape", "/absolute", "C:outside", "file:stream", "NUL.txt", "end.",
                     "space /file", "one\\two", "one//two", "./file", "one/../file"):
            with self.subTest(name=name), self.assertRaises(helper.AcceptanceError):
                helper.safe_parts(name)
        self.assertEqual(helper.safe_parts("usr/bin/red-agent"), ("usr", "bin", "red-agent"))

    def test_zip_traversal_refuses_without_escape(self):
        archive = self.zip([("RED/ok", b"ok"), ("../escape", b"bad")])
        with self.assertRaises(helper.AcceptanceError):
            helper.extract_archive(archive, self.root / "extract", self.deadline)
        self.assertFalse((self.root / "escape").exists())

    def test_archive_case_alias_and_file_parent_refuse(self):
        for index, entries in enumerate(([('RED/A', b'a'), ('RED/a', b'b')], [('RED/a', b'a'), ('RED/a/b', b'b')])):
            with self.subTest(entries=entries):
                archive = self.zip(entries)
                with self.assertRaises(helper.AcceptanceError):
                    helper.extract_archive(archive, self.root / f"extract{index}", self.deadline)

    def test_archive_unicode_normalization_alias_refuses_before_extraction(self):
        # HFS+/APFS can alias composed and decomposed Unicode spellings.
        archive = self.zip([("RED/caf\u00e9", b"a"), ("RED/cafe\u0301", b"b"),
                            ("RED/RELEASE_MANIFEST.json", b"{}")])
        with self.assertRaises(helper.AcceptanceError):
            helper.extract_archive(archive, self.root / "extract", self.deadline)
        self.assertFalse((self.root / "extract").exists())

    def test_tar_symlink_escape_and_hardlink_refuse(self):
        for kind, link in ((tarfile.SYMTYPE, "../../outside"), (tarfile.LNKTYPE, "RED/target")):
            archive = self.root / "candidate.tar.gz"
            with tarfile.open(archive, "w:gz") as stream:
                entry = tarfile.TarInfo("RED/link")
                entry.type, entry.linkname = kind, link
                stream.addfile(entry)
            with self.assertRaises(helper.AcceptanceError):
                helper.extract_archive(archive, self.root / "extract", self.deadline)

    def test_manifest_hash_and_exact_source_identity_are_checked(self):
        package = self.root / "package"
        package.mkdir()
        (package / "sample").write_bytes(b"actual")
        self.manifest(package, {"path": "sample", "type": "file", "size": 6, "sha256": "0" * 64})
        with self.assertRaises(helper.AcceptanceError):
            helper.verify_manifest(package, "0.6.0", "linux-x86_64", "a" * 40, self.deadline)
        with self.assertRaises(helper.AcceptanceError):
            helper.verify_manifest(package, "0.6.0", "linux-x86_64", "b" * 40, self.deadline)

    def test_manifest_undeclared_payload_refuses(self):
        package = self.root / "package"
        package.mkdir()
        (package / "sample").write_bytes(b"ok")
        (package / "surprise").write_bytes(b"unlisted")
        self.manifest(package, {"path": "sample", "type": "file", "size": 2,
                                "sha256": hashlib.sha256(b"ok").hexdigest()})
        with self.assertRaises(helper.AcceptanceError):
            helper.verify_manifest(package, "0.6.0", "linux-x86_64", "a" * 40, self.deadline)

    def test_environment_cannot_inherit_sdk_paths(self):
        environment = helper.clean_environment({"PATH": "F:/Qt/bin", "QT_PLUGIN_PATH": "sdk",
            "QT_QPA_PLATFORM_PLUGIN_PATH": "sdk", "LD_LIBRARY_PATH": "sdk", "DYLD_FALLBACK_LIBRARY_PATH": "sdk",
            "QML2_IMPORT_PATH": "sdk", "DISPLAY": ":17", "SystemRoot": "C:/Windows"})
        self.assertNotIn("Qt", environment["PATH"])
        for name in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "LD_LIBRARY_PATH",
                     "DYLD_FALLBACK_LIBRARY_PATH", "QML2_IMPORT_PATH"):
            self.assertNotIn(name, environment)
        self.assertEqual(environment["DISPLAY"], ":17")

    def test_stalled_process_has_actual_deadline(self):
        start = time.monotonic()
        with self.assertRaises(helper.AcceptanceError):
            helper.run_command([sys.executable, "-c", "import time; time.sleep(20)"], self.root,
                               os.environ.copy(), start + 0.2)
        self.assertLess(time.monotonic() - start, 3)

    def test_combined_stdout_stderr_limit_is_enforced(self):
        with self.assertRaises(helper.AcceptanceError):
            helper.run_command([sys.executable, "-c", "import os; os.write(1,b'a'*2048);os.write(2,b'b'*2048)"],
                               self.root, os.environ.copy(), self.deadline, log_limit=3000)

    def test_successful_command_preserves_structured_output(self):
        result = helper.run_command([sys.executable, "-c", "print('{\"ok\":true}')"], self.root,
                                    os.environ.copy(), self.deadline)
        self.assertEqual(result["exitCode"], 0)
        self.assertEqual(result["stdout"].strip(), b'{"ok":true}')

    def protocol_commands(self):
        script = self.root / "protocol_fixture.py"
        script.write_text('''import hashlib,json,os,sys
from pathlib import Path
role,args=sys.argv[1],sys.argv[2:]
bad=os.environ.get("SYNTHETIC_BAD", "")
if role=="mcp":
 for line in sys.stdin:
  try: request=json.loads(line)
  except ValueError:
   print(json.dumps({"jsonrpc":"2.0","id":None,"error":{"code":-32700}}));continue
  if "id" not in request: continue
  method=request["method"]
  result=({"protocolVersion":"2024-11-05","serverInfo":{"name":"sed-mcp","version":"0.1.0"},"capabilities":{"tools":{}}}
    if method=="initialize" else {"tools":[{"name":"sed_inspect"},{"name":"sed_diagnose"}]} if method=="tools/list" else {})
  print(json.dumps({"jsonrpc":"2.0","id":99 if bad=="rpc-id" else request["id"],"result":result}))
elif args[0]=="--help": print(json.dumps({"ok":True,"protocol":"sed-agent","schemaVersion":1,"usage":["red-agent new"]}))
elif args[0]=="schema": print(json.dumps({"type":"string","enum":["new","inspect","validate"],"x-commands":[{"name":"new"},{"name":"inspect"},{"name":"validate"}]}))
elif args[0]=="new":
 Path(args[1]).write_bytes(b"synthetic document")
 print(json.dumps({"ok":True,"command":"new","kind":"mots","outputSha256":hashlib.sha256(b"synthetic document").hexdigest()}))
elif args[0] in ("inspect","validate"):
 digest=hashlib.sha256(Path(args[1]).read_bytes()).hexdigest()
 print(json.dumps({"ok":True,"command":args[0],"kind":1,"inputSha256":digest,"sourceRevision":digest,"counts":{"sectors":1},"valid":bad!="invalid-document"}))
else:
 print(json.dumps({"ok":False,"exitCode":2}));sys.exit(2)
''', encoding="utf-8")
        return [sys.executable, str(script), "agent"], [sys.executable, str(script), "mcp"]

    def test_protocols_validate_fresh_fixture_and_handshake(self):
        agent, mcp = self.protocol_commands()
        records = []
        try:
            result = helper.exercise_protocols(agent, mcp, self.root, os.environ.copy(), self.deadline, records)
        except helper.AcceptanceError as error:
            self.fail(f"Valid source-aligned CLI/MCP protocol was refused: {error}")
        self.assertEqual(result.get("fixtureSha256"), hashlib.sha256(b"synthetic document").hexdigest())
        self.assertEqual(result.get("mcpProtocolVersion"), "2024-11-05")
        self.assertGreaterEqual(len(records), 7)

    def test_protocols_refuse_false_clean_report_and_wrong_rpc_ids(self):
        for index, bad in enumerate(("invalid-document", "rpc-id")):
            agent, mcp = self.protocol_commands()
            workspace = self.root / str(index)
            workspace.mkdir()
            environment = dict(os.environ, SYNTHETIC_BAD=bad)
            with self.subTest(bad=bad), self.assertRaises(helper.AcceptanceError):
                helper.exercise_protocols(agent, mcp, workspace, environment, self.deadline, [])

    def test_regular_zip_extracts_and_complete_manifest_verifies(self):
        data = b"project-owned payload"
        manifest = {"schemaVersion": 1, "version": "0.6.0", "platform": "linux-x86_64",
                    "sourceCommit": "a" * 40, "files": [{"path": "sample", "type": "file", "size": len(data),
                    "sha256": hashlib.sha256(data).hexdigest()}]}
        archive = self.zip([("RED/sample", data), ("RED/RELEASE_MANIFEST.json", json.dumps(manifest))])
        package = helper.extract_archive(archive, self.root / "extract", self.deadline)
        result = helper.verify_manifest(package, "0.6.0", "linux-x86_64", "a" * 40, self.deadline)
        self.assertEqual(result["fileCount"], 1)
        self.assertTrue(result["manifestSourceCommitVerified"])
        self.assertEqual((package / "sample").read_bytes(), data)

    def test_regular_pax_tar_extracts_and_complete_manifest_verifies(self):
        archive = self.root / "candidate.tar.gz"
        data = b"project-owned TAR payload"
        manifest = {"schemaVersion": 1, "version": "0.6.0", "platform": "linux-x86_64",
                    "sourceCommit": "a" * 40, "files": [{"path": "usr/bin/sample", "type": "file", "size": len(data),
                    "sha256": hashlib.sha256(data).hexdigest()}]}
        with tarfile.open(archive, "w:gz", format=tarfile.PAX_FORMAT) as stream:
            for name, content in (("RED/usr/bin/sample", data),
                                  ("RED/RELEASE_MANIFEST.json", json.dumps(manifest).encode())):
                info = tarfile.TarInfo(name)
                info.size, info.mode = len(content), 0o755
                info.pax_headers = {"comment": "bounded extension"}
                stream.addfile(info, io.BytesIO(content))
        package = helper.extract_archive(archive, self.root / "extract", self.deadline)
        result = helper.verify_manifest(package, "0.6.0", "linux-x86_64", "a" * 40, self.deadline)
        self.assertEqual(result["fileCount"], 1)
        self.assertEqual((package / "usr/bin/sample").read_bytes(), data)
        if os.name != "nt":
            self.assertTrue((package / "usr/bin/sample").stat().st_mode & stat.S_IXUSR)

    def test_supervisor_deadline_reports_failure_and_stops_owned_worker(self):
        worker = self.root / "stall.py"
        worker.write_text("import sys,time\nsys.stdin.readline()\ntime.sleep(20)\n", encoding="utf-8")
        start = time.monotonic()
        result = helper.supervise([sys.executable, str(worker)], self.root / "state.json", 0.25)
        self.assertEqual(result.get("status"), "failed")
        self.assertIn("deadline", result.get("error", "").lower())
        self.assertLess(time.monotonic() - start, 3)

    def test_archive_hash_refusal_retains_failure_receipt_and_source_bytes(self):
        archive = self.zip([("RED/unused", b"untouched")])
        before = archive.read_bytes()
        result = helper.run_acceptance(archive, "0" * 64, "0.6.0", "windows-x86_64", "a" * 40,
                                       timeout=5, scratch_root=self.root)
        self.assertEqual(result.get("status"), "failed")
        self.assertIn("sha-256", result.get("error", "").lower())
        self.assertIs(result.get("guiRenderAccepted"), False)
        self.assertEqual(archive.read_bytes(), before)

    def test_manifest_detects_extra_case_alias_after_execution(self):
        # Portable archive admission rejects aliases initially; a process may
        # introduce one later on a case-sensitive filesystem.
        package = self.root / "case-package"
        package.mkdir()
        (package / "sample").write_bytes(b"ok")
        self.manifest(package, {"path": "sample", "type": "file", "size": 2,
                                "sha256": hashlib.sha256(b"ok").hexdigest()})
        with patch.object(helper.os, "walk", return_value=iter([(str(package), [], ["sample", "SAMPLE", "RELEASE_MANIFEST.json"])])):
            with self.assertRaises(helper.AcceptanceError):
                helper.verify_manifest(package, "0.6.0", "linux-x86_64", "a" * 40, self.deadline)

    def test_windows_manifest_labels_caller_only_source_binding(self):
        package = self.root / "windows"
        package.mkdir()
        (package / "sample").write_bytes(b"ok")
        document = {"format": 1, "version": "0.6.0", "platform": "Windows", "manifest_self_excluded": True,
                    "files": [{"path": "sample", "bytes": 2, "sha256": hashlib.sha256(b"ok").hexdigest()}]}
        (package / "RELEASE_MANIFEST.json").write_text(json.dumps(document))
        result = helper.verify_manifest(package, "0.6.0", "windows-x86_64", "a" * 40, self.deadline)
        self.assertIs(result["manifestSourceCommitVerified"], False)

    def test_signature_failure_blocks_after_checking_companions(self):
        records = []
        with patch.object(helper, "run_command", side_effect=[
            {"exitCode": 0, "stdout": b"", "stderr": b"valid"},
            {"exitCode": 0, "stdout": b"", "stderr": b"valid"},
            {"exitCode": 1, "stdout": b"", "stderr": b"invalid companion"}]):
            with self.assertRaises(helper.AcceptanceError):
                helper.verify_signatures(self.root, self.root, {}, self.deadline, records)
        self.assertEqual([record["command"][-1] for record in records],
                         [str(self.root / "RED.app"), str(self.root / "red-agent"), str(self.root / "red-mcp")])
        self.assertIn("invalid companion", records[-1]["stderrPreview"])

    def test_supervisor_cleans_descendants_without_touching_peer(self):
        marker = self.root / "child-finished"
        worker = self.root / "spawn.py"
        child = f"import time;from pathlib import Path;time.sleep(2);Path({str(marker)!r}).write_text('escaped')"
        worker.write_text("import subprocess,sys,time\nsys.stdin.readline()\nsubprocess.Popen([sys.executable,'-c',"
                          + repr(child) + "])\ntime.sleep(20)\n", encoding="utf-8")
        peer = subprocess.Popen([sys.executable, "-c", "import time;time.sleep(20)"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            result = helper.supervise([sys.executable, str(worker)], self.root / "state.json", 0.5)
            self.assertEqual(result["status"], "failed")
            time.sleep(2.2)
            self.assertFalse(marker.exists())
            self.assertIsNone(peer.poll())
        finally:
            peer.kill()
            peer.wait(timeout=2)

    def test_receipt_is_create_only_and_failure_is_machine_readable(self):
        archive = self.zip([("RED/unused", b"untouched")])
        receipt = self.root / "receipt.json"
        command = [sys.executable, "-B", str(SCRIPT), str(archive), "--archive-sha256", "0" * 64,
                   "--version", "0.6.0", "--platform", "windows-x86_64", "--commit", "a" * 40,
                   "--receipt", str(receipt), "--scratch-root", str(self.root), "--timeout-seconds", "5"]
        first = subprocess.run(command, capture_output=True, timeout=10)
        self.assertEqual(first.returncode, 1, first.stderr)
        content = receipt.read_bytes()
        self.assertEqual(json.loads(content)["status"], "failed")
        self.assertFalse(json.loads(content)["guiRenderAccepted"])
        second = subprocess.run(command, capture_output=True, timeout=10)
        self.assertEqual(second.returncode, 2)
        self.assertEqual(receipt.read_bytes(), content)

    def test_size_budget_refuses_before_materializing_payload(self):
        archive = self.zip([("RED/large", b"12345")])
        with patch.object(helper, "MAX_FILE_BYTES", 4), self.assertRaises(helper.AcceptanceError):
            helper.extract_archive(archive, self.root / "extract", self.deadline)
        self.assertFalse((self.root / "extract").exists())

    def test_archive_metadata_is_bounded_before_general_parser(self):
        archive = self.zip([("RED/sample", b"ok")])
        with patch.object(helper, "MAX_METADATA_BYTES", 8), self.assertRaisesRegex(helper.AcceptanceError, "metadata"):
            helper.extract_archive(archive, self.root / "extract-zip", self.deadline)

    def test_tar_metadata_is_bounded_before_general_parser(self):
        archive = self.root / "pax.tar.gz"
        with tarfile.open(archive, "w:gz", format=tarfile.PAX_FORMAT,
                          pax_headers={"comment": "x" * 128}) as stream:
            info = tarfile.TarInfo("RED/sample")
            info.size = 2
            stream.addfile(info, io.BytesIO(b"ok"))
        with patch.object(helper, "MAX_METADATA_BYTES", 64), self.assertRaisesRegex(helper.AcceptanceError, "metadata"):
            helper.extract_archive(archive, self.root / "extract-tar", self.deadline)

    def test_windows_manifest_cannot_bypass_unix_commit_admission(self):
        package = self.root / "wrong-format"
        package.mkdir()
        (package / "sample").write_bytes(b"ok")
        document = {"format": 1, "version": "0.6.0", "platform": "linux-x86_64",
                    "files": [{"path": "sample", "bytes": 2, "sha256": hashlib.sha256(b"ok").hexdigest()}]}
        (package / "RELEASE_MANIFEST.json").write_text(json.dumps(document))
        with self.assertRaises(helper.AcceptanceError):
            helper.verify_manifest(package, "0.6.0", "linux-x86_64", "a" * 40, self.deadline)

    def test_internal_relative_symlink_extracts_and_verifies(self):
        probe = self.root / "symlink-capability"
        try:
            probe.symlink_to("probe-target")
        except OSError as error:
            if os.name == "nt" and getattr(error, "winerror", None) == 1314:
                self.skipTest("This Windows account lacks symlink privilege; native Unix package gate executes this case")
            raise
        probe.unlink()
        archive = self.root / "links.zip"
        manifest = {"schemaVersion": 1, "version": "0.6.0", "platform": "macos-arm64",
                    "sourceCommit": "a" * 40, "files": [
                        {"path": "Versions/A/payload", "type": "file", "size": 2,
                         "sha256": hashlib.sha256(b"ok").hexdigest()},
                        {"path": "payload", "type": "symlink", "target": "Versions/A/payload"}]}
        with zipfile.ZipFile(archive, "w") as stream:
            stream.writestr("RED/Versions/A/payload", b"ok")
            stream.writestr("RED/RELEASE_MANIFEST.json", json.dumps(manifest))
            link = zipfile.ZipInfo("RED/payload")
            link.create_system = 3
            link.external_attr = (stat.S_IFLNK | 0o777) << 16
            stream.writestr(link, "Versions/A/payload")
        package = helper.extract_archive(archive, self.root / "extract", self.deadline)
        result = helper.verify_manifest(package, "0.6.0", "macos-arm64", "a" * 40, self.deadline)
        self.assertEqual(result["fileCount"], 2)
        self.assertEqual((package / "payload").read_bytes(), b"ok")

    def test_framework_directory_symlink_chain_extracts_and_verifies(self):
        probe = self.root / "symlink-capability"
        try:
            probe.symlink_to("probe-target")
        except OSError as error:
            if os.name == "nt" and getattr(error, "winerror", None) == 1314:
                self.skipTest("This Windows account lacks symlink privilege; native Unix package gate executes this case")
            raise
        probe.unlink()
        archive = self.root / "framework.zip"
        files = {"QtCore.framework/Versions/A/QtCore": b"synthetic Mach-O placeholder",
                 "QtCore.framework/Versions/A/Resources/Info.plist": b"synthetic plist"}
        links = {"QtCore.framework/Versions/Current": "A",
                 "QtCore.framework/QtCore": "Versions/Current/QtCore",
                 "QtCore.framework/Resources": "Versions/Current/Resources"}
        entries = [{"path": name, "type": "file", "size": len(data),
                    "sha256": hashlib.sha256(data).hexdigest()} for name, data in files.items()]
        entries.extend({"path": name, "type": "symlink", "target": target} for name, target in links.items())
        manifest = {"schemaVersion": 1, "version": "0.6.0", "platform": "macos-arm64",
                    "sourceCommit": "a" * 40, "files": entries}
        with zipfile.ZipFile(archive, "w") as stream:
            for name, data in files.items():
                stream.writestr("RED/" + name, data)
            stream.writestr("RED/RELEASE_MANIFEST.json", json.dumps(manifest))
            for name, target in links.items():
                info = zipfile.ZipInfo("RED/" + name)
                info.create_system = 3
                info.external_attr = (stat.S_IFLNK | 0o777) << 16
                stream.writestr(info, target)
        package = helper.extract_archive(archive, self.root / "extract", self.deadline)
        result = helper.verify_manifest(package, "0.6.0", "macos-arm64", "a" * 40, self.deadline)
        self.assertEqual(result["fileCount"], 5)
        self.assertEqual((package / "QtCore.framework/QtCore").read_bytes(), files["QtCore.framework/Versions/A/QtCore"])
        self.assertEqual((package / "QtCore.framework/Resources/Info.plist").read_bytes(), b"synthetic plist")


if __name__ == "__main__":
    unittest.main()
