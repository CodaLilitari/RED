"""Move selected RED static model templates to MotS's ordinary Thing draw pass.

The caller supplies the final baked model bytes which will ship with the JKL.
The native sector pass can use per-placement ARCH mono/R/G/B. Ordinary Things
use the 3DO scalar on residual Gouraud faces. Prove those inputs agree first.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

from walkway_geometry import counted, jkl_flags, require


LEVELGEO = 0x8
IGNORE_GOURAUD_DISTANCE = 0x04000000
ARCH_TOLERANCE = 0.00001
MAX_JKL = 64 << 20
MAX_MODEL = 8 << 20
MAX_MODEL_BYTES = 256 << 20
MODEL_NAME = re.compile(r"[a-z0-9_-]+\.3do", re.I | re.ASCII)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def content(line):
    return re.split(r"[#;]", line, maxsplit=1)[0].strip()


def model_lighting(raw):
    require(0 < len(raw) <= MAX_MODEL, "3DO size outside supported range")
    rows = [text for line in raw.decode("latin-1").splitlines() if (text := content(line))]
    for label, expected in (("GEOSETS", "1"), ("GEOSET", "0"), ("MESHES", "1"),
                            ("MESH", "0"), ("LIGHTINGMODE", "3")):
        values = [m[1] for row in rows if (m := re.fullmatch(label + r"\s+(\d+)", row, re.I))]
        require(values == [expected], f"Expected a single {label} {expected} declaration")
    scalars = []
    for row in counted(rows, "VERTICES"):
        words = row.split()
        require(len(words) == 4, "Expected XYZ and one scalar per model vertex")
        scalar = float(words[3])
        require(math.isfinite(scalar), "Non-finite model intensity")
        scalars.append(scalar)
    live = set()
    residual_faces = 0
    for row in counted(rows, "FACES"):
        fields = row.split(None, 7)
        require(len(fields) == 8, "Incomplete 3DO face")
        mode, count = int(fields[3]), int(fields[6])
        require(mode in (0, 3), f"Unsupported face lighting mode {mode}")
        corners = re.sub(r"\s*,\s*", ",", fields[7]).split()
        require(3 <= count <= 256 and len(corners) == count, "Unexpected face corner count")
        for corner in corners:
            pair = re.fullmatch(r"(\d+),(-?\d+)", corner)
            require(pair and int(pair[1]) < len(scalars), "Unexpected model vertex index")
            if mode == 3:
                live.add(int(pair[1]))
        residual_faces += mode == 3
    return scalars, live, residual_faces


def section_rows(lines):
    """Retain physical line indices so only the flag values change in the output."""
    needed = {"templates", "things", "archlighting"}
    sections = {}
    active = None
    for i, line in enumerate(lines):
        text = content(line)
        header = re.fullmatch(r"section\s*:\s*(\S+)", text, re.I)
        if header:
            require(active is None, "Missing section END")
            name = header[1].lower()
            if name in needed:
                require(name not in sections, f"Duplicate section {name}")
                active = sections[name] = []
        elif active is not None and text:
            if text.lower() == "end":
                active = None
            else:
                active.append((i, text))
    require(active is None and set(sections) == needed, "Expected complete MotS template, Thing and ARCH tables")
    return sections


def table(rows, label):
    require(rows and re.fullmatch(r"world\s+" + label + r"\s+\d+", rows[0][1], re.I),
            f"Missing WORLD {label} declaration")
    capacity = int(rows[0][1].split()[-1])
    require(len(rows) - 1 <= capacity, f"{label} rows exceed capacity")
    return rows[1:]


def properties(words, allowed):
    props = {}
    for token in words:
        key, sep, value = token.lower().partition("=")
        require(sep and value and key in allowed and key not in props,
                f"Unexpected or repeated static architecture property {token}")
        props[key] = value
    return props


def model_templates(rows, models, parent):
    by_name = {}
    for i, row in table(rows, "templates"):
        words = row.lower().split()
        require(len(words) >= 2 and words[0] not in by_name, "Incomplete or duplicate template")
        by_name[words[0]] = i, words[1], words[2:]
    require(parent in by_name and by_name[parent][1] == "none", "Expected a root static world template")
    root = properties(by_name[parent][2], {"type", "move", "thingflags", "collide"})
    require(root.get("type") == "cog" and root.get("move") in ("none", "path"),
            "Expected a stationary RED world parent")

    selected = {}
    bound = set()
    for name, (i, base, words) in by_name.items():
        if base != parent or not any(w.startswith("model3d=") and w[8:] in models for w in words):
            continue
        props = properties(words, {"model3d", "thingflags", "collide", "size", "movesize"})
        model = props["model3d"]
        flags = jkl_flags(props.get("thingflags", ""))
        require(flags & LEVELGEO, f"{name} does not have LEVELGEO")
        require(model not in bound, f"Ambiguous template ownership for {model}")
        selected[name] = i, model, (flags & ~LEVELGEO) | IGNORE_GOURAUD_DISTANCE
        bound.add(model)
    require(bound == set(models), "Some requested models have no static template under the supplied parent")
    require(all(base not in selected for _, base, _ in by_name.values()),
            "A selected template passes its flags to a descendant")
    return selected


def arch_records(rows):
    require(rows and re.fullmatch(r"num\s+archobjects\s+\d+", rows[0][1], re.I),
            "Missing ARCH object count")
    records = {}
    current = None
    for _, row in rows[1:]:
        header = re.fullmatch(r"archobject\s+(\d+)", row, re.I)
        if header:
            index = int(header[1])
            require(index not in records, "Duplicate ARCH record")
            current = records[index] = []
        else:
            require(current is not None, "Unexpected ARCH row before record")
            current.append(row)
    require(set(records) == set(range(len(records))) and len(records) == int(rows[0][1].split()[-1]),
            "ARCH object count or indices do not match")
    return records


def check_placement(arch, scalars, live):
    require(len(arch) == len(scalars) + 3 and re.fullmatch(r"nummeshes:\s*1", arch[0], re.I)
            and re.fullmatch(r"mesh\s+0", arch[1], re.I)
            and re.fullmatch(r"numvertices:\s*" + str(len(scalars)), arch[2], re.I),
            "Placed ARCH layout differs from the selected 3DO")
    maximum = 0.0
    for i, row in enumerate(arch[3:]):
        number, colon, values = row.partition(":")
        channels = tuple(map(float, values.split()))
        require(colon and int(number) == i and len(channels) == 4 and all(map(math.isfinite, channels)),
                f"Invalid ARCH vertex {i}")
        if i in live:
            difference = max(abs(value - scalars[i]) for value in channels)
            require(difference <= ARCH_TOLERANCE,
                    f"Ordinary draw pass would lose ARCH light at vertex {i} (difference {difference:g})")
            maximum = max(maximum, difference)
    return maximum


def switch_draw_pass(jkl, models, parent_template):
    """Return candidate JKL bytes and proof for a named inventory of final baked model bytes."""
    require(0 < len(jkl) <= MAX_JKL and models, "Expected a bounded JKL and a nonempty model inventory")
    require(all(MODEL_NAME.fullmatch(name) for name in models), "Expected bare .3do names")
    require(sum(map(len, models.values())) <= MAX_MODEL_BYTES, "Selected models exceed the size budget")
    normalized = {name.lower(): data for name, data in models.items()}
    require(len(normalized) == len(models), "Duplicate selected model name")
    lines = jkl.decode("latin-1").splitlines(keepends=True)
    sections = section_rows(lines)
    selected = model_templates(sections["templates"], normalized, parent_template.lower())
    records = arch_records(sections["archlighting"])
    lighting = {model: model_lighting(raw) for model, raw in normalized.items()}
    report = {"staticPlacements": 0, "residualGouraudFaces": 0, "residualGouraudVertices": 0,
              "maxArchDifference": 0.0, "inputSha256": sha(jkl),
              "models": {name: sha(value) for name, value in sorted(normalized.items())}}
    placed = set()
    seen_slots = set()
    capacity = int(sections["things"][0][1].split()[-1]) if sections["things"] else 0
    for _, row in table(sections["things"], "things"):
        slot, colon, body = row.partition(":")
        fields = body.lower().split()
        require(colon and slot.isdecimal() and fields, "Malformed Thing row")
        index = int(slot)
        require(index < capacity and index not in seen_slots, "Duplicate or out-of-capacity Thing slot")
        seen_slots.add(index)
        template = fields[0]
        if template not in selected:
            continue
        require(len(fields) == 10, f"Thing {slot} has missing ARCH data or placement overrides")
        arch_id = int(fields[-1])
        require(arch_id in records, f"Thing {slot} lacks a bound ARCH record")
        model = selected[template][1]
        scalars, live, faces = lighting[model]
        try:
            maximum = check_placement(records[arch_id], scalars, live)
        except ValueError as exc:
            raise ValueError(f"Thing {slot}, {model}, ARCH {arch_id}: {exc}") from exc
        placed.add(template)
        report["staticPlacements"] += 1
        report["residualGouraudFaces"] += faces
        report["residualGouraudVertices"] += len(live)
        report["maxArchDifference"] = max(report["maxArchDifference"], maximum)
    require(placed == set(selected), "A selected template lacks a verified placement")
    report["residualGouraudModels"] = sum(bool(live) for _, live, _ in lighting.values())
    for i, _, flags in selected.values():
        span = re.search(r"(?i)(?<!\S)thingflags=([^\s#;]+)", lines[i])
        require(span, "Missing explicit flags on selected template")
        lines[i] = lines[i][:span.start(1)] + hex(flags) + lines[i][span.end(1):]
    result = "".join(lines).encode("latin-1")
    report["outputSha256"] = sha(result)
    return result, report


def read_bounded(path, limit):
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    require(len(data) <= limit, f"{path.name} exceeds its byte limit")
    return data


def write_fresh(dest, data):
    with tempfile.NamedTemporaryFile(dir=dest.parent, delete=False) as file:
        stage = Path(file.name)
    try:
        stage.write_bytes(data)
        os.link(stage, dest)
    finally:
        stage.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jkl", type=Path, required=True, help="extracted MotS JKL with ARCHLIGHTING")
    parser.add_argument("--plan", type=Path, required=True, help="successful RED jo-ja-plan JSON for MotS")
    parser.add_argument("--models", type=Path, required=True, help="directory containing the final baked 3DOs")
    parser.add_argument("--parent-template", required=True, help="exact static world parent, e.g. _jjworld")
    parser.add_argument("--output", type=Path, required=True, help="new JKL file")
    args = parser.parse_args()
    plan = json.loads(read_bounded(args.plan, 32 << 20))
    require(isinstance(plan, dict) and plan.get("command") == "jo-ja-plan" and plan.get("ok") is True
            and plan.get("target") == "mots" and isinstance(plan.get("models"), list),
            "Expected a successful RED MotS model plan")
    models = {}
    total = 0
    for item in plan["models"]:
        name = item.get("name") if isinstance(item, dict) else None
        require(isinstance(name, str) and MODEL_NAME.fullmatch(name) and name.lower() not in models,
                "Invalid or duplicate model name in plan")
        raw = read_bounded(args.models / name, min(MAX_MODEL, MAX_MODEL_BYTES - total))
        total += len(raw)
        models[name.lower()] = raw
    result, report = switch_draw_pass(read_bounded(args.jkl, MAX_JKL), models, args.parent_template)
    write_fresh(args.output, result)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        sys.exit(str(error))
