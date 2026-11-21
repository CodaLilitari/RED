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


def verify(blender_sith, directory):
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
    return {"independentReader": "Blender-Sith portable load3do, strict allowErrors=False",
            "readerCommit": revision, "fixtureFile": fixture.name, "sourceFile": source.name,
            "sourceSha256": source_hash, "candidateFile": candidate.name, "candidateSha256": candidate_hash,
            "reviewedOutputSha256": receipt["reviewedOutputSha256"], "operations": EXPECTED_OPERATIONS,
            "independentReadback": fields, "originalUnchanged": True, "runtimeVerified": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("blender_sith", type=Path, help="checkout of the pinned Blender-Sith revision")
    parser.add_argument("evidence_directory", type=Path, help="output of sed_model_studio_dialog_tests --evidence-directory")
    args = parser.parse_args()
    report = verify(args.blender_sith, args.evidence_directory)
    write_json(args.evidence_directory / "independent-readback.json", report)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
