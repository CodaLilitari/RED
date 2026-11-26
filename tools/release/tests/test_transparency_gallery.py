import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[2] / "engine-capture"
sys.path.insert(0, str(TOOLS))
from verify_episode_pair import open_edition, verify


class TransparencyGallery(unittest.TestCase):
    def test_edition_pair_and_single_change_controls(self):
        def build(case, edition):
            out = root / f"{case}-{edition}"
            subprocess.run([sys.executable, "-B", str(TOOLS / "fixtures/transparency-gallery/make_project.py"),
                            str(out), "--control", case, "--edition", str(edition)], check=True, timeout=20)
            return out / "viewlab.gob"

        def unpack(path):
            with open_edition(path) as (_, stream, members):
                result = {}
                for name, (offset, size) in members.items():
                    stream.seek(offset)
                    result[name] = stream.read(size)
                    self.assertEqual(result[name], (path.parent / name).read_bytes())
                return result

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            indexed = build("revised", 8)
            report = verify(indexed, build("revised", 16))
            self.assertEqual((report["sharedIndexedMaterials"], report["convertedMaterials"]), (1, 3))
            baseline = unpack(indexed)
            for case, expected in (("ordinary-rib", "jkl/viewlab.jkl"), ("no-shaft", "jkl/viewlab.jkl"),
                                   ("overlap-crest", "3do/vgsign.3do")):
                with self.subTest(control=case):
                    members = unpack(build(case, 8))
                    self.assertEqual(members.keys(), baseline.keys())
                    self.assertEqual({k for k in members if members[k] != baseline[k]}, {expected})


if __name__ == "__main__":
    unittest.main()
