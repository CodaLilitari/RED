"""Verify a Model Studio GUI evidence directory with the pinned Blender-Sith reader."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from native_authoring_readback import check_candidate, digest, write_json


READER_COMMIT = "34d256c30f66fe9826e5a3318f2fd4397257f24c"
EXPECTED_OPERATIONS = [
    {"op": "transformMesh", "geoset": 1, "sourceMesh": 1, "scale": 1.25,
     "translation": [0.125, 0, 0], "radiusPolicy": "preserve"},
    {"op": "setTextureVertex", "geoset": 1, "sourceMesh": 1, "textureVertex": 2,
     "u": -16.5, "v": 35.25},
]


def verify_uv(loader, directory, fixture):
    source = directory / "uv-shared-seam.3do"
    receipt = json.loads((directory / "uv-inspection-receipt.json").read_text(encoding="utf-8"))
    operations = [{"op": "setTextureVertex", "geoset": 0, "sourceMesh": 0,
                   "textureVertex": 1, "u": -4.0, "v": 7.0}]
    expected_names = ["uv-variant-first.3do", "uv-variant-second.3do"]
    if receipt["sourceFile"] != source.name or receipt["operations"] != operations:
        raise ValueError("UV receipt does not identify the expected source and negative/tiled row edit.")
    if receipt["originalUnchanged"] is not True or receipt["runtimeVerified"] is not False:
        raise ValueError("UV receipt must preserve the source and separate inspection from runtime evidence.")
    expected_source = fixture.read_bytes().replace(b"3: 1 1\n", b"3: 1 0\n")
    expected_source = expected_source.replace(b"1: 0 0x0 4 3 3 0.000000 3 1,1 3,3 2,2",
                                              b"1: 0 0x0 4 3 3 0.000000 3 1,3 3,1 2,2", 1)
    if source.read_bytes() != expected_source or digest(source) != receipt["sourceSha256"]:
        raise ValueError("UV source differs from the owned coincident-seam fixture or captured hash.")
    if receipt["materialSha256"] != digest(fixture.with_name("relay_skin.mat")):
        raise ValueError("The inspected MAT differs from the owned relay material.")
    if receipt["paletteSha256"] != digest(directory / "uv-inspection.cmp"):
        raise ValueError("The inspected CMP differs from the captured palette bytes.")
    if [item["candidateFile"] for item in receipt["candidates"]] != expected_names:
        raise ValueError("UV receipt must identify both distinct published variants in order.")
    candidates = []
    for item in receipt["candidates"]:
        candidate = directory / item["candidateFile"]
        actual_hash = digest(candidate)
        if actual_hash != item["reviewedOutputSha256"]:
            raise ValueError(f"{candidate.name} differs from its reviewed Studio output hash.")
        fields = check_candidate(loader, source, candidate, operations)
        candidates.append({"candidateFile": candidate.name, "candidateSha256": actual_hash,
                           "independentReadback": fields})
    if candidates[0]["candidateSha256"] != candidates[1]["candidateSha256"]:
        raise ValueError("Publishing the same cumulative UV revision produced different bytes.")
    return {"sourceFile": source.name, "sourceSha256": digest(source), "operations": operations,
            "materialSha256": receipt["materialSha256"], "paletteSha256": receipt["paletteSha256"],
            "candidates": candidates, "originalUnchanged": True, "runtimeVerified": False}


def verify(blender_sith, directory, uv_inspection=False):
    if not __debug__:
        raise ValueError("Run without Python optimization so the independent field comparison is active.")
    reader = blender_sith.resolve()
    git = ["git", "-C", str(reader)]
    revision = subprocess.check_output([*git, "rev-parse", "HEAD"], text=True).strip()
    if revision != READER_COMMIT:
        raise ValueError(f"Blender-Sith must be at {READER_COMMIT}; found {revision}.")
    if subprocess.check_output([*git, "status", "--porcelain", "--", "sith"], text=True).strip():
        raise ValueError("Blender-Sith's sith reader package has local changes.")
    sys.path.insert(0, str(reader))
    from sith.model import model3doLoader
    if Path(model3doLoader.__file__).resolve() != reader / "sith/model/model3doLoader.py":
        raise ValueError("The strict 3DO reader must come from the supplied Blender-Sith checkout.")

    source = directory / "source.3do"
    candidate = directory / "mixed.3do"
    fixture = Path(__file__).with_name("relay_sentry.3do")
    receipt = json.loads((directory / "studio-receipt.json").read_text(encoding="utf-8"))
    if receipt["sourceFile"] != source.name or receipt["candidateFile"] != candidate.name:
        raise ValueError("The Studio receipt does not identify source.3do and mixed.3do.")
    if receipt["originalUnchanged"] is not True or receipt["runtimeVerified"] is not False:
        raise ValueError("The Studio receipt must identify unchanged source and unverified target runtime.")
    if receipt["operations"] != EXPECTED_OPERATIONS:
        raise ValueError("The Studio receipt does not match the expected transform and UV edit.")

    source_hash = digest(source)
    candidate_hash = digest(candidate)
    if source_hash != digest(fixture) or source_hash != receipt["sourceSha256"]:
        raise ValueError("source.3do does not match both relay_sentry.3do and the reviewed source hash.")
    if candidate_hash != receipt["reviewedOutputSha256"]:
        raise ValueError("mixed.3do does not match the output hash shown in Studio.")

    # check_candidate uses strict load3do and the shared plain-field comparison.
    fields = check_candidate(model3doLoader.load3do, source, candidate, EXPECTED_OPERATIONS)
    report = {"independentReader": "Blender-Sith portable load3do, strict allowErrors=False",
            "readerCommit": revision, "fixtureFile": fixture.name, "sourceFile": source.name,
            "sourceSha256": source_hash, "candidateFile": candidate.name, "candidateSha256": candidate_hash,
            "reviewedOutputSha256": receipt["reviewedOutputSha256"], "operations": EXPECTED_OPERATIONS,
            "independentReadback": fields, "originalUnchanged": True, "runtimeVerified": False}
    if uv_inspection:
        report["uvInspection"] = verify_uv(model3doLoader.load3do, directory, fixture)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("blender_sith", type=Path, help="checkout of the pinned Blender-Sith revision")
    parser.add_argument("evidence_directory", type=Path, help="output of sed_model_studio_dialog_tests --evidence-directory")
    parser.add_argument("--uv-inspection", action="store_true", help="also independently parse both visual UV variants")
    args = parser.parse_args()
    report = verify(args.blender_sith, args.evidence_directory, args.uv_inspection)
    write_json(args.evidence_directory / "independent-readback.json", report)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
