#!/usr/bin/env python3
"""Generate the measured COG registry tables for sed_core lexical preflight.

Reads the measured retail registry JSON files (the only measured source for
verb/message availability) and emits a deterministic C++ header with both
retail tables, the measured global-float names, and the SHA-256 of every
source JSON file. Hand-editing a table name without regenerating from the
measured data breaks the recorded table digest, which the core tests check.

Usage:
    python generate_cog_registry.py [--refs DIR] [--out HEADER]

--refs defaults to the measured registry directory of the jk-mots-cog
workflow skill. The output defaults to the checked-in generated header in
src/core. Output is byte-for-byte deterministic for identical inputs.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

DEFAULT_REFS = Path(
    r"C:\Users\Andrei\.codex\skills\jk-mots-cog-workflow\references"
)

# Canonical digest sections, in order. The C++ side recomputes the digest
# over exactly this serialization: "<label>\n" then each name + "\n".
SECTIONS = [
    "jk-verbs",
    "jk-messages",
    "mots-verbs",
    "mots-messages",
    "jk-global-floats",
    "mots-global-floats",
]


def fail(message: str) -> None:
    raise SystemExit(f"generate_cog_registry: {message}")


def load_registry(path: Path) -> tuple[bytes, dict[str, list[str]], str]:
    raw = path.read_bytes()
    try:
        rows = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        fail(f"{path}: unreadable registry JSON: {exc}")
    if not isinstance(rows, list) or not rows:
        fail(f"{path}: registry must be a non-empty JSON array")

    binary_hashes = set()
    tables: dict[str, list[str]] = {"verb": [], "message": []}
    seen: set[tuple[str, str]] = set()
    for row in rows:
        if not isinstance(row, dict):
            fail(f"{path}: registry row is not an object: {row!r}")
        kind = row.get("kind")
        name = row.get("name")
        if kind not in tables:
            fail(f"{path}: unmeasured kind {kind!r} must not enter a retail table")
        if not isinstance(name, str) or not name:
            fail(f"{path}: registry row without a name: {row!r}")
        lowered = name.lower()
        if lowered != name or not all(
            ch.isascii() and (ch.islower() or ch.isdigit() or ch == "_")
            for ch in lowered
        ):
            fail(f"{path}: name {name!r} is not a lower-case ASCII spelling")
        if (kind, lowered) in seen:
            fail(f"{path}: duplicate {kind} spelling {lowered!r}")
        seen.add((kind, lowered))
        tables[kind].append(lowered)
        binary_hashes.add(row.get("binary_sha256"))
    if len(binary_hashes) != 1 or not next(iter(binary_hashes)):
        fail(f"{path}: rows do not share exactly one binary SHA-256")

    for names in tables.values():
        names.sort()
    return raw, tables, next(iter(binary_hashes))


def load_global_floats(path: Path) -> dict[str, list[str]]:
    raw = path.read_bytes()
    try:
        rows = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        fail(f"{path}: unreadable global-float JSON: {exc}")
    floats: dict[str, list[str]] = {"jk": [], "mots": []}
    for row in rows:
        if not isinstance(row, dict) or row.get("kind") != "float":
            continue
        game = row.get("game")
        name = row.get("name")
        if game not in floats or not isinstance(name, str):
            fail(f"{path}: unexpected global-float row: {row!r}")
        floats[game].append(name.lower())
    for names in floats.values():
        names.sort()
        if len(names) != len(set(names)):
            fail(f"{path}: duplicate global-float name")
    return floats


def emit_array(lines: list[str], symbol: str, names: list[str]) -> None:
    lines.append(
        f"inline constexpr std::array<std::string_view, {len(names)}> {symbol}{{"
    )
    for name in names:
        lines.append(f'    "{name}",')
    lines.append("};")
    lines.append("")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--refs", type=Path, default=DEFAULT_REFS)
    parser.add_argument("--out", type=Path, default=None)
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    out = args.out or (
        repo_root / "src/core/include/sed/assets/cog_registry_tables.hpp"
    )

    jk_raw, jk, jk_binary = load_registry(args.refs / "jk101-retail-registry.json")
    mots_raw, mots, mots_binary = load_registry(
        args.refs / "mots-retail-registry.json"
    )
    floats_path = args.refs / "retail-global-floats.json"
    floats = load_global_floats(floats_path)
    floats_raw = floats_path.read_bytes()

    tables = {
        "jk-verbs": jk["verb"],
        "jk-messages": jk["message"],
        "mots-verbs": mots["verb"],
        "mots-messages": mots["message"],
        "jk-global-floats": floats["jk"],
        "mots-global-floats": floats["mots"],
    }
    canonical = "".join(
        f"{label}\n" + "".join(f"{name}\n" for name in tables[label])
        for label in SECTIONS
    )
    tables_sha = hashlib.sha256(canonical.encode("ascii")).hexdigest()

    lines: list[str] = []
    lines.append("// Generated by tools/cog-registry/generate_cog_registry.py.")
    lines.append("// DO NOT EDIT BY HAND. Regenerate from the measured registry")
    lines.append("// JSON; the table digest below makes hand-patched spellings")
    lines.append("// detectable (see sed::assets::cogRegistryTablesIntact).")
    lines.append("//")
    lines.append("// Measured sources (SHA-256 of each source JSON file):")
    lines.append(f"//   jk101-retail-registry.json  {hashlib.sha256(jk_raw).hexdigest()}")
    lines.append(f"//   mots-retail-registry.json   {hashlib.sha256(mots_raw).hexdigest()}")
    lines.append(f"//   retail-global-floats.json   {hashlib.sha256(floats_raw).hexdigest()}")
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <array>")
    lines.append("#include <string_view>")
    lines.append("")
    lines.append("namespace sed::assets::cog_registry")
    lines.append("{")
    lines.append("")
    lines.append(f'inline constexpr std::string_view kJkRegistrySourceJsonSha256 = "{hashlib.sha256(jk_raw).hexdigest()}";')
    lines.append(f'inline constexpr std::string_view kMotsRegistrySourceJsonSha256 = "{hashlib.sha256(mots_raw).hexdigest()}";')
    lines.append(f'inline constexpr std::string_view kGlobalFloatsSourceJsonSha256 = "{hashlib.sha256(floats_raw).hexdigest()}";')
    lines.append(f'inline constexpr std::string_view kJkRetailBinarySha256 = "{jk_binary}";')
    lines.append(f'inline constexpr std::string_view kMotsRetailBinarySha256 = "{mots_binary}";')
    lines.append(f'inline constexpr std::string_view kCogRegistryTablesSha256 = "{tables_sha}";')
    lines.append("")
    emit_array(lines, "kJkVerbs", tables["jk-verbs"])
    emit_array(lines, "kJkMessages", tables["jk-messages"])
    emit_array(lines, "kMotsVerbs", tables["mots-verbs"])
    emit_array(lines, "kMotsMessages", tables["mots-messages"])
    emit_array(lines, "kJkGlobalFloats", tables["jk-global-floats"])
    emit_array(lines, "kMotsGlobalFloats", tables["mots-global-floats"])
    lines.append("} // namespace sed::assets::cog_registry")
    lines.append("")

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(("\n".join(lines)).encode("ascii"))
    print(f"wrote {out} (tables sha256 {tables_sha})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
