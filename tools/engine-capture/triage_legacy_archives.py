"""Inspect a folder of JK/MotS GOB and GOO archives with red-agent."""
import argparse
from collections import Counter
from contextlib import contextmanager
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import threading

from verify_episode_pair import archive_index

MAX_CHILDREN = 4096
MAX_ARCHIVES = 64
MAX_STOCK = 4
MAX_BYTES = 256 << 20
MAX_TOTAL_BYTES = 1 << 30
MAX_ENTRIES = 16384
MAX_MAPS = 32
MAX_TOTAL_MAPS = 128
MAX_AGENT_INPUT_BYTES = 8 << 30
MAX_MAP_BYTES = 8 << 20
MAX_OUTPUT = 2 << 20
MAX_SECONDS = 30
MAX_DETAILS = 16


class Refused(ValueError):
    """An input or check could not be inspected within this contract."""


@dataclass
class Budget:
    bytes_left: int
    maps_left: int
    agent_bytes_left: int


def is_link(info):
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info, 'st_file_attributes', 0)
                                               & stat.FILE_ATTRIBUTE_REPARSE_POINT)


@contextmanager
def directory(path):
    """Pin the selected directory where descriptor-relative opens are available."""
    info = path.lstat()
    if is_link(info) or not stat.S_ISDIR(info.st_mode):
        raise Refused('not-regular-directory')
    fd = None
    try:
        if os.open in os.supports_dir_fd and os.scandir in os.supports_fd:
            fd = os.open(path, os.O_DIRECTORY | os.O_NOFOLLOW)
            if not os.path.samestat(info, os.fstat(fd)):
                raise Refused('directory-changed')
        yield fd, info
    finally:
        if fd is not None:
            os.close(fd)


def candidates(path, fd):
    names = []
    with os.scandir(fd if fd is not None else path) as entries:
        for index, entry in enumerate(entries):
            if index >= MAX_CHILDREN:
                raise Refused('directory-child-limit')
            if entry.name.lower().endswith(('.gob', '.goo')):
                names.append(entry.name)
                if len(names) > MAX_ARCHIVES:
                    raise Refused('archive-count-limit')
    if not names:
        raise Refused('no-archives')
    return sorted(names, key=lambda name: (name.casefold(), name))


def capture(path, destination, owner, budget):
    fd, parent_info = owner
    source = path if fd is None else path.name
    before = os.stat(source, dir_fd=fd, follow_symlinks=False)
    if is_link(before) or not stat.S_ISREG(before.st_mode):
        raise Refused('not-regular-file')
    if before.st_size > min(budget.bytes_left, MAX_BYTES):
        raise Refused('source-byte-limit')
    flags = (os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0)
             | getattr(os, 'O_NONBLOCK', 0))
    digest = hashlib.sha256()
    with os.fdopen(os.open(source, flags, dir_fd=fd), 'rb') as stream:
        current = os.fstat(stream.fileno())
        if (not os.path.samestat(before, current) or before.st_size != current.st_size
                or (fd is None and not os.path.samestat(parent_info, path.parent.lstat()))):
            raise Refused('source-changed')
        remaining = current.st_size
        budget.bytes_left -= remaining
        with destination.open('wb') as output:
            while remaining:
                block = stream.read(min(remaining, 1 << 20))
                if not block:
                    raise Refused('source-changed')
                digest.update(block)
                output.write(block)
                remaining -= len(block)
        if stream.read(1) or os.fstat(stream.fileno()).st_mtime_ns != current.st_mtime_ns:
            raise Refused('source-changed')
    return {'name': path.name, 'bytes': current.st_size, 'sha256': digest.hexdigest()}


def inspect_index(path):
    with path.open('rb') as stream:
        try:
            entries = archive_index(stream, max_entries=MAX_ENTRIES)
        except ValueError as error:
            raise Refused(str(error)[:240]) from error
    end = 0
    for offset, length in sorted((offset, length) for offset, length in entries.values() if length):
        if offset < end:
            raise Refused('overlapping-payloads')
        end = offset + length
    return entries


def agent_check(agent, cwd, command, request):
    (cwd / 'request.json').write_text(json.dumps(request), encoding='ascii')
    with subprocess.Popen([agent, command, 'level.jkl', 'request.json'], cwd=cwd,
                          stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL) as proc:
        timed_out = threading.Event()

        def expire():
            timed_out.set()
            proc.kill()

        timer = threading.Timer(MAX_SECONDS, expire)
        timer.start()
        try:
            data = proc.stdout.read(MAX_OUTPUT + 1)
            if len(data) > MAX_OUTPUT:
                raise Refused('agent-output-limit')
            proc.wait()
            if timed_out.is_set():
                raise Refused('agent-timeout')
        finally:
            timer.cancel()
            proc.kill()
            timer.join()
        try:
            reply = json.loads(data)
        except (ValueError, RecursionError) as error:
            raise Refused('invalid-agent-reply') from error
        if not isinstance(reply, dict):
            raise Refused('invalid-agent-reply')
        if proc.returncode or reply.get('ok') is not True:
            raise Refused(str(reply.get('error', f'agent-exit-{proc.returncode}'))[:240])
        if reply.get('command') != command or reply.get('inputSha256') != request['inputSha256']:
            raise Refused('agent-receipt-mismatch')
        return reply


def validation(agent, cwd, base, mounts):
    request = dict(base, strict=True, resourceArchives=mounts,
                   fanDegeneracyLimits={'maxIssues': 4096, 'maxFanTriangles': 1000000},
                   seamLimits={'maxSectorPairs': 1000000, 'maxSurfacePairs': 250000, 'maxEdgePairs': 1000000})
    result = agent_check(agent, cwd, 'validate', request)
    issues = result['issues']
    counts = Counter(item.get('code', 'cog-reference') for item in issues)
    # Keep uncommon loader-limit errors visible even after many ordinary fan warnings.
    examples = {}
    fields = ('severity', 'code', 'message', 'section', 'line', 'sector', 'surface',
              'corners', 'limit', 'cog', 'parameter')
    for item in issues:
        code = item.get('code', 'cog-reference')
        if code not in examples and len(examples) < MAX_DETAILS:
            examples[code] = {k: v[:240] if isinstance(v, str) else v for k, v in item.items() if k in fields}
    return {'loaded': 'counts' in result, 'rawStructure': result['rawStructureStatus'], 'valid': result['valid'],
            'seams': result['seamStatus'], 'fans': result.get('fanDegeneracyStatus', 'not-run'),
            'cogReferences': result['cogReferenceStatus'], 'issueCounts': dict(counts),
            'issues': list(examples.values()), 'detailsTruncated': len(examples) < len(issues)}


def dependencies(agent, cwd, base, mounts, inventories):
    result = agent_check(agent, cwd, 'dependencies', dict(base, archives=mounts,
                         limits={'maxDetailRecords': 512, 'maxDetailBytes': 262144}))
    actual = [(a['sha256'], a['entries']) for a in result['sourceArchives']]
    if actual != [(a['sha256'], a['entries']) for a in inventories]:
        raise Refused('agent-mount-index-mismatch')
    unresolved = [dict(path=n['path'][:240], resolution=n['resolution'])
                  for n in result['nodes'] if n['resolution'] in ('missing', 'ambiguous')]
    fields = ('scanComplete', 'detailsComplete', 'allReferencesResolved', 'missingObserved', 'ambiguousObserved',
              'unsupportedObserved', 'issuesObserved', 'unverifiedScopes', 'scanReasons')
    summary = {key: result[key] for key in fields}
    summary['unresolved'] = unresolved[:MAX_DETAILS]
    summary['unresolvedTruncated'] = len(summary['unresolved']) < result['missingObserved'] + result['ambiguousObserved']
    return summary


def check_error(error):
    if isinstance(error, OSError):
        return f'file-error-{error.errno}'
    if isinstance(error, Refused):
        return str(error)[:240]
    return 'invalid-agent-reply'


def inspect_map(agent, cwd, stream, name, extent, mounts, inventories, budget):
    offset, length = extent
    row = {'name': name, 'bytes': length, 'status': 'incomplete'}
    if length > MAX_MAP_BYTES:
        return dict(row, reason='map-byte-limit')
    stream.seek(offset)
    data = stream.read(length)
    if len(data) != length:
        return dict(row, reason='truncated-snapshot')
    row['sha256'] = hashlib.sha256(data).hexdigest()
    # Both native commands capture the map and mounts. Reserve both before launching either.
    charge = 2 * (length + sum(item['bytes'] for item in inventories))
    if charge > budget.agent_bytes_left:
        return dict(row, reason='agent-input-budget')
    budget.agent_bytes_left -= charge
    (cwd / 'level.jkl').write_bytes(data)
    base = {'schemaVersion': 1, 'inputSha256': row['sha256']}
    try:
        checks = row['validation'] = validation(agent, cwd, base, mounts)
    except (OSError, Refused, KeyError, TypeError) as error:
        row['validation'] = {'reason': check_error(error)}
        row['dependencies'] = {'reason': 'validation-unavailable'}
        return row
    if not checks['loaded']:
        row['status'] = 'findings' if checks['rawStructure'] == 'checked' else 'incomplete'
        row['dependencies'] = {'reason': 'raw-preflight-stopped'}
        return row
    try:
        refs = row['dependencies'] = dependencies(agent, cwd, base, mounts, inventories)
    except (OSError, Refused, KeyError, TypeError) as error:
        row['dependencies'] = {'reason': check_error(error)}
        return row
    if refs['scanComplete'] and checks['seams'] == checks['fans'] == 'complete':
        row['status'] = ('findings' if checks['issueCounts'] or not checks['valid'] or refs['issuesObserved']
                         or refs['missingObserved'] or refs['ambiguousObserved'] else 'checked')
        if refs['unsupportedObserved'] or refs['unverifiedScopes']:
            row['status'] = 'incomplete'
    return row


def triage(folder, agent, stock):
    if len(stock) > MAX_STOCK:
        raise Refused('stock-count-limit')
    report = {'schemaVersion': 1, 'runtimeVerified': False, 'resourceScope': 'episode-then-ordered-stock',
              'stock': [], 'archives': []}
    budget = Budget(MAX_TOTAL_BYTES, MAX_TOTAL_MAPS, MAX_AGENT_INPUT_BYTES)
    with directory(folder) as owner, tempfile.TemporaryDirectory(prefix='red-triage-') as temp:
        names = candidates(folder, owner[0])
        cwd = Path(temp)
        mounts = []
        for index, path in enumerate(stock):
            staged = cwd / f'stock{index}.gob'
            try:
                with directory(path.parent) as stock_owner:
                    record = capture(path, staged, stock_owner, budget)
                record['entries'] = len(inspect_index(staged))
            except (Refused, OSError) as error:
                raise Refused(f'stock[{index}] {path.name}: {check_error(error)}') from error
            report['stock'].append(record)
            mounts.append({'path': staged.name, 'sha256': record['sha256'], 'stock': True})
        staged = cwd / 'episode.gob'
        for name in names:
            row = {'name': name, 'status': 'refused'}
            report['archives'].append(row)
            try:
                row.update(capture(folder / name, staged, owner, budget))
                entries = inspect_index(staged)
                row['entries'] = len(entries)
                row['episodeDefinitionPresent'] = 'episode.jk' in entries
                maps = sorted(key for key in entries if key.endswith('.jkl'))
                row['mapCount'] = len(maps)
                if len(maps) > min(MAX_MAPS, budget.maps_left):
                    raise Refused('map-count-limit')
                budget.maps_left -= len(maps)
                sources = [{'path': staged.name, 'sha256': row['sha256']}, *mounts]
                inventories = [row, *report['stock']]
                with staged.open('rb') as stream:
                    row['maps'] = [inspect_map(agent, cwd, stream, key, entries[key], sources, inventories, budget)
                                   for key in maps]
                states = {m['status'] for m in row['maps']}
                row['status'] = next((s for s in ('incomplete', 'findings', 'checked') if s in states), 'no-maps')
            except Refused as error:
                row['reason'] = str(error)
            except OSError as error:
                row['reason'] = f'file-error-{error.errno}'
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    parser.add_argument('--agent', default='red-agent', help='RED CLI executable')
    parser.add_argument('--stock', type=Path, action='append', default=[], help='ordered stock GOB/GOO (repeatable)')
    args = parser.parse_args()
    try:
        agent = shutil.which(args.agent)
        if not agent:
            raise Refused('red-agent-unavailable')
        report = triage(args.folder, os.path.abspath(agent), args.stock)
        print(json.dumps(report, sort_keys=True))
        return int(any(a['status'] != 'checked' for a in report['archives']))
    except (Refused, OSError) as error:
        reason = f'file-error-{error.errno}' if isinstance(error, OSError) else str(error)
        print(json.dumps({'schemaVersion': 1, 'status': 'refused', 'reason': reason}))
        return 2


if __name__ == '__main__':
    sys.exit(main())
