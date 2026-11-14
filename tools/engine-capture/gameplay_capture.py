"""Bounded RED controller for the existing openjkdf2-agent-v1 ordinary-input protocol.

Scenario validation/execution and key validation are adapted from OpenJKDF2
scripts/agent_scenario.py and scripts/agent_workspace.py at
10902f0230f25f7d010fa1bbb0acd5de736e629d. RED owns staging, process lifetime,
log retention and the outer cancellation/deadline; this module never launches
an engine, evaluates code, injects COG messages or changes simulation state.

OpenJKDF2 is Copyright (C) OpenJKDF2 contributors.
Permission to use, copy, modify, and/or distribute this software for any purpose
without fee is hereby granted.
THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.
"""
from __future__ import annotations
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import stat
import time

PROTOCOL = "openjkdf2-agent-v1"
MAX_REPLY = 1 << 20
MAX_JOURNAL = 16 << 20
MAX_COMMANDS = 4096
MAX_RECIPE = 256 << 10
MAX_CAPTURE_BYTES = 192 << 20
MARKER = ".openjkdf2-agent-session"

KEYS = {chr(97 + n): 4 + n for n in range(26)}
KEYS.update({f"digit{n}": 29 + n for n in range(1, 10)})
KEYS.update({"digit0": 39, "return": 40, "enter": 40, "escape": 41, "esc": 41,
             "backspace": 42, "tab": 43, "space": 44, "minus": 45, "equals": 46,
             "grave": 53, "insert": 73, "home": 74, "pageup": 75, "delete": 76,
             "end": 77, "pagedown": 78, "right": 79, "left": 80, "down": 81,
             "up": 82, "lctrl": 224, "lshift": 225, "lalt": 226,
             "rctrl": 228, "rshift": 229, "ralt": 230})
KEYS.update({f"f{n}": 57 + n for n in range(1, 13)})


class AgentError(RuntimeError):
    pass


class AgentTimeout(AgentError):
    pass


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            value.update(block)
    return value.hexdigest()


def atomic_json(path, value):
    path = Path(path)
    payload = (json.dumps(value, indent=2, allow_nan=False) + '\n').encode('utf-8')
    if len(payload) > MAX_JOURNAL:
        raise AgentError('Scenario receipt exceeds 16 MiB')
    temporary = path.with_name('.' + path.name + '.tmp')
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(payload)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def read_regular(path, limit=MAX_REPLY):
    """Never follow a mailbox symlink or block on a FIFO/device."""
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except FileNotFoundError:
        return None
    except OSError as exc:
        raise AgentError(f"Cannot safely open {Path(path).name}: {exc.strerror}") from exc
    with os.fdopen(fd, "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
            raise AgentError(f"Invalid or oversized file: {Path(path).name}")
        value = stream.read(limit + 1)
    if len(value) > limit:
        raise AgentError(f"Oversized file: {Path(path).name}")
    return value


def read_json(path):
    data = read_regular(path)
    if data is None:
        return None
    try:
        value = json.loads(data, object_pairs_hook=reject_duplicates,
                           parse_constant=lambda value: (_ for _ in ()).throw(ValueError('Non-finite JSON')))
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise AgentError(f'Invalid JSON response: {exc}') from exc
    if not isinstance(value, dict):
        raise AgentError('Expected a JSON response object')
    return value


def keycode(name):
    value = str(name).lower()
    if value in KEYS:
        return KEYS[value]
    if value.isascii() and value.isdecimal() and 1 <= int(value) <= 255:
        return int(value)
    raise AgentError(f"Unknown SDL scancode/key: {name}")


def validate_command(command):
    if not isinstance(command, str) or not command or any(ord(c) < 32 or ord(c) > 126 for c in command):
        raise AgentError("Command must be one printable ASCII line")
    if command in {"ping", "state", "release", "capture", "quit"}:
        return command
    if command.startswith("text "):
        if 1 <= len(command[5:]) <= 127 and command[5:].strip():
            return command
        raise AgentError("Text must contain 1..127 printable ASCII characters")
    parts = command.split(" ")
    if len(parts) == 3 and parts[0] in {"key", "mouse", "button"}:
        if not all(re.fullmatch(r"-?[0-9]+", v) for v in parts[1:]):
            raise AgentError("Input command arguments must be integers")
        a, b = map(int, parts[1:])
        valid = ((parts[0] == "key" and 1 <= a <= 255 and b in (0, 1))
                 or (parts[0] == "button" and 1 <= a <= 5 and b in (0, 1))
                 or (parts[0] == "mouse" and abs(a) <= 2048 and abs(b) <= 2048))
        if valid:
            return command
    raise AgentError("Unsupported command or arguments outside their allowed range")


MISSING = object()
NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}")
FIELD = re.compile(r"[A-Za-z][A-Za-z0-9]*(?:\.(?:[A-Za-z][A-Za-z0-9]*|[0-9]+))*")


def number(value):
    try:
        return type(value) in (int, float) and math.isfinite(value)
    except OverflowError:
        return False


def keys(value, allowed, label):
    if not isinstance(value, dict) or set(value) - set(allowed):
        raise AgentError(f"{label} must be an object with only these fields: {', '.join(allowed)}")


def duration(value, maximum, label, *, allow_zero=False):
    if not number(value) or value > maximum or (value < 0 if allow_zero else value <= 0):
        raise AgentError(f"{label} must be a finite number {'from 0' if allow_zero else 'above 0'} to {maximum}")


def literal(value, depth=0):
    if depth > 4:
        return False
    if value is None or type(value) is bool or isinstance(value, str):
        return not isinstance(value, str) or len(value) <= 1024
    if number(value):
        return True
    return isinstance(value, list) and len(value) <= 256 and all(literal(v, depth + 1) for v in value)


def validate_expect(expect):
    if not isinstance(expect, dict) or not 1 <= len(expect) <= 32:
        raise AgentError("expect must contain 1..32 field comparisons")
    for field, expected in expect.items():
        if not isinstance(field, str) or len(field) > 128 or not FIELD.fullmatch(field):
            raise AgentError("Expected field names use dot-separated object keys or array indices")
        if isinstance(expected, dict):
            if expected == {"notNull": True} and type(expected['notNull']) is bool:
                continue
            if not expected or set(expected) - {"min", "max"} or not all(number(v) for v in expected.values()):
                raise AgentError(f"Invalid comparison for {field}; use min/max or notNull: true")
            if expected.get('min', -math.inf) > expected.get('max', math.inf):
                raise AgentError(f"Comparison bounds are reversed for {field}")
        elif not literal(expected):
            raise AgentError(f"Invalid literal comparison for {field}")


def validate_scenario(value):
    try:
        encoded = json.dumps(value, allow_nan=False).encode('utf-8')
    except (ValueError, TypeError, RecursionError) as exc:
        raise AgentError(f'Invalid recipe JSON: {exc}') from exc
    if len(encoded) > MAX_RECIPE:
        raise AgentError('Gameplay recipe exceeds 256 KiB')
    keys(value, ('schemaVersion', 'name', 'launch', 'steps'), 'Scenario')
    if type(value.get('schemaVersion')) is not int or value['schemaVersion'] != 1:
        raise AgentError("Scenario schemaVersion must be integer 1")
    if not isinstance(value.get('name'), str) or not 1 <= len(value['name']) <= 120 or any(ord(c) < 32 for c in value['name']):
        raise AgentError("Scenario name must contain 1..120 printable characters")
    launch = value.get('launch')
    keys(launch, ('game', 'episode', 'map'), 'launch')
    if launch.get('game') not in ('jk', 'mots'):
        raise AgentError("launch.game must be jk or mots")
    for field in ('episode', 'map'):
        if not isinstance(launch.get(field), str) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,100}', launch[field]):
            raise AgentError(f"launch.{field} must be a simple engine name, not a path")
    steps = value.get('steps')
    if not isinstance(steps, list) or not 1 <= len(steps) <= 256:
        raise AgentError("Scenario must contain 1..256 steps")
    result = copy.deepcopy(value)
    names, checkpoints = set(), set()
    captures = 0
    fields = {'wait': ('expect', 'timeout', 'stableFor', 'retryTap'), 'assert': ('expect', 'distanceFrom'),
              'checkpoint': ('capture',), 'tap': ('key', 'seconds'), 'command': ('value',),
              'sleep': ('seconds',)}
    for index, step in enumerate(result['steps'], 1):
        if not isinstance(step, dict) or not isinstance(step.get('op'), str) or step['op'] not in fields:
            raise AgentError(f"Step {index} uses an unsupported operation")
        op = step['op']
        keys(step, ('op', 'name', *fields[op]), f'Step {index}')
        name = step.setdefault('name', f'step-{index:03d}-{op}')
        if not isinstance(name, str) or not NAME.fullmatch(name) or name in names:
            raise AgentError(f"Step {index} needs a unique simple name of up to 64 characters")
        names.add(name)
        if op in ('wait', 'assert'):
            if 'expect' in step:
                validate_expect(step['expect'])
            elif op == 'wait' or 'distanceFrom' not in step:
                raise AgentError(f"Step {name} needs an expectation")
        if op == 'wait':
            duration(step.setdefault('timeout', 30), 120, 'Wait timeout')
            duration(step.setdefault('stableFor', 0), step['timeout'], 'stableFor', allow_zero=True)
            if 'retryTap' in step:
                retry = step['retryTap']
                keys(retry, ('key', 'seconds', 'every'), 'retryTap')
                keycode(retry.get('key', ''))
                duration(retry.setdefault('seconds', .1), 1.5, 'Retry tap seconds')
                duration(retry.setdefault('every', 1), 30, 'Retry interval')
                if retry['every'] < max(.2, retry['seconds']):
                    raise AgentError('Retry interval must be at least 0.2 seconds and no shorter than its tap')
        elif op == 'assert' and 'distanceFrom' in step:
            reference = step['distanceFrom']
            keys(reference, ('checkpoint', 'min', 'max'), 'distanceFrom')
            if not isinstance(reference.get('checkpoint'), str) or reference['checkpoint'] not in checkpoints:
                raise AgentError("distanceFrom must name an earlier checkpoint")
            bounds = {k: v for k, v in reference.items() if k != 'checkpoint'}
            if not bounds or not all(number(v) and v >= 0 for v in bounds.values()) or bounds.get('min', 0) > bounds.get('max', math.inf):
                raise AgentError("distanceFrom requires nonnegative, ordered min/max bounds")
        elif op == 'checkpoint':
            if type(step.setdefault('capture', True)) is not bool:
                raise AgentError("checkpoint.capture must be boolean")
            captures += step['capture']
            checkpoints.add(name)
        elif op == 'tap':
            keycode(step.get('key', ''))
            duration(step.setdefault('seconds', .15), 1.5, 'Tap seconds')
        elif op == 'sleep':
            duration(step.get('seconds'), 30, 'Sleep seconds')
        elif op == 'command':
            command = validate_command(step.get('value'))
            if command.split(' ', 1)[0] not in {'key', 'mouse', 'button', 'text', 'release'}:
                raise AgentError("command steps accept ordinary input or release; the runner owns quit and observation")
    if captures > 31:
        raise AgentError("At most 31 capture checkpoints are allowed; one report slot is reserved for failure evidence")
    return result


def lookup(state, field):
    value = state
    for part in field.split('.'):
        if isinstance(value, dict):
            value = value.get(part, MISSING)
        elif isinstance(value, list) and part.isdecimal() and int(part) < len(value):
            value = value[int(part)]
        else:
            return MISSING
    return value


def equal(actual, expected):
    if actual is MISSING:
        return False
    if number(expected):
        return number(actual) and actual == expected
    if isinstance(expected, list):
        return isinstance(actual, list) and len(actual) == len(expected) and all(equal(a, e) for a, e in zip(actual, expected))
    return type(actual) is type(expected) and actual == expected


def mismatches(state, expect):
    failures = []
    for field, expected in expect.items():
        actual = lookup(state, field)
        if isinstance(expected, dict):
            if 'notNull' in expected:
                matches = actual is not MISSING and actual is not None
            else:
                matches = (number(actual) and actual >= expected.get('min', -math.inf)
                           and actual <= expected.get('max', math.inf))
        else:
            matches = equal(actual, expected)
        if not matches:
            failures.append({'field': field, 'expected': expected,
                             'actual': None if actual is MISSING else actual,
                             'missing': actual is MISSING})
    return failures


class ScenarioRunner:
    def __init__(self, spec, result, session, deadline, interrupted, client):
        self.spec, self.result, self.session = spec, result, Path(session)
        self.deadline, self.interrupted = deadline, interrupted
        self.checkpoints = {}
        self.last_state = None
        self.row = None
        self.client = client
        self.capture_count = 0
        self.capture_bytes = 0

    def remaining(self, deadline=None):
        if self.interrupted:
            raise AgentError("Scenario interrupted by signal")
        remaining = min(self.deadline, deadline or self.deadline) - time.monotonic()
        if remaining <= 0:
            raise AgentTimeout("Scenario or step timed out")
        return remaining

    def request(self, command, deadline=None):
        response = self.client.request(command, timeout=min(10, self.remaining(deadline)))
        self.row.setdefault('requestIds', []).append(response['id'])
        return response['result']

    def state(self, deadline=None):
        state = self.request('state', deadline)
        try:
            json.dumps(state, allow_nan=False)
        except ValueError as exc:
            raise AgentError("Engine state contains a non-finite value") from exc
        self.last_state = self.row['state'] = state
        self.result.setdefault('initialState', state)
        self.result['finalState'] = state
        return state

    def capture(self):
        if self.capture_count >= 32:
            raise AgentError('Native capture count exceeds 32')
        self.capture_count += 1
        capture = self.request('capture')
        name = capture.get('file')
        width, height = capture.get('width'), capture.get('height')
        if (not isinstance(name, str) or not re.fullmatch(r'frame-[1-9][0-9]*\.ppm', name)
                or type(width) is not int or type(height) is not int
                or not 1 <= width <= 2048 or not 1 <= height <= 2048
                or width * height > 2097152):
            raise AgentError("Invalid capture receipt")
        if self.capture_bytes + width * height * 3 + 64 > MAX_CAPTURE_BYTES:
            raise AgentError('Native capture evidence exceeds 192 MiB')
        data = read_regular(self.session / 'agent' / name, width * height * 3 + 64)
        header = f'P6\n{width} {height}\n255\n'.encode('ascii')
        if data is None or not data.startswith(header) or len(data) != len(header) + width * height * 3:
            raise AgentError("Capture receipt does not identify a complete native RGB image")
        self.capture_bytes += len(data)
        self.row['capture'] = dict(capture, sha256=hashlib.sha256(data).hexdigest())

    def sleep(self, seconds, deadline=None):
        finish = time.monotonic() + seconds
        while time.monotonic() < finish:
            time.sleep(min(.05, max(0, finish - time.monotonic()), self.remaining(deadline)))

    def tap(self, key, seconds, deadline=None):
        if self.remaining(deadline) <= seconds:
            raise AgentTimeout("Not enough execution budget for this key tap")
        code = keycode(key)
        self.request(f'key {code} 1', deadline)
        original_error = None
        try:
            self.sleep(seconds, deadline)
        except BaseException as exc:
            original_error = exc
            raise
        finally:
            try:
                self.request(f'key {code} 0', deadline)
            except (Exception, KeyboardInterrupt) as release_error:
                if original_error is None:
                    raise
                original_error.add_note(f'Key-up also failed: {release_error}')

    def execute(self, step):
        op = step['op']
        self.remaining()
        if op == 'command':
            self.request(step['value'])
        elif op == 'tap':
            self.tap(step['key'], step['seconds'])
        elif op == 'sleep':
            self.sleep(step['seconds'])
        elif op == 'checkpoint':
            self.checkpoints[step['name']] = self.state()
            if step['capture']:
                self.capture()
        elif op == 'wait':
            finish = min(self.deadline, time.monotonic() + step['timeout'])
            since = None
            next_retry = 0
            while time.monotonic() < finish:
                self.row['mismatches'] = mismatches(self.state(finish), step['expect'])
                now = time.monotonic()
                if not self.row['mismatches']:
                    since = now if since is None else since
                    if now - since >= step['stableFor']:
                        return
                else:
                    since = None
                    retry = step.get('retryTap')
                    if retry and now >= next_retry and finish - now > retry['seconds']:
                        self.tap(retry['key'], retry['seconds'], finish)
                        next_retry = time.monotonic() + retry['every']
                self.sleep(min(.1, max(0, finish - time.monotonic())), finish)
            fields = ', '.join(step['expect'])
            raise AgentTimeout(f'Wait timed out for {fields}')
        elif op == 'assert':
            state = self.state()
            failures = mismatches(state, step.get('expect', {}))
            self.row['mismatches'] = failures
            if 'distanceFrom' in step:
                reference = step['distanceFrom']
                points = [lookup(s, 'player.position') for s in (state, self.checkpoints[reference['checkpoint']])]
                if any(not isinstance(p, list) or len(p) != 3 or not all(number(v) for v in p) for p in points):
                    raise AgentError('distanceFrom requires a valid finite player position at both checkpoints')
                distance = math.dist(*points)
                if not number(distance):
                    raise AgentError('distanceFrom calculation must be finite')
                self.row['distance'] = distance
                self.row['distanceFrom'] = reference
                if not reference.get('min', 0) <= distance <= reference.get('max', math.inf):
                    failures.append({'field': 'player.position distance', 'expected': reference, 'actual': distance})
            if failures:
                raise AgentError('Assertion failed: ' + ', '.join(f['field'] for f in failures))

    def run(self):
        for step, row in zip(self.spec['steps'], self.result['steps']):
            self.row = row
            started = time.monotonic()
            row['status'] = 'running'
            atomic_json(self.session / 'scenario.json', self.result)
            try:
                self.execute(step)
                row['status'] = 'passed'
            except (Exception, KeyboardInterrupt) as exc:
                row.update(status='failed', error=str(exc))
                failure = {'index': len(self.result['steps']) + 1, 'name': 'failure-evidence',
                           'op': 'failure-capture', 'status': 'unavailable'}
                if self.last_state is not None:
                    failure['state'] = self.last_state
                self.result['steps'].append(failure)
                self.row = failure
                try:
                    self.capture()
                    failure['status'] = 'passed'
                except (Exception, KeyboardInterrupt) as capture_error:
                    failure['error'] = str(capture_error)
                raise
            finally:
                row['durationSeconds'] = round(time.monotonic() - started, 6)
                atomic_json(self.session / 'scenario.json', self.result)


def reject_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise AgentError(f'Duplicate JSON key: {key}')
        result[key] = value
    return result


class Client:
    """One controller in one new private session; bounded, ID-matched native journal."""

    def __init__(self, session, process, deadline):
        self.session, self.process, self.deadline = Path(session), process, deadline
        self.directory = self.session / 'agent'
        self.sequence, self.journal_bytes = 0, 0

    def request(self, command, timeout=10):
        command = validate_command(command)
        if self.sequence >= MAX_COMMANDS:
            raise AgentError('Gameplay command budget exceeds 4096')
        # Reserve enough space for one maximum-size reply before issuing input.
        if self.journal_bytes + MAX_REPLY + 2048 > MAX_JOURNAL:
            raise AgentError('Gameplay command journal exceeds 16 MiB')
        finish = min(self.deadline, time.monotonic() + timeout)
        if time.monotonic() >= finish:
            raise AgentTimeout('Gameplay execution deadline reached')
        if self.process.poll() is not None:
            raise AgentError('Engine exited before acknowledging the next command')
        mailbox = self.directory / 'command.txt'
        if mailbox.exists() or mailbox.is_symlink():
            raise AgentError('An unconsumed command is pending; it cannot be replaced')
        self.sequence += 1
        request_id = self.sequence
        payload = f'{PROTOCOL}\n{request_id}\n{command}\n'.encode('ascii')
        temporary = self.directory / '.command.tmp'
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            with os.fdopen(fd, 'wb') as stream:
                stream.write(payload)
                stream.flush()
            os.link(temporary, mailbox)
        finally:
            temporary.unlink(missing_ok=True)
        record = {'id': request_id, 'command': command}
        started = time.monotonic()
        try:
            while time.monotonic() < finish:
                response = read_json(self.directory / 'response.json')
                if response is not None:
                    value = response.get('id')
                    if type(value) is not int or not 1 <= value <= 2147483647:
                        raise AgentError('Native response has an invalid command ID')
                    if value > request_id:
                        raise AgentError('Native response skipped the requested command ID')
                    if value == request_id:
                        record['response'] = response
                        if (type(response.get('schemaVersion')) is not int or response['schemaVersion'] != 1
                                or type(response.get('ok')) is not bool
                                or response.get('command') != command.split(' ', 1)[0]):
                            raise AgentError('Native response does not match the requested protocol/command')
                        if not response['ok']:
                            raise AgentError(str(response.get('error', 'Engine rejected command')))
                        if not isinstance(response.get('result'), dict):
                            raise AgentError('Native response has no result object')
                        return response
                if self.process.poll() is not None:
                    raise AgentError('Engine exited without an acknowledged command')
                time.sleep(.01)
            raise AgentTimeout(f'No native response for command {request_id}')
        except BaseException as exc:
            record['error'] = str(exc)
            raise
        finally:
            record['elapsedSeconds'] = round(time.monotonic() - started, 6)
            data = (json.dumps(record, allow_nan=False) + '\n').encode('utf-8')
            # A bounded reply plus bounded error/input cannot exceed the reservation.
            if len(data) > MAX_REPLY + 2048:
                raise AgentError('Native journal record exceeds its reserved budget')
            fd = os.open(self.directory / 'commands.jsonl', os.O_WRONLY | os.O_APPEND
                         | os.O_CREAT | os.O_NOFOLLOW | os.O_NONBLOCK, 0o600)
            with os.fdopen(fd, 'ab') as stream:
                if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                    raise AgentError('Native command journal must be a regular file')
                stream.write(data)
            self.journal_bytes += len(data)


def prepare(output, spec):
    output.chmod(0o700)
    (output / 'agent').mkdir(mode=0o700)
    marker = output / MARKER
    with marker.open('x', encoding='ascii') as stream:
        stream.write('openjkdf2-agent-session-v1\n')
    marker.chmod(0o600)
    atomic_json(output / 'scenario-input.json', spec)


def run(spec, output, process, deadline):
    """Called while RED owns and drains the engine. Return compact evidence identity."""
    client = Client(output, process, deadline)
    result = {'schemaVersion': 1, 'name': spec['name'], 'status': 'running',
              'scenarioSha256': digest(output / 'scenario-input.json'),
              'controllerSha256': digest(__file__), 'protocol': PROTOCOL,
              'physicsOverrides': False, 'deterministicSimulation': False,
              'steps': [{'index': index, 'name': step['name'], 'op': step['op'], 'status': 'not_run'}
                        for index, step in enumerate(spec['steps'], 1)]}
    atomic_json(output / 'scenario.json', result)
    original_error = None
    try:
        ping = client.request('ping', timeout=min(30, max(.01, deadline - time.monotonic())))
        if ping['result'].get('protocol') != PROTOCOL:
            raise AgentError('Engine did not acknowledge openjkdf2-agent-v1')
        result['handshake'] = ping
        ScenarioRunner(spec, result, output, deadline, (), client).run()
    except BaseException as exc:
        original_error = exc
        result.update(status='failed', error=f'{type(exc).__name__}: {exc}')
    finally:
        # Never send a second command over a timed-out pending mailbox. The
        # supervisor still owns cleanup; failed cleanup cannot become success.
        if process.poll() is None:
            for command in ('release', 'quit'):
                try:
                    client.request(command, timeout=min(2, max(.01, deadline - time.monotonic())))
                except (AgentError, OSError) as exc:
                    result.setdefault('shutdownErrors', []).append(str(exc))
                    break
        else:
            result.setdefault('shutdownErrors', []).append('Engine exited before release/quit')
        if result.get('shutdownErrors') and original_error is None:
            original_error = AgentError('Gameplay release/quit did not finish cleanly')
            result.update(status='failed', error=str(original_error))
        if original_error is None:
            result['status'] = 'passed'
        result['commands'] = client.sequence
        history = output / 'agent/commands.jsonl'
        if history.is_file() and not history.is_symlink():
            result['journal'] = {'path': 'agent/commands.jsonl', 'sha256': digest(history),
                                 'bytes': history.stat().st_size}
        atomic_json(output / 'scenario.json', result)
    if original_error is not None:
        raise original_error
    captures = [dict(step['capture'], step=step['name'], path='agent/' + step['capture']['file'])
                for step in result['steps'] if 'capture' in step]
    return {'status': 'passed', 'assertions': 'only the explicit recipe assertions were checked',
            'scenarioPath': 'scenario.json', 'scenarioSha256': digest(output / 'scenario.json'),
            'scenarioInputSha256': result['scenarioSha256'], 'controllerSha256': result['controllerSha256'],
            'journal': result['journal'], 'commands': client.sequence, 'captures': captures,
            'physicsOverrides': False, 'deterministicSimulation': False}

