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
        def samples(xs, step_ms=50):
            return [{"ms":i*step_ms,"players":[{"id":7,"position":[x,0,0] if not isinstance(x,list) else x,"sector":3}]}
                    for i,x in enumerate(xs)]
        local=samples([0,.1,.2,.3])
        good=movement(local,samples([0,.08,.18,.3]),7)
        self.assertTrue(good["passed"])
        self.assertTrue(good["endpointAgreement"])
        self.assertEqual(good["routeAgreement"],"agrees")
        stale=movement(local,samples([0,0,0,0]),7)
        self.assertFalse(stale["passed"])
        self.assertTrue(stale["movedLocally"])
        reversed_path=movement(local,samples([0,-.1,-.2,-.3]),7)
        self.assertFalse(reversed_path["passed"])

    def test_matching_endpoints_do_not_hide_a_detour(self):
        # Lily's counterexample: the remote observer wanders 4 JKU and returns to the same final position.
        def samples(points, step_ms=50):
            return [{"ms":i*step_ms,"players":[{"id":7,"position":p,"sector":3}]} for i,p in enumerate(points)]
        straight=[[x/10,0,0] for x in range(4)]
        detour=[[0,0,0]]+[[.1,y/10,0] for y in range(1,41)]+[[.2,y/10,0] for y in range(40,-1,-1)]+[[.3,0,0]]
        wandered=movement(samples(straight),samples(detour,10),7)
        self.assertTrue(wandered["endpointAgreement"])
        self.assertEqual(wandered["routeAgreement"],"diverges")
        self.assertGreater(wandered["routeDeviation"],3.9)
        self.assertFalse(wandered["passed"])
        # Going forward, back and forward again along the same line stays near the route but is not the route.
        back_and_forth=movement(samples([[x/10,0,0] for x in (0,1,2,3)]),
                                samples([[x/10,0,0] for x in (0,1,2,3,2,1,2,3)]),7)
        self.assertEqual(back_and_forth["routeAgreement"],"diverges")
        self.assertFalse(back_and_forth["passed"])
        # Two remote positions 400 ms apart cannot rule out a detour between them.
        sparse=movement(samples(straight),samples([[0,0,0],[.15,0,0],[.3,0,0]],400),7)
        self.assertEqual(sparse["routeAgreement"],"unverified")
        self.assertFalse(sparse["passed"])
        untimed=movement([{"players":[{"id":7,"position":p,"sector":3}]} for p in straight],samples(straight),7)
        self.assertEqual(untimed["routeAgreement"],"unverified")
        self.assertFalse(untimed["passed"])


if __name__=="__main__":
    unittest.main()
