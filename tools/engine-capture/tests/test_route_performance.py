import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from route_performance import compare, ProfileError


def sha(data):
    return hashlib.sha256(data).hexdigest()


class RoutePerformance(unittest.TestCase):
    def test_paired_tail_noise_failed_behavior_and_target_mismatch(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            observed = {"renderer": "synthetic test device", "driver": "fixture", "hardwareClass": "synthetic",
                        "cpu": "synthetic", "resolution": [640, 480], "vsync": False, "fpsCap": 0,
                        "physics": "normal", "clock": "native pre-swap monotonic nanoseconds"}
            trials = []
            def write(path, data):
                data = data if isinstance(data, bytes) else json.dumps(data).encode()
                (root / path).write_bytes(data)
                return {"path": path, "sha256": sha(data)}
            for route in ("stationary", "walk"):
                for geometry in ("model-scene", "hybrid-sectors"):
                    for cache in ("off", "on"):
                        for repeat in range(3):
                            key = f"{route}-{geometry}-{cache}-{repeat}"
                            dt = (18 if geometry == "model-scene" else 12) + repeat * .2
                            fields = "frame,ns,sector,x,y,z,visibleSectors,sectorFrustums\n"
                            for i in range(33):
                                y = i * .02 if route == "walk" else 0
                                fields += f"{i},{int(i*dt*1e6)},0,0,{y},0,1,1\n"
                            trace = write(key + ".csv", fields.encode())
                            package = sha(geometry.encode())
                            engine = sha(b"engine")
                            report = {"status": "completed", "captureMode": "gameplay", "processExitCode": 0,
                                      "immutableInputsUnchanged": True, "episodeArchive": {"archiveSha256": package,
                                      "looseEpisodeFilesStaged": False}, "gameplay": {"status": "passed"}}
                            manifest = [{"role":"engine","sha256":engine},{"role":"config","destination":"config.json","sha256":sha(b'c')}]
                            scenario = {"status":"passed","steps":[{"op":"checkpoint","name":name,"status":"passed",
                                        "state":{"renderedFrame":idx,"player":{"position":[0,idx*.02 if route=="walk" else 0,0],"headPYR":[0,0,0]}}}
                                        for name,idx in (("warmup",0),("begin",6),("end",32))]}
                            outcome = write(key+"s.json",scenario)
                            report["gameplay"].update(scenarioSha256=outcome["sha256"], scenarioInputSha256=sha(route.encode()))
                            trials.append({"route":route,"geometry":geometry,"cache":cache,"repeat":repeat+1,
                                 "target":observed,"packageSha256":package,"engineSha256":engine,
                                 "recipeSha256":sha(route.encode()),"frames":trace,"launcher":write(key+"l.json",report),
                                 "scenario":outcome,"staged":write(key+"m.json",manifest),
                                 "nativeTarget":write(key+"target",b"renderer=synthetic test device\ndriver=fixture\nwidth=640\nheight=480\nswapInterval=0\nfpsCap=0\n"),
                                 "observedCache":write(key+"cache",cache.encode())})
            plan={"schemaVersion":1,"title":"Fixture","runs":trials}
            result=compare(plan,root)
            self.assertEqual(len(result["conditions"]),8)
            self.assertTrue(any(row["exceedsRepeatSpread"] for row in result["comparisons"]))
            self.assertEqual(result["hardwareBenchmark"],False)
            self.assertIsNone(result["conditions"][0]["runs"][0]["gpuTimeMs"])
            proof = write("coverage.txt", b"Fixture review: intended opening is missing.")
            checks = {k:{"result":"passed","evidence":proof} for k in ("silhouettes","transparentOpenings","sourceCoverage","gameplayCollision")}
            checks["transparentOpenings"]["result"] = "failed"
            plan["optimizationReviews"] = [write("review.json", {"baselinePackageSha256":sha(b"model-scene"),
                    "candidatePackageSha256":sha(b"hybrid-sectors"),"checks":checks})]
            rejected = compare(plan, root)["comparisons"][0]
            self.assertTrue(rejected["exceedsRepeatSpread"])
            self.assertEqual(rejected["optimizationStatus"], "rejected or incomplete")
            bad=trials[-1]; original=bad["launcher"];failed={"status":"failed"}
            bad["launcher"]=write("failed.json",failed)
            result=compare(plan,root)
            self.assertFalse(any(c["exceedsRepeatSpread"] for c in result["comparisons"] if c["route"]=="walk" and c["candidate"]=="hybrid-sectors/on"))
            bad["launcher"]=original
            bad["target"]={**observed,"resolution":[320,240]}
            with self.assertRaises(ProfileError):
                compare(plan,root)
            bad["target"]=observed
            bad["frames"]["sha256"]="0"*64
            with self.assertRaises(ProfileError):
                compare(plan,root)


if __name__ == "__main__":
    unittest.main()
