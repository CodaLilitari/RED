"""Build an allowlisted level-workflow skill ZIP and research feed; no publication."""
import argparse, hashlib, json, pathlib, re, zipfile

def digest(data): return hashlib.sha256(data).hexdigest()
def dump(path,value): path.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')

def build(root,output,version,revision):
    root,output=pathlib.Path(root).resolve(),pathlib.Path(output).resolve()
    if not re.fullmatch(r'\d+\.\d+\.\d+',version) or revision<1: raise ValueError('Invalid version/revision')
    if output.exists(): raise ValueError('Choose a new output directory')
    if (root/'VERSION').read_text(encoding='utf-8').strip()!=version: raise ValueError('VERSION differs from requested full release')
    paths=[root/name for name in ('SKILL.md','VERSION','LICENSE','NOTICE.md','agents/openai.yaml')]
    paths+=sorted((root/'references').rglob('*.md'))+sorted((root/'references').rglob('*.json'))
    paths+=sorted((root/'scripts').glob('*.py'))
    payloads={p.relative_to(root).as_posix():p.read_bytes() for p in paths}
    for name,data in payloads.items():
        text=data.decode('utf-8')
        if re.search(r'(?i)C:[\\/]Users[\\/]|(?:discord(?:app)?\.com/channels/\d)|\b\d{17,20}\b',text):
            raise ValueError('Potential private path/message locator in '+name)
    entries=[{'path':name,'bytes':len(data),'sha256':digest(data)} for name,data in sorted(payloads.items())]
    manifest={'schema_version':1,'name':'jk-mots-level-workflow','version':version,'files':entries}
    references=[e for e in entries if e['path'].startswith('references/')]
    state={'schema_version':1,'version':version,'research_revision':revision,'files':references}
    payloads['PACKAGE-MANIFEST.json']=(json.dumps(manifest,indent=2)+'\n').encode()
    payloads['research-update-state.json']=(json.dumps(state,indent=2)+'\n').encode()
    output.mkdir(parents=True)
    zip_path=output/f'jk-mots-level-workflow-{version}.zip'
    with zipfile.ZipFile(zip_path,'x',compression=zipfile.ZIP_DEFLATED) as archive:
        for name,data in sorted(payloads.items()):
            info=zipfile.ZipInfo('jk-mots-level-workflow/'+name,date_time=(2026,1,1,0,0,0)); info.compress_type=zipfile.ZIP_DEFLATED
            archive.writestr(info,data)
    for entry in references:
        path=output/'releases'/version/entry['path']; path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(payloads[entry['path']])
    feed={'schema_version':1,'scope':'research-only','version':version,'requires_skill_version':version,'research_revision':revision,'files':references}
    dump(output/'research-updates.json',feed)
    dump(output/'PACKAGE-MANIFEST.json',manifest)
    (output/'SHA256SUMS.txt').write_text(f'{digest(zip_path.read_bytes())}  {zip_path.name}\n',encoding='utf-8')
    receipt={'zip':str(zip_path),'zipSha256':digest(zip_path.read_bytes()),'members':len(payloads),'referenceFiles':len(references),'privateSourcesIncluded':False,'published':False}
    dump(output/'build-receipt.json',receipt)
    return receipt

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('--root',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[1]); p.add_argument('--output',required=True,type=pathlib.Path); p.add_argument('--version',required=True); p.add_argument('--revision',type=int,required=True)
    a=p.parse_args(); print(json.dumps(build(a.root,a.output,a.version,a.revision),indent=2))
