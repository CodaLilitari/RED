import sys
import unittest
import json
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compare_cog_trace import compare


class DispatchComparison(unittest.TestCase):
    def test_recorded_jk_handler_delivery_and_return_trip(self):
        path = Path(__file__).resolve().parents[3] / "tests/fixtures/behavior-workbench/stock-door-lift.json"
        witness = json.loads(path.read_text(encoding="utf-8"))
        trace = "\n".join(witness["acceptedEngineDispatch"])
        for script, observed in witness["simulator"].items():
            self.assertTrue(compare(observed, trace, script)["passed"], script)
        closed, bottom, top, returned = witness["ordinaryControlCheckpoints"]
        self.assertEqual([point["sector"] for point in (closed, bottom, top, returned)], [0, 1, 1, 1])
        self.assertGreater(top["position"][2] - bottom["position"][2], 0.5)
        self.assertLess(abs(returned["position"][2] - bottom["position"][2]), 0.002)

    def test_selected_handlers_and_references_must_match_in_order(self):
        checks = {"passed": True, "breakpoints": [
            {"message": "activate", "senderType": 3, "senderRef": 7},
            {"message": "arrived", "senderType": 3, "senderRef": 7}]}
        log = "RED_R11_MESSAGE\tcog\\other.cog\t3\t0\t0\n" + (
            "RED_R11_MESSAGE\tcog\\door.cog\t1\t3\t7\n"
            "RED_R11_MESSAGE\tcog\\door.cog\t11\t3\t7\n")
        self.assertTrue(compare(checks, log, "door.cog")["passed"])
        self.assertFalse(compare(checks, log.replace("\t11\t3\t7", "\t11\t3\t8"), "door.cog")["passed"])
        self.assertFalse(compare(checks, "\n".join(reversed(log.splitlines())), "door.cog")["passed"])
        self.assertFalse(compare(checks, log, "missing.cog")["passed"])
        self.assertFalse(compare({**checks,"passed":False}, log, "door.cog")["passed"])

if __name__ == "__main__":
    unittest.main()
