#!/usr/bin/env python3
"""Engine capture runner (D-14 pattern port).

Stages a hash-bound file set into a fresh output directory, asks a
``sed-engine-capture-v1`` engine to render one frame to ``capture.ppm``
(P6 RGB8), and writes a ``launcher-receipt.json`` describing exactly
what happened.

The optional archive mode stages GOB/GOO files without loose episode overrides.
An optional Linux gameplay recipe selects the existing ordinary-input agent
protocol, retaining native command/state/image evidence instead of posing a camera.

Honesty contract: a completed posed run proves the staged engine wrote a
well-formed PPM; a completed gameplay run proves only its explicit recipe and
acknowledged shutdown. The receipt always carries
``effectiveCameraObserved: false`` and
``runtimeResourceResolutionObserved: false`` -- this runner does not
observe the engine's effective camera or its runtime resource
resolution, and no capture produced here is acceptance evidence for
the F-09/F-11 engine gate until a verified capture-v1 engine build
and owned game assets exist. See README.md.

The staged executable names select the engine profile, and a profile
only runs on its own operating system:

- windows: ``openjkdf2-64.exe`` (engine), ``OpenAL32.dll`` and
  ``libGameNetworkingSockets.dll`` (runtime)
- linux: ``openjkdf2`` (engine; system OpenAL), launched in its own
  process group with private Xvfb by default, or explicit SDL offscreen
  capture with a required engine-reported graphics receipt

Usage: invoke_engine_capture.py <request.json> --output <new-dir>
Exit codes: 0 completed, 1 failed (receipt written), 2 refused
(preflight; no output directory is created).
"""

import hashlib
import json
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path, PurePosixPath

ENGINE_PROFILES = {
    "windows": {"openjkdf2-64.exe": "engine", "OpenAL32.dll": "runtime",
                "libGameNetworkingSockets.dll": "runtime"},
    "linux": {"openjkdf2": "engine"},
}
RESERVED = {"capture.ppm", "capture.ppm.json", "capture-request.txt",
            "request.json", "launcher-receipt.json", "manifest-before.json",
            "manifest-after.json", "engine-stdout.log", "engine-stderr.log",
            ".capture-progress.json", ".capture-progress.json.tmp",
            ".admitted-request.json", ".openjkdf2-capture-session", "session"}
ROLES = {"engine", "runtime", "resource", "scenario", "config"}
GAMEPLAY_RESERVED = {"agent", ".openjkdf2-agent-session", "scenario.json",
                     "scenario-input.json", "gameplay-receipt.json"}


class CaptureError(Exception):
    """Preflight refusal: no output directory, no receipt."""


def require(condition, message):
    if not condition:
        raise CaptureError(message)


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8", newline="\n")


def write_progress(output, receipt):
    # A killed worker must leave either the old or the new complete checkpoint.
    temporary = output / ".capture-progress.json.tmp"
    write_json(temporary, receipt)
    os.replace(temporary, output / ".capture-progress.json")


def integer(value, low, high, name):
    require(isinstance(value, int) and not isinstance(value, bool)
            and low <= value <= high,
            f"{name} must be an integer in [{low}, {high}].")
    return value


def number(value, low, high, name):
    require(isinstance(value, (int, float)) and not isinstance(value, bool)
            and low <= value <= high,
            f"{name} must be a number in [{low}, {high}].")
    return value


def exact_keys(value, required, optional, name):
    require(isinstance(value, dict), f"{name} must be a JSON object.")
    keys = set(value)
    require(required <= keys, f"{name} is missing keys: {sorted(required - keys)}.")
    require(keys <= required | optional,
            f"{name} has unsupported keys: {sorted(keys - required - optional)}.")


def safe_destination(value):
    require(isinstance(value, str) and value and "\\" not in value,
            "File destinations must be non-empty POSIX relative paths.")
    # Inspect raw components: PurePath silently removes '.' and empty parts.
    # Use one portable contract even when admitting a Linux profile on Windows.
    for component in value.split("/"):
        require(component not in ("", ".", "..")
                and not component.endswith((".", " "))
                and not any(ord(char) < 32 or char in '<>:"|?*' for char in component),
                f"Unsafe file destination: {value!r}.")
        stem = component.split(".", 1)[0].rstrip(" .").casefold()
        require(stem not in {"con", "prn", "aux", "nul", "clock$", "conin$", "conout$"}
                and re.fullmatch(r"(?:com|lpt)[1-9\u00b9\u00b2\u00b3]", stem) is None,
                f"Device name in file destination: {value!r}.")
    require(value.split("/", 1)[0].casefold() not in RESERVED,
            f"File destination collides with a runner output: {value!r}.")
    return value


def validate_request(request):
    exact_keys(request, {"schemaVersion", "episode", "map", "files"},
               {"camera", "frame", "engineTimeoutMs", "runnerTimeoutMs", "logLimitBytes",
                "motsCompat", "graphicsBackend", "episodeArchive", "gameplay"},
               "request")
    require(request["schemaVersion"] == 1, "schemaVersion must be 1.")
    episode, level = request["episode"], request["map"]
    require(isinstance(episode, str)
            and re.fullmatch(r"[A-Za-z0-9_-]{1,31}", episode),
            "Invalid episode name.")
    require(isinstance(level, str) and re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", level)
            and "/" not in level and "\\" not in level,
            "Invalid map file name.")
    require("gameplay" in request or "camera" in request, "Posed capture requires camera.")
    normalized = None
    if "camera" in request:
        camera = request["camera"]
        exact_keys(camera, {"width", "height", "sector", "position",
                            "pitchYawRoll", "horizontalFov"}, set(), "camera")
        normalized = {
            "width": integer(camera["width"], 64, 2048, "width"),
            "height": integer(camera["height"], 64, 2048, "height"),
            "sector": integer(camera["sector"], 0, 1000000, "sector"),
            "horizontalFov": number(camera["horizontalFov"], 20, 140, "horizontalFov"),
        }
        require(normalized["width"] * normalized["height"] <= 2097152,
                "Capture pixel budget exceeded.")
        for name in ("position", "pitchYawRoll"):
            values = camera[name]
            require(isinstance(values, list) and len(values) == 3,
                    f"{name} must contain three numbers.")
            normalized[name] = [number(v, -1000000, 1000000, name) for v in values]
    gameplay = "gameplay" in request
    archive = request.get("episodeArchive")
    if "episodeArchive" in request:
        require(archive in (f"episode/{episode}.gob", f"episode/{episode}.goo"),
                "episodeArchive must be episode/<episode>.gob or episode/<episode>.goo.")
    frame = integer(request.get("frame", 3), 1, 120, "frame")
    engine_timeout = integer(request.get("engineTimeoutMs", 180000 if gameplay else 30000),
                             1000, 600000 if gameplay else 60000, "engineTimeoutMs")
    runner_timeout = integer(request.get("runnerTimeoutMs", 210000 if gameplay else 45000),
                             1000, 630000 if gameplay else 90000, "runnerTimeoutMs")
    require(runner_timeout >= engine_timeout,
            "runnerTimeoutMs must cover engineTimeoutMs.")
    log_limit = integer(request.get("logLimitBytes", 1 << 20),
                        1024, 16 << 20, "logLimitBytes")
    mots_compat = request.get("motsCompat", True)
    require(isinstance(mots_compat, bool), "motsCompat must be a boolean.")

    files = request["files"]
    require(isinstance(files, list) and files, "files must be a non-empty array.")
    if archive or gameplay:
        require(len(files) <= 512, "Archive/gameplay requests accept at most 512 staged files.")
    staged = []
    destinations = {}
    destination_keys = set()
    directory_keys = set()
    for item in files:
        exact_keys(item, {"source", "destination", "sha256", "role"}, set(), "file")
        dest = safe_destination(item["destination"])
        if gameplay:
            require(dest.split("/", 1)[0].casefold() not in GAMEPLAY_RESERVED,
                    f"File destination collides with a gameplay output: {dest!r}.")
        parts = dest.casefold().split("/")
        key = "/".join(parts)
        parents = {"/".join(parts[:index]) for index in range(1, len(parts))}
        require(key not in destination_keys and key not in directory_keys
                and not parents.intersection(destination_keys),
                f"Duplicate or conflicting destination: {dest}.")
        destination_keys.add(key)
        directory_keys.update(parents)
        role = item["role"]
        require(role in ROLES, f"Unsupported file role: {role!r}.")
        require(isinstance(item["sha256"], str)
                and re.fullmatch(r"[0-9a-fA-F]{64}", item["sha256"]),
                "Each file requires a SHA256 identity.")
        source = Path(item["source"])
        require(source.is_file() and not source.is_symlink(),
                f"Source file is missing or a link: {source}.")
        if role in ("engine", "runtime"):
            require("/" not in dest, "Engine binaries stage at the top level.")
        elif role == "config":
            require("/" not in dest, "Config files stage at the top level.")
        elif role == "resource":
            require(dest.startswith("resource/"),
                    "Resource files must live under resource/.")
        elif archive:
            require(re.fullmatch(r"episode/[A-Za-z0-9_-]{1,31}\.(?:gob|goo)", dest),
                    "Archive mode accepts only direct episode/*.gob or *.goo scenario files; no loose overrides.")
        else:
            require(dest.startswith(f"episode/{episode}/"),
                    "Scenario files must live under the selected episode directory.")
        destinations[dest] = role
        staged.append({"source": str(source), "destination": dest,
                       "sha256": item["sha256"].lower(), "role": role})

    profiles = []
    for name, binaries in ENGINE_PROFILES.items():
        staged_binaries = {d: r for d, r in destinations.items()
                           if r in ("engine", "runtime")}
        if staged_binaries == binaries:
            profiles.append(name)
        elif staged_binaries and set(staged_binaries) <= set(binaries):
            raise CaptureError(
                f"The {name} engine profile needs exactly: {sorted(binaries)}.")
    require(len(profiles) == 1, "Supply exactly one engine profile's binaries.")
    profile = profiles[0]
    if "graphicsBackend" in request:
        require(profile == "linux", "graphicsBackend is supported only for the Linux profile.")
        require(isinstance(request["graphicsBackend"], str)
                and request["graphicsBackend"] in ("xvfb", "sdl-offscreen"),
                "graphicsBackend must be exactly 'xvfb' or 'sdl-offscreen'.")
    for config in ("registry.json", "openjkdf2_cvars.json"):
        require(destinations.get(config) == "config",
                f"The request must stage {config} with role 'config'.")
    if archive:
        require(destinations.get(archive) == "scenario",
                "The request must stage episodeArchive with role 'scenario'.")
    else:
        require(destinations.get(f"episode/{episode}/episode.jk") == "scenario",
                "The request must stage the episode.jk scenario file.")
        require(destinations.get(f"episode/{episode}/jkl/{level}") == "scenario",
                "The request must stage the requested map under episode/<episode>/jkl/.")
    if gameplay:
        require(profile == "linux", "Gameplay capture requires an agent-enabled Linux engine.")
        require(destinations.get("resource/jk_.cd") == "resource",
                "Gameplay requires an explicit resource/jk_.cd installation marker.")
        from gameplay_capture import AgentError, validate_scenario
        try:
            recipe = validate_scenario(request["gameplay"])
        except (AgentError, ValueError, TypeError, RecursionError) as exc:
            raise CaptureError(f"Invalid gameplay recipe: {exc}") from exc
        require(recipe["launch"] == {"game": "mots" if mots_compat else "jk",
                                     "episode": episode, "map": level},
                "Gameplay launch must match episode, map and motsCompat.")

    result = {"schemaVersion": 1, "engineProfile": profile, "episode": episode,
              "map": level, **({"camera": normalized} if normalized is not None else {}), "frame": frame,
              "engineTimeoutMs": engine_timeout, "runnerTimeoutMs": runner_timeout,
              "logLimitBytes": log_limit, "motsCompat": mots_compat,
              "files": staged}
    if "graphicsBackend" in request:
        result["graphicsBackend"] = request["graphicsBackend"]
    if archive:
        result["episodeArchive"] = archive
    if gameplay:
        result["gameplay"] = recipe
    return result


def engine_executable(request):
    return next(name for name, role in ENGINE_PROFILES[request["engineProfile"]].items()
                if role == "engine")


def profile_runs_here(profile):
    return (profile == "windows" and os.name == "nt") \
        or (profile == "linux" and sys.platform.startswith("linux"))


def command_for(request, output):
    gameplay = "gameplay" in request
    command = [str(output / engine_executable(request)), "-agent" if gameplay else "-capture",
               "-autostart", "-sp"]
    if request.get("motsCompat", True):
        command.append("-motsCompat")
    command += (["-windowGUI"] if gameplay else ["-noHUD"]) + ["-episode", request["episode"],
                "-map", request["map"]]
    return command


def validate_episode_archive(request, path):
    """Read only a bounded JK v0x14 index; never extract or certify resource resolution."""
    require(path.is_file() and not path.is_symlink(), "Episode archive must be a regular file.")
    size = path.stat().st_size
    require(16 <= size <= (2 << 30), "Episode archive must contain 16 bytes to 2 GiB.")
    with path.open("rb") as stream:
        header = stream.read(16)
        require(len(header) == 16, "Truncated episode archive header.")
        magic, version, index_offset, count = struct.unpack("<4sIII", header)
        require((magic, version, index_offset) == (b"GOB ", 20, 12),
                "Only JK GOB/GOO v0x14 archives with the native index layout are supported.")
        require(1 <= count <= 65536 and 16 + count * 136 <= size,
                "Episode archive index is oversized or truncated.")
        names = set()
        for _ in range(count):
            entry = stream.read(136)
            require(len(entry) == 136, "Truncated episode archive index.")
            offset, length, raw = struct.unpack("<II128s", entry)
            require(b"\0" in raw, "Episode archive name must be NUL terminated.")
            name = raw.split(b"\0", 1)[0]
            require(name and all(32 < c < 127 and c not in b'/:"*?<>|' for c in name),
                    "Episode archive names must be canonical relative ASCII paths.")
            require(name == name.lower() and all(p not in (b"", b".", b"..") for p in name.split(b"\\")),
                    "Episode archive names must be lowercase native backslash paths without traversal.")
            require(name not in names, "Duplicate episode archive entry.")
            require(offset >= 16 + count * 136 and offset <= size and length <= size - offset,
                    "Episode archive entry is outside its payload bounds.")
            names.add(name)
    level = ("jkl\\" + request["map"].lower()).encode("ascii")
    require(b"episode.jk" in names, "Episode archive has no episode.jk entry.")
    require(level in names, "Episode archive has no requested map entry.")
    return {"path": request["episodeArchive"], "entries": count,
            "mapEntry": level.decode("ascii"), "archiveSha256": sha256_file(path),
            "looseEpisodeFilesStaged": False, "runtimeResourceResolutionObserved": False}


def request_text(request, image):
    c = request["camera"]
    text = str(image)
    require(image.is_absolute() and image.suffix == ".ppm" and len(text) <= 1023
            and text.isascii()
            and not any(ord(ch) < 32 or ch == '"' for ch in text),
            "Capture output needs an ASCII absolute .ppm path of at most 1023 characters.")
    pose = " ".join(format(v, ".17g")
                    for v in [*c["position"], *c["pitchYawRoll"], c["horizontalFov"]])
    return (f"sed-engine-capture-v1\n{c['width']} {c['height']} {c['sector']} "
            f"{request['frame']} {request['engineTimeoutMs']}\n{pose}\n{image}\n")


class PrivateDisplay:
    """A private Xvfb server (Linux profile). The display number is
    allocated by Xvfb via -displayfd, TCP listening is disabled, and
    the server process is always reaped on exit."""

    def __init__(self, width=1280, height=1024):
        self.width, self.height = width, height
        self.process = None
        self.display = None

    def __enter__(self):
        xvfb = shutil.which("Xvfb")
        require(xvfb is not None,
                "Linux capture needs Xvfb (package xvfb) for a private display.")
        read_fd, write_fd = os.pipe()
        try:
            try:
                self.process = subprocess.Popen(
                    [xvfb, "-displayfd", str(write_fd), "-nolisten", "tcp",
                     "-screen", "0", f"{self.width}x{self.height}x24"],
                    pass_fds=(write_fd,), stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE)
            finally:
                os.close(write_fd)
            deadline = time.monotonic() + 10.0
            buf = b""
            os.set_blocking(read_fd, False)
            while b"\n" not in buf:
                require(self.process.poll() is None,
                        "Xvfb exited before its readiness notification.")
                require(time.monotonic() < deadline,
                        "Xvfb display did not become ready within 10 seconds.")
                try:
                    chunk = os.read(read_fd, 64)
                except BlockingIOError:
                    time.sleep(0.05)
                    continue
                require(chunk, "Xvfb closed its readiness pipe without a display number.")
                buf += chunk
            number = buf.decode("ascii").strip()
            require(number.isdigit() and int(number) <= 65535,
                    "Malformed Xvfb readiness notification.")
            self.display = f":{int(number)}"
        except BaseException:
            self.close()
            raise
        finally:
            os.close(read_fd)
        return self

    def receipt(self):
        return {"display": self.display, "tcpListening": False,
                "userVisible": False}

    def close(self):
        process = self.process
        self.process = None
        if process is None:
            return
        try:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        finally:
            if process.stderr is not None:
                process.stderr.close()

    def __exit__(self, *exc):
        self.close()
        return False


def kill_process_tree(process):
    """Kill the engine and, on POSIX, its whole process group. A
    descendant that starts a new session can escape the group; that
    limit is recorded in the receipt rather than hidden."""
    if process.poll() is not None:
        return
    if os.name == "posix":
        try:
            os.killpg(process.pid, 9)
        except (ProcessLookupError, PermissionError):
            process.kill()
    else:
        try:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=15)
        except Exception:
            process.kill()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        pass


def execute_engine(command, cwd, env, timeout_ms, stdout_path, stderr_path,
                   log_limit=1 << 20, supervised=False, on_start=None, controller=None):
    """Drain both pipes continuously under one retained-byte budget.

    The supervisor owns the worker and its descendants. Direct unit callers
    retain the legacy independent engine process group.
    """
    kwargs = {"cwd": cwd, "env": env}
    if os.name == "posix" and not supervised:
        kwargs["start_new_session"] = True
    elif os.name == "nt":
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
    remaining = log_limit
    lock = threading.Lock()
    observations = {}
    errors = []
    controlled = {}

    def drain(pipe, handle, name):
        nonlocal remaining
        observed = retained = 0
        try:
            while True:
                chunk = pipe.read1(65536)
                if not chunk:
                    break
                observed += len(chunk)
                with lock:
                    keep = min(remaining, len(chunk))
                    handle.write(chunk[:keep])
                    handle.flush()
                    remaining -= keep
                    retained += keep
        except Exception as exc:
            errors.append(exc)
        finally:
            pipe.close()
            observations[name] = {"bytesObserved": observed,
                                  "truncatedForReceipt": observed > retained}

    with open(stdout_path, "wb") as out, open(stderr_path, "wb") as err:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)
        readers = [threading.Thread(target=drain, args=(pipe, handle, path.name), daemon=True)
                   for pipe, handle, path in ((process.stdout, out, stdout_path),
                                              (process.stderr, err, stderr_path))]
        try:
            if on_start is not None:
                on_start(process.pid)
            for reader in readers:
                reader.start()
            if controller is None:
                exit_code = process.wait(timeout=timeout_ms / 1000.0)
            else:
                deadline = time.monotonic() + timeout_ms / 1000.0
                try:
                    controlled['gameplay'] = controller(process, deadline)
                except Exception as exc:
                    controlled['controllerError'] = f'{type(exc).__name__}: {exc}'
                    if supervised and os.name == "posix":
                        process.kill()
                        process.wait(timeout=2)
                    else:
                        kill_process_tree(process)
                exit_code = process.wait(timeout=max(.001, deadline - time.monotonic()))
            timed_out = False
        except subprocess.TimeoutExpired:
            if supervised and os.name == "posix":
                # The engine shares the worker's owned group. Killing that
                # group here would prevent the worker's failure receipt.
                process.kill()
                process.wait(timeout=2)
            else:
                kill_process_tree(process)
            exit_code, timed_out = None, True
        except BaseException:
            if supervised and os.name == "posix":
                process.kill()
                process.wait(timeout=2)
            else:
                kill_process_tree(process)
            for pipe in (process.stdout, process.stderr):
                pipe.close()
            raise
        for reader in readers:
            reader.join()
        if errors:
            raise errors[0]
        result = {"processId": process.pid, "processExitCode": exit_code,
                  "timedOut": timed_out}
        result.update(controlled)
        for path in (stdout_path, stderr_path):
            result[path.name] = dict(observations[path.name], bytes=path.stat().st_size,
                                     sha256=sha256_file(path))
        return result


def validate_offscreen_graphics(graphics):
    exact_keys(graphics, {"schemaVersion", "requestedBackend", "sdlVideoDriver",
                          "sdlVersion", "glVendor", "glRenderer", "glVersion"},
               set(), "Engine graphics report")
    integer(graphics["schemaVersion"], 1, 1, "graphics.schemaVersion")
    require(graphics["requestedBackend"] == "sdl-offscreen",
            "Engine graphics report does not match requested backend 'sdl-offscreen'.")
    for name, limit in (("sdlVideoDriver", 31), ("sdlVersion", 31),
                        ("glVendor", 255), ("glRenderer", 255), ("glVersion", 255)):
        value = graphics[name]
        require(isinstance(value, str) and 1 <= len(value) <= limit
                and all(32 <= ord(char) <= 126 for char in value),
                f"graphics.{name} must contain 1..{limit} printable ASCII characters.")
    require(graphics["sdlVideoDriver"] == "offscreen",
            "Engine graphics report did not observe the requested 'offscreen' SDL driver.")
    require(re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", graphics["sdlVersion"]),
            "graphics.sdlVersion must be a runtime major.minor.micro version.")
    return dict(graphics)


def validate_capture(output, request):
    image = output / "capture.ppm"
    require(image.is_file() and not image.is_symlink(),
            "Engine did not write capture.ppm.")
    c = request["camera"]
    header = f"P6\n{c['width']} {c['height']}\n255\n".encode("ascii")
    expected = c["width"] * c["height"] * 3
    with image.open("rb") as handle:
        require(handle.read(len(header)) == header,
                "capture.ppm is not the requested P6 image.")
        payload = handle.read(expected + 1)
    require(len(payload) == expected,
            f"capture.ppm payload is {len(payload)} bytes (bounded read), expected {expected}.")
    result = {"validPpm": True, "imageSha256": sha256_bytes(header + payload)}
    receipt_path = output / "capture.ppm.json"
    offscreen = request.get("graphicsBackend") == "sdl-offscreen"
    if offscreen:
        require(receipt_path.exists(), "Offscreen capture requires an engine graphics receipt.")
    if receipt_path.exists():
        require(receipt_path.is_file() and not receipt_path.is_symlink()
                and receipt_path.stat().st_size <= 16384,
                "Engine receipt is missing or exceeds 16 KiB.")
        try:
            engine_receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        except (ValueError, UnicodeDecodeError) as exc:
            raise CaptureError(f"Engine receipt is not valid JSON: {exc}") from exc
        require(isinstance(engine_receipt, dict), "Engine receipt must be an object.")
        for key in ("width", "height"):
            if key in engine_receipt:
                require(engine_receipt[key] == c[key],
                        f"Engine receipt does not match requested {key}.")
        if offscreen:
            require("graphics" in engine_receipt, "Engine receipt must include graphics.")
            result["engineReportedGraphics"] = validate_offscreen_graphics(engine_receipt["graphics"])
        result["engineReportedReceipt"] = engine_receipt
        result["engineReceiptSha256"] = sha256_file(receipt_path)
    return result


def contained_destination(output, value):
    safe_destination(value)
    root = output.resolve()
    destination = output / PurePosixPath(value)
    resolved = destination.resolve()
    require(resolved != root and resolved.is_relative_to(root),
            f"Staged destination escapes output directory: {value!r}.")
    return destination


def stage_files(request, output):
    records = []
    for item in request["files"]:
        dest = contained_destination(output, item["destination"])
        source = Path(item["source"])
        digest = sha256_file(source)
        require(digest == item["sha256"],
                f"Source hash mismatch for {item['source']}: {digest} != {item['sha256']}.")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest = contained_destination(output, item["destination"])
        require(not dest.exists() and not dest.is_symlink(),
                f"Staged destination already exists: {item['destination']}.")
        # Exclusive creation also refuses a target introduced after the check.
        size = 0
        with source.open("rb") as source_handle, dest.open("xb") as handle:
            for block in iter(lambda: source_handle.read(1 << 20), b""):
                handle.write(block)
                size += len(block)
        if request["engineProfile"] == "linux" and item["role"] == "engine":
            dest.chmod(dest.stat().st_mode | stat.S_IXUSR)
        require(sha256_file(dest) == item["sha256"],
                f"Staged hash mismatch for {item['destination']}.")
        records.append({"source": item["source"],
                        "destination": item["destination"], "role": item["role"],
                        "sha256": item["sha256"], "bytes": size})
    return records


def after_manifest(before, output):
    records = []
    for item in before:
        source = Path(item["source"])
        staged = contained_destination(output, item["destination"])
        record = dict(item)
        record["sourceUnchanged"] = source.is_file() \
            and sha256_file(source) == item["sha256"]
        record["stagedUnchanged"] = staged.is_file() \
            and sha256_file(staged) == item["sha256"]
        records.append(record)
    return records


def read_request(request_path, enforce_host=True):
    path = Path(request_path)
    require(path.is_file() and not path.is_symlink(), "Request must be a regular file, not a link.")
    with path.open("rb") as handle:
        raw = handle.read((2 << 20) + 1)
    require(len(raw) <= (2 << 20), "Request exceeds the 2 MiB byte budget.")
    def reject_duplicates(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f"Duplicate JSON key: {key}.")
            result[key] = value
        return result
    try:
        document = json.loads(raw.decode("utf-8"), object_pairs_hook=reject_duplicates)
    except (ValueError, UnicodeDecodeError, RecursionError) as exc:
        raise CaptureError(f"Request is not valid JSON: {exc}") from exc
    request = validate_request(document)
    if enforce_host:
        require(profile_runs_here(request["engineProfile"]),
                f"The {request['engineProfile']} engine profile does not run on this OS.")
    return raw, request


def initial_receipt(request, output):
    receipt = {"schemaVersion": 1, "status": "failed", "failureStage": "staging",
               "engineProfile": request["engineProfile"], "engineStarted": False,
               "processId": None, "processExitCode": None, "timedOut": False,
               "requestedCamera": request.get("camera"),
               "requestedFrame": request["frame"],
               "limits": {"engineTimeoutMs": request["engineTimeoutMs"],
                          "runnerTimeoutMs": request["runnerTimeoutMs"],
                          "logLimitBytes": request["logLimitBytes"]},
               "runtimeResourceResolutionObserved": False,
               "effectiveCameraObserved": False,
               "acceptance": "unverified",
               "outputDirectory": str(output), "runnerTimedOut": False,
               "identityCheckComplete": False, "inputsUnchanged": None}
    if request["engineProfile"] == "linux":
        receipt["requestedGraphicsBackend"] = request.get("graphicsBackend", "xvfb")
    if "gameplay" in request or "episodeArchive" in request:
        receipt["immutableInputsUnchanged"] = None
        receipt["mutableConfigDestinations"] = [f["destination"] for f in request["files"]
                                                 if f["role"] == "config"]
    if "gameplay" in request:
        receipt.pop("requestedCamera")
        receipt.pop("requestedFrame")
        receipt.update(captureMode="gameplay", physicsOverrides=False,
                       deterministicSimulation=False, graphicsBackendObserved=False)
    return receipt


def _run_capture(request_path, output, supervised=False):
    if supervised:
        # The parent establishes ownership before this worker can spawn anything.
        require(sys.stdin.buffer.readline() == b"start\n", "Capture worker was not admitted.")
    _, request = read_request(request_path)
    if not supervised:
        require(not output.exists() and not output.is_symlink(),
                f"Output directory already exists: {output}.")
        output.mkdir(parents=True)
    receipt = initial_receipt(request, output)
    write_progress(output, receipt)
    try:
        before = stage_files(request, output)
        write_json(output / "manifest-before.json", before)
        write_json(output / "request.json", request)
        gameplay = "gameplay" in request
        if "episodeArchive" in request:
            receipt["episodeArchive"] = validate_episode_archive(
                request, output / request["episodeArchive"])
        capture_request = output / "capture-request.txt"
        if not gameplay:
            capture_request.write_text(
                request_text(request, (output / "capture.ppm").resolve()),
                encoding="ascii", newline="\n")
            receipt["captureRequestSha256"] = sha256_file(capture_request)
        command = command_for(request, output)
        receipt["command"] = command

        # The capture-v1 engine discovers its request through this
        # variable and refuses to run without the isolated-session
        # marker staged beside the staged binaries.
        if gameplay:
            from gameplay_capture import prepare as prepare_gameplay
            marker = output / "resource/jk_.cd"
            require(marker.stat().st_size == 4, "Installation marker must contain exactly four bytes.")
            magic = struct.unpack("<I", marker.read_bytes())[0]
            allowed = {0x3b426929} if request["motsCompat"] else {
                0x69973284, 0x699232c4, 0x69923384, 0x69923284}
            require(magic in allowed, "Installation marker does not match the requested game.")
            prepare_gameplay(output, request["gameplay"])
            receipt["captureSession"] = "private .openjkdf2-agent-session with native mailbox"
        else:
            (output / ".openjkdf2-capture-session").write_text(
                "red-engine-capture-runner\n", encoding="ascii", newline="\n")
            receipt["captureSession"] = "staged .openjkdf2-capture-session marker"
        receipt["motsCompat"] = request["motsCompat"]
        env = dict(os.environ)
        if gameplay:
            env.pop("OPENJKDF2_CAPTURE_REQUEST", None)
            env["OPENJKDF2_AGENT_DIR"] = str((output / "agent").resolve())
        else:
            env["OPENJKDF2_CAPTURE_REQUEST"] = str(capture_request.resolve())
            env.pop("OPENJKDF2_AGENT_DIR", None)
        # Backend intent belongs to this validated request. In particular, a
        # Windows capture must not inherit a Linux selection from its parent.
        env.pop("OPENJKDF2_CAPTURE_BACKEND", None)
        display = None
        try:
            if request["engineProfile"] == "linux":
                backend = request.get("graphicsBackend", "xvfb")
                if backend == "xvfb":
                    display = PrivateDisplay()
                    display.__enter__()
                    env["DISPLAY"] = display.display
                    video_driver = "x11"
                else:
                    env.pop("DISPLAY", None)
                    env.pop("WAYLAND_DISPLAY", None)
                    video_driver = "offscreen"
                    env.update(LIBGL_ALWAYS_SOFTWARE="1", GALLIUM_DRIVER="llvmpipe",
                               LP_NUM_THREADS="1", MESA_SHADER_CACHE_DISABLE="true")
                session = output / "session"
                (session / "home").mkdir(parents=True)
                (session / "runtime").mkdir(parents=True)
                env["HOME"] = str(session / "home")
                env["XDG_RUNTIME_DIR"] = str(session / "runtime")
                if gameplay:
                    (session / "runtime").chmod(0o700)
                    for name in ("DATA", "CONFIG", "CACHE", "STATE"):
                        directory = session / name.lower()
                        directory.mkdir(mode=0o700)
                        env[f"XDG_{name}_HOME"] = str(directory)
                env["SDL_VIDEO_DRIVER"] = env["SDL_VIDEODRIVER"] = video_driver
                env["SDL_AUDIO_DRIVER"] = env["SDL_AUDIODRIVER"] = "dummy"
                env["ALSOFT_DRIVERS"] = "null"
                if gameplay:
                    if backend == "sdl-offscreen":
                        env["EGL_PLATFORM"] = "surfaceless"
                else:
                    env["OPENJKDF2_CAPTURE_BACKEND"] = backend
                if display is not None:
                    receipt["privateDisplay"] = display.receipt()
                receipt["containment"] = (
                    ("worker and engine share an owned POSIX process group; a descendant "
                     if supervised else "engine ran in its own POSIX process group; a descendant ") +
                    "that starts a new session can escape the group")
            receipt["failureStage"] = "execution"
            write_progress(output, receipt)

            def engine_started(pid):
                receipt.update(engineStarted=True, processId=pid)
                write_progress(output, receipt)

            controller_args = {}
            if gameplay:
                from gameplay_capture import run as run_gameplay
                controller_args["controller"] = lambda process, deadline: run_gameplay(
                    request["gameplay"], output, process, deadline)
            execution = execute_engine(command, output, env,
                                       request["engineTimeoutMs"],
                                       output / "engine-stdout.log",
                                       output / "engine-stderr.log",
                                       log_limit=request["logLimitBytes"],
                                       supervised=supervised, on_start=engine_started, **controller_args)
            receipt.update(execution)
            receipt["engineStarted"] = execution.get("processId") is not None
            write_progress(output, receipt)
            require(not execution["timedOut"], "Engine exceeded engineTimeoutMs.")
            require("controllerError" not in execution, execution.get("controllerError", ""))
            require(execution["processExitCode"] == 0,
                    f"Engine exited with code {execution['processExitCode']}.")
        finally:
            if display is not None:
                display.close()
        receipt["failureStage"] = "capture-validation"
        write_progress(output, receipt)
        if not gameplay:
            receipt.update(validate_capture(output, request))
        else:
            require(receipt.get("gameplay", {}).get("status") == "passed",
                    "Engine did not complete the explicit gameplay recipe.")
    except CaptureError as exc:
        receipt["error"] = f"{type(exc).__name__}: {exc}"
    except Exception as exc:  # receipt must still describe the failure
        receipt["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        failure_stage = receipt["failureStage"]
        receipt["failureStage"] = "post-run-identity"
        write_progress(output, receipt)
        try:
            after = after_manifest(before, output) if "before" in dir() else []
        except Exception as exc:
            after = []
            failure_stage = "post-run-identity"
            receipt.setdefault("error", f"Post-run identity check failed: {exc}")
        if after:
            write_json(output / "manifest-after.json", after)
            receipt["inputsUnchanged"] = all(
                item["sourceUnchanged"] and item["stagedUnchanged"]
                for item in after)
            receipt["identityCheckComplete"] = True
            unchanged = receipt["inputsUnchanged"]
            if "gameplay" in request or "episodeArchive" in request:
                unchanged = all(item["sourceUnchanged"] and
                                (item["stagedUnchanged"] or item["role"] == "config") for item in after)
                receipt["immutableInputsUnchanged"] = unchanged
                receipt["stagedConfigChanges"] = [item["destination"] for item in after
                                                   if item["role"] == "config" and not item["stagedUnchanged"]]
            if not unchanged:
                receipt["status"] = "failed"
                receipt["failureStage"] = "post-run-identity"
                receipt.setdefault(
                    "error", "Source or staged input changed during the run; "
                             "inspect manifest-after.json.")
        if "error" not in receipt:
            receipt["status"] = "completed"
            receipt["failureStage"] = None
        elif receipt.get("immutableInputsUnchanged", receipt.get("inputsUnchanged")) is not False:
            receipt["failureStage"] = failure_stage
        write_progress(output, receipt)
        write_json(output / "launcher-receipt.json", receipt)
    return receipt


class WorkerTree:
    """Own a Windows kill-on-close job, or a private POSIX process group."""

    def __init__(self):
        self.process = None
        self.job = None
        if os.name == "nt":
            import ctypes
            from ctypes import wintypes

            class BasicLimits(ctypes.Structure):
                _fields_ = [("ProcessTime", ctypes.c_longlong), ("JobTime", ctypes.c_longlong),
                            ("Flags", wintypes.DWORD), ("MinWorkingSet", ctypes.c_size_t),
                            ("MaxWorkingSet", ctypes.c_size_t), ("ActiveProcesses", wintypes.DWORD),
                            ("Affinity", ctypes.c_size_t), ("Priority", wintypes.DWORD),
                            ("Scheduling", wintypes.DWORD)]

            class IoCounters(ctypes.Structure):
                _fields_ = [(name, ctypes.c_ulonglong) for name in
                            ("ReadOps", "WriteOps", "OtherOps", "ReadBytes", "WriteBytes", "OtherBytes")]

            class ExtendedLimits(ctypes.Structure):
                _fields_ = [("Basic", BasicLimits), ("Io", IoCounters),
                            ("ProcessMemory", ctypes.c_size_t), ("JobMemory", ctypes.c_size_t),
                            ("PeakProcessMemory", ctypes.c_size_t), ("PeakJobMemory", ctypes.c_size_t)]

            self.api = ctypes.WinDLL("kernel32", use_last_error=True)
            self.api.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
            self.api.CreateJobObjectW.restype = wintypes.HANDLE
            self.api.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int,
                                                         ctypes.c_void_p, wintypes.DWORD]
            self.api.SetInformationJobObject.restype = wintypes.BOOL
            self.api.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
            self.api.AssignProcessToJobObject.restype = wintypes.BOOL
            self.api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            self.api.OpenProcess.restype = wintypes.HANDLE
            self.api.CloseHandle.argtypes = [wintypes.HANDLE]
            self.api.CloseHandle.restype = wintypes.BOOL
            self.job = self.api.CreateJobObjectW(None, None)
            if not self.job:
                raise ctypes.WinError(ctypes.get_last_error())
            limits = ExtendedLimits()
            limits.Basic.Flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            if not self.api.SetInformationJobObject(self.job, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
                error = ctypes.WinError(ctypes.get_last_error())
                self.close()
                raise error

    def start(self, command):
        kwargs = {"start_new_session": True} if os.name == "posix" else {
            "creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, **kwargs)
        if self.job is not None:
            import ctypes
            # The child is blocked on its stdin admission token until attached.
            handle = self.api.OpenProcess(0x0100 | 0x0001, False, self.process.pid)
            if not handle:
                raise ctypes.WinError(ctypes.get_last_error())
            try:
                if not self.api.AssignProcessToJobObject(self.job, handle):
                    raise ctypes.WinError(ctypes.get_last_error())
            finally:
                self.api.CloseHandle(handle)
        self.process.stdin.write(b"start\n")
        self.process.stdin.flush()
        self.process.stdin.close()
        return self.process

    def close(self):
        process, self.process = self.process, None
        if self.job is not None:
            if not self.api.CloseHandle(self.job):
                import ctypes
                raise ctypes.WinError(ctypes.get_last_error())
            self.job = None
        if process is None:
            return
        if process.stdin is not None:
            process.stdin.close()
        if os.name == "posix":
            try:
                os.killpg(process.pid, 9)
            except ProcessLookupError:
                pass
        elif process.poll() is None:
            # Also handles failure before assignment to the job. No child can
            # have passed its admission read in that case.
            process.kill()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            raise CaptureError("Owned capture worker did not exit during the 2-second cleanup grace.")


def _worker_command(request_path, output):
    return [sys.executable, str(Path(__file__).resolve()), str(request_path),
            "--output", str(output), "--capture-worker"]


def run_capture(request_path, output, cancel_event=None):
    # Admission performs only request parsing/schema/metadata checks. The work
    # deadline starts before output preparation; hashes/copies/engine/reporting
    # run solely in the owned, interruptible child. OS cleanup has a 2s grace.
    raw, request = read_request(request_path)
    require(cancel_event is None or not cancel_event.is_set(), "Capture cancelled before staging.")
    require(not output.exists() and not output.is_symlink(),
            f"Output directory already exists: {output}.")
    started = time.monotonic()
    deadline = started + request["runnerTimeoutMs"] / 1000.0
    output.mkdir(parents=True)
    receipt = initial_receipt(request, output)
    receipt["runnerDeadlineScope"] = "admitted request through post-run identity collection"
    receipt["cleanupGraceMs"] = 2000
    if os.name == "nt":
        receipt["containment"] = "worker and descendants belong to an owned kill-on-close Windows job"
    admitted = output / ".admitted-request.json"
    admitted.write_bytes(raw)
    write_progress(output, receipt)
    tree = None
    timed_out = False
    cancelled = False
    error = None
    cleanup_error = None
    try:
        tree = WorkerTree()
        process = tree.start(_worker_command(admitted.resolve(), output.resolve()))
        if cancel_event is None:
            process.wait(timeout=max(0, deadline - time.monotonic()))
        else:
            while process.poll() is None:
                if cancel_event.is_set():
                    cancelled = True
                    break
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    timed_out = True
                    break
                try:
                    process.wait(timeout=min(0.05, remaining))
                except subprocess.TimeoutExpired:
                    continue
        if time.monotonic() >= deadline:
            timed_out = True
        elif not cancelled and process.returncode != 0:
            error = f"Capture worker exited with code {process.returncode}."
    except subprocess.TimeoutExpired:
        timed_out = True
    except Exception as exc:
        error = f"{type(exc).__name__}: {exc}"
    finally:
        if tree is not None:
            try:
                tree.close()
            except Exception as exc:
                cleanup_error = f"Capture cleanup failed: {exc}"
    # Never re-hash the entire input set in the supervisor after expiry. The
    # bounded checkpoint is the worker's last complete observation, not success.
    progress = output / ".capture-progress.json"
    if progress.is_file() and not progress.is_symlink() and progress.stat().st_size <= 65536:
        try:
            checkpoint = json.loads(progress.read_text(encoding="utf-8"))
            if isinstance(checkpoint, dict):
                receipt.update(checkpoint)
        except (OSError, ValueError, UnicodeDecodeError):
            pass
    receipt["runnerDurationMs"] = round((time.monotonic() - started) * 1000)
    receipt["cleanupComplete"] = cleanup_error is None
    if cleanup_error is not None:
        receipt["cleanupError"] = cleanup_error
    if cancelled:
        receipt.update(status="failed", cancelled=True, runnerTimedOut=False, timedOut=False,
                       identityCheckComplete=False, inputsUnchanged=None,
                       error="Capture cancelled by its controlling caller.")
        if receipt["failureStage"] is None:
            receipt["failureStage"] = "finalization"
    elif timed_out:
        receipt.update(status="failed", runnerTimedOut=True, timedOut=True,
                       identityCheckComplete=False, inputsUnchanged=None,
                       error="Runner exceeded runnerTimeoutMs.")
        if receipt["failureStage"] is None:
            receipt["failureStage"] = "finalization"
    elif error is not None:
        receipt["status"] = "failed"
        receipt.setdefault("error", error)
    elif not (output / "launcher-receipt.json").is_file():
        receipt.update(status="failed", error="Capture worker did not publish its final receipt.")
    if cleanup_error is not None:
        receipt["status"] = "failed"
        receipt.setdefault("error", cleanup_error)
    if (cancelled or timed_out) and "immutableInputsUnchanged" in receipt:
        receipt["immutableInputsUnchanged"] = None
    # A terminated worker cannot finish its pipe counters. Hash only the small,
    # retained logs; never invent how many bytes were discarded before it died.
    retained = 0
    for name in ("engine-stdout.log", "engine-stderr.log"):
        path = output / name
        if name in receipt:
            continue
        if path.is_file() and not path.is_symlink():
            with path.open("rb") as handle:
                data = handle.read(request["logLimitBytes"] + 1)
            retained += len(data)
            if retained > request["logLimitBytes"]:
                receipt.update(status="failed", error="Retained engine logs exceeded logLimitBytes.")
                break
            receipt[name] = {"bytes": len(data), "sha256": sha256_bytes(data),
                             "bytesObserved": None, "truncatedForReceipt": None,
                             "observationComplete": False}
    write_json(output / "launcher-receipt.json", receipt)
    return receipt


def main(argv=None):
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("request", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--validate-only", action="store_true",
                        help="Validate request schema/source metadata without staging or launching on any host.")
    parser.add_argument("--cancel-on-stdin", action="store_true",
                        help="Opt in to ownership cancellation: any stdin byte or EOF cancels this run.")
    parser.add_argument("--capture-worker", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if not args.validate_only and args.output is None:
        parser.error("--output is required unless --validate-only is used")
    if args.capture_worker and (args.validate_only or args.cancel_on_stdin):
        parser.error("Worker mode cannot use inspection or ownership flags")
    try:
        if args.validate_only:
            _, request = read_request(args.request, enforce_host=False)
            print(json.dumps({"status": "validated", "request": request,
                              "canRunHere": profile_runs_here(request["engineProfile"]),
                              "sourceHashesVerified": False}))
            return 0
        cancel_event = None
        if args.cancel_on_stdin:
            cancel_event = threading.Event()

            def watch_owner():
                try:
                    # Avoid the buffered stdin lock during interpreter shutdown.
                    os.read(sys.stdin.fileno(), 1)
                finally:
                    cancel_event.set()

            threading.Thread(target=watch_owner, daemon=True).start()
        receipt = (_run_capture(args.request, args.output, supervised=True)
                   if args.capture_worker else
                   run_capture(args.request, args.output, cancel_event=cancel_event)
                   if args.cancel_on_stdin else run_capture(args.request, args.output))
    except CaptureError as exc:
        print(json.dumps({"status": "refused",
                          "error": f"{type(exc).__name__}: {exc}"}),
              file=sys.stderr)
        return 2
    print(json.dumps({"status": receipt["status"],
                      "receipt": str(Path(receipt["outputDirectory"])
                                     / "launcher-receipt.json")}))
    return 0 if receipt["status"] == "completed" else 1


if __name__ == "__main__":
    raise SystemExit(main())

