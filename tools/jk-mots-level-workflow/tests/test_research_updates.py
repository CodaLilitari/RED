"""Research updater tests: portable fixtures, no external network or credentials."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/research_updates.py"
spec = importlib.util.spec_from_file_location("research_updates", SCRIPT)
u = importlib.util.module_from_spec(spec)
spec.loader.exec_module(u)


class UpdateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "references").mkdir()
        self.old = b"Old tested method.\n"
        self.new = b"Corrected method with sources.\n"
        self.path = self.root / "references/method.md"
        self.path.write_bytes(self.old)
        self.package = {"version": "1.1.0", "files": [{"path": "references/method.md", "sha256": u.sha(self.old)}]}
        (self.root / "PACKAGE-MANIFEST.json").write_text(json.dumps(self.package))
        self.manifest = {"schema_version": 1, "scope": "research-only", "version": "1.1.0", "requires_skill_version": "1.1.0", "research_revision": 1,
                         "files": [{"path": "references/method.md", "bytes": len(self.new), "sha256": u.sha(self.new)}]}
        self.calls = []

    def fetch(self, url, limit):
        self.calls.append(url)
        return json.dumps(self.manifest).encode() if url == u.MANIFEST_URL else self.new

    def test_check_sends_no_local_data_and_writes_nothing(self):
        before = {p.name: p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        report = u.update(self.root, fetcher=self.fetch)
        self.assertFalse(report["applied"])
        self.assertEqual(report["changed_files"], ["references/method.md"])
        self.assertEqual(self.calls, [u.MANIFEST_URL])
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.root.rglob("*") if p.is_file()})

    def test_apply_then_update_again_uses_local_baseline(self):
        self.assertTrue(u.update(self.root, True, self.fetch)["applied"])
        self.assertEqual(self.path.read_bytes(), self.new)
        self.new = b"A further verified improvement.\n"
        self.manifest["research_revision"] = 2
        self.manifest["files"][0].update(bytes=len(self.new), sha256=u.sha(self.new))
        self.assertTrue(u.update(self.root, True, self.fetch)["applied"])
        self.assertEqual(self.path.read_bytes(), self.new)

    def test_preserves_local_edits(self):
        self.path.write_bytes(b"User annotation")
        with self.assertRaisesRegex(ValueError, "Local research edits"):
            u.update(self.root, True, self.fetch)
        self.assertEqual(self.path.read_bytes(), b"User annotation")
        self.assertEqual(self.calls, [u.MANIFEST_URL])

    def test_corrupt_download_leaves_all_files_unchanged(self):
        self.manifest["files"][0]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "hash/size"):
            u.update(self.root, True, self.fetch)
        self.assertEqual(self.path.read_bytes(), self.old)
        self.assertFalse((self.root / u.STATE_NAME).exists())

    def test_code_paths_traversal_duplicates_and_oversize_refused(self):
        for name in ["scripts/evil.py", "SKILL.md", "references/../../outside.md", "references/a%20.md", "references/a:stream.md", "references\\a.md", "references/folder./method.md", "references/CON.md", "references/LPT1.json"]:
            with self.subTest(name=name), self.assertRaises(ValueError):
                u.research_path(name)
        self.manifest["files"].append(dict(self.manifest["files"][0]))
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            u.validate_manifest(self.manifest)
        self.manifest["files"].pop()
        self.manifest["files"][0]["bytes"] = u.MAX_FILE + 1
        with self.assertRaisesRegex(ValueError, "size"):
            u.validate_manifest(self.manifest)

    def test_foreign_redirect_and_plain_http_refused(self):
        for url in ["http://" + u.BASE.split("://")[1], "https://example.com/research.json", u.BASE + "../outside.json", u.BASE + "file.json?key=secret", u.BASE + "a\\b.md", u.BASE + "a\nb.md"]:
            with self.subTest(url=url), self.assertRaises(ValueError):
                u.safe_url(url)

    def test_newer_required_skill_and_downgrade_refused(self):
        self.manifest["requires_skill_version"] = "1.2.0"
        self.assertFalse(u.update(self.root, fetcher=self.fetch)["compatible_skill"])
        with self.assertRaisesRegex(ValueError, "complete skill ZIP"):
            u.update(self.root, True, self.fetch)
        self.manifest["requires_skill_version"] = "1.1.0"
        (self.root / u.STATE_NAME).write_text(json.dumps({"schema_version": 1, "research_revision": 2, "files": []}))
        with self.assertRaisesRegex(ValueError, "downgrade"):
            u.update(self.root, True, self.fetch)

    def test_edit_during_download_preserved(self):
        def raced(url, limit):
            if url != u.MANIFEST_URL:
                self.path.write_bytes(b"An edit while network request was pending")
            return self.fetch(url, limit)
        with self.assertRaisesRegex(ValueError, "while downloading"):
            u.update(self.root, True, raced)
        self.assertEqual(self.path.read_bytes(), b"An edit while network request was pending")

    def test_handled_write_failure_rolls_back(self):
        original = u.os.replace
        def replace(a, b):
            if Path(b).name == u.STATE_NAME:
                raise OSError("simulated disk failure")
            return original(a, b)
        with mock.patch.object(u.os, "replace", replace), self.assertRaises(OSError):
            u.update(self.root, True, self.fetch)
        self.assertEqual(self.path.read_bytes(), self.old)
        self.assertFalse((self.root / u.STATE_NAME).exists())

    def test_deleted_baseline_file_is_a_local_conflict(self):
        self.path.unlink()
        report = u.update(self.root, fetcher=self.fetch)
        self.assertEqual(report["local_conflicts"], ["references/method.md"])
        with self.assertRaisesRegex(ValueError, "Local research edits"):
            u.update(self.root, True, self.fetch)
        self.assertFalse(self.path.exists())

    def test_rollback_does_not_overwrite_concurrent_edit(self):
        original = u.os.replace
        def replace(a, b):
            if Path(b).name == u.STATE_NAME:
                self.path.write_bytes(b"A user's new edit after replacement")
                raise OSError("simulated state-write failure")
            return original(a, b)
        with mock.patch.object(u.os, "replace", replace), self.assertRaisesRegex(ValueError, "Rollback preserved"):
            u.update(self.root, True, self.fetch)
        self.assertEqual(self.path.read_bytes(), b"A user's new edit after replacement")

    def test_fallback_uses_its_own_payload_base(self):
        from urllib.error import URLError
        calls=[]
        def alternate(url,limit):
            calls.append(url)
            if url==u.MANIFEST_URL: raise URLError('primary unavailable')
            if url==u.MANIFEST_URLS[1]: return json.dumps(self.manifest).encode()
            self.assertEqual(url,u.FALLBACK_BASE+'references/method.md')
            return self.new
        report=u.update(self.root,True,alternate)
        self.assertTrue(report['applied'])
        self.assertEqual(report['source'],u.MANIFEST_URLS[1])
        self.assertEqual(len(calls),3)

    def test_invalid_manifest_does_not_trigger_fallback(self):
        self.manifest['scope']='execute-code'
        with self.assertRaises(ValueError): u.update(self.root,True,self.fetch)
        self.assertEqual(self.calls,[u.MANIFEST_URL])

    def test_source_repository_prefix_escape_refused(self):
        for url in [u.FALLBACK_BASE+'../secret.json',u.FALLBACK_BASE.replace('/tools/jk-mots-level-workflow/','/other/'),'https://gitlab.com/other/project/-/raw/main/updates/x.json']:
            with self.subTest(url=url),self.assertRaises(ValueError): u.safe_url(url)


if __name__ == "__main__":
    unittest.main()
