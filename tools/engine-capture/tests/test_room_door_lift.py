import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


FIXTURE = Path(__file__).resolve().parents[1] / "fixtures" / "room-door-lift"


class RoomDoorLift(unittest.TestCase):
    def test_portable_project_exercises_packaged_door_and_lift(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "project"
            subprocess.run([sys.executable, "-B", str(FIXTURE / "make_project.py"), str(root)], check=True)
            profile = json.loads((root / "red-project.json").read_text())
            mount, = profile["mounts"]
            data = (root / mount["path"]).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), mount["sha256"])
            count, = struct.unpack_from("<I", data, 12)
            names = {struct.unpack_from("<II128s", data, 16 + i * 136)[2].split(b"\0")[0]
                     .decode().replace("\\", "/") for i in range(count)}
            for entry in ("cog/rlcontrol.cog", "3do/rlgate.3do", "3do/rlplatform.3do",
                          "mat/rlwall.mat", "misc/cmp/rlgray.cmp"):
                self.assertIn(entry, names)
            level = (root / profile["levels"][0]["export"]).read_text()
            self.assertIn("World adjoins 2", level)
            self.assertIn("rlcontrol.cog", level)
            scenario, = profile["scenarios"]
            steps = json.loads((root / scenario["definition"]).read_text())["steps"]
            self.assertTrue(all(s["op"] in {"wait", "assert", "checkpoint", "tap", "sleep"} for s in steps))
            names = [s["name"] for s in steps if s["op"] == "checkpoint"]
            self.assertEqual(names, ["spawn", "closed-door", "on-lift", "upper-stop"])
            self.assertEqual([s["key"] for s in steps if s["op"] == "tap" and s["key"] == "space"], ["space", "space"])
            self.assertTrue(any(s.get("expect", {}).get("player.position.2", {}).get("min", 0) > .3
                                for s in steps))


if __name__ == "__main__":
    unittest.main()
