"""Read an explicit RED or SED-MotS installation without editing levels or launching UI."""
import argparse, hashlib, json, pathlib, subprocess, sys

def run(path, args):
    completed = subprocess.run([str(path),*args],capture_output=True,text=True,encoding='utf-8',timeout=30)
    if completed.returncode:
        raise ValueError(f'{path.name} exited {completed.returncode}: {completed.stderr[:1500]} {completed.stdout[:1500]}')
    return json.loads(completed.stdout)

def file_identity(path):
    digest=hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024*1024), b''): digest.update(chunk)
    return {'path':str(path),'sha256':digest.hexdigest()}

def probe(root=None, agent_path=None):
    root=pathlib.Path(root).resolve() if root else None
    if agent_path:
        agent=pathlib.Path(agent_path).resolve()
    elif root:
        candidates=[root/p for p in ['agent/sed-agent.exe','agent/sed-agent',
                    'red-agent.exe','red-agent','bin/red-agent.exe','bin/red-agent',
                    'tools/sed-agent/Release/red-agent.exe','tools/sed-agent/red-agent']]
        candidates=[p for p in candidates if p.is_file()]
        if len(candidates)!=1:
            raise ValueError('Expected one known agent executable under the explicit root; use --agent with its exact path')
        agent=candidates[0]
    else: raise ValueError('Supply an explicit --agent or --sed-root')
    if not agent.is_file(): raise ValueError('The explicit agent executable does not exist')
    schema=run(agent,['schema'])
    result={'agent':file_identity(agent),'schemaVersion':schema.get('schemaVersion'),
            'exitCodes':schema.get('exitCodes'),
            'limits':'Read-only capability probe; no level edit, graphical preview, engine test or MCP connection established'}
    if 'patchJsonSchema' in schema:
        result['implementation']='sed-mots-dotnet'
        result['agent']['version']=run(agent,['--version'])
        result['commands']=[{'name':x['name'],'usage':x['usage'],'writesFiles':x['writesFiles']}
                            for x in run(agent,['schema','commands'])['commands']]
        docs=['AGENT_PROTOCOL.md','AGENT_WORKFLOWS.md','MCP_INTEGRATION.md']
        if root:
            mcp=root/'mcp/sed-mcp.exe'
            if mcp.is_file():
                result['mcp']=file_identity(mcp)
                result['mcp']['version']=run(mcp,['--version'])
    elif schema.get('protocol')=='sed-agent' and isinstance(schema.get('operations'),list):
        result['implementation']='red-cpp-family'
        result['commands']=schema.get('commands',[])
        result['operations']=schema['operations']
        result['snapshotNote']=schema.get('snapshotNote')
        result['agent']['version']=None
        result['versionNote']='This CLI has no --version contract; executable hash identifies the inspected build'
        docs=['agent_workflow.md','capability_matrix.md','gap_register.md','sed_format.md']
    else: raise ValueError('Unrecognized agent discovery contract; inspect its schema before selecting a workflow')
    if root:
        result['sedRoot']=str(root)
        result['documentation']={name:(root/'docs'/name).is_file() for name in docs}
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config',type=pathlib.Path); p.add_argument('--sed-root',type=pathlib.Path)
    p.add_argument('--agent',type=pathlib.Path,help='Exact RED or SED-MotS agent executable; overrides executable discovery')
    p.add_argument('--output',type=pathlib.Path)
    args=p.parse_args()
    config=json.loads(args.config.read_text(encoding='utf-8-sig')) if args.config else {}
    root=args.sed_root or config.get('sedRoot')
    agent=args.agent or config.get('agentPath')
    if not root and not agent: p.error('Supply --agent, --sed-root or a private --config')
    result=probe(root, agent)
    if args.output:
        with args.output.open('x',encoding='utf-8') as f: json.dump(result,f,indent=2); f.write('\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':
    try: main()
    except (ValueError,OSError,subprocess.SubprocessError) as exc:
        print(json.dumps({'error':str(exc)})); sys.exit(2)
