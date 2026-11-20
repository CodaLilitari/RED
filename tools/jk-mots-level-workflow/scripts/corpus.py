"""Private, offline research index. Standard library only; never uploads anything."""
from __future__ import annotations
import argparse, collections, contextlib, datetime as dt, hashlib, html.parser, json, os, pathlib, re, sqlite3, sys, tempfile

STAGES = {'reviewed', 'source_inspected', 'runtime_verified', 'reused'}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def read_json(path):
    return json.loads(pathlib.Path(path).read_text(encoding='utf-8-sig'))

def emit(value):
    print(json.dumps(value, ensure_ascii=False, indent=2))

def write_new(path, value):
    with pathlib.Path(path).open('x', encoding='utf-8') as f:
        json.dump(value, f, ensure_ascii=False, indent=2)
        f.write('\n')

def atomic_json(path, value):
    path = pathlib.Path(path)
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent, delete=False, suffix='.tmp') as f:
        json.dump(value, f, ensure_ascii=False, indent=2)
        f.write('\n')
        temp = pathlib.Path(f.name)
    try:
        os.replace(temp, path)
    finally:
        temp.unlink(missing_ok=True)

class TextExtractor(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts, self.skip = [], 0
    def handle_starttag(self, tag, attrs):
        if tag in {'script', 'style'}: self.skip += 1
        if tag in {'p', 'br', 'div', 'li', 'tr', 'h1', 'h2', 'h3'}: self.parts.append('\n')
    def handle_endtag(self, tag):
        if tag in {'script', 'style'}: self.skip = max(0, self.skip - 1)
    def handle_data(self, data):
        if not self.skip: self.parts.append(data)

def items(data):
    if isinstance(data, list): return data, ''
    for key in ('items', 'messages'):
        if isinstance(data.get(key), list): return data[key], key
    raise ValueError('No supported items/messages array; register this input as text or add an explicit adapter')

def source_rows(spec, raw):
    text = raw.decode('utf-8-sig')
    kind = spec['kind']
    if kind == 'discord_jsonl':
        for line, body in enumerate(text.splitlines(), 1):
            if body.strip(): yield json.loads(body), {'line': line}
    elif kind == 'discord_json':
        rows, key = items(json.loads(text))
        for i, row in enumerate(rows):
            if not isinstance(row, dict) or not row.get('id') or not isinstance(row.get('content'), str):
                raise ValueError('Unsupported message row at index '+str(i)+'; use an explicit text-document adapter for UI transcripts')
            yield row, {'array': key, 'index': i}
    elif kind in {'text', 'html'}:
        if kind == 'html':
            parser = TextExtractor(); parser.feed(text)
            text = re.sub(r'\n[ \t]*\n+', '\n\n', ''.join(parser.parts))
        yield {'id': 'file:' + sha(str(pathlib.Path(spec['path']).resolve()).encode()), 'content': text,
               'title': spec.get('title', pathlib.Path(spec['path']).name), 'url': spec.get('url', '')}, {'document': True}
    else:
        raise ValueError('Unsupported source kind: ' + kind)

def normalized(row, spec):
    identity = str(row['id'])
    body = row['content']
    channel = row.get('group') or spec.get('channel', 'unknown')
    stamp = row.get('datetime', '')
    # This independent creation time never overwrites potentially contaminated UI timestamps.
    derived = ''
    if re.fullmatch(r'\d{17,20}', identity):
        derived = dt.datetime.fromtimestamp(((int(identity) >> 22) + 1420070400000) / 1000, dt.timezone.utc).isoformat()
    content_hash = sha(body.encode('utf-8'))
    # Preserve edited/collector variants rather than silently selecting one body's text.
    key = identity + ':' + content_hash
    return (key, identity, content_hash, row.get('title', ''), body, row.get('author', ''), channel,
            stamp, derived, row.get('url', spec.get('url', '')), spec.get('privacy', 'private'))

def build(manifest_path, db_path):
    manifest_path, db_path = pathlib.Path(manifest_path), pathlib.Path(db_path)
    manifest = read_json(manifest_path)
    if manifest.get('schemaVersion') != 1: raise ValueError('Unsupported manifest schema')
    db_path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(dir=db_path.parent, suffix='.sqlite.tmp'); os.close(fd)
    temp = pathlib.Path(temp_name)
    db = sqlite3.connect(temp)
    try:
        db.executescript('''
        CREATE TABLE sources(path TEXT PRIMARY KEY, sha256 TEXT NOT NULL, kind TEXT, privacy TEXT, scope TEXT, count INTEGER);
        CREATE TABLE records(key TEXT PRIMARY KEY, id TEXT, content_hash TEXT, title TEXT, body TEXT, author TEXT, channel TEXT, reported_time TEXT, snowflake_time TEXT, url TEXT, privacy TEXT);
        CREATE INDEX record_id ON records(id); CREATE INDEX channel_time ON records(channel,snowflake_time);
        CREATE TABLE occurrences(record_key TEXT, source_path TEXT, ordinal INTEGER, locator TEXT, metadata TEXT, PRIMARY KEY(source_path,ordinal));
        CREATE INDEX occurrence_key ON occurrences(record_key);
        CREATE VIRTUAL TABLE search USING fts5(key UNINDEXED, title, body, tokenize='unicode61');
        CREATE TABLE metadata(name TEXT PRIMARY KEY,value TEXT);
        ''')
        seen = set()
        for spec in manifest['sources']:
            path = pathlib.Path(spec['path']).resolve()
            if str(path).casefold() in seen: raise ValueError('Duplicate source path: ' + str(path))
            seen.add(str(path).casefold())
            raw = path.read_bytes()
            digest = sha(raw)
            if digest != spec['sha256'].lower(): raise ValueError('Source changed; explicitly refresh manifest before rebuild: ' + str(path))
            count = 0
            for count, (row, locator) in enumerate(source_rows(spec, raw), 1):
                values = normalized(row, spec)
                db.execute('INSERT OR IGNORE INTO records VALUES (?,?,?,?,?,?,?,?,?,?,?)', values)
                metadata = {k:v for k,v in row.items() if k not in {'content','rendered','renderedText'}}
                db.execute('INSERT INTO occurrences VALUES (?,?,?,?,?)', (values[0], str(path), count, json.dumps(locator), json.dumps(metadata, ensure_ascii=False)))
            db.execute('INSERT INTO sources VALUES (?,?,?,?,?,?)', (str(path), digest, spec['kind'], spec.get('privacy','private'), spec.get('scope','Acquired only; semantic coverage unspecified'),count))
        db.execute('INSERT INTO search SELECT key,title,body FROM records')
        db.execute('INSERT INTO metadata VALUES (?,?)', ('manifest_sha256', sha(manifest_path.read_bytes())))
        db.execute('INSERT INTO metadata VALUES (?,?)', ('manifest_path', str(manifest_path.resolve())))
        db.commit()
        result = stats(db)
        if db.execute('PRAGMA integrity_check').fetchone()[0] != 'ok': raise ValueError('SQLite integrity check failed')
        db.close()
        os.replace(temp, db_path)
        return result
    finally:
        db.close()
        temp.unlink(missing_ok=True)

@contextlib.contextmanager
def connect(path):
    # A missing index must not be silently created by a read command.
    db = sqlite3.connect(pathlib.Path(path).resolve().as_uri() + '?mode=ro', uri=True)
    db.row_factory = sqlite3.Row
    try:
        yield db
    finally:
        db.close()

def stats(db):
    return {'sources': db.execute('SELECT COUNT(*) FROM sources').fetchone()[0],
            'recordsIncludingVariants': db.execute('SELECT COUNT(*) FROM records').fetchone()[0],
            'distinctIds': db.execute('SELECT COUNT(DISTINCT id) FROM records').fetchone()[0],
            'distinctMessageIds': db.execute("SELECT COUNT(DISTINCT id) FROM records WHERE id NOT LIKE 'file:%'").fetchone()[0],
            'documentIds': db.execute("SELECT COUNT(DISTINCT id) FROM records WHERE id LIKE 'file:%'").fetchone()[0],
            'occurrences': db.execute('SELECT COUNT(*) FROM occurrences').fetchone()[0],
            'coverage': 'Acquired/indexed only. Search matches and count totals do not establish review or completeness.'}

def load_events(path):
    return read_json(path) if pathlib.Path(path).exists() else []

def state_for(key, events):
    selected = [x for x in events if x['recordKey'] == key]
    return {'acquired': True, **{stage: [x for x in selected if x['stage'] == stage] for stage in sorted(STAGES)}}

def search(db, query, limit, offset, channel=None, author=None):
    # Treat words literally, not as SQL/FTS syntax. Users can search C++ and quotes safely.
    tokens = re.findall(r'\w+', query, flags=re.UNICODE)
    if not tokens: raise ValueError('Query needs at least one word')
    expression = ' AND '.join('"' + token.replace('"','""') + '"' for token in tokens)
    clause, params = '', [expression]
    if channel: clause += ' AND r.channel=?'; params.append(channel)
    if author: clause += ' AND r.author=?'; params.append(author)
    rows = db.execute('SELECT r.key,r.id,r.title,r.author,r.channel,r.reported_time,r.snowflake_time,snippet(search,2,\'[\',\']\',\'…\',32) AS snippet FROM search JOIN records r ON r.key=search.key WHERE search MATCH ?' + clause + ' ORDER BY bm25(search), r.key LIMIT ? OFFSET ?',params+[limit,offset]).fetchall()
    return [dict(r) for r in rows]

def get_records(db, identity):
    rows = db.execute('SELECT * FROM records WHERE id=? OR key=? ORDER BY key', (identity,identity)).fetchall()
    if not rows: raise ValueError('ID/key not found')
    return rows

def exact_payload(db, occurrence):
    spec = db.execute('SELECT * FROM sources WHERE path=?',(occurrence['source_path'],)).fetchone()
    raw = pathlib.Path(spec['path']).read_bytes()
    if sha(raw) != spec['sha256']: raise ValueError('Indexed source changed; refusing stale exact-context read: '+spec['path'])
    locator = json.loads(occurrence['locator'])
    text = raw.decode('utf-8-sig')
    if 'line' in locator: return json.loads(text.splitlines()[locator['line']-1])
    if 'index' in locator:
        data = json.loads(text)
        return (data[locator['array']] if locator['array'] else data)[locator['index']]
    return {'text': text}

def show(db, identity, context, events, raw=False):
    output = []
    for row in get_records(db,identity):
        entry = dict(row)
        entry['evidenceStates'] = state_for(row['key'],events)
        occurrences = [dict(x) for x in db.execute('SELECT o.*,s.sha256,s.kind,s.scope FROM occurrences o JOIN sources s ON s.path=o.source_path WHERE record_key=? ORDER BY source_path,ordinal',(row['key'],))]
        for o in occurrences:
            o['locator'] = json.loads(o['locator']); o['metadata'] = json.loads(o['metadata'])
        entry['occurrences'] = occurrences
        if raw:
            original = db.execute('SELECT * FROM occurrences WHERE record_key=? ORDER BY source_path,ordinal',(row['key'],)).fetchone()
            entry['exactSourcePayload'] = exact_payload(db,original)
        if context and row['snowflake_time']:
            before = db.execute('SELECT DISTINCT id,snowflake_time FROM records WHERE channel=? AND snowflake_time<? AND snowflake_time<>\'\' ORDER BY snowflake_time DESC LIMIT ?',(row['channel'],row['snowflake_time'],context)).fetchall()
            after = db.execute('SELECT DISTINCT id,snowflake_time FROM records WHERE channel=? AND snowflake_time>? ORDER BY snowflake_time LIMIT ?',(row['channel'],row['snowflake_time'],context)).fetchall()
            entry['neighborIds'] = [x['id'] for x in reversed(before)] + [x['id'] for x in after]
            entry['contextCaveat'] = 'Nearest acquired IDs in this channel/group only, not guaranteed contiguous conversation. Read IDs with show --raw; unknown channel records are never grouped.'
            if row['channel'] == 'unknown': entry['neighborIds'] = []
        output.append(entry)
    return output

def add_evidence(db, events_path, key, stage, receipt, note):
    if stage not in STAGES: raise ValueError('Unsupported evidence stage')
    row = get_records(db,key)
    if len(row) != 1 or row[0]['key'] != key: raise ValueError('Evidence requires one exact record key, not a possibly edited message ID')
    receipt = pathlib.Path(receipt).resolve()
    event = {'recordKey': key, 'stage': stage, 'recordedAt': dt.datetime.now(dt.timezone.utc).isoformat(),
             'receipt': str(receipt), 'receiptSha256':sha(receipt.read_bytes()), 'note':note}
    events = load_events(events_path)
    events.append(event)
    atomic_json(events_path,events)
    return event

def update_lead(ledger_path, finding_path):
    finding=read_json(finding_path)
    if not isinstance(finding,dict) or not re.fullmatch(r'[a-z0-9][a-z0-9-]{0,79}',finding.get('id','')):
        raise ValueError('Lead needs a stable lowercase id')
    if not finding.get('capability') or not finding.get('disposition') or not finding.get('next_acceptance_case'):
        raise ValueError('Lead needs capability, disposition and next_acceptance_case')
    ledger=read_json(ledger_path); previous=next((x for x in ledger['leads'] if x['id']==finding['id']),None)
    ledger.setdefault('history',[]).append({'id':finding['id'],'recordedAt':dt.datetime.now(dt.timezone.utc).isoformat(),'previous':previous,'inputSha256':sha(pathlib.Path(finding_path).read_bytes())})
    ledger['leads']=[x for x in ledger['leads'] if x['id']!=finding['id']]+[finding]
    ledger['revision']=ledger.get('revision',0)+1
    atomic_json(ledger_path,ledger)
    return {'id':finding['id'],'revision':ledger['revision'],'publication':'none; private ledger only'}

def export_findings(ledger_path, identities, output):
    ledger = read_json(ledger_path)
    leads = {x['id']:x for x in ledger['leads']}
    packet=[]
    for identity in identities:
        lead=leads[identity]
        approved=lead.get('shareable')
        if not approved or not approved.get('originalAnalysis') or not approved.get('reviewedForSharing'):
            raise ValueError('Finding not separately authored/reviewed for sharing: '+identity)
        # Closed allowlist: no raw records, private source refs, messages or paths get serialized.
        urls=approved.get('publicSources',[])
        if any(not u.startswith('https://') or 'discord' in u.lower() for u in urls):
            raise ValueError('Shareable source must be public HTTPS and not Discord')
        for field in ('title','finding','qualification','nextAction'):
            if not isinstance(approved.get(field),str): raise ValueError('Missing shareable '+field)
            if re.search(r'(?i)(?:[a-z]:[\\/]|discord(?:app)?\.com|\b\d{17,20}\b)', approved[field]):
                raise ValueError('Private locator in shareable '+field)
        packet.append({'id': identity, **{k:approved[k] for k in ('title','finding','qualification','nextAction')}, 'publicSources':urls})
    write_new(output, {'schemaVersion':1,'scope':'Original findings only; prepared locally, not uploaded or published','findings':packet})
    return {'output':str(pathlib.Path(output).resolve()),'sha256':sha(pathlib.Path(output).read_bytes()),'findings':len(packet)}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--home',type=pathlib.Path,required=True,help='Explicit private index directory')
    commands=parser.add_subparsers(dest='command',required=True)
    commands.add_parser('build'); commands.add_parser('stats')
    p=commands.add_parser('search'); p.add_argument('query'); p.add_argument('--limit',type=int,default=10); p.add_argument('--offset',type=int,default=0); p.add_argument('--channel'); p.add_argument('--author')
    p=commands.add_parser('show'); p.add_argument('id'); p.add_argument('--context',type=int,default=0); p.add_argument('--raw',action='store_true')
    p=commands.add_parser('evidence'); p.add_argument('key'); p.add_argument('stage',choices=sorted(STAGES)); p.add_argument('--receipt',required=True); p.add_argument('--note',required=True)
    p=commands.add_parser('leads'); p.add_argument('--id')
    p=commands.add_parser('update-lead'); p.add_argument('finding',help='Exact JSON finding; previous record retained in private ledger history')
    p=commands.add_parser('export-findings'); p.add_argument('ids',nargs='+'); p.add_argument('--output',required=True)
    p=commands.add_parser('refresh-source'); p.add_argument('path',help='Exact existing allowlisted source path; invalidates prior source hash')
    args=parser.parse_args()
    home=args.home.resolve(); manifest=home/'manifest.json'; db_path=home/'corpus.sqlite'; events=home/'evidence-events.json'; ledger=home/'leads.json'
    if args.command=='build': emit(build(manifest,db_path)); return
    if args.command=='refresh-source':
        data=read_json(manifest); target=pathlib.Path(args.path).resolve(); found=False
        for spec in data['sources']:
            if pathlib.Path(spec['path']).resolve()==target:
                spec['sha256']=sha(target.read_bytes()); found=True
        if not found: raise ValueError('Path is not in this manifest; add explicit source specification first')
        atomic_json(manifest,data); emit({'refreshed':str(target),'indexNeedsRebuild':True}); return
    if args.command=='leads':
        data=read_json(ledger)
        emit([x for x in data['leads'] if not args.id or x['id']==args.id]); return
    if args.command=='update-lead': emit(update_lead(ledger,args.finding)); return
    if args.command=='export-findings': emit(export_findings(ledger,args.ids,args.output)); return
    with connect(db_path) as db:
        if args.command=='stats': emit(stats(db))
        elif args.command=='search':
            if not 1<=args.limit<=100 or args.offset<0: raise ValueError('limit 1..100; offset nonnegative')
            emit({'scope':'Private acquired text; results do not imply semantic review','results':search(db,args.query,args.limit,args.offset,args.channel,args.author)})
        elif args.command=='show':
            if not 0<=args.context<=20: raise ValueError('context 0..20')
            emit(show(db,args.id,args.context,load_events(events),args.raw))
        elif args.command=='evidence': emit(add_evidence(db,events,args.key,args.stage,args.receipt,args.note))

if __name__=='__main__':
    if hasattr(sys.stdout,'reconfigure'): sys.stdout.reconfigure(encoding='utf-8')
    try: main()
    except (ValueError,KeyError,OSError,sqlite3.Error) as exc:
        emit({'error':str(exc)}); sys.exit(2)
