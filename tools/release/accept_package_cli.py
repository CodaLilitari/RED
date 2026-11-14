"""Bounded extracted-package CLI acceptance; never a GUI/render acceptance claim."""
import hashlib
import argparse
import importlib.util
import gzip
import json
import os
from pathlib import Path
import platform as host
import re
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import unicodedata
import zipfile

MAX_ENTRIES = 20000
MAX_FILE_BYTES = 512 * 1024 * 1024
MAX_TOTAL_BYTES = 4 * 1024 * 1024 * 1024
MAX_MANIFEST_BYTES = 8 * 1024 * 1024
MAX_METADATA_BYTES = 16 * 1024 * 1024


class AcceptanceError(Exception):
    pass


def safe_parts(name):
    require(isinstance(name, str) and 0 < len(name) <= 4096, "Invalid archive path length")
    parts = tuple(name.split("/"))
    for part in parts:
        require(part not in ("", ".", "..") and not part.endswith((" ", ".")), "Unsafe path component")
        require(not any(ord(c) < 32 or c in '\\:<>"|?*' for c in part), "Unsafe portable path")
        require(not re.fullmatch(r"(?i)(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\..*)?", part),
                "Reserved device path")
    return parts


def require(condition, message):
    if not condition:
        raise AcceptanceError(message)


def portable_key(name):
    return unicodedata.normalize("NFC", name).casefold()


def remaining(deadline):
    value = deadline - time.monotonic()
    require(value > 0, "Package acceptance deadline exceeded")
    return value


def hash_file(path, deadline):
    digest = hashlib.sha256()
    count = 0
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            remaining(deadline)
            count += len(block)
            require(count <= MAX_TOTAL_BYTES, "File exceeds hash budget")
            digest.update(block)
    return digest.hexdigest()


def link_target(parts, target):
    require(isinstance(target, str) and 0 < len(target) <= 4096 and not target.startswith("/"),
            "Invalid symlink target")
    resolved = list(parts[:-1])
    for component in target.split("/"):
        if component == "..":
            require(bool(resolved), "Symlink escapes package")
            resolved.pop()
        elif component != ".":
            safe_parts(component)
            resolved.append(component)
    require(bool(resolved), "Symlink target is package parent")
    return tuple(resolved)


def admitted_archive_stream(archive, destination, deadline):
    if zipfile.is_zipfile(archive):
        with archive.open("rb") as source:
            source.seek(0, os.SEEK_END)
            size = source.tell()
            source.seek(max(0, size - 65557))
            tail = source.read(65557)
        position = tail.rfind(b"PK\x05\x06")
        require(position >= 0 and len(tail) - position >= 22, "ZIP end metadata is missing")
        _, disk, directory_disk, local_count, count, directory_bytes, offset, comment = struct.unpack(
            "<4s4H2LH", tail[position:position + 22])
        require(disk == directory_disk == 0 and local_count == count and count < 65535
                and count <= MAX_ENTRIES and directory_bytes <= MAX_METADATA_BYTES
                and offset != 0xffffffff and offset + directory_bytes <= size
                and position + 22 + comment == len(tail), "ZIP metadata exceeds bounds or uses unsupported ZIP64/multidisk framing")
        remaining(deadline)
        return zipfile.ZipFile(archive)

    # A streaming size check alone is too late for tarfile's PAX/GNU metadata
    # parser. Bound decompression and every raw extension before that parser.
    descriptor, name = tempfile.mkstemp(prefix="admitted-tar-", suffix=".tar", dir=destination.parent)
    uncompressed = Path(name)
    total = 0
    with os.fdopen(descriptor, "wb") as output, gzip.open(archive, "rb") as source:
        while block := source.read(1024 * 1024):
            remaining(deadline)
            total += len(block)
            require(total <= MAX_TOTAL_BYTES, "TAR decompression exceeds budget")
            output.write(block)
    require(total % 512 == 0, "Truncated TAR framing")
    metadata, count = 0, 0
    with uncompressed.open("rb") as source:
        while header := source.read(512):
            remaining(deadline)
            if not any(header):
                while trailing := source.read(1024 * 1024):
                    remaining(deadline)
                    require(not any(trailing), "TAR has nonzero trailing data")
                break
            info = tarfile.TarInfo.frombuf(header, "utf-8", "surrogateescape")
            count += 1
            require(count <= MAX_ENTRIES, "TAR entry budget exceeded")
            require(info.type in (tarfile.REGTYPE, tarfile.AREGTYPE, tarfile.DIRTYPE, tarfile.SYMTYPE,
                                  tarfile.XHDTYPE, tarfile.XGLTYPE, tarfile.GNUTYPE_LONGNAME, tarfile.GNUTYPE_LONGLINK),
                    "TAR contains unsupported entry type")
            require(0 <= info.size <= MAX_FILE_BYTES, "TAR file exceeds size budget")
            if info.type in (tarfile.XHDTYPE, tarfile.XGLTYPE, tarfile.GNUTYPE_LONGNAME, tarfile.GNUTYPE_LONGLINK):
                metadata += info.size
                require(info.size <= 65536 and metadata <= MAX_METADATA_BYTES, "TAR metadata exceeds budget")
            skip = ((info.size + 511) // 512) * 512
            require(source.tell() + skip <= total, "TAR member exceeds available bytes")
            source.seek(skip, os.SEEK_CUR)
    return tarfile.open(uncompressed, "r:")


def extract_archive(archive, destination, deadline):
    require(not destination.exists(), "Extraction destination already exists")
    remaining(deadline)
    entries = []
    stream = admitted_archive_stream(archive, destination, deadline)
    try:
        total = 0
        for item in stream.infolist() if isinstance(stream, zipfile.ZipFile) else stream:
            remaining(deadline)
            require(len(entries) < MAX_ENTRIES, "Archive entry budget exceeded")
            is_zip = isinstance(stream, zipfile.ZipFile)
            name = item.filename if is_zip else item.name
            directory = item.is_dir() if is_zip else item.isdir()
            parts = safe_parts(name[:-1] if directory and name.endswith("/") else name)
            mode = item.external_attr >> 16 if is_zip else item.mode
            symbolic = stat.S_ISLNK(mode) if is_zip else item.issym()
            regular = (stat.S_IFMT(mode) in (0, stat.S_IFREG)) if is_zip else item.isfile()
            require(directory or symbolic or regular, "Archive contains unsupported entry type")
            if not is_zip:
                require(not item.islnk() and not item.sparse, "Hardlinks and sparse files are refused")
            size = item.file_size if is_zip else item.size
            require(0 <= size <= MAX_FILE_BYTES, "Archive file exceeds size budget")
            total += size
            require(total <= MAX_TOTAL_BYTES, "Archive exceeds expanded size budget")
            target = None
            if symbolic:
                require(size <= 4096, "Symlink target exceeds budget")
                if is_zip:
                    with stream.open(item) as source:
                        target = source.read(4097).decode("utf-8", errors="strict")
                else:
                    target = item.linkname
                link_target(parts, target)
            entries.append((parts, "directory" if directory else "symlink" if symbolic else "file", item, target, mode))

        # Include implied directories so differently cased parents cannot alias.
        paths, explicit = {}, set()
        for parts, kind, _, _, _ in entries:
            key = tuple(portable_key(p) for p in parts)
            require(key not in explicit, "Archive has duplicate/case-alias entries")
            explicit.add(key)
            for length in range(1, len(parts) + 1):
                prefix, original = key[:length], parts[:length]
                wanted = kind if length == len(parts) else "directory"
                previous = paths.get(prefix)
                require(previous is None or previous == (original, wanted), "Archive paths collide")
                paths[prefix] = (original, wanted)
        manifest_paths = [parts for parts, kind, _, _, _ in entries
                          if kind == "file" and parts[-1] == "RELEASE_MANIFEST.json"]
        require(len(manifest_paths) == 1 and len(manifest_paths[0]) in (1, 2),
                "Archive must have one package-root manifest")
        prefix = manifest_paths[0][:-1]
        require(all(parts[:len(prefix)] == prefix for parts, _, _, _, _ in entries), "Multiple archive roots")
        for parts, kind, _, target, _ in entries:
            if kind == "symlink":
                resolved = link_target(parts, target)
                require(resolved[:len(prefix)] == prefix and len(resolved) > len(prefix), "Symlink escapes package root")

        destination.mkdir()
        for parts, kind, item, _, mode in entries:
            remaining(deadline)
            target = destination.joinpath(*parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            if kind == "directory":
                target.mkdir(exist_ok=True)
            elif kind == "file":
                source = stream.open(item) if isinstance(stream, zipfile.ZipFile) else stream.extractfile(item)
                require(source is not None, "Missing archive data")
                expected = item.file_size if isinstance(stream, zipfile.ZipFile) else item.size
                count = 0
                with source, target.open("xb") as output:
                    while block := source.read(min(1024 * 1024, expected - count + 1)):
                        remaining(deadline)
                        count += len(block)
                        require(count <= expected, "Expanded archive data exceeds declared size")
                        output.write(block)
                require(count == expected, "Truncated archive data")
                target.chmod(0o755 if mode & 0o111 else 0o644)
        # Compare resolved link targets with the same canonical root even when
        # the extraction parent is an OS alias, such as /var on macOS.
        package = destination.joinpath(*prefix).resolve(strict=True)
        for parts, kind, _, target, _ in entries:
            if kind == "symlink":
                destination.joinpath(*parts).symlink_to(target)
        for parts, kind, _, _, _ in entries:
            if kind == "symlink":
                resolved = destination.joinpath(*parts).resolve(strict=True)
                require(resolved.is_relative_to(package), "Symlink chain leaves package")
        return package
    except (OSError, ValueError, UnicodeError, tarfile.TarError, zipfile.BadZipFile) as error:
        raise AcceptanceError(str(error)) from error
    finally:
        stream.close()


def verify_manifest(package, version, platform, commit, deadline):
    manifest = package / "RELEASE_MANIFEST.json"
    require(manifest.is_file() and not manifest.is_symlink(), "Missing regular package manifest")
    with manifest.open("rb") as stream:
        raw = stream.read(MAX_MANIFEST_BYTES + 1)
    require(len(raw) <= MAX_MANIFEST_BYTES, "Manifest exceeds size budget")
    try:
        document = json.loads(raw)
    except (ValueError, UnicodeError) as error:
        raise AcceptanceError("Invalid manifest JSON") from error
    require(isinstance(document, dict) and document.get("version") == version, "Package version mismatch")
    windows = document.get("format") == 1 and "schemaVersion" not in document
    require((windows and platform == "windows-x86_64")
            or (not windows and document.get("schemaVersion") == 1 and platform != "windows-x86_64"),
            "Unsupported manifest schema for platform")
    require(document.get("platform") == ("Windows" if windows and platform == "windows-x86_64" else platform),
            "Package platform mismatch")
    has_commit = "sourceCommit" in document
    require((windows and not has_commit) or document.get("sourceCommit") == commit, "Package source commit mismatch")
    entries = document.get("files")
    require(isinstance(entries, list) and 0 < len(entries) <= MAX_ENTRIES, "Manifest entry budget invalid")
    declared, folded = set(), set()
    total = 0
    for entry in entries:
        remaining(deadline)
        require(isinstance(entry, dict), "Invalid manifest entry")
        parts = safe_parts(entry.get("path"))
        name = "/".join(parts)
        require(name != "RELEASE_MANIFEST.json" and portable_key(name) not in folded, "Duplicate manifest entry")
        folded.add(portable_key(name))
        declared.add(name)
        path = package.joinpath(*parts)
        kind = "file" if windows else entry.get("type")
        if kind == "symlink":
            target = entry.get("target")
            link_target(parts, target)
            require(path.is_symlink() and os.readlink(path) == target, "Manifest symlink mismatch")
            require(path.resolve(strict=True).is_relative_to(package), "Manifest symlink escapes package")
        else:
            size = entry.get("bytes" if windows else "size")
            require(kind == "file" and type(size) is int and 0 <= size <= MAX_FILE_BYTES, "Invalid manifest file size")
            total += size
            require(total <= MAX_TOTAL_BYTES, "Manifest expanded size exceeds budget")
            require(path.is_file() and not path.is_symlink() and path.stat().st_size == size, "Manifest file size/type mismatch")
            require(re.fullmatch("[0-9a-f]{64}", str(entry.get("sha256", ""))) is not None
                    and hash_file(path, deadline) == entry["sha256"], "Manifest SHA-256 mismatch")
    actual = set()
    for directory, folders, files in os.walk(package, followlinks=False):
        remaining(deadline)
        for name in files + [name for name in folders if (Path(directory) / name).is_symlink()]:
            relative = (Path(directory) / name).relative_to(package).as_posix()
            if relative != "RELEASE_MANIFEST.json":
                require(len(actual) < MAX_ENTRIES, "Extracted file budget exceeded")
                actual.add(relative)
    require(actual == declared, "Manifest does not exactly cover extracted payload")
    return {"manifestSha256": hashlib.sha256(raw).hexdigest(), "fileCount": len(entries),
            "manifestSourceCommitVerified": has_commit}


def clean_environment(source):
    environment = {name: value for name, value in source.items()
                   if not name.upper().startswith(("QT_", "QML", "LD_", "DYLD_", "PYTHON"))}
    environment["PATH"] = (str(Path(environment.get("SystemRoot", r"C:\Windows")) / "System32")
                           if os.name == "nt" else "/usr/bin:/bin:/usr/sbin:/sbin")
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    return environment


def run_command(command, cwd, environment, deadline, input_bytes=b"", log_limit=2097152):
    remaining(deadline)
    require(0 < log_limit <= 8 * 1024 * 1024 and len(input_bytes) <= 4096, "Invalid process I/O budget")
    # Production calls run inside the supervised worker's owned Job/group.
    process = subprocess.Popen(command, cwd=cwd, env=environment, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output = [bytearray(), bytearray()]
    lock, exceeded = threading.Lock(), threading.Event()

    def read(pipe, index):
        while block := pipe.read(4096):
            with lock:
                available = log_limit - sum(map(len, output))
                output[index].extend(block[:max(0, available)])
                if len(block) > available:
                    exceeded.set()
                    break

    readers = [threading.Thread(target=read, args=(pipe, index), daemon=True)
               for index, pipe in enumerate((process.stdout, process.stderr))]
    for reader in readers:
        reader.start()
    try:
        process.stdin.write(input_bytes)
        process.stdin.close()
        while process.poll() is None:
            require(not exceeded.is_set(), "Process aggregate log limit exceeded")
            time.sleep(min(0.01, remaining(deadline)))
        for reader in readers:
            reader.join(timeout=min(0.25, remaining(deadline)))
        require(not any(reader.is_alive() for reader in readers), "Child descendants retained output pipes")
        require(not exceeded.is_set(), "Process aggregate log limit exceeded")
        return {"exitCode": process.returncode, "stdout": bytes(output[0]), "stderr": bytes(output[1])}
    finally:
        if process.poll() is None:
            process.kill()
        process.wait(timeout=2)
        for reader in readers:
            reader.join(timeout=0.25)
        if not process.stdin.closed:
            process.stdin.close()
        for pipe, reader in zip((process.stdout, process.stderr), readers):
            if not reader.is_alive():
                pipe.close()


def exercise_protocols(agent, mcp, workspace, environment, deadline, records):
    def invoke(command, expected=0, input_bytes=b"", parse=True):
        result = recorded_command(command, workspace, environment, deadline, records, input_bytes)
        require(result["exitCode"] == expected, "Packaged command returned an unexpected exit code")
        if not parse:
            return result
        try:
            value = json.loads(result["stdout"])
        except (ValueError, UnicodeError) as error:
            raise AcceptanceError("Packaged command did not return JSON") from error
        require(isinstance(value, dict), "Packaged command did not return a JSON object")
        return value

    help_result = invoke([*agent, "--help"])
    require(help_result.get("ok") is True and help_result.get("protocol") == "sed-agent"
            and help_result.get("schemaVersion") == 1 and bool(help_result.get("usage")), "Agent help identity mismatch")
    schema = invoke([*agent, "schema", "commands"])
    require(schema.get("type") == "string" and isinstance(schema.get("enum"), list)
            and {"new", "inspect", "validate"}.issubset(schema["enum"]), "Agent command schema is incomplete")
    fixture = workspace / "acceptance-fixture.sed"
    require(not fixture.exists(), "Acceptance fixture destination already exists")
    created = invoke([*agent, "new", str(fixture), "mots"])
    require(created.get("ok") is True and created.get("command") == "new" and created.get("kind") == "mots"
            and fixture.is_file() and 0 < fixture.stat().st_size <= MAX_FILE_BYTES, "Agent did not create a MotS fixture")
    digest = hash_file(fixture, deadline)
    require(created.get("outputSha256") == digest, "Created fixture hash mismatch")
    inspected = invoke([*agent, "inspect", str(fixture)])
    require(inspected.get("ok") is True and inspected.get("command") == "inspect"
            and inspected.get("kind") == 1 and inspected.get("counts", {}).get("sectors") == 1
            and inspected.get("inputSha256") == digest, "Agent fixture inspection mismatch")
    validated = invoke([*agent, "validate", str(fixture)])
    require(validated.get("ok") is True and validated.get("command") == "validate" and validated.get("valid") is True
            and validated.get("counts", {}).get("sectors") == 1 and validated.get("inputSha256") == digest,
            "Agent did not validate the generated fixture")
    refused = invoke([*agent, "__package_acceptance_invalid_command__"], expected=2)
    require(refused.get("ok") is False and refused.get("exitCode") == 2, "Agent usage refusal mismatch")

    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
            "protocolVersion": "2024-11-05", "capabilities": {},
            "clientInfo": {"name": "red-package-cli-acceptance", "version": "1"}}},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
        {"jsonrpc": "2.0", "id": 2, "method": "ping"},
        {"jsonrpc": "2.0", "id": 3, "method": "tools/list"}]
    incoming = ("\n".join(json.dumps(message) for message in messages) + "\n{invalid\n").encode()
    result = invoke([*mcp, "--workspace", str(workspace / "mcp-workspace")], input_bytes=incoming, parse=False)
    try:
        responses = [json.loads(line) for line in result["stdout"].splitlines()]
    except (ValueError, UnicodeError) as error:
        raise AcceptanceError("MCP returned malformed JSON") from error
    require(len(responses) == 4 and all(isinstance(row, dict) and row.get("jsonrpc") == "2.0" for row in responses),
            "MCP response framing mismatch")
    require([row.get("id") for row in responses] == [1, 2, 3, None], "MCP response IDs mismatch")
    initialized = responses[0].get("result", {})
    require(initialized.get("protocolVersion") == "2024-11-05"
            and initialized.get("serverInfo", {}).get("name") == "sed-mcp"
            and "tools" in initialized.get("capabilities", {}), "MCP initialization identity mismatch")
    require(responses[1].get("result") == {}, "MCP ping failed")
    catalog = responses[2].get("result", {}).get("tools")
    require(isinstance(catalog, list) and all(isinstance(row, dict) for row in catalog)
            and {"sed_inspect", "sed_diagnose"}.issubset({row.get("name") for row in catalog}), "MCP tool catalog incomplete")
    require(responses[3].get("error", {}).get("code") == -32700, "MCP malformed-input refusal mismatch")
    require(hash_file(fixture, deadline) == digest, "Read-only package inspection mutated its fixture")
    return {"fixtureSha256": digest, "mcpProtocolVersion": initialized["protocolVersion"], "mcpToolCount": len(catalog)}


def recorded_command(command, workspace, environment, deadline, records, input_bytes=b""):
    record = {"command": command, "status": "running"}
    records.append(record)
    try:
        result = run_command(command, workspace, environment, min(deadline, time.monotonic() + 20), input_bytes)
        record.update({"status": "exited", "exitCode": result["exitCode"]})
        for name in ("stdout", "stderr"):
            record[name + "Bytes"] = len(result[name])
            record[name + "Sha256"] = hashlib.sha256(result[name]).hexdigest()
            record[name + "Preview"] = result[name][:4096].decode("utf-8", errors="replace")
        return result
    except Exception as error:
        record.update({"status": "failed", "error": str(error)})
        raise


def supervise(command, state_path, timeout):
    deadline = time.monotonic() + timeout
    module_path = Path(__file__).resolve().parents[1] / "engine-capture" / "invoke_engine_capture.py"
    spec = importlib.util.spec_from_file_location("release_owned_process_tree", module_path)
    owner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(owner)
    tree = owner.WorkerTree()
    error, code = None, None
    try:
        process = tree.start(command)
        try:
            code = process.wait(timeout=remaining(deadline))
        except subprocess.TimeoutExpired:
            error = "Package acceptance deadline exceeded"
    except Exception as failure:
        error = str(failure)
    finally:
        try:
            tree.close()
        except Exception as failure:
            error = f"Owned worker cleanup failed: {failure}"
    result = {}
    if state_path.is_file():
        try:
            with state_path.open("rb") as stream:
                raw = stream.read(MAX_MANIFEST_BYTES + 1)
            require(len(raw) <= MAX_MANIFEST_BYTES, "Worker receipt exceeds size budget")
            result = json.loads(raw)
            require(isinstance(result, dict), "Worker receipt is not an object")
        except (OSError, ValueError, AcceptanceError) as failure:
            error, result = str(failure), {}
    if error or code != 0 or result.get("status") != "passed":
        result.update({"status": "failed", "error": error or result.get("error") or "Worker failed without a success receipt"})
    result["guiRenderAccepted"] = False
    return result


def run_acceptance(archive, archive_sha256, version, platform, commit, timeout=120, scratch_root=None):
    require(re.fullmatch("[0-9a-f]{64}", archive_sha256) is not None, "Expected archive SHA-256 is required")
    require(re.fullmatch("[0-9a-f]{40}", commit) is not None, "Exact source commit is required")
    require(re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version) is not None, "Exact release version is required")
    require(platform in ("windows-x86_64", "linux-x86_64", "macos-arm64"), "Unsupported package platform")
    require(type(timeout) in (int, float) and 1 <= timeout <= 600, "Timeout must be 1..600 seconds")
    scratch = Path(tempfile.mkdtemp(prefix="red-package-cli-", dir=scratch_root)).resolve()
    request = {"archive": str(Path(archive).absolute()), "archiveSha256": archive_sha256,
               "version": version, "platform": platform, "sourceCommit": commit, "timeoutSeconds": timeout}
    request_path = scratch / "request.json"
    request_path.write_text(json.dumps(request), encoding="utf-8")
    state_path = scratch / "worker-state.json"
    result = supervise([sys.executable, "-B", str(Path(__file__).resolve()), "--worker", str(request_path)], state_path, timeout)
    result.update({"acceptanceKind": "package-cli", "guiRenderAccepted": False, "workspace": str(scratch),
                   "archiveSha256Expected": archive_sha256, "sourceCommitExpected": commit,
                   "versionExpected": version, "platformExpected": platform})
    return result


def write_state(path, state):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def verify_layout(package, platform):
    if platform == "windows-x86_64":
        binary_directory, suffix = package, ".exe"
    elif platform == "linux-x86_64":
        binary_directory, suffix = package / "usr" / "bin", ""
    else:
        binary_directory, suffix = package / "RED.app" / "Contents" / "MacOS", ""
    companions = package if platform == "macos-arm64" else binary_directory
    agent, mcp = companions / ("red-agent" + suffix), companions / ("red-mcp" + suffix)
    runner = binary_directory / "tools" / "engine-capture" / "invoke_engine_capture.py"
    required = [binary_directory / ("red-editor" + suffix), agent, mcp, runner,
                runner.parent / "README.md", runner.parent / "gameplay_capture.py",
                package / "LICENSE", package / "THIRD_PARTY_NOTICES.md"]
    required.extend(binary_directory / "data" / name for name in (
        "df2jk.lst", "ijim.tpl", "master.tpl", "mats.jlh", "mats.jll", "mots.tpl", "mtpls.jlh",
        "mtpls.jll", "shapes.tpl", "tpls.jlh", "tpls.jll"))
    if platform == "linux-x86_64":
        required.append(package / "red-editor")
    for path in required:
        require(path.is_file() and not path.is_symlink() and path.stat().st_size > 0,
                f"Missing/nonregular required package asset: {path.relative_to(package)}")
    return agent, mcp, runner


def verify_signatures(package, workspace, environment, deadline, records):
    for path in (package / "RED.app", package / "red-agent", package / "red-mcp"):
        arguments = ["/usr/bin/codesign", "--verify", "--strict", "--verbose=2"]
        if path.suffix == ".app":
            arguments.append("--deep")
        result = recorded_command([*arguments, str(path)], workspace, environment, deadline, records)
        require(result["exitCode"] == 0, "Extracted macOS signature verification failed")


def worker(request_path):
    # No parsing, extraction or child execution before the supervisor owns us.
    require(sys.stdin.buffer.readline(16) == b"start\n", "Missing supervisor admission")
    request = json.loads(request_path.read_text(encoding="utf-8"))
    scratch, state_path = request_path.parent, request_path.parent / "worker-state.json"
    deadline = time.monotonic() + request["timeoutSeconds"]
    state = {"status": "running", "phase": "archive-snapshot", "commands": [], "guiRenderAccepted": False}

    class Records(list):
        def append(self, value):
            super().append(value)
            write_state(state_path, state)

    records = Records()
    state["commands"] = records
    write_state(state_path, state)
    try:
        archive = Path(request["archive"])
        require(archive.is_file() and not archive.is_symlink(), "Archive must be a regular file")
        snapshot = scratch / "archive.snapshot"
        digest, count = hashlib.sha256(), 0
        with archive.open("rb") as source, snapshot.open("xb") as output:
            while block := source.read(1024 * 1024):
                remaining(deadline)
                count += len(block)
                require(count <= MAX_TOTAL_BYTES, "Archive exceeds size budget")
                digest.update(block)
                output.write(block)
        require(digest.hexdigest() == request["archiveSha256"], "Archive SHA-256 mismatch")
        state.update({"archiveSha256Verified": digest.hexdigest(), "phase": "extraction"})
        write_state(state_path, state)
        package = extract_archive(snapshot, scratch / "extracted", deadline)
        state.update(verify_manifest(package, request["version"], request["platform"], request["sourceCommit"], deadline))
        state["phase"] = "host-admission"
        machine = host.machine().lower()
        observed = ("windows-x86_64" if sys.platform == "win32" and machine in ("amd64", "x86_64") else
                    "linux-x86_64" if sys.platform.startswith("linux") and machine in ("amd64", "x86_64") else
                    "macos-arm64" if sys.platform == "darwin" and machine == "arm64" else "unsupported")
        require(observed == request["platform"], "Package must execute on its matching native host")
        agent, mcp, runner = verify_layout(package, request["platform"])
        workspace = scratch / "commands"
        workspace.mkdir()
        environment = clean_environment(os.environ)
        if request["platform"] == "macos-arm64":
            state["phase"] = "extracted-signatures"
            verify_signatures(package, workspace, environment, deadline, records)
            state["macOsExtractedSignaturesVerified"] = True
        state["phase"] = "packaged-cli-protocols"
        state.update(exercise_protocols([str(agent)], [str(mcp)], workspace, environment, deadline, records))
        state["phase"] = "capture-runner-admission"
        result = recorded_command([sys.executable, "-B", str(runner), "--help"], workspace, environment, deadline, records)
        require(result["exitCode"] == 0 and b"--validate-only" in result["stdout"], "Packaged capture runner help failed")
        invalid = workspace / "invalid-capture-request.json"
        invalid.write_text("{}", encoding="utf-8")
        result = recorded_command([sys.executable, "-B", str(runner), str(invalid), "--validate-only"],
                                  workspace, environment, deadline, records)
        require(result["exitCode"] == 2 and json.loads(result["stderr"]).get("status") == "refused",
                "Capture runner malformed-request refusal failed")
        require(not (workspace / "launcher-receipt.json").exists(), "Capture inspection unexpectedly staged output")
        state["phase"] = "final-byte-verification"
        final_manifest = verify_manifest(package, request["version"], request["platform"], request["sourceCommit"], deadline)
        require(final_manifest["manifestSha256"] == state["manifestSha256"], "Package manifest changed during acceptance")
        require(hash_file(archive, deadline) == request["archiveSha256"], "Original archive changed during acceptance")
        state.update({"status": "passed", "phase": "complete", "packageBytesUnchanged": True})
    except Exception as error:
        state.update({"status": "failed", "error": f"{type(error).__name__}: {error}"})
    write_state(state_path, state)
    return 0 if state["status"] == "passed" else 1


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--worker":
        return worker(Path(sys.argv[2]))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--archive-sha256", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", required=True, choices=("windows-x86_64", "linux-x86_64", "macos-arm64"))
    parser.add_argument("--commit", required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--timeout-seconds", type=float, default=120)
    parser.add_argument("--scratch-root", type=Path)
    args = parser.parse_args()
    try:
        require(not args.receipt.exists(), "Receipt destination already exists")
        try:
            result = run_acceptance(args.archive, args.archive_sha256, args.version, args.platform,
                                    args.commit, args.timeout_seconds, args.scratch_root)
        except Exception as error:
            result = {"acceptanceKind": "package-cli", "status": "failed", "guiRenderAccepted": False, "error": str(error)}
        with args.receipt.open("x", encoding="utf-8") as stream:
            stream.write(json.dumps(result, indent=2) + "\n")
        print(json.dumps({"status": result["status"], "receipt": str(args.receipt), "guiRenderAccepted": False}))
        return 0 if result["status"] == "passed" else 1
    except (OSError, AcceptanceError) as error:
        print(f"Package CLI acceptance: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
