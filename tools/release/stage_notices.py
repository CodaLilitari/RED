#!/usr/bin/env python3
"""Stage selected Qt SDK notices and inventory a finished Unix release package."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys


ATTRIBUTION_MODULES = ("qtcore", "qtgui", "qtnetwork", "qtmultimedia", "qtsvg")
SBOM_MODULES = ("qtbase", "qtmultimedia", "qtsvg")
REQUIRED_LICENSES = ("GPL-2.0-only.txt", "GPL-3.0-only.txt", "LGPL-3.0-only.txt")


def require_file(path):
    if not path.is_file():
        raise ValueError(f"required file is missing: {path}")
    if path.stat().st_size == 0:
        raise ValueError(f"required file is empty: {path}")
    return path


def notice_files(licenses, attributions, sbom, version):
    """Validate the entire notice set before copying any of it."""
    for name in REQUIRED_LICENSES:
        require_file(licenses / name)
    selected = [(path, Path("LICENSES") / path.name) for path in sorted(licenses.glob("*.txt"))]
    for module in ATTRIBUTION_MODULES:
        matches = sorted((attributions / module).glob("*-attribution-*.html"))
        if not matches:
            raise ValueError(f"required {module} attribution HTML is missing: {attributions / module}")
        selected.extend((path, Path("attributions") / module / path.name) for path in matches)
    for module in SBOM_MODULES:
        path = require_file(sbom / f"{module}-{version}.spdx.json")
        document = json.loads(path.read_text(encoding="utf-8"))
        if (not isinstance(document, dict) or not str(document.get("spdxVersion", "")).startswith("SPDX-")
                or not isinstance(document.get("packages"), list) or not document["packages"]):
            raise ValueError(f"invalid SPDX document: {path}")
        selected.append((path, Path("sbom") / path.name))
    for path, _ in selected:
        require_file(path)
    return selected


def stage(package, kit, version):
    kit = kit.resolve(strict=True)
    if kit.parent.name != version:
        raise ValueError(f"Qt kit does not belong to selected version {version}: {kit}")
    qt_root = kit.parent.parent
    selected = notice_files(kit.parent / "Src" / "LICENSES", qt_root / "Docs" / f"Qt-{version}",
                            kit / "sbom", version)
    destination = package / "licenses" / "qt" / version
    destination.mkdir(parents=True, exist_ok=False)
    for source, relative in selected:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)


def write_manifest(package, version, qt_version, platform, commit):
    if not re.fullmatch(r"[0-9a-fA-F]{40}", commit):
        raise ValueError("source commit must be an exact 40-character Git commit")
    notices = package / "licenses" / "qt" / qt_version
    if not notices.is_dir():
        raise ValueError("required Qt notices have not been staged")
    notice_files(notices / "LICENSES", notices / "attributions", notices / "sbom", qt_version)
    manifest = package / "RELEASE_MANIFEST.json"
    temporary = package / ".RELEASE_MANIFEST.json.tmp"
    entries = []
    for path in sorted(package.rglob("*")):
        if path in (manifest, temporary):
            continue
        relative = path.relative_to(package).as_posix()
        if path.is_symlink():
            target = os.readlink(path)
            if Path(target).is_absolute() or not path.resolve(strict=True).is_relative_to(package):
                raise ValueError(f"symlink points outside release package: {relative}")
            entries.append({"path": relative, "type": "symlink", "target": target})
        elif path.is_file():
            with path.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            entries.append({"path": relative, "type": "file", "size": path.stat().st_size, "sha256": digest})
        elif not path.is_dir():
            raise ValueError(f"unsupported package entry: {relative}")
    document = {"schemaVersion": 1, "version": version, "qtVersion": qt_version,
                "platform": platform, "sourceCommit": commit, "files": entries}
    temporary.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    temporary.replace(manifest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("stage", "manifest"))
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--qt-version", required=True)
    parser.add_argument("--qt-kit", type=Path)
    parser.add_argument("--version")
    parser.add_argument("--platform")
    parser.add_argument("--commit")
    args = parser.parse_args()
    try:
        package = args.package.resolve(strict=True)
        if not package.is_dir() or not re.fullmatch(r"\d+\.\d+\.\d+", args.qt_version):
            raise ValueError("expected an existing package directory and exact Qt release version")
        if args.command == "stage":
            if not args.qt_kit:
                raise ValueError("stage requires --qt-kit")
            stage(package, args.qt_kit, args.qt_version)
        else:
            if not all((args.version, args.platform, args.commit)):
                raise ValueError("manifest requires --version, --platform and --commit")
            write_manifest(package, args.version, args.qt_version, args.platform, args.commit)
    except (OSError, ValueError) as error:
        print(f"Release package error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
