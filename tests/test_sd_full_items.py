"""Actual reduced prefix plus explicitly synthetic later full-route controls."""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import test_sd_items_lock as items_tests
from test_sd_items_lock import menu_row
from test_sd_gci_profile import ready_rows
from test_sd_original_route import route_rows
from authored_sd_reference_plan import make_input_plan
from reference_versus_sequence_capture import raw_pad
from retail_input_plan import NEUTRAL_PAD, DISCONNECTED_PAD
from sd_original_menu_plan import gci_sd_prefix_packet, validate_packet
from sd_reference_diagnostic import GciRulesMenuReceiver, SdDiagnosticError, SCOPE, PCS


class FullItemsTests(unittest.TestCase):
    setUp=items_tests.ItemsLockTests.setUp
    def prefix(self):
        r=GciRulesMenuReceiver(make_input_plan(5),self.profile,full_route=True,guarded_items=True)
        lead=ready_rows(self.profile)[:-1]
        lead[0]['payload'].update(menu_probe='sd_prefix',recipe_sha256=r.plan['authored_recipe_sha256'])
        for row in lead:r.accept(row)
        fixture=json.loads((Path(__file__).parent/'fixtures/sd-items-passing-order.json').read_text())
        for actual in fixture['rows']:
            row=deepcopy(actual);row['seq']=r.seq
            row['payload']['menu_consumed']-=fixture['initial_rules_ready_menu_consumed']-1
            r.accept(row)
        return r

    def pad(self,r,button=None):
        raw=b''.join(bytes.fromhex(p)+b'\0' for p in
            (raw_pad(buttons=[button]) if button else NEUTRAL_PAD,NEUTRAL_PAD,
             DISCONNECTED_PAD,DISCONNECTED_PAD))
        r.accept({'seq':r.seq,'event':'progress','source_tick':0,'payload':{
            'diagnostic':SCOPE,'name':'menu_input','pc':PCS['menu_input'],'consumed':0,
            'menu_consumed':r.menu_consumed+1,'slices':[{
                'tag':3,'flags':0,'address':0x80001000,'hex':raw.hex()}]}})

    def committed(self):
        r=self.prefix()
        for value in (2,1,0):
            self.pad(r,'D_RIGHT')
            r.accept(menu_row(r.seq,row=31,value=value,count=r.menu_consumed))
            # A copied held bank can outlive the observed decrement.
            self.pad(r,'D_RIGHT')
            self.assertEqual(r.items_rights,3-value)
            self.pad(r)
        self.pad(r,'B')
        row=menu_row(r.seq,count=r.menu_consumed)
        fields=row['payload']['slices'];fields.pop()
        flow=next(s for s in fields if s['tag']==45);raw=bytearray.fromhex(flow['hex'])
        raw[0]=13;raw[3]=5;raw[4]=0;raw[17]=0;flow['hex']=raw.hex()
        r.accept(row)
        self.pad(r,'B')  # Queued commit bank after the Rules owner appears.
        self.pad(r)
        return r

    def test_menu7_keeps_exact_bytes_order_and_historical_packets(self):
        old,new=gci_sd_prefix_packet(),gci_sd_prefix_packet(7)
        validate_packet(old);validate_packet(new)
        self.assertEqual([(a['label'],a['p1'],a['p2']) for a in old['actions']],
                         [(a['label'],a['p1'],a['p2']) for a in new['actions']])
        self.assertEqual(old['css'],new['css']);self.assertEqual(old['sss'],new['sss'])
        self.assertNotIn('items_locked',old['actions'][-2]['before'])
        self.assertEqual(new['actions'][-2]['before']['items_locked'],0)

    def test_actual_passing_prefix_and_synthetic_commit(self):
        r=self.prefix()
        self.assertEqual(r.items_frequency,3);self.assertTrue(r.items_up_seen)
        self.assertFalse(r.items_committed)
        r=self.committed()
        self.assertTrue(r.items_committed);self.assertEqual(r.items_rights,3)

    def test_full_items_negative_progression(self):
        for mutation in ('wrongvalue','missinglock','prematureB','secondUp','newA','earlyexit','pendingRight'):
            r=self.prefix()
            with self.subTest(mutation=mutation),self.assertRaises(SdDiagnosticError):
                if mutation=='prematureB':self.pad(r,'B')
                elif mutation=='secondUp':self.pad(r,'D_UP')
                elif mutation=='newA':self.pad(r,'A')
                elif mutation=='pendingRight':
                    self.pad(r,'D_RIGHT');self.pad(r);self.pad(r,'D_RIGHT')
                else:
                    row=menu_row(r.seq,row=31,value=2,count=r.menu_consumed)
                    if mutation=='missinglock':row['payload']['slices'].pop()
                    if mutation=='earlyexit':
                        row['payload']['slices'].pop()
                        flow=next(s for s in row['payload']['slices'] if s['tag']==45)
                        raw=bytearray.fromhex(flow['hex']);raw[0]=13;flow['hex']=raw.hex()
                    r.accept(row)

    def test_held_right_cannot_supply_the_next_decrement(self):
        r=self.prefix()
        self.pad(r,'D_RIGHT')
        r.accept(menu_row(r.seq,row=31,value=2,count=r.menu_consumed))
        self.pad(r,'D_RIGHT')
        self.assertEqual(r.items_rights,1)
        self.assertFalse(r.items_right_pending)
        with self.assertRaisesRegex(SdDiagnosticError,'without declared Right'):
            r.accept(menu_row(r.seq,row=31,value=1,count=r.menu_consumed))

    def test_synthetic_later_css_tie_sd_stays_strict(self):
        r=self.committed()
        # Existing synthetic route control supplies CSS/FD, neutral tick/clock,
        # exact normalized VS/SD payloads. None of this is actual native trace.
        rows=route_rows(self.profile)[6:]
        for row in rows:
            row=deepcopy(row);row['seq']=r.seq
            if row['event']=='progress' and row['payload'].get('name')=='menu':
                row['payload']['menu_consumed']=r.menu_consumed
            r.accept(row)
            if row['payload'].get('name')=='menu' and any(s['tag']==55 for s in row['payload']['slices']):
                self.confirm(r,row)
        self.assertTrue(r.ended)

    def confirm(self,r,stage_row):
        # Explicit synthetic accepted-selection transition and complete countdown.
        self.pad(r,'A')
        for value in range(30,-1,-1):
            row=deepcopy(stage_row);row['seq']=r.seq;row['source_tick']=31-value
            row['payload']['menu_consumed']=r.menu_consumed
            row['payload']['slices'].append({'tag':17,'flags':0,'address':0x80479d30,'hex':'020201010000'})
            next(s for s in row['payload']['slices'] if s['tag']==55)['hex']=value.to_bytes(4,'big').hex()
            r.accept(row)
            if value==30:self.pad(r)

    def test_later_vs_requires_commit_and_exact_normalized_payload(self):
        for mutation in ('missingcommit','wrongrumble'):
            r=self.prefix() if mutation=='missingcommit' else self.committed()
            rows=route_rows(self.profile)[6:]
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):
                for row in rows:
                    row=deepcopy(row);row['seq']=r.seq
                    if row['event']=='progress' and row['payload'].get('name')=='menu':
                        row['payload']['menu_consumed']=r.menu_consumed
                    if row['payload'].get('name')=='vs_entry' and mutation=='wrongrumble':
                        field=next(s for s in row['payload']['slices'] if s['tag']==4 and s['flags']==0)
                        raw=bytearray.fromhex(field['hex']);raw[0x6c]^=0x80;field['hex']=raw.hex()
                    r.accept(row)
                    if row['payload'].get('name')=='menu' and any(s['tag']==55 for s in row['payload']['slices']):
                        self.confirm(r,row)
