"""Bounded native installation acceptance for an exact extracted RED package."""
import importlib.util
import argparse
import hashlib
import json
import math
import os
import platform as host
from pathlib import Path
import re
import struct
import time
import sys
import tempfile
import uuid
import zlib

_spec = importlib.util.spec_from_file_location("red_package_cli_shared", Path(__file__).with_name("accept_package_cli.py"))
cli = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cli)
MAX_PNG_BYTES = 8 * 1024 * 1024
MAX_REPORT_BYTES = 512 * 1024
RUNTIME_NAMES = ("df2jk.lst", "ijim.tpl", "master.tpl", "mats.jlh", "mats.jll", "mots.tpl",
                 "mtpls.jlh", "mtpls.jll", "shapes.tpl", "tpls.jlh", "tpls.jll")


def read_bounded(path, limit, deadline):
    cli.remaining(deadline)
    cli.require(path.is_file() and not path.is_symlink() and path.stat().st_size <= limit,
                f"Missing, linked or oversized artifact: {path.name}")
    with path.open("rb") as stream:
        raw = stream.read(limit + 1)
    cli.require(len(raw) <= limit, f"Artifact exceeded its read budget: {path.name}")
    cli.remaining(deadline)
    return raw


def json_object(raw):
    try:
        value = json.loads(raw)
    except (ValueError, UnicodeError) as error:
        raise cli.AcceptanceError("Malformed native JSON evidence") from error
    cli.require(isinstance(value, dict), "Native JSON evidence must be an object")
    return value


def inspect_png_regions(raw, probes, deadline):
    cli.require(isinstance(raw, bytes) and len(raw) <= MAX_PNG_BYTES
                and raw.startswith(b"\x89PNG\r\n\x1a\n"), "Invalid bounded native PNG")
    offset, chunks, header, ended, data_ended = 8, 0, None, False, False
    compressed = bytearray()
    while offset < len(raw):
        cli.remaining(deadline)
        chunks += 1
        cli.require(chunks <= 4096 and offset + 12 <= len(raw), "Invalid PNG chunk framing")
        length = struct.unpack_from(">I", raw, offset)[0]
        kind = raw[offset + 4:offset + 8]
        cli.require(length <= MAX_PNG_BYTES and offset + length + 12 <= len(raw)
                    and all(65 <= c <= 90 or 97 <= c <= 122 for c in kind), "Invalid PNG chunk length/type")
        data = raw[offset + 8:offset + 8 + length]
        crc = struct.unpack_from(">I", raw, offset + 8 + length)[0]
        cli.require(zlib.crc32(kind + data) == crc, "PNG chunk CRC mismatch")
        offset += length + 12
        if header is None:
            cli.require(kind == b"IHDR" and length == 13, "PNG requires one complete first IHDR")
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", data)
            cli.require(64 <= width <= 4096 and 64 <= height <= 4096 and depth == 8
                        and color in (2, 6) and compression == filtering == interlace == 0,
                        "Native PNG must be bounded noninterlaced 8-bit RGB/RGBA")
            header = (width, height, 3 if color == 2 else 4)
        elif kind == b"IDAT":
            cli.require(not data_ended, "PNG image data chunks are not contiguous")
            compressed.extend(data)
        elif kind == b"IEND":
            cli.require(length == 0 and compressed and offset == len(raw), "Invalid PNG end/trailing bytes")
            ended = True
            break
        else:
            cli.require(kind not in (b"IHDR", b"tRNS", b"acTL", b"fcTL", b"fdAT") and kind[0] & 0x20,
                        "Unsupported critical/transparency/animated PNG chunk")
            if compressed:
                data_ended = True
    cli.require(header is not None and ended, "Incomplete native PNG")
    width, height, channels = header
    centers = []
    cli.require(isinstance(probes, dict), "Native probes are missing")
    for name in ("firstFace", "secondFace", "background"):
        point = probes.get(name)
        cli.require(isinstance(point, list) and len(point) == 2
                    and all(type(v) in (float, int) and math.isfinite(v) and 0 < v < 1 for v in point),
                    "Native probe lies outside the image")
        center = (math.floor(point[0] * (width - 1) + 0.5), math.floor(point[1] * (height - 1) + 0.5))
        cli.require(3 <= center[0] < width - 3 and 3 <= center[1] < height - 3,
                    "Native probe neighborhood crosses an edge")
        cli.require(all(abs(center[0] - old[0]) > 6 or abs(center[1] - old[1]) > 6 for old in centers),
                    "Native probe neighborhoods overlap")
        centers.append(center)
    stride = width * channels
    expected = height * (stride + 1)
    try:
        decoder = zlib.decompressobj()
        filtered = decoder.decompress(compressed, expected + 1)
        cli.require(len(filtered) == expected and decoder.eof and not decoder.unused_data
                    and not decoder.unconsumed_tail, "PNG inflated payload length or framing mismatch")
    except zlib.error as error:
        raise cli.AcceptanceError("Invalid PNG compression") from error
    previous = bytes(stride)
    bright, dark = [True] * 3, [True] * 3
    for y in range(height):
        cli.remaining(deadline)
        filter_type = filtered[y * (stride + 1)]
        cli.require(filter_type <= 4, "Unsupported PNG row filter")
        row = bytearray(filtered[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if filter_type:
            for index in range(stride):
                left = row[index - channels] if index >= channels else 0
                above = previous[index]
                upper_left = previous[index - channels] if index >= channels else 0
                if filter_type == 4:
                    prediction = left + above - upper_left
                    pa, pb, pc = abs(prediction - left), abs(prediction - above), abs(prediction - upper_left)
                    predictor = left if pa <= pb and pa <= pc else above if pb <= pc else upper_left
                else:
                    predictor = left if filter_type == 1 else above if filter_type == 2 else (left + above) // 2
                row[index] = (row[index] + predictor) & 255
        for region, (cx, cy) in enumerate(centers):
            if abs(y - cy) <= 3:
                for x in range(cx - 3, cx + 4):
                    pixel = row[x * channels:(x + 1) * channels]
                    opaque = channels == 3 or pixel[3] >= 250
                    bright[region] = bright[region] and opaque and min(pixel[:3]) >= 200
                    dark[region] = dark[region] and opaque and max(pixel[:3]) <= 12
        previous = row
    return {"dimensions": [width, height],
            "regions": ["bright" if bright[i] else "dark" if dark[i] else "other" for i in range(3)]}


def validate_native_report(executable, report_directory, process_result, version, qt_version, platform, deadline):
    cli.require(process_result["exitCode"] == 0, "Extracted native check returned a failure exit code")
    cli.require(report_directory.is_dir() and not report_directory.is_symlink(), "Native report directory is missing or linked")
    report_bytes = read_bounded(report_directory / "installation-report.json", MAX_REPORT_BYTES, deadline)
    report = json_object(report_bytes)
    cli.require(report == json_object(process_result["stdout"]), "Native stdout and persisted receipt differ")
    request = json_object(read_bounded(report_directory / "request.json", 4096, deadline))
    child = report.get("child")
    cli.require(isinstance(child, dict)
                and type(report.get("schemaVersion")) is type(child.get("schemaVersion")) is int
                and report.get("schemaVersion") == child.get("schemaVersion") == 1
                and report.get("acceptanceKind") == "native-installation" and report.get("status") == child.get("status") == "passed"
                and report.get("nativeFramesAccepted") is True, "Native check did not produce a complete success receipt")
    identifier = report.get("runId")
    try:
        cli.require(isinstance(identifier, str) and str(uuid.UUID(identifier)) == identifier, "Invalid native run identifier")
    except (ValueError, AttributeError) as error:
        raise cli.AcceptanceError("Invalid native run identifier") from error
    cli.require(type(request.get("schemaVersion")) is int and request.get("schemaVersion") == 1
                and request.get("runId") == child.get("runId") == identifier,
                "Native request and receipt run identities differ")
    for value in (request.get("executable"), report.get("executable"), child.get("executable")):
        cli.require(isinstance(value, str) and Path(value).is_absolute()
                    and Path(value).resolve() == executable.resolve(), "Native executable identity mismatch")
    cli.require(isinstance(report.get("reportDirectory"), str)
                and Path(report["reportDirectory"]).resolve() == report_directory.resolve(), "Native report scope mismatch")
    wanted = {"windows-x86_64": ("windows",), "macos-arm64": ("cocoa",), "linux-x86_64": ("xcb", "wayland")}
    cli.require(child.get("nativePlatform") in wanted.get(platform, ())
                and child.get("appVersion") == version and child.get("qtVersion") == qt_version,
                "Native platform/version/Qt identity mismatch")
    milestones = child.get("milestones")
    cli.require(isinstance(milestones, dict) and all(milestones.get(key) is True for key in
                ("shellExposed", "contextValid", "sceneShaderLinked", "runtimeDataParsed")), "Incomplete native milestones")
    graphics = child.get("gl")
    cli.require(isinstance(graphics, dict) and all(isinstance(graphics.get(key), str)
                and 0 < len(graphics[key]) <= 1024 for key in ("vendor", "renderer", "version")), "Incomplete native renderer identity")
    records = child.get("frames")
    cli.require(isinstance(records, list) and len(records) == 2, "Native check needs exactly two inspected frames")
    inspected, hashes = [], []
    for name, record in zip(("frame-before.png", "frame-after.png"), records):
        cli.require(isinstance(record, dict) and record.get("file") == name, "Unexpected native frame name")
        raw = read_bounded(report_directory / name, MAX_PNG_BYTES, deadline)
        digest = hashlib.sha256(raw).hexdigest()
        cli.require(record.get("bytes") == len(raw) and record.get("sha256") == digest, "Native frame hash/size mismatch")
        inspected.append(inspect_png_regions(raw, child.get("probes"), deadline))
        hashes.append(digest)
    cli.require(inspected[0]["dimensions"] == inspected[1]["dimensions"]
                and inspected[0]["regions"] == ["bright", "dark", "dark"]
                and inspected[1]["regions"] == ["dark", "bright", "dark"] and hashes[0] != hashes[1],
                "Independent native pixels lack the moving face and stable background")
    runtime = child.get("runtimeData")
    cli.require(isinstance(runtime, list) and len(runtime) == len(RUNTIME_NAMES)
                and all(isinstance(item, dict) and isinstance(item.get("file"), str) for item in runtime)
                and {item.get("file") for item in runtime} == set(RUNTIME_NAMES), "Incomplete installed data evidence")
    for record in runtime:
        raw = read_bounded(executable.parent / "data" / record["file"], 4 * 1024 * 1024, deadline)
        cli.require(record.get("bytes") == len(raw) and record.get("sha256") == hashlib.sha256(raw).hexdigest(),
                    "Parsed native runtime data does not match installed bytes")
    return {"nativeInstallationAccepted": True, "independentPixelsVerified": True,
            "nativePlatform": child["nativePlatform"], "renderer": graphics["renderer"], "gl": graphics,
            "frameSha256": hashes, "frameDimensions": inspected[0]["dimensions"], "nativeRunId": identifier,
            "nativeReceiptSha256": hashlib.sha256(report_bytes).hexdigest()}


def exercise_native(command, executable, report_directory, workspace, environment, deadline,
                    version, qt_version, platform, records):
    cli.require(not report_directory.exists() and not report_directory.is_symlink(), "Native report scope already exists")
    arguments = [*command, "--check-installation", "--report-directory", str(report_directory)]
    record = {"command": arguments, "status": "running"}
    records.append(record)
    try:
        result = cli.run_command(arguments, workspace, environment, min(deadline, time.monotonic() + 35))
        record.update(status="exited", exitCode=result["exitCode"])
        for name in ("stdout", "stderr"):
            record[name + "Bytes"] = len(result[name])
            record[name + "Sha256"] = hashlib.sha256(result[name]).hexdigest()
            record[name + "Preview"] = result[name][:4096].decode("utf-8", errors="replace")
        return validate_native_report(executable, report_directory, result, version, qt_version, platform, deadline)
    except Exception as error:
        record.update(status="failed", error=str(error))
        raise


def capture_storage_sentinels(paths, deadline):
    cli.require(isinstance(paths, (list, tuple)) and len(paths) <= 64, "Too many storage sentinels")
    result = {}
    for supplied in paths:
        path = Path(supplied)
        cli.require(path.is_absolute() and len(str(path)) <= 4096 and not path.is_symlink(),
                    "Storage sentinels must be explicit absolute nonlinked file paths")
        if path.exists():
            raw = read_bounded(path, 8 * 1024 * 1024, deadline)
            result[str(path)] = {"exists": True, "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
        else:
            result[str(path)] = {"exists": False}
    return result


def verify_storage_sentinels(snapshot, deadline):
    cli.require(capture_storage_sentinels(list(snapshot), deadline) == snapshot,
                "An explicit user-storage sentinel changed during native acceptance")
    return True


def native_host_platform():
    machine = host.machine().lower()
    return ("windows-x86_64" if sys.platform == "win32" and machine in ("amd64", "x86_64") else
            "linux-x86_64" if sys.platform.startswith("linux") and machine in ("amd64", "x86_64") else
            "macos-arm64" if sys.platform == "darwin" and machine == "arm64" else "unsupported")


def isolated_profile(workspace):
    profile = workspace / "profile"
    profile.mkdir()
    environment = cli.clean_environment(os.environ)
    environment = {key: value for key, value in environment.items() if not key.upper().startswith("SED_")}
    for name, relative in {"HOME": "home", "USERPROFILE": "home", "APPDATA": "roaming",
            "LOCALAPPDATA": "local", "XDG_CONFIG_HOME": "config", "XDG_DATA_HOME": "data",
            "XDG_STATE_HOME": "state", "XDG_CACHE_HOME": "cache", "TMP": "temp", "TEMP": "temp",
            "TMPDIR": "temp"}.items():
        directory = profile / relative
        directory.mkdir(exist_ok=True)
        environment[name] = str(directory)
    sentinels = []
    for relative in ("home/.red/recovery/sentinel", "config/RED/red.ini", "roaming/RED/red.ini",
                     "local/RED/recovery/sentinel", "home/.config/RED/sed.ini"):
        path = profile / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"Synthetic normal-storage sentinel; installation check must not change this.\n")
        sentinels.append(path)
    sentinels.append(profile / "config/RED/unexpected.ini")
    return environment, sentinels


def perform_acceptance(request_path):
    scratch = request_path.parent
    state_path = scratch / "worker-state.json"
    request = json_object(read_bounded(request_path, 32768, time.monotonic() + 5))
    deadline = time.monotonic() + request["timeoutSeconds"]
    state = {"status": "running", "phase": "archive-snapshot", "commands": [],
             "acceptanceKind": "package-native-installation", "nativeInstallationAccepted": False,
             "guiRenderAccepted": False, "guiWorkflowAccepted": False}

    class Records(list):
        def append(self, value):
            super().append(value)
            cli.write_state(state_path, state)

    records = Records()
    state["commands"] = records
    cli.write_state(state_path, state)
    try:
        archive = Path(request["archive"])
        cli.require(archive.is_file() and not archive.is_symlink(), "Archive must be a regular file")
        snapshot = scratch / "archive.snapshot"
        digest, count = hashlib.sha256(), 0
        with archive.open("rb") as source, snapshot.open("xb") as output:
            while block := source.read(1024 * 1024):
                cli.remaining(deadline)
                count += len(block)
                cli.require(count <= cli.MAX_TOTAL_BYTES, "Archive exceeds size budget")
                digest.update(block)
                output.write(block)
        cli.require(digest.hexdigest() == request["archiveSha256"], "Archive SHA-256 mismatch")
        state.update(archiveSha256Verified=digest.hexdigest(), phase="extraction")
        cli.write_state(state_path, state)
        package = cli.extract_archive(snapshot, scratch / "extracted", deadline)
        state.update(cli.verify_manifest(package, request["version"], request["platform"], request["sourceCommit"], deadline))
        state["phase"] = "host-admission"
        cli.require(native_host_platform() == request["platform"], "Package must execute on its matching native host")
        _, _, runner = cli.verify_layout(package, request["platform"])
        executable = runner.parents[2] / ("red-editor.exe" if request["platform"] == "windows-x86_64" else "red-editor")
        manifest = json_object(read_bounded(package / "RELEASE_MANIFEST.json", cli.MAX_MANIFEST_BYTES, deadline))
        qt_version = manifest.get("qt_version" if request["platform"] == "windows-x86_64" else "qtVersion")
        cli.require(isinstance(qt_version, str) and re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", qt_version),
                    "Package manifest must identify the exact Qt version")
        workspace = scratch / "commands"
        workspace.mkdir()
        environment, owned_sentinels = isolated_profile(workspace)
        sentinels = capture_storage_sentinels([*owned_sentinels, *request.get("userStorageSentinels", [])], deadline)
        state.update(userStorageSentinelsBefore=sentinels, sentinelScope="owned-profile-and-explicit-files",
                     registryStateVerified=False)
        if request["platform"] == "macos-arm64":
            state["phase"] = "extracted-signatures"
            cli.verify_signatures(package, workspace, environment, deadline, records)
            state["macOsExtractedSignaturesVerified"] = True
        state["phase"] = "native-installation"
        cli.write_state(state_path, state)
        # Exercise Unicode through the real public command on every host.
        report = workspace / "native-report-漢字-Δ"
        state["nativeReportDirectory"] = str(report)
        native = exercise_native([str(executable)], executable, report, workspace, environment, deadline,
            request["version"], qt_version, request["platform"], records)
        state["phase"] = "final-byte-verification"
        cli.write_state(state_path, state)
        final = cli.verify_manifest(package, request["version"], request["platform"], request["sourceCommit"], deadline)
        cli.require(final["manifestSha256"] == state["manifestSha256"], "Package manifest changed during native acceptance")
        cli.require(cli.hash_file(archive, deadline) == request["archiveSha256"], "Original archive changed during native acceptance")
        verify_storage_sentinels(sentinels, deadline)
        cli.require(native.get("nativeInstallationAccepted") is True, "Native acceptance did not finish")
        state.update(native)
        state.update(status="passed", phase="complete", packageBytesUnchanged=True, userStorageSentinelsUnchanged=True)
    except Exception as error:
        state.update(status="failed", nativeInstallationAccepted=False, error=f"{type(error).__name__}: {error}")
    cli.write_state(state_path, state)
    return 0 if state["status"] == "passed" else 1


def run_acceptance(archive, archive_sha256, version, platform, commit, timeout=180,
                   scratch_root=None, user_storage_sentinels=()):
    cli.require(re.fullmatch("[0-9a-f]{64}", archive_sha256) is not None, "Expected archive SHA-256 is required")
    cli.require(re.fullmatch("[0-9a-f]{40}", commit) is not None, "Exact source commit is required")
    cli.require(re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version) is not None, "Exact release version is required")
    cli.require(platform in ("windows-x86_64", "linux-x86_64", "macos-arm64"), "Unsupported package platform")
    cli.require(type(timeout) in (int, float) and 1 <= timeout <= 600, "Timeout must be 1..600 seconds")
    cli.require(len(user_storage_sentinels) <= 32 and all(Path(item).is_absolute() and len(str(item)) <= 4096
                for item in user_storage_sentinels), "Storage sentinels require at most 32 explicit absolute files")
    scratch = Path(tempfile.mkdtemp(prefix="red-package-native-", dir=scratch_root)).resolve()
    request = {"archive": str(Path(archive).absolute()), "archiveSha256": archive_sha256, "version": version,
               "platform": platform, "sourceCommit": commit, "timeoutSeconds": timeout,
               "userStorageSentinels": [str(path) for path in user_storage_sentinels]}
    request_path = scratch / "request.json"
    request_path.write_text(json.dumps(request), encoding="utf-8")
    result = cli.supervise([sys.executable, "-B", str(Path(__file__).resolve()), "--worker", str(request_path)],
                           scratch / "worker-state.json", timeout)
    result["nativeInstallationAccepted"] = result.get("status") == "passed" and result.get("nativeInstallationAccepted") is True
    result.update(acceptanceKind="package-native-installation", guiRenderAccepted=False, guiWorkflowAccepted=False,
                  workspace=str(scratch), archiveSha256Expected=archive_sha256, sourceCommitExpected=commit,
                  versionExpected=version, platformExpected=platform)
    return result


def publish_receipt(path, result):
    descriptor, name = tempfile.mkstemp(prefix=".red-native-receipt-", suffix=".tmp", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(json.dumps(result, indent=2) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        # Same-directory link is atomic create-only; never replace a race winner.
        os.link(temporary, path)
    finally:
        temporary.unlink()


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--worker":
        cli.require(sys.stdin.buffer.readline(16) == b"start\n", "Missing supervisor admission")
        return perform_acceptance(Path(sys.argv[2]))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--archive-sha256", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", required=True, choices=("windows-x86_64", "linux-x86_64", "macos-arm64"))
    parser.add_argument("--commit", required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--timeout-seconds", type=float, default=180)
    parser.add_argument("--scratch-root", type=Path)
    parser.add_argument("--user-storage-sentinel", type=Path, action="append", default=[])
    args = parser.parse_args()
    try:
        cli.require(not args.receipt.exists() and not args.receipt.is_symlink(), "Receipt destination already exists")
        try:
            result = run_acceptance(args.archive, args.archive_sha256, args.version, args.platform, args.commit,
                                    args.timeout_seconds, args.scratch_root, args.user_storage_sentinel)
        except Exception as error:
            result = {"acceptanceKind": "package-native-installation", "status": "failed",
                      "nativeInstallationAccepted": False, "guiRenderAccepted": False,
                      "guiWorkflowAccepted": False, "error": str(error)}
        publish_receipt(args.receipt, result)
        print(json.dumps({"status": result["status"], "receipt": str(args.receipt),
                          "nativeInstallationAccepted": result["nativeInstallationAccepted"], "guiWorkflowAccepted": False}))
        return 0 if result["status"] == "passed" else 1
    except (OSError, cli.AcceptanceError) as error:
        print(f"Package native acceptance: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
