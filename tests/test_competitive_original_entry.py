"""Asset-free scoped controls; synthetic competitive continuation is not capture evidence."""
from copy import deepcopy
import json
from pathlib import Path
import struct
import unittest

from test_sd_items_lock import ItemsLockTests, menu_row
from test_sd_gci_profile import ready_rows
from test_sd_original_route import route_rows
from test_sd_full_items import FullItemsTests
from authored_sd_reference_plan import make_input_plan, recipe
from sd_original_menu_plan import gci_competitive_entry_packet, validate_packet
from sd_reference_diagnostic import GciRulesMenuReceiver, Receiver, CompetitiveItemsProgress, SdDiagnosticError
from retail_input_plan import NEUTRAL_PAD, verify_entry, load_plan
from retail_setup_validation import _decode_setup
from reference_versus_sequence_capture import raw_pad


class CompetitiveOriginalEntryTests(unittest.TestCase):
    setUp = ItemsLockTests.setUp
    pad = FullItemsTests.pad

    def initial(self):
        r=GciRulesMenuReceiver(make_input_plan(6), self.profile, full_route=True,
                              guarded_items=True, competitive_entry=True)
        rows=ready_rows(self.profile)[:-1]
        rows[0]['payload'].update(menu_probe='competitive_entry',recipe_sha256=r.plan['authored_recipe_sha256'])
        for row in rows:r.accept(row)
        f=json.loads((Path(__file__).parent/'fixtures/sd-items-held-entry-order.json').read_text())
        # Retained original locked-entry PAD/menu bytes are exact; portable
        # readiness/count rebasing is reconstructed. No eight-minute state is
        # asserted by these old menu rows. Later lock-clear is synthetic below.
        for actual in f['rows']:
            row=deepcopy(actual);row['seq']=r.seq
            row['payload']['menu_consumed']-=f['initial_rules_ready_menu_consumed']-1
            r.accept(row)
        self.assertTrue(r.items_entry_drain_closed)
        self.assertEqual(r.latest_menu['items_locked'],1)
        return r

    def committed(self):
        r=self.initial()
        packet=gci_competitive_entry_packet()
        r.accept(menu_row(r.seq,count=r.menu_consumed))  # synthetic lock-clear
        actions=packet['actions']; first=next(i for i,a in enumerate(actions) if a['label']=='item-0-off')
        for a in actions[first:]:
            if a['label']=='Rules-start-CSS':break
            self.pad(r, next(b for b in ('A','B','D_RIGHT','D_DOWN','D_UP')
                             if a['p1']==raw_pad(buttons=[b])))
            # A held copy is legal even after its observed value/row changed;
            # it never grants another transition without neutral.
            after=a['after']
            row=menu_row(r.seq,row=after['row'],value=after.get('value',1),count=r.menu_consumed)
            if after['kind']==13:
                row['payload']['slices'].pop()
                field=next(s for s in row['payload']['slices'] if s['tag']==45)
                data=bytearray.fromhex(field['hex']);data[0]=13;data[4]=0;data[17]=0;field['hex']=data.hex()
                field=next(s for s in row['payload']['slices'] if s['tag']==46)
                field['hex']='0005'+'00'*6
            r.accept(row)
            self.pad(r, next(b for b in ('A','B','D_RIGHT','D_DOWN','D_UP')
                             if a['p1']==raw_pad(buttons=[b])))
            self.pad(r)
            if after['kind']==13:
                # Source-derived return lifecycle, explicitly synthetic here.
                for tick in range(1,6):
                    returned=deepcopy(row);returned['seq']=r.seq;returned['source_tick']=tick
                    returned['payload']['menu_consumed']=r.menu_consumed
                    next(s for s in returned['payload']['slices'] if s['tag']==46)['hex']=(5-tick).to_bytes(2,'big').hex()+'00'*6
                    r.accept(returned)
        self.assertTrue(r.items_committed)
        self.assertEqual(r.competitive_items.off,set(range(31)))
        self.assertEqual(r.competitive_items.frequency_rights,3)
        return r

    def entry_rows(self,r, mutation=None, *, setup_samples=1):
        # Existing synthetic CSS/SSS control, then explicitly authored competitive
        # setup. These are receiver controls, not original eight-minute evidence.
        rows=deepcopy(route_rows(self.profile)[6:])
        for row in rows:
            name=row['payload'].get('name')
            if name=='input' and setup_samples == 0:
                continue
            if name=='vs_entry':
                for s in row['payload']['slices']:
                    if s['tag']==4:
                        b=bytearray.fromhex(s['hex']);b[1]|=1;b[2]|=8
                        b[0x10:0x14]=(480).to_bytes(4,'big');b[0x20:0x28]=bytes.fromhex('fffffff80000000f')
                        s['hex']=b.hex()
                rules=bytearray(24)
                for offset,value in ((2,1),(4,4),(6,10),(8,8),(9,1)):rules[offset]=value
                save=bytearray(self.profile['save']+b''.join(self.profile['banks'][:2]))
                save[0x448]=0xff;save[0x450:0x458]=bytes.fromhex('0000000010000000')
                addr=r.loaded_context['save_address']
                row['payload']['slices'] += [dict(tag=38,flags=0,address=addr-0x18,hex=rules.hex()),
                    dict(tag=39,flags=0,address=addr,hex=save.hex())]
                self.entry_payload=row['payload']
                if mutation is not None:mutation(row)
            if name=='vs_setup':
                row['payload']['consumed']=setup_samples
                normal=next(s['hex'] for s in self.entry_payload['slices'] if s['tag']==4 and s['flags']==0)
                b=bytearray.fromhex(normal)
                row['payload']['slices']=[dict(tag=4,flags=0,address=0x80001000,hex=b.hex())]
                for slot in range(2):
                    head=bytearray(256);head[12]=slot
                    row['payload']['slices'] += [dict(tag=t,flags=slot,address=0x80001000,hex=data.hex())
                        for t,data in ((5,head),(7,struct.pack('>f',0)),(8,b'\4'))]
            if name=='tick':break
            row['seq']=r.seq
            if name=='menu':row['payload']['menu_consumed']=r.menu_consumed
            r.accept(row)
            if name=='vs_setup':break
            if name=='menu' and any(s['tag']==55 for s in row['payload']['slices']):
                FullItemsTests.confirm(self,r,row)
        r.accept(dict(seq=r.seq,event='end',source_tick=0,payload=dict(status='interrupted',natural=False)))

    def test_actual_entry_and_synthetic_complete_profile_prefix(self):
        r=self.committed();self.entry_rows(r)
        self.assertTrue(r.ended);self.assertEqual(r.phase_order,('vs_entry','vs_setup'))
        self.assertEqual(r.tick_count,0)

    def test_actual_zero_setup_consumption_does_not_invent_a_gameplay_sample(self):
        # Existing original V6 seq1740/1741 both consumed0/menu_consumed540
        # and tick240. Competitive setup bytes remain synthetic here; the
        # source-derived zero-input setup lifetime is the actual control.
        r=self.committed();self.entry_rows(r,setup_samples=0)
        self.assertTrue(r.ended);self.assertEqual(r.consumed,0)
        self.assertGreater(r.menu_consumed,0)
        r.ended=False;r.menu_consumed=0
        with self.assertRaisesRegex(SdDiagnosticError,'source input consumption'):
            r.accept(dict(seq=r.seq,event='end',source_tick=0,
                          payload=dict(status='interrupted',natural=False)))

    def test_explicit_scope_recipe_menu_and_legacy_rejection(self):
        p=make_input_plan(6);m=gci_competitive_entry_packet();validate_packet(m)
        self.assertEqual(m['authored_recipe_sha256'],p['authored_recipe_sha256'])
        self.assertEqual(m['items']['rows'],list(range(16))+list(range(30,15,-1)))
        self.assertEqual(set(m['items']['rows']),set(range(31)))
        self.assertEqual(recipe(5)['expected_setup']['time_limit_seconds'],60)
        with self.assertRaises(SdDiagnosticError):Receiver(p)
        with self.assertRaises(SdDiagnosticError):GciRulesMenuReceiver(p,self.profile,full_route=True,guarded_items=True)
        with self.assertRaises(SdDiagnosticError):GciRulesMenuReceiver(make_input_plan(5),self.profile,full_route=True,guarded_items=True,competitive_entry=True)
        wrong=deepcopy(m);wrong['actions'][-1]['p1']=NEUTRAL_PAD
        with self.assertRaises(ValueError):validate_packet(wrong)

    def test_switch_pending_direction_value_and_neutral_guards(self):
        for change in ('skip','up','locked','value','newA','duplicateTransition','earlyB'):
            p=CompetitiveItemsProgress();state=dict(scene=1,kind=16,row=0,value=1,entering=1,cooldown=0,items_locked=0)
            p.observe(state,1);neutral=[NEUTRAL_PAD]*2;A=[raw_pad(buttons=['A']),NEUTRAL_PAD]
            with self.subTest(change=change),self.assertRaises(SdDiagnosticError):
                if change=='skip':p.observe(dict(state,row=1),2)
                elif change=='up':p.input(state,[raw_pad(buttons=['D_UP']),NEUTRAL_PAD],neutral)
                elif change=='locked':p.input(dict(state,items_locked=1),A,neutral)
                elif change=='earlyB':p.input(state,[raw_pad(buttons=['B']),NEUTRAL_PAD],neutral)
                else:
                    p.input(state,A,neutral)
                    if change=='value':p.observe(dict(state,value=2),2)
                    else:
                        state['value']=0;p.observe(state,2)
                        if change=='newA':p.input(state,A,neutral)
                        else:p.observe(dict(state,row=1),3)

    def test_actual_committed_return_countdown_and_held_back_release(self):
        rows=json.loads((Path(__file__).parent/'fixtures/competitive-items-return-order.json').read_text())['rows']
        p=CompetitiveItemsProgress()
        # Earlier traversal is covered separately. This bounded actual fixture
        # starts with the declared, verified all-off/None predecessor context.
        p.current=(32,0);p.off=set(range(31));p.frequency_rights=3
        from sd_reference_diagnostic import menu_state, slices
        previous=[NEUTRAL_PAD]*2
        state=menu_state(slices(rows[0]['payload']))
        for row in rows[1:]:
            payload=row['payload']
            if payload['name']=='menu_input':
                raw=bytes.fromhex(next(s['hex'] for s in payload['slices'] if s['tag']==3))
                pad=[raw[n:n+11].hex() for n in (0,12)]
                if p.return_state is None:p.input(dict(state,items_locked=0),pad,previous)
                else:p.return_input(pad,previous,row['source_tick'])
                previous=pad
            else:
                state=menu_state(slices(payload))
                if p.return_state is None:p.commit(state,row['source_tick'],row['seq'])
                else:p.observe_return(state,row['source_tick'],row['seq'])
                if state['cooldown']==0:break
        self.assertIsNone(p.return_state)
        self.assertEqual([v['cooldown'] for v in p.return_inventory],[5,4,3,2,1,0])

    def test_return_initial_countdown_owner_and_input_negatives(self):
        neutral=[NEUTRAL_PAD]*2;back=[raw_pad(buttons=['B']),NEUTRAL_PAD]
        first=dict(scene=1,kind=13,row=5,value=0,entering=0,cooldown=5)
        def prepared():
            p=CompetitiveItemsProgress();p.current=(32,0);p.off=set(range(31));p.frequency_rights=3
            p.commit_seen=True;p.commit(first,332,1127);return p
        for mutation in ('initial0','initial4','initial6','skip','duplicate','foreignowner','foreigninput','newB','postneutralB','inputtick','missingneutral'):
            with self.subTest(mutation=mutation),self.assertRaises(SdDiagnosticError):
                p=prepared()
                if mutation.startswith('initial'):
                    p.return_state=None;p.commit(dict(first,cooldown=int(mutation[7:])),332,1127)
                elif mutation=='skip':p.observe_return(dict(first,cooldown=3),334,1129)
                elif mutation=='duplicate':p.observe_return(first,332,1129)
                elif mutation=='foreignowner':p.observe_return(dict(first,row=4,cooldown=4),333,1129)
                elif mutation=='foreigninput':p.return_input([raw_pad(buttons=['A']),NEUTRAL_PAD],back,332)
                elif mutation=='newB':p.return_input(back,neutral,332)
                elif mutation=='postneutralB':
                    p.return_input(neutral,back,332);p.return_input(back,neutral,332)
                elif mutation=='inputtick':p.return_input(neutral,back,333)
                else:
                    for tick in range(333,338):p.observe_return(dict(first,cooldown=337-tick),tick,1127+tick-332)

    def test_friendly_fire_optin_keeps_default_decoder_strict(self):
        r=self.committed();self.entry_rows(r)
        raw=r.records['vs_entry'][(4,0)].hex()
        verify_entry(r.plan,raw)
        with self.assertRaises(ValueError):_decode_setup(raw)
        for offset,mask in ((0,0x20),(1,1),(2,8),(0x10,1),(0x20,1),(0x30,1),(0x6c,0x80)):
            b=bytearray.fromhex(raw);b[offset]^=mask
            with self.subTest(offset=offset),self.assertRaises(ValueError):verify_entry(r.plan,b.hex())

    def test_complete_committed_source_context_is_required(self):
        for change in ('rules','preferences','missingprefs','wrongaddress','unrelatedsetup'):
            r=self.committed()
            def mutate(row):
                fields=row['payload']['slices']
                tag=38 if change=='rules' else 4 if change=='unrelatedsetup' else 39
                s=next(s for s in fields if s['tag']==tag and s['flags']==0)
                if change=='missingprefs':fields.remove(s)
                elif change=='wrongaddress':s['address']+=4
                else:
                    b=bytearray.fromhex(s['hex'])
                    offset=5 if change=='rules' else 0x460 if change=='preferences' else 0x6d
                    b[offset]^=1;s['hex']=b.hex()
            with self.subTest(change=change),self.assertRaises(ValueError):self.entry_rows(r,mutate)

    def test_prefix_cannot_admit_gameplay_legacy_completion_or_unfinished_native_stream(self):
        from unittest import mock
        for boundary in ('tick','legacy','native'):
            r=self.committed();self.entry_rows(r)
            if boundary=='native':
                with mock.patch('sd_reference_diagnostic.read_status',return_value={
                        'state':'interrupted','completed':False,'invalid':False,'error':None}), \
                     mock.patch('sd_reference_diagnostic.validate_stream',side_effect=ValueError('missing native footer')):
                    with self.assertRaises(ValueError):r.finish('unused','unused','unused')
                continue
            r.ended=False
            row=dict(seq=r.seq,event='end',source_tick=0,payload=dict(status='completed',natural=True))
            if boundary=='tick':
                from sd_reference_diagnostic import SCOPE,PCS
                row.update(event='progress',payload=dict(diagnostic=SCOPE,name='tick',pc=PCS['tick'],
                            consumed=r.consumed,slices=[]))
            with self.assertRaises(SdDiagnosticError):r.accept(row)
