"""Release comparison contracts, using owned v0x14 archives and real ZIPs."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

from test_archive_gameplay import gob_bytes


TOOL = Path(__file__).resolve().parents[1] / "compare_episode_releases.py"


def identity(data):
    return {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


class ReleaseDiffTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.base = Path(temp.name) / "Arena.GOB"
        self.candidate = Path(temp.name) / "edition.zip"
        self.base.write_bytes(gob_bytes([("fixed", b"")]))

    def command(self, *paths, code):
        run = subprocess.run([sys.executable, "-B", str(TOOL), *paths],
                             capture_output=True, timeout=10)
        self.assertEqual(run.returncode, code, run.stderr or run.stdout)
        return json.loads(run.stdout)

    def wrap(self, entries):
        with zipfile.ZipFile(self.candidate, "w", zipfile.ZIP_DEFLATED) as z:
            for name, data in entries:
                z.writestr(name, data)

    def test_sorted_payload_delta_and_rename(self):
        old = gob_bytes([("XYZ", b"x"), ("COG\\door.cog", b"old"), ("stable", b""), ("abc", b"a")])
        new = gob_bytes([("xyz", b"y"), ("cog/DOOR.COG", b"new"), ("STABLE", b""), ("beta", b"b")])
        self.base.write_bytes(old)
        self.wrap([("Docs/lisez-moi.txt", b"documentation"), ("Episode/Arena2.goo", new)])
        self.base.chmod(0o444)
        self.candidate.chmod(0o444)
        result = self.command(self.base, self.candidate, code=1)
        self.assertEqual(result["classification"], "payloadDifferences")
        self.assertTrue(result["installNameChanged"])
        self.assertEqual(result["unchanged"], 1)
        self.assertEqual(result["baseline"], {"name": "Arena.GOB", **identity(old)})
        self.assertEqual(result["candidate"], {"name": "Arena2.goo", **identity(new)})
        self.assertEqual(result["added"], [{"name": "beta", "candidate": identity(b"b")}])
        self.assertEqual(result["removed"], [{"name": "abc", "baseline": identity(b"a")}])
        self.assertEqual(hashlib.sha256(self.base.read_bytes()).hexdigest(), result["baseline"]["sha256"])
        self.assertEqual(result["changed"], [
            {"name": "cog/door.cog", "baseline": identity(b"old"), "candidate": identity(b"new")},
            {"name": "xyz", "baseline": identity(b"x"), "candidate": identity(b"y")}])

    def test_repacked_archive_vs_wrapper_only_and_renamed_identical_bytes(self):
        old = gob_bytes([("COG\\door.cog", b"x"), ("map", b"y")])
        self.base.write_bytes(old)
        new = gob_bytes([("map", b"y"), ("cog/door.cog", b"x")])
        self.wrap([("readme", b"notes"), ("episodes/arena.gob", new)])
        diff = self.command(self.base, self.candidate, code=0)
        self.assertEqual(diff["classification"], "repacked")
        self.assertFalse(diff["installNameChanged"])
        self.assertEqual(diff["unchanged"], 2)
        self.wrap([("docs/notes", b"new notes"), ("arena.gob", old)])
        self.assertEqual(self.command(self.base, self.candidate, code=0)["classification"], "byteIdentical")
        renamed = self.base.with_name("original.goo")
        renamed.write_bytes(old)
        result = self.command(self.base, renamed, code=1)
        self.assertEqual(result["classification"], "byteIdentical")
        self.assertTrue(result["installNameChanged"])

    def test_binary_archive_comment_preserves_selected_episode(self):
        for comment in (b"notes PK\x05\x06", b"PK\x05\x06" + bytes(50) + b"PK\x05\x06",
                        b"x" * 65531 + b"PK\x05\x06"):
            with self.subTest(length=len(comment)):
                with zipfile.ZipFile(self.candidate, "w", zipfile.ZIP_DEFLATED) as wrapper:
                    wrapper.writestr("arena.gob", self.base.read_bytes())
                    wrapper.comment = comment
                original = self.candidate.read_bytes()
                report = self.command(self.base, self.candidate, code=0)
                self.assertEqual(report["classification"], "byteIdentical")
                self.assertEqual(report["candidate"]["sha256"], hashlib.sha256(self.base.read_bytes()).hexdigest())
                self.assertEqual(self.candidate.read_bytes(), original)
                self.candidate.write_bytes(original[:-1])
                self.assertEqual(set(self.command(self.base, self.candidate, code=2)), {"error"})

    def test_no_partial_comparison_on_invalid_input_or_usage(self):
        invalid = self.candidate.with_suffix(".goo")
        good = bytearray(self.base.read_bytes())
        for data in [b"", good[:20], gob_bytes([]),
                     gob_bytes([("dir/../file", b"a")]),
                     gob_bytes([("COG\\thing", b"a"), ("cog/thing", b"b")]),
                     good[:12] + struct.pack("<I", 65537) + good[16:]]:
            with self.subTest(header=data[:20]):
                invalid.write_bytes(data)
                self.assertEqual(set(self.command(self.base, invalid, code=2)), {"error"})
        self.assertEqual(set(self.command(self.base, code=2)), {"error"})

    def test_no_archive_multiple_archives_and_unsafe_zip_names(self):
        raw = self.base.read_bytes()
        for entries in [[], [("readme.txt", b"")], [("one.gob", raw), ("two.goo", raw)],
                        [("../one.gob", raw)], [("one.gob", raw), ("../notes", b"x")]]:
            with self.subTest(names=[n for n, _ in entries]):
                self.wrap(entries)
                self.assertEqual(set(self.command(self.base, self.candidate, code=2)), {"error"})

    def test_index_after_data_and_large_multiblock_payload(self):
        from test_verify_episode_pair import package
        big = b"0123456789abcdef" * 140000
        files = {"jkl/arena.jkl": big}
        package(self.base, files, index_at_end=True)
        self.wrap([("Arena.GOB", gob_bytes(list(files.items())))])
        result = self.command(self.base, self.candidate, code=0)
        self.assertEqual(result["classification"], "repacked")
        self.assertEqual(result["unchanged"], 1)

    def test_payload_ranges_and_names(self):
        raw = gob_bytes([("first", b"abcd"), ("second", b"efgh")])
        for offset, value in [(16, 0), (16, 12), (152, len(raw)), (152, len(raw) - 5)]:
            with self.subTest(offset=offset, value=value):
                damaged = bytearray(raw)
                struct.pack_into("<I", damaged, offset, value)
                self.base.write_bytes(damaged)
                self.assertEqual(set(self.command(self.base, self.base, code=2)), {"error"})
        for name in ["a\x7fb", "x" * 128, "file//bar", "c:/drive"]:
            with self.subTest(name=name):
                self.base.write_bytes(gob_bytes([(name, b"")]))
                self.assertEqual(set(self.command(self.base, self.base, code=2)), {"error"})

    def test_limits_enforced_at_capture_and_index(self):
        from unittest.mock import patch
        import compare_episode_releases as helper
        cases = [("MAX_ARCHIVE", self.base.stat().st_size - 1, self.base),
                 ("MAX_ZIP", 50, self.candidate),
                 ("MAX_ZIP_DIRECTORY", 46, self.candidate),
                 ("MAX_ZIP_MEMBERS", 1, self.candidate),
                 ("MAX_GOB_MEMBERS", 0, self.base)]
        self.wrap([("notes", b""), ("arena.gob", self.base.read_bytes())])
        for setting, value, path in cases:
            with self.subTest(limit=setting), patch.object(helper, setting, value):
                with self.assertRaises(ValueError):
                    helper.inspect_release(path)
        with patch.object(helper, "source_identity", side_effect=[(self.base.stat().st_size, 0, 0),
                                                                  (self.base.stat().st_size, 1, 1)]):
            with self.assertRaisesRegex(ValueError, "changed"):
                helper.inspect_release(self.base)

    def test_zip_crc_counts_unsafe_names_and_unsupported_compression(self):
        raw = self.base.read_bytes()
        self.wrap([("arena.gob", raw)])
        original = self.candidate.read_bytes()
        end = original.rindex(b"PK\x05\x06")
        central = original.index(b"PK\x01\x02")
        for offset, packing, value in [
            (end + 8, "<HH", (2, 2)), (end + 10, "<H", (4097,)), (end + 12, "<I", (2**32-1,)),
            (central + 16, "<I", (0,)), (central + 10, "<H", (99,)),
            (central + 8, "<H", (1,)), (central + 24, "<I", (len(raw) + 5,)),
            (central + 24, "<I", (len(raw) - 1,)), (central + 46, "B", (255,))]:
            with self.subTest(offset=offset, value=value):
                damaged = bytearray(original)
                struct.pack_into(packing, damaged, offset, *value)
                self.candidate.write_bytes(damaged)
                self.assertEqual(set(self.command(self.base, self.candidate, code=2)), {"error"})
        self.candidate.write_bytes(original[:-1])
        self.assertEqual(set(self.command(self.base, self.candidate, code=2)), {"error"})
        self.wrap([("notes", b"first"), ("NOTES", b"other"), ("arena.gob", raw)])
        self.assertEqual(set(self.command(self.base, self.candidate, code=2)), {"error"})

    def test_zip_companion_contents_outside_comparison(self):
        # Deliberately break the unrelated readme's CRC; comparing the episode must
        # neither inflate it nor claim to have established its integrity.
        raw = self.base.read_bytes()
        with zipfile.ZipFile(self.candidate, "w", zipfile.ZIP_STORED) as z:
            z.writestr("説明.txt", b"UNIQUE_NOTES")
            z.writestr("arena.gob", raw)
        self.candidate.write_bytes(self.candidate.read_bytes().replace(b"UNIQUE_NOTES", b"EDITED_NOTES"))
        result = self.command(self.base, self.candidate, code=0)
        self.assertEqual(result["classification"], "byteIdentical")
        self.assertEqual(result["inputs"], {"baseline": "Arena.GOB", "candidate": "edition.zip"})

    def test_refuses_ambiguous_zip_alternate_path_and_truncated_directory_comment(self):
        import zlib
        info = zipfile.ZipInfo("arena.gob")
        alternate = struct.pack("<BI", 1, zlib.crc32(b"arena.gob")) + b"../other.goo"
        info.extra = struct.pack("<HH", 0x7075, len(alternate)) + alternate
        with zipfile.ZipFile(self.candidate, "w") as z:
            z.writestr(info, self.base.read_bytes())
        self.assertEqual(set(self.command(self.base, self.candidate, code=2)), {"error"})
        self.wrap([("arena.gob", self.base.read_bytes())])
        data = bytearray(self.candidate.read_bytes())
        struct.pack_into("<H", data, data.index(b"PK\x01\x02") + 32, 12)
        self.candidate.write_bytes(data)
        self.assertEqual(set(self.command(self.base, self.candidate, code=2)), {"error"})

    def test_selected_local_header_matches_directory(self):
        self.wrap([("arena.gob", self.base.read_bytes())])
        raw = self.candidate.read_bytes()
        directory = raw.index(b"PK\x01\x02")
        for offset, fmt, value in [(6, "<H", 1), (8, "<H", 0), (14, "<I", 0),
                                   (18, "<I", 1), (22, "<I", 1), (directory + 34, "<H", 1)]:
            with self.subTest(offset=offset):
                data = bytearray(raw)
                struct.pack_into(fmt, data, offset, value)
                self.candidate.write_bytes(data)
                self.assertEqual(set(self.command(self.base, self.candidate, code=2)), {"error"})

    def test_streaming_zip_data_descriptor(self):
        import io

        class NonSeekable(io.BytesIO):
            def seek(self, *args):
                raise io.UnsupportedOperation()

        output = NonSeekable()
        with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("arena.gob", self.base.read_bytes())
        raw = output.getvalue()
        self.candidate.write_bytes(raw)
        self.assertEqual(self.command(self.base, self.candidate, code=0)["classification"], "byteIdentical")
        data = bytearray(raw)
        descriptor = data.index(b"PK\x07\x08")
        struct.pack_into("<I", data, descriptor + 4, 0)
        self.candidate.write_bytes(data)
        self.assertEqual(set(self.command(self.base, self.candidate, code=2)), {"error"})


    def test_stdout_is_reproducible_across_hash_seeds(self):
        self.base.write_bytes(gob_bytes([("z", b"1"), ("m", b"2"), ("a", b"3")]))
        self.wrap([("arena.gob", gob_bytes([("z", b"2"), ("m", b"4"), ("a", b"5")]))])
        outputs = []
        for seed in ["17", "23"]:
            run = subprocess.run([sys.executable, "-B", str(TOOL), self.base, self.candidate],
                                 env={**os.environ, "PYTHONHASHSEED": seed},
                                 capture_output=True, timeout=10)
            self.assertEqual(run.returncode, 1)
            outputs.append(run.stdout)
        self.assertEqual(outputs[0], outputs[1])
