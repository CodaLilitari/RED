#!/usr/bin/env python3
"""Check the public portfolio for research updates; optionally apply verified prose/data.

No executable code is updated, no local files are uploaded, and no telemetry is sent.
The default operation only checks. Run --help for installation and conflict behavior.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import tempfile
from urllib.parse import urlsplit
from urllib.request import Request, build_opener, HTTPRedirectHandler

BASE = "https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-level-workflow/"
MANIFEST_URL = BASE + "research-updates.json"
FALLBACK_BASE = "https://raw.githubusercontent.com/CodaLilitari/RED/red/main/tools/jk-mots-level-workflow/"
MANIFEST_URLS = (MANIFEST_URL, FALLBACK_BASE + "research-updates.json")
MAX_FILE = 1024 * 1024
MAX_TOTAL = 8 * MAX_FILE
STATE_NAME = "research-update-state.json"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def version(value):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", value):
        raise ValueError("Expected a numeric major.minor.patch version")
    return tuple(map(int, value.split(".")))


def safe_url(url):
    if "\\" in url or any(ord(c) < 32 or ord(c) == 127 for c in url):
        raise ValueError("Ambiguous/control characters in update URL refused")
    p = urlsplit(url)
    approved = [urlsplit(base) for base in (BASE, FALLBACK_BASE)]
    if p.scheme != "https" or not any(p.netloc == b.netloc and p.path.startswith(b.path) for b in approved) or p.query or p.fragment:
        raise ValueError("Update URL leaves the fixed research publication routes")
    if "%" in p.path or any(part in (".", "..") for part in p.path.split("/")):
        raise ValueError("Encoded or relative update URL refused")
    return url


class SameOriginRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        safe_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def fetch(url, limit):
    request = Request(safe_url(url), headers={"User-Agent": "JK-MotS-Level-Research-Updater/1.0", "Accept": "application/json,text/plain,*/*"})
    with build_opener(SameOriginRedirect()).open(request, timeout=20) as response:
        safe_url(response.url)
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError("Update response exceeds its size limit")
    return data


def research_path(name):
    if not isinstance(name, str):
        raise ValueError("Research path must be text")
    p = PurePosixPath(name)
    parts = name.split("/")
    if (not name.startswith("references/") or p.suffix not in (".md", ".json") or
            any(x in ("", ".", "..") for x in parts) or
            any(c in name for c in "\\:%?#") or
            any(not re.fullmatch(r"[a-zA-Z0-9_.-]+", x) or x.endswith(".") or
                re.fullmatch(r"(?i)(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", x) for x in parts)):
        raise ValueError("Only plain relative references/*.md and *.json paths are accepted")
    return Path(*p.parts)


def unlinked(root, relative):
    # lstat also detects Windows junction/reparse ancestors; no directory traversal.
    absolute = root.absolute()
    for part in [*reversed(absolute.parents), absolute, *[absolute.joinpath(*relative.parts[:n]) for n in range(1, len(relative.parts)+1)]]:
        try:
            info = part.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("Linked/reparse update destination refused")
    return absolute / relative


def validate_manifest(m):
    if m.get("schema_version") != 1 or m.get("scope") != "research-only":
        raise ValueError("Unsupported update manifest")
    version(m["version"])
    version(m["requires_skill_version"])
    if type(m.get("research_revision")) is not int or m["research_revision"] < 1:
        raise ValueError("Invalid research revision")
    files = m.get("files")
    if not isinstance(files, list) or not 1 <= len(files) <= 100:
        raise ValueError("Invalid research file list")
    seen, total = set(), 0
    for entry in files:
        name = entry["path"]
        research_path(name)
        if name.casefold() in seen:
            raise ValueError("Duplicate/case-colliding research file")
        seen.add(name.casefold())
        if type(entry.get("bytes")) is not int or not 0 <= entry["bytes"] <= MAX_FILE:
            raise ValueError("Invalid research file size")
        if not isinstance(entry.get("sha256"), str) or not re.fullmatch(r"[a-f0-9]{64}", entry["sha256"]):
            raise ValueError("Invalid research digest")
        total += entry["bytes"]
    if total > MAX_TOTAL:
        raise ValueError("Research update exceeds total size limit")
    return m


def local_baseline(root):
    manifest_path = unlinked(root, Path("PACKAGE-MANIFEST.json"))
    package = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.exists() else None
    if package is None:
        return None, {}, 0
    installed_version = version(package["version"])
    baseline = {e["path"]: e["sha256"] for e in package["files"]}
    state_path = unlinked(root, Path(STATE_NAME))
    revision = 0
    if state_path.exists():
        state = json.loads(state_path.read_text(encoding="utf-8"))
        if state.get("schema_version") != 1:
            raise ValueError("Unsupported local update state")
        revision = state["research_revision"]
        baseline.update({e["path"]: e["sha256"] for e in state["files"]})
    return installed_version, baseline, revision


def update(root, apply=False, fetcher=fetch):
    root = Path(root).absolute()
    # Fallback is for network availability only. A malformed/untrusted manifest is not silently bypassed.
    from urllib.error import URLError
    failures = []
    for source_url in MANIFEST_URLS:
        try:
            manifest_bytes = fetcher(source_url, MAX_FILE)
            break
        except (URLError, TimeoutError, ConnectionError) as exc:
            failures.append({"source": source_url, "error": str(exc)})
    else:
        raise ValueError("All research feeds unavailable: " + json.dumps(failures))
    m = validate_manifest(json.loads(manifest_bytes))
    source_base = source_url.rsplit("/", 1)[0] + "/"
    installed, baseline, revision = local_baseline(root)
    compatible = installed is not None and installed >= version(m["requires_skill_version"])
    changes, conflicts, observed = [], [], {}
    for entry in m["files"]:
        target = unlinked(root, research_path(entry["path"]))
        current = sha(target.read_bytes()) if target.is_file() else None
        observed[entry["path"]] = current
        if current != entry["sha256"]:
            changes.append(entry)
            if current != baseline.get(entry["path"]):
                conflicts.append(entry["path"])
    report = {"source": source_url, "availability_failures": failures, "available_version": m["version"], "research_revision": m["research_revision"],
              "compatible_skill": compatible, "changed_files": [e["path"] for e in changes],
              "local_conflicts": conflicts, "applied": False,
              "full_release": BASE + "index.html"}
    if not apply or not changes:
        return report
    if not compatible:
        raise ValueError("Install the current complete skill ZIP first; this research requires a newer skill or a package baseline")
    if m["research_revision"] < revision:
        raise ValueError("Research downgrade refused")
    if conflicts:
        raise ValueError("Local research edits preserved; compare conflicts before updating: " + ", ".join(conflicts))
    payloads = []
    for entry in changes:
        payload_base = source_base if source_base == FALLBACK_BASE else source_base + "releases/" + m["version"] + "/"
        url = payload_base + entry["path"]
        data = fetcher(safe_url(url), entry["bytes"])
        if len(data) != entry["bytes"] or sha(data) != entry["sha256"]:
            raise ValueError("Research download hash/size mismatch: " + entry["path"])
        data.decode("utf-8")
        if entry["path"].endswith(".json"):
            json.loads(data)
        payloads.append((research_path(entry["path"]), data))
    state = {"schema_version": 1, "version": m["version"], "research_revision": m["research_revision"], "files": m["files"]}
    payloads.append((Path(STATE_NAME), (json.dumps(state, indent=2) + "\n").encode()))
    # Stage all verified files before any replacement; restore pre-images on failure.
    originals, written = {}, []
    with tempfile.TemporaryDirectory(prefix=".research-update-", dir=root) as temp:
        staging = Path(temp)
        for rel, data in payloads:
            target = unlinked(root, rel)
            originals[rel] = target.read_bytes() if target.exists() else None
            if rel.as_posix() in observed:
                current_hash = sha(originals[rel]) if originals[rel] is not None else None
                if current_hash != observed[rel.as_posix()]:
                    raise ValueError("Research changed while downloading; local edits preserved")
            stage = staging / rel
            stage.parent.mkdir(parents=True, exist_ok=True)
            stage.write_bytes(data)
        try:
            for rel, _ in payloads:
                target = unlinked(root, rel)
                current = target.read_bytes() if target.exists() else None
                if current != originals[rel]:
                    raise ValueError("Research changed during update; stopping")
                target.parent.mkdir(parents=True, exist_ok=True)
                os.replace(staging / rel, target)
                written.append(rel)
        except Exception as failure:
            recovery_conflicts = []
            installed_payload = dict(payloads)
            for rel in reversed(written):
                try:
                    target = unlinked(root, rel)
                    current = target.read_bytes() if target.exists() else None
                    if current != installed_payload[rel]:
                        recovery_conflicts.append(rel.as_posix())
                        continue
                    if originals[rel] is None:
                        target.unlink()
                    else:
                        target.write_bytes(originals[rel])
                except (OSError, ValueError):
                    recovery_conflicts.append(rel.as_posix())
            if recovery_conflicts:
                raise ValueError("Rollback preserved concurrent changes or could not restore files; compare manually: " + ", ".join(recovery_conflicts)) from failure
            raise
    report["applied"] = True
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--skill-root", type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument("--apply", action="store_true", help="Apply verified research only; preserve local edits and never update scripts/SKILL.md")
    args = p.parse_args()
    try:
        result = update(args.skill_root, args.apply)
        print(json.dumps(result, indent=2))
    except Exception as exc:
        print(json.dumps({"status": "update_unavailable", "error": str(exc), "offline_workflow_available": True}, indent=2))
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
