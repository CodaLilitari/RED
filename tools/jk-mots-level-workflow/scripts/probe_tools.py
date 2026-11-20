"""Inspect the selected RED release without opening a level or launching a window."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

RED_AGENTS = (
    'red-agent', 'red-agent.exe', 'bin/red-agent', 'bin/red-agent.exe',
    'usr/bin/red-agent', 'Contents/MacOS/red-agent',
    'tools/sed-agent/red-agent', 'tools/sed-agent/Release/red-agent.exe',
)
SED_AGENTS = ('agent/sed-agent.exe', 'agent/sed-agent')


def run(path, args):
    result = subprocess.run(
        [str(path), *args], capture_output=True, text=True, encoding='utf-8', timeout=30
    )
    if result.returncode:
        raise ValueError(f'{path.name} exited {result.returncode}: {result.stderr[:1500]} {result.stdout[:1500]}')
    return json.loads(result.stdout)


def file_identity(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return {'path': str(path), 'sha256': digest.hexdigest()}


def resolve_agent(root, exact, legacy):
    if exact:
        path = pathlib.Path(exact).expanduser().resolve()
        if not path.is_file():
            raise ValueError(f'Agent executable does not exist: {path}')
        return path
    if root:
        names = SED_AGENTS if legacy else RED_AGENTS
        candidates = {p.resolve() for name in names if (p := root / name).is_file()}
        if len(candidates) != 1:
            raise ValueError('Expected one matching agent under the selected package or build root. Use --agent with its exact path.')
        return candidates.pop()
    command = 'sed-agent' if legacy else 'red-agent'
    found = shutil.which(command)
    if found:
        return pathlib.Path(found).resolve()
    raise ValueError(f'{command} not found. Supply an exact --agent or the extracted distribution root.')


def probe(root=None, agent_path=None, legacy=False):
    root = pathlib.Path(root).expanduser().resolve() if root else None
    agent = resolve_agent(root, agent_path, legacy)
    schema = run(agent, ['schema'])
    result = {'agent': file_identity(agent), 'schemaVersion': schema.get('schemaVersion'),
              'exitCodes': schema.get('exitCodes'), 'limits': 'Read-only CLI capability inventory'}
    if schema.get('protocol') == 'sed-agent' and isinstance(schema.get('operations'), list):
        if legacy:
            raise ValueError('Selected agent reports RED. Omit --legacy-sed.')
        result.update(implementation='red-cpp-family', commands=schema.get('commands', []),
                      operations=schema['operations'], snapshotNote=schema.get('snapshotNote'))
        result['agent']['version'] = None
        result['versionNote'] = 'Use the release identity and executable hash for this RED build.'
        mcp = agent.with_name('red-mcp' + agent.suffix)
        docs = ('agent_workflow.md', 'capability_matrix.md', 'gap_register.md', 'sed_format.md')
    elif 'patchJsonSchema' in schema:
        if not legacy:
            raise ValueError('Selected agent reports deprecated SED-MotS. Select RED, or use --legacy-sed for historical reproduction.')
        result['implementation'] = 'sed-mots-dotnet'
        result['agent']['version'] = run(agent, ['--version'])
        result['commands'] = [{key: row[key] for key in ('name', 'usage', 'writesFiles')}
                              for row in run(agent, ['schema', 'commands'])['commands']]
        mcp = root / 'mcp' / ('sed-mcp' + agent.suffix) if root else None
        docs = ('AGENT_PROTOCOL.md', 'AGENT_WORKFLOWS.md', 'MCP_INTEGRATION.md')
    else:
        raise ValueError('Unrecognized agent discovery contract. Inspect its schema before selecting an editor.')
    if mcp and mcp.is_file():
        result['mcp'] = file_identity(mcp)
        if legacy:
            result['mcp']['version'] = run(mcp, ['--version'])
    if root:
        result['root'] = str(root)
        result['sedRoot' if legacy else 'redRoot'] = str(root)
        result['documentation'] = {name: (root / 'docs' / name).is_file() for name in docs}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    default_config = pathlib.Path(__file__).resolve().parents[1] / 'local-config.json'
    parser.add_argument('--config', type=pathlib.Path, help='Private installation configuration')
    parser.add_argument('--red-root', type=pathlib.Path, help='Extracted RED package or build root')
    parser.add_argument('--sed-root', type=pathlib.Path, help='Compatibility root option. Historical .NET use requires --legacy-sed.')
    parser.add_argument('--legacy-sed', action='store_true', help='Inspect deprecated .NET SED-MotS for historical use')
    parser.add_argument('--agent', type=pathlib.Path, help='Exact agent executable')
    parser.add_argument('--output', type=pathlib.Path, help='Write receipt to a new file')
    args = parser.parse_args()
    if args.red_root and (args.sed_root or args.legacy_sed):
        parser.error('Select one editor root and mode')
    root = args.red_root or args.sed_root
    agent = args.agent
    if not (root or agent):
        config_path = args.config or default_config
        config = json.loads(config_path.read_text(encoding='utf-8-sig')) if args.config or config_path.is_file() else {}
        if args.legacy_sed:
            root = config.get('sedRoot')
            agent = config.get('legacyAgentPath')
            if not agent and not config.get('redRoot'):
                agent = config.get('agentPath')
        else:
            root = config.get('redRoot')
            agent = config.get('agentPath') if root or not config.get('sedRoot') else None
    result = probe(root, agent, args.legacy_sed)
    if args.output:
        with args.output.open('x', encoding='utf-8') as f:
            json.dump(result, f, indent=2)
            f.write('\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        print(json.dumps({'error': str(exc)}))
        sys.exit(2)
