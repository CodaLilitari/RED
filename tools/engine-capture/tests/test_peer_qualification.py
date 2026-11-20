import sys
import unittest
import json
import tempfile
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from peer_qualification import movement, qualify


class PeerMovement(unittest.TestCase):
    def test_local_target_recording_and_broken_link(self):
        example=Path(__file__).resolve().parents[1]/"examples"/"peer-profile"/"observed-loopback.zip"
        with tempfile.TemporaryDirectory() as directory, zipfile.ZipFile(example) as archive:
            root=Path(directory)
            for record in archive.infolist():
                path=root / record.filename
                self.assertTrue(path.resolve().is_relative_to(root.resolve()))
                self.assertLess(record.file_size, 1<<20)
                path.parent.mkdir(parents=True,exist_ok=True)
                path.write_bytes(archive.read(record))
            plan=json.loads((root/"profile.json").read_text())
            result=qualify(plan,root)
            self.assertTrue(result["passed"])
            self.assertEqual(result["transport"],"private-loopback")
            proof=result["observations"]
            self.assertFalse(proof["brokenSyncRejected"]["synchronizationAssertion"]["passed"])
            self.assertEqual(proof["thrownContact"]["observed"]["host"],[("+grenade1",34,307,-1)])
            self.assertEqual(proof["thrownContact"]["observed"]["guest"],[("+grenade1",34,307,-1)])
            plan["endpoints"]["guest"]["observations"]=plan["endpoints"]["host"]["observations"]
            with self.assertRaisesRegex(ValueError,"role"):
                qualify(plan,root)

    def test_bilateral_view_and_broken_link(self):
        def samples(xs):
            return [{"players":[{"id":7,"position":[x,0,0],"sector":3}]} for x in xs]
        local=samples([0,.1,.2,.3])
        good=movement(local,samples([0,.08,.18,.3]),7)
        self.assertTrue(good["passed"])
        stale=movement(local,samples([0,0,0,0]),7)
        self.assertFalse(stale["passed"])
        self.assertTrue(stale["movedLocally"])
        reversed_path=movement(local,samples([0,-.1,-.2,-.3]),7)
        self.assertFalse(reversed_path["passed"])


if __name__=="__main__":
    unittest.main()
