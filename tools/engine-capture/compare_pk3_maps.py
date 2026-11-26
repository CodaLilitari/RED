"""Compare Raven map variants and hand an exact captured BSP to RED's importer."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile

from compare_episode_releases import copy_zip_member, open_checked_zip, source_identity
from verify_episode_pair import require


def inspect_with_agent(agent, path):
    result = subprocess.run([str(agent), "jo-ja-inspect", str(path)], capture_output=True, timeout=120)
    row = json.loads(result.stdout)
    require(result.returncode == 0, row.get("error", "RED could not parse this BSP"))
    return row


def publish_selected(capture, dest):
    # Complete the copy before atomically admitting a new filename in the destination directory.
    with tempfile.NamedTemporaryFile(dir=dest.parent, delete=False) as staged:
        path = Path(staged.name)
    try:
        shutil.copyfile(capture, path)
        os.link(path, dest)
    finally:
        path.unlink()


def compare(packages, inspect, selected=None, output=None):
    require(1 <= len(packages) <= 32, "Choose 1 to 32 PK3 packages")
    require((selected is None) == (output is None), "Use --select and --output together")
    if selected is not None:
        require(re.fullmatch(r"[0-9a-fA-F]{64}", selected), "Select the full BSP SHA-256")
        selected = selected.lower()
        require(output.suffix.lower() == ".bsp", "Choose a new .bsp output")
    variants, paths = {}, {}
    remaining = 1024 * 1024 * 1024
    with tempfile.TemporaryDirectory(prefix="red-pk3-") as tmp:
        root = Path(tmp)
        candidate = root / "candidate.bsp"
        for package in packages:
            require(package.suffix.lower() == ".pk3", "Expected a selected .pk3 file")
            seen = set()
            with package.open("rb") as stream:
                identity = source_identity(stream)
                with open_checked_zip(stream, identity[0]) as (wrapper, archive):
                    for info in archive.infolist():
                        name = info.orig_filename.replace("\\", "/")
                        key = name.casefold()
                        if not key.startswith("maps/") or not key.endswith(".bsp"):
                            continue
                        require(all(part not in ("", ".", "..") for part in name.split("/")) and
                                all(c.isprintable() and c not in ':*?"<>|' for c in name),
                                "Invalid installed map path")
                        require(key not in seen, "Duplicate installed map path within one PK3")
                        seen.add(key)
                        require(not stat.S_ISLNK(info.external_attr >> 16) and not info.flag_bits & 1 and
                                info.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED),
                                "Expected an unencrypted regular BSP member")
                        require(0 < info.file_size <= min(128 * 1024 * 1024, remaining),
                                "BSP exceeds the per-map or combined capture budget")
                        remaining -= info.file_size
                        digest = hashlib.sha256()
                        with candidate.open("wb") as dst:
                            copy_zip_member(wrapper, archive, info, dst, digest)
                        sha = digest.hexdigest()
                        if sha not in variants:
                            summary = inspect(candidate)
                            require(summary["sourceSha256"] == sha, "Inspector read different source bytes")
                            classes = summary["entityClasses"]
                            variants[sha] = {"members": [], "bytes": info.file_size,
                                "worldSurfaces": summary["worldSurfaces"], "entityClasses": classes,
                                "playerStarts": sum(n for c, n in classes.items() if c in
                                    ("info_player_start", "info_player_deathmatch", "info_player_duel",
                                     "info_player_duel1", "info_player_duel2", "team_CTF_redplayer",
                                     "team_CTF_blueplayer", "team_CTF_redspawn", "team_CTF_bluespawn")),
                                "triggers": sum(n for c, n in classes.items() if c.startswith("trigger_"))}
                            if selected == sha:
                                candidate.rename(root / "selected.bsp")
                        variants[sha]["members"].append({"package": str(package), "path": name})
                        paths.setdefault(key, set()).add(sha)
                require(identity == source_identity(stream), "Package changed while being captured")
            require(seen, f"No maps/*.bsp members in {package}")
        report = {"paths": {key: sorted(digests) for key, digests in sorted(paths.items())}, "variants": variants}
        if selected is not None:
            require(selected in variants, "Selected BSP SHA-256 is absent from these packages")
            publish_selected(root / "selected.bsp", output)
            report["selectedSource"] = {"path": str(output.resolve()), "sha256": selected}
        return report


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("packages", type=Path, nargs="+")
    cli.add_argument("--agent", default="red-agent", help="RED companion executable")
    cli.add_argument("--select", metavar="SHA256")
    cli.add_argument("--output", type=Path, help="new BSP for the selected exact bytes")
    args = cli.parse_args()
    try:
        report = compare(args.packages, lambda path: inspect_with_agent(args.agent, path), args.select, args.output)
        print(json.dumps(report, indent=2))
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
