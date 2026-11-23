"""Exercise compiled RED commands and compare candidates using Blender-Sith's portable loader."""
import argparse
import copy
from enum import Enum
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys


class ReadbackFailure(RuntimeError):
    """A check failed. Raised explicitly so python -O cannot skip a check, as it would an assert."""


def require(condition, message):
    if not condition:
        raise ReadbackFailure(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def plain(value):
    if isinstance(value, Enum):
        return plain(value.value)
    if value is None or isinstance(value, (str, int, float, bool)):
        return value
    if isinstance(value, (list, tuple)):
        return [plain(item) for item in value]
    if isinstance(value, dict):
        return {key: plain(item) for key, item in value.items() if key != "model_name"}
    return plain(vars(value))


def run(agent, *arguments):
    result = subprocess.run([str(agent), *map(str, arguments)], text=True, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {arguments}\n{result.stdout}\n{result.stderr}")
    return json.loads(result.stdout)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def check_candidate(loader, source, candidate, operations):
    baseline, before_version = loader(source, allowErrors=False)
    actual, after_version = loader(candidate, allowErrors=False)
    require(before_version == after_version, "The candidate changed the 3DO version")
    expected = copy.deepcopy(plain(baseline))
    for operation in operations:
        mesh = expected["geoset_list"][operation["geoset"]]["mesh_list"][operation["sourceMesh"]]
        if operation["op"] == "setTextureVertex":
            mesh["tv"][operation["textureVertex"]] = [
                struct.unpack("<f", struct.pack("<f", operation[key]))[0] for key in ("u", "v")
            ]
        else:
            scale = Fraction(operation["scale"])
            translation = [Fraction(component) for component in operation["translation"]]
            mesh["v"] = [[float(scale * Fraction(component) + translation[axis])
                          for axis, component in enumerate(vertex)] for vertex in mesh["v"]]
    require(plain(actual) == expected, "Independent loader found a changed or unpreserved model field")
    return {
        "version": float(after_version),
        "geosets": len(actual.geosets),
        "meshes": sum(len(geoset.meshes) for geoset in actual.geosets),
        "positionRows": sum(len(mesh.vertices) for geoset in actual.geosets for mesh in geoset.meshes),
        "uvRows": sum(len(mesh.uvs) for geoset in actual.geosets for mesh in geoset.meshes),
        "allOtherParsedFieldsEqual": True,
    }


def exercise(agent, loader, output_dir, source, name, operations):
    inspection = run(agent, "model-native-inspect", source)
    require(inspection["inputSha256"] == digest(source), "Inspection hashed a different source")
    require(inspection["runtimeVerified"] is False, "Inspection claimed runtime verification")
    request = {"schemaVersion": 1, "profile": "classic2.1", "inputSha256": digest(source),
               "operations": operations}
    request_path = output_dir / f"{name}-request.json"
    write_json(request_path, request)
    plan = run(agent, "model-native-plan", source, request_path)
    require(plan["written"] is False, "Planning wrote a file")
    candidate = output_dir / f"{name}.3do"
    require(not candidate.exists(), "The candidate path already exists")
    receipt = run(agent, "model-native-apply", source, request_path,
                  plan["requestSha256"], plan["outputSha256"], candidate)
    require(receipt["written"] is True, "Apply did not report a written candidate")
    require(receipt["inputSha256"] == inspection["inputSha256"] == digest(source), "The receipt names a different source")
    require(receipt["outputSha256"] == plan["outputSha256"] == digest(candidate), "The written candidate does not match the plan")
    require(receipt["requestSha256"] == plan["requestSha256"], "The receipt names a different request")
    readback = check_candidate(loader, source, candidate, operations)
    write_json(output_dir / f"{name}-plan.json", plan)
    write_json(output_dir / f"{name}-receipt.json", receipt)
    return candidate, {"name": name, "sourceSha256": digest(source), "candidateSha256": digest(candidate),
                       "requestSha256": receipt["requestSha256"], "independentReadback": readback}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("agent", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument("blender_sith", type=Path)
    parser.add_argument("output_directory", type=Path)
    args = parser.parse_args()
    args.output_directory.mkdir(parents=True, exist_ok=False)
    sys.path.insert(0, str(args.blender_sith))
    from sith.model.model3doLoader import load3do

    original_digest = digest(args.source)
    uv_operations = [{"op": "setTextureVertex", "geoset": 1, "sourceMesh": 1,
                      "textureVertex": 2, "u": -16.5, "v": 35.25}]
    shape_operations = [{"op": "transformMesh", "geoset": 1, "sourceMesh": 1,
                         "scale": 1.25, "translation": [0.125, -0.25, 0.5], "radiusPolicy": "preserve"}]
    uv_candidate, uv_report = exercise(args.agent, load3do, args.output_directory,
                                      args.source, "uv", uv_operations)
    _, shape_report = exercise(args.agent, load3do, args.output_directory,
                              args.source, "shape", shape_operations)
    _, chained_report = exercise(args.agent, load3do, args.output_directory,
                                uv_candidate, "uv-then-shape", shape_operations)
    require(digest(args.source) == original_digest, "The original source changed")
    pin = subprocess.run(["git", "-C", str(args.blender_sith), "rev-parse", "HEAD"],
                         check=True, text=True, capture_output=True).stdout.strip()
    report = {"independentReader": "Blender-Sith portable load3do, strict allowErrors=False",
              "readerCommit": pin, "originalUnchanged": True, "runtimeVerified": False,
              "scenarios": [uv_report, shape_report, chained_report]}
    write_json(args.output_directory / "independent-readback.json", report)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
