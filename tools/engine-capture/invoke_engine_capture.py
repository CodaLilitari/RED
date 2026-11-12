#!/usr/bin/env python3
"""Engine capture runner (D-14 pattern port).

Stages a hash-bound file set into a fresh output directory, asks a
``sed-engine-capture-v1`` engine to render one frame to ``capture.ppm``
(P6 RGB8), and writes a ``launcher-receipt.json`` describing exactly
what happened.

Honesty contract: a completed run only proves the staged engine wrote
a well-formed PPM. The receipt always carries
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
  process group on a private Xvfb display (displays are allocated by
  Xvfb itself via ``-displayfd``; TCP listening is disabled)

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
import subprocess
import sys
import time
from pathlib import Path, PurePosixPath

ENGINE_PROFILES = {
    "windows": {"openjkdf2-64.exe": "engine", "OpenAL32.dll": "runtime",
                "libGameNetworkingSockets.dll": "runtime"},
    "linux": {"openjkdf2": "engine"},
}
RESERVED = {"capture.ppm", "capture.ppm.json", "capture-request.txt",
            "request.json", "launcher-receipt.json", "manifest-before.json",
            "manifest-after.json", "engine-stdout.log", "engine-stderr.log"}
ROLES = {"engine", "runtime", "resource", "scenario", "config"}


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
    path = PurePosixPath(value)
    require(not path.is_absolute() and ".." not in path.parts
            and "." not in path.parts,
            f"Unsafe file destination: {value!r}.")
    require(path.name not in RESERVED,
            f"File destination collides with a runner output: {value!r}.")
    return value


def validate_request(request):
    exact_keys(request, {"schemaVersion", "episode", "map", "camera", "files"},
               {"frame", "engineTimeoutMs", "runnerTimeoutMs", "logLimitBytes",
                "motsCompat"},
               "request")
    require(request["schemaVersion"] == 1, "schemaVersion must be 1.")
    episode, level = request["episode"], request["map"]
    require(isinstance(episode, str)
            and re.fullmatch(r"[A-Za-z0-9_-]{1,31}", episode),
            "Invalid episode name.")
    require(isinstance(level, str) and re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", level)
            and "/" not in level and "\\" not in level,
            "Invalid map file name.")
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
    frame = integer(request.get("frame", 3), 1, 120, "frame")
    engine_timeout = integer(request.get("engineTimeoutMs", 30000),
                             1000, 60000, "engineTimeoutMs")
    runner_timeout = integer(request.get("runnerTimeoutMs", 45000),
                             1000, 90000, "runnerTimeoutMs")
    require(runner_timeout >= engine_timeout,
            "runnerTimeoutMs must cover engineTimeoutMs.")
    log_limit = integer(request.get("logLimitBytes", 1 << 20),
                        1024, 16 << 20, "logLimitBytes")
    mots_compat = request.get("motsCompat", True)
    require(isinstance(mots_compat, bool), "motsCompat must be a boolean.")

    files = request["files"]
    require(isinstance(files, list) and files, "files must be a non-empty array.")
    staged = []
    destinations = {}
    for item in files:
        exact_keys(item, {"source", "destination", "sha256", "role"}, set(), "file")
        dest = safe_destination(item["destination"])
        require(dest not in destinations, f"Duplicate destination: {dest}.")
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
    for config in ("registry.json", "openjkdf2_cvars.json"):
        require(destinations.get(config) == "config",
                f"The request must stage {config} with role 'config'.")
    require(destinations.get(f"episode/{episode}/episode.jk") == "scenario",
            "The request must stage the episode.jk scenario file.")
    require(destinations.get(f"episode/{episode}/jkl/{level}") == "scenario",
            "The request must stage the requested map under episode/<episode>/jkl/.")

    return {"schemaVersion": 1, "engineProfile": profile, "episode": episode,
            "map": level, "camera": normalized, "frame": frame,
            "engineTimeoutMs": engine_timeout, "runnerTimeoutMs": runner_timeout,
            "logLimitBytes": log_limit, "motsCompat": mots_compat,
            "files": staged}


def engine_executable(request):
    return next(name for name, role in ENGINE_PROFILES[request["engineProfile"]].items()
                if role == "engine")


def profile_runs_here(profile):
    return (profile == "windows" and os.name == "nt") \
        or (profile == "linux" and sys.platform.startswith("linux"))


def command_for(request, output):
    command = [str(output / engine_executable(request)), "-capture",
               "-autostart", "-sp"]
    if request.get("motsCompat", True):
        command.append("-motsCompat")
    command += ["-noHUD", "-episode", request["episode"],
                "-map", request["map"]]
    return command


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


def execute_engine(command, cwd, env, timeout_ms, stdout_path, stderr_path):
    """Run the engine with output drained to files. Returns a dict
    with exit code / timeout state. Separated so tests can substitute
    a synthetic engine without a real capture-v1 binary."""
    kwargs = {"cwd": cwd, "env": env}
    if os.name == "posix":
        kwargs["start_new_session"] = True
    elif os.name == "nt":
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
    with open(stdout_path, "wb") as out, open(stderr_path, "wb") as err:
        process = subprocess.Popen(command, stdout=out, stderr=err, **kwargs)
        try:
            exit_code = process.wait(timeout=timeout_ms / 1000.0)
            return {"processId": process.pid, "processExitCode": exit_code,
                    "timedOut": False}
        except subprocess.TimeoutExpired:
            kill_process_tree(process)
            return {"processId": process.pid, "processExitCode": None,
                    "timedOut": True}


def validate_capture(output, request):
    image = output / "capture.ppm"
    require(image.is_file() and not image.is_symlink(),
            "Engine did not write capture.ppm.")
    c = request["camera"]
    header = f"P6\n{c['width']} {c['height']}\n255\n".encode("ascii")
    data = image.read_bytes()
    require(data.startswith(header), "capture.ppm is not the requested P6 image.")
    payload = len(data) - len(header)
    expected = c["width"] * c["height"] * 3
    require(payload == expected,
            f"capture.ppm payload is {payload} bytes, expected {expected}.")
    result = {"validPpm": True, "imageSha256": sha256_bytes(data)}
    receipt_path = output / "capture.ppm.json"
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
        result["engineReportedReceipt"] = engine_receipt
        result["engineReceiptSha256"] = sha256_file(receipt_path)
    return result


def stage_files(request, output):
    records = []
    for item in request["files"]:
        source = Path(item["source"])
        data = source.read_bytes()
        digest = sha256_bytes(data)
        require(digest == item["sha256"],
                f"Source hash mismatch for {item['source']}: {digest} != {item['sha256']}.")
        dest = output / PurePosixPath(item["destination"])
        dest.parent.mkdir(parents=True, exist_ok=True)
        require(not dest.exists() and not dest.is_symlink(),
                f"Staged destination already exists: {item['destination']}.")
        dest.write_bytes(data)
        if request["engineProfile"] == "linux" and item["role"] == "engine":
            dest.chmod(dest.stat().st_mode | stat.S_IXUSR)
        require(sha256_file(dest) == item["sha256"],
                f"Staged hash mismatch for {item['destination']}.")
        records.append({"source": item["source"],
                        "destination": item["destination"], "role": item["role"],
                        "sha256": item["sha256"], "bytes": len(data)})
    return records


def after_manifest(before, output):
    records = []
    for item in before:
        source = Path(item["source"])
        staged = output / PurePosixPath(item["destination"])
        record = dict(item)
        record["sourceUnchanged"] = source.is_file() \
            and sha256_file(source) == item["sha256"]
        record["stagedUnchanged"] = staged.is_file() \
            and sha256_file(staged) == item["sha256"]
        records.append(record)
    return records


def run_capture(request_path, output):
    raw = Path(request_path).read_bytes()
    try:
        document = json.loads(raw.decode("utf-8"))
    except (ValueError, UnicodeDecodeError) as exc:
        raise CaptureError(f"Request is not valid JSON: {exc}") from exc
    request = validate_request(document)
    require(profile_runs_here(request["engineProfile"]),
            f"The {request['engineProfile']} engine profile does not run on this OS.")
    require(not output.exists() and not output.is_symlink(),
            f"Output directory already exists: {output}.")
    output.mkdir(parents=True)

    receipt = {"schemaVersion": 1, "status": "failed", "failureStage": "staging",
               "engineProfile": request["engineProfile"], "engineStarted": False,
               "processId": None, "processExitCode": None, "timedOut": False,
               "requestedCamera": request["camera"],
               "requestedFrame": request["frame"],
               "limits": {"engineTimeoutMs": request["engineTimeoutMs"],
                          "runnerTimeoutMs": request["runnerTimeoutMs"],
                          "logLimitBytes": request["logLimitBytes"]},
               "runtimeResourceResolutionObserved": False,
               "effectiveCameraObserved": False,
               "acceptance": "unverified",
               "outputDirectory": str(output)}
    try:
        before = stage_files(request, output)
        write_json(output / "manifest-before.json", before)
        write_json(output / "request.json", request)
        capture_request = output / "capture-request.txt"
        capture_request.write_text(
            request_text(request, (output / "capture.ppm").resolve()),
            encoding="ascii", newline="\n")
        receipt["captureRequestSha256"] = sha256_file(capture_request)
        command = command_for(request, output)
        receipt["command"] = command

        # The capture-v1 engine discovers its request through this
        # variable and refuses to run without the isolated-session
        # marker staged beside the staged binaries.
        (output / ".openjkdf2-capture-session").write_text(
            "red-engine-capture-runner\n", encoding="ascii", newline="\n")
        receipt["captureSession"] = "staged .openjkdf2-capture-session marker"
        receipt["motsCompat"] = request["motsCompat"]
        env = dict(os.environ)
        env["OPENJKDF2_CAPTURE_REQUEST"] = str(capture_request.resolve())
        display = None
        try:
            if request["engineProfile"] == "linux":
                display = PrivateDisplay()
                display.__enter__()
                env["DISPLAY"] = display.display
                session = output / "session"
                (session / "home").mkdir(parents=True)
                (session / "runtime").mkdir(parents=True)
                env["HOME"] = str(session / "home")
                env["XDG_RUNTIME_DIR"] = str(session / "runtime")
                env["SDL_VIDEODRIVER"] = "x11"
                env["SDL_AUDIODRIVER"] = "dummy"
                env["ALSOFT_DRIVERS"] = "null"
                receipt["privateDisplay"] = display.receipt()
                receipt["containment"] = (
                    "engine ran in its own POSIX process group; a descendant "
                    "that starts a new session can escape the group")
            receipt["failureStage"] = "execution"
            receipt["engineStarted"] = True
            execution = execute_engine(command, output, env,
                                       request["engineTimeoutMs"],
                                       output / "engine-stdout.log",
                                       output / "engine-stderr.log")
            receipt.update(execution)
            require(not execution["timedOut"], "Engine exceeded engineTimeoutMs.")
            require(execution["processExitCode"] == 0,
                    f"Engine exited with code {execution['processExitCode']}.")
        finally:
            if display is not None:
                display.close()
        for log_name in ("engine-stdout.log", "engine-stderr.log"):
            log_path = output / log_name
            if log_path.is_file():
                receipt[log_name] = {"bytes": log_path.stat().st_size,
                                     "sha256": sha256_file(log_path),
                                     "truncatedForReceipt": False}
        receipt["failureStage"] = "capture-validation"
        receipt.update(validate_capture(output, request))
        receipt["status"] = "completed"
        receipt["failureStage"] = None
    except CaptureError as exc:
        receipt["error"] = f"{type(exc).__name__}: {exc}"
    except Exception as exc:  # receipt must still describe the failure
        receipt["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        try:
            after = after_manifest(before, output) if "before" in dir() else []
        except Exception:
            after = []
        if after:
            write_json(output / "manifest-after.json", after)
            receipt["inputsUnchanged"] = all(
                item["sourceUnchanged"] and item["stagedUnchanged"]
                for item in after)
            if not receipt["inputsUnchanged"]:
                receipt["status"] = "failed"
                receipt["failureStage"] = "post-run-identity"
                receipt.setdefault(
                    "error", "Source or staged input changed during the run; "
                             "inspect manifest-after.json.")
        write_json(output / "launcher-receipt.json", receipt)
    return receipt


def main(argv=None):
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("request", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        receipt = run_capture(args.request, args.output)
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

