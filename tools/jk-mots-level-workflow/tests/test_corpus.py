"""Meaningful private-index invariants, using synthetic evidence only."""
import importlib.util,json,pathlib,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('corpus',ROOT/'scripts/corpus.py'); c=importlib.util.module_from_spec(spec); spec.loader.exec_module(c)

class CorpusTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.home=pathlib.Path(self.tmp.name)
        self.source=self.home/'messages.json'
        synthetic_id=(1577836800000-1420070400000)<<22
        self.rows=[{'id':str(synthetic_id),'content':'RGB surface storage retained','group':'editing','datetime':'wrong-ui-date'},
                   {'id':str(synthetic_id+(1000<<22)),'content':'Baked light reply','group':'editing'},
                   {'id':str(synthetic_id+(2000<<22)),'content':'Unrelated room','group':'elsewhere'}]
        self.source.write_text(json.dumps({'items':self.rows}),encoding='utf-8')
        self.manifest=self.home/'manifest.json';self.dbpath=self.home/'corpus.sqlite'
        self.write_manifest([self.source]);c.build(self.manifest,self.dbpath)
    def write_manifest(self,paths):
        self.manifest.write_text(json.dumps({'schemaVersion':1,'sources':[{'path':str(p),'kind':'discord_json','sha256':c.sha(p.read_bytes()),'privacy':'private'} for p in paths]}),encoding='utf-8')
    def test_search_and_exact_payload(self):
        with c.connect(self.dbpath) as db:
            found=c.search(db,'RGB storage',10,0)
            self.assertEqual(len(found),1)
            result=c.show(db,found[0]['id'],1,[],True)[0]
            self.assertEqual(result['exactSourcePayload'],self.rows[0])
            self.assertEqual(result['neighborIds'],[self.rows[1]['id']])
            self.assertEqual(result['reported_time'],'wrong-ui-date')
            self.assertTrue(result['snowflake_time'].startswith('2020-01-01'))
    def test_acquisition_does_not_imply_review(self):
        with c.connect(self.dbpath) as db:
            row=c.show(db,self.rows[0]['id'],0,[])[0]
            self.assertTrue(row['evidenceStates']['acquired'])
            self.assertEqual(row['evidenceStates']['reviewed'],[])
    def test_changed_source_refuses_exact_read_and_keeps_old_index(self):
        old=self.dbpath.read_bytes();self.source.write_text('{}',encoding='utf-8')
        with self.assertRaisesRegex(ValueError,'Source changed'):c.build(self.manifest,self.dbpath)
        self.assertEqual(self.dbpath.read_bytes(),old)
        with c.connect(self.dbpath) as db,self.assertRaisesRegex(ValueError,'source changed'):
            c.show(db,self.rows[0]['id'],0,[],True)
    def test_duplicate_occurrences_and_edited_variants(self):
        other=self.home/'other.json';other.write_text(json.dumps({'items':[self.rows[0],dict(self.rows[1],content='Changed reply')]}),encoding='utf-8')
        self.write_manifest([self.source,other]);c.build(self.manifest,self.dbpath)
        with c.connect(self.dbpath) as db:
            self.assertEqual(c.stats(db)['occurrences'],5)
            self.assertEqual(c.stats(db)['distinctIds'],3)
            self.assertEqual(c.stats(db)['recordsIncludingVariants'],4)
            self.assertEqual(len(c.show(db,self.rows[1]['id'],0,[])),2)
    def test_evidence_requires_exact_variant_and_receipt(self):
        receipt=self.home/'review.txt';receipt.write_text('Read the main body',encoding='utf-8');events=self.home/'events.json'
        with c.connect(self.dbpath) as db:
            with self.assertRaisesRegex(ValueError,'exact record key'):c.add_evidence(db,events,self.rows[0]['id'],'reviewed',receipt,'read')
            key=c.get_records(db,self.rows[0]['id'])[0]['key'];e=c.add_evidence(db,events,key,'reviewed',receipt,'read')
            self.assertEqual(e['receiptSha256'],c.sha(receipt.read_bytes()))
            self.assertEqual(c.state_for(key,c.load_events(events))['runtime_verified'],[])
    def test_private_payload_excluded_from_export(self):
        lead={'id':'finding','raw':'PRIVATE_SENTINEL','source_message_ids':[self.rows[0]['id']], 'shareable':{'originalAnalysis':True,'reviewedForSharing':True,'title':'Storage contract','finding':'Check allocated channels.','qualification':'Source only.','nextAction':'Run fixture.','publicSources':['https://example.org/source']}}
        ledger=self.home/'leads.json';ledger.write_text(json.dumps({'leads':[lead]}),encoding='utf-8');output=self.home/'packet.json'
        c.export_findings(ledger,['finding'],output)
        text=output.read_text();self.assertNotIn('PRIVATE_SENTINEL',text);self.assertNotIn(self.rows[0]['id'],text)
        with self.assertRaises(FileExistsError):c.export_findings(ledger,['finding'],output)
    def test_unreviewed_or_private_locator_export_refused(self):
        ledger=self.home/'leads.json';lead={'id':'x','shareable':None};ledger.write_text(json.dumps({'leads':[lead]}))
        with self.assertRaises(ValueError):c.export_findings(ledger,['x'],self.home/'packet.json')
        lead['shareable']={'originalAnalysis':True,'reviewedForSharing':True,'title':'X','finding':self.rows[0]['id'],'qualification':'Q','nextAction':'N','publicSources':[]};ledger.write_text(json.dumps({'leads':[lead]}))
        with self.assertRaisesRegex(ValueError,'Private locator'):c.export_findings(ledger,['x'],self.home/'packet.json')
    def test_bad_fts_input_is_not_an_operator(self):
        with c.connect(self.dbpath) as db:
            self.assertEqual(c.search(db,'RGB OR " injection',10,0),[])
    def test_jsonl_exact_line_and_html_page_adapter(self):
        raw=(json.dumps(self.rows[0])+'\n\n'+json.dumps(self.rows[1])+'\n').encode()
        parsed=list(c.source_rows({'kind':'discord_jsonl'},raw));self.assertEqual(parsed[1][1],{'line':3})
        doc=list(c.source_rows({'kind':'html','path':str(self.home/'page.html')},b'<script>hidden()</script><p>Visible &amp; useful</p>'))[0][0]
        self.assertNotIn('hidden()',doc['content']);self.assertIn('Visible & useful',doc['content'])

    def test_unknown_message_shape_refused_without_partial_index(self):
        with self.assertRaisesRegex(ValueError,'Unsupported message row'):
            list(c.source_rows({'kind':'discord_json'},json.dumps({'items':[{'text':'UI only'}]}).encode()))

    def test_lead_update_retains_history_without_promotion(self):
        ledger=self.home/'leads.json';finding=self.home/'finding.json'
        old={'id':'rgb','capability':'Readiness','disposition':'Proposed','next_acceptance_case':'Source check','shareable':None}
        ledger.write_text(json.dumps({'leads':[old],'revision':1}))
        changed=dict(old,disposition='Source inspected');finding.write_text(json.dumps(changed))
        c.update_lead(ledger,finding);saved=c.read_json(ledger)
        self.assertEqual(saved['revision'],2);self.assertEqual(saved['history'][0]['previous'],old)
        self.assertIsNone(saved['leads'][0]['shareable'])

if __name__=='__main__':unittest.main()
