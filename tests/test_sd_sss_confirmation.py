"""Actual FD/A/countdown prefix; completion to zero is a separate synthetic control."""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import test_sd_items_lock as items_tests
from authored_sd_reference_plan import make_input_plan
from retail_input_plan import NEUTRAL_PAD, DISCONNECTED_PAD
from reference_versus_sequence_capture import raw_pad
from sd_reference_diagnostic import GciRulesMenuReceiver, SdDiagnosticError, slices, stage_state


def fixture():
    return json.loads((Path(__file__).parent/'fixtures/sd-sss-confirmation-order.json').read_text())


class SssConfirmationTests(unittest.TestCase):
    setUp=items_tests.ItemsLockTests.setUp

    def receiver(self):
        # Scoped reducer starts after unrelated readiness/Items phases. This is
        # an injected unit context, not a native receiver lifecycle claim.
        r=GciRulesMenuReceiver(make_input_plan(5),self.profile,full_route=True,guarded_items=True)
        r.started=r.ready=r.scene_owner_seen=True
        r.seq=1534;r.menu_consumed=463;r.latest_menu={'scene':9}
        r.last_pad=[NEUTRAL_PAD]*2+[DISCONNECTED_PAD]*2
        return r

    def through_a(self):
        r=self.receiver()
        for row in fixture()['rows'][:4]:r.accept(deepcopy(row))
        return r

    def retirement(self):
        rows=json.loads((Path(__file__).parent/'fixtures/sd-sss-retirement-order.json').read_text())['rows']
        r=self.receiver()
        # Actual three rows, injected earlier countdown/route context only.
        r.seq=1687;r.menu_consumed=538;r.sss_countdown=0;r.sss_countdown_tick=238
        r.sss_confirmation={'source_tick':149,'stage':{'index':25,'kind':32,'cooldown':0}}
        r.sss_confirmation_neutral=True;r.stage=r.final_stage=r.sss_confirmation['stage'].copy()
        return r,deepcopy(rows)

    def test_actual_retirement_is_observation_not_countdown_progress(self):
        r,rows=self.retirement()
        for row in rows:r.accept(row)
        self.assertEqual(r.sss_countdown_inventory,[{'seq':1688,'source_tick':239,'cooldown':0}])
        self.assertEqual(r.sss_retirement_inventory[0]['seq'],1689)
        self.assertEqual(r.sss_retirement['routing'],'020201020100')
        self.assertEqual(r.sss_countdown_tick,239)
        self.assertFalse(r.ended)

    def test_retirement_requires_frozen_completed_own_state(self):
        for mutation in ('active_duplicate','tick','ticktype','count','cooldown','index','owner','route','address','missing','early','release'):
            r,rows=self.retirement()
            for row in rows[:2]:r.accept(row)
            row=rows[2]
            field=lambda tag:next(s for s in row['payload']['slices'] if s['tag']==tag)
            if mutation=='active_duplicate':field(17)['hex']='020201010000'
            if mutation=='tick':row['source_tick']+=1
            if mutation=='ticktype':row['source_tick']=239.0
            if mutation=='count':row['payload']['menu_consumed']+=1
            if mutation=='cooldown':field(55)['hex']='00000001'
            if mutation=='index':field(41)['hex']='18';field(42)['address']-=0x1c
            if mutation=='owner':field(40)['hex']='08'
            if mutation=='route':field(17)['hex']='020201020000'
            if mutation=='address':field(17)['address']+=1
            if mutation=='missing':row['payload']['slices'].remove(field(17))
            if mutation=='early':r.sss_countdown=1
            if mutation=='release':r.sss_confirmation_neutral=False
            with self.subTest(mutation=mutation),self.assertRaises(SdDiagnosticError):r.accept(row)

    def test_retirement_rejects_new_input_or_return_to_active(self):
        for mutation in ('input','active'):
            r,rows=self.retirement()
            for row in rows:r.accept(row)
            row=deepcopy(rows[0 if mutation=='input' else 1]);row['seq']=r.seq
            if mutation=='input':row['payload']['menu_consumed']+=1
            with self.subTest(mutation=mutation),self.assertRaises(SdDiagnosticError):r.accept(row)

    def test_actual_constructor_and_confirmation_prefix(self):
        first=fixture()['initial_constructor_row']
        self.assertEqual(stage_state(slices(first['payload']),first['payload'])['cooldown'],19)
        r=self.receiver()
        for row in fixture()['rows']:r.accept(deepcopy(row))
        self.assertEqual(r.sss_confirmation['seq'],1537)
        self.assertEqual(r.sss_confirmation['stage'],{'index':25,'kind':32,'cooldown':0})
        self.assertEqual([x['cooldown'] for x in r.sss_countdown_inventory],list(range(30,17,-1)))
        self.assertTrue(r.sss_confirmation_neutral)
        self.assertEqual(r.final_stage,{'index':25,'kind':32,'cooldown':0})
        self.assertFalse(r.ended)  # Actual prefix never reached zero or VS/SD.

    def test_unarmed_constructor_rejects_all_selection_only_values(self):
        for value in range(21,31):
            row=deepcopy(fixture()['rows'][0])
            next(s for s in row['payload']['slices'] if s['tag']==55)['hex']=value.to_bytes(4,'big').hex()
            with self.subTest(value=value),self.assertRaises(SdDiagnosticError):self.receiver().accept(row)

    def test_a_requires_fd_zero_and_preceding_neutral(self):
        for mutation in ('foreignkind','nonzero','heldA','unobservedA'):
            r=self.receiver();rows=deepcopy(fixture()['rows'][:5])
            if mutation in ('foreignkind','nonzero'):
                for row in (rows[0],rows[2]):
                    field=next(s for s in row['payload']['slices'] if s['tag']==(42 if mutation=='foreignkind' else 55))
                    field['hex']='21' if mutation=='foreignkind' else '00000001'
            if mutation=='unobservedA':
                field=rows[3]['payload']['slices'][0];raw=bytearray.fromhex(field['hex']);raw[:2]=b'\0\0';field['hex']=raw.hex()
            with self.subTest(mutation=mutation),self.assertRaises(SdDiagnosticError):
                for i,row in enumerate(rows):
                    if mutation=='heldA' and i==3:r.last_pad[:2]=[raw_pad(buttons=['A']),NEUTRAL_PAD]
                    r.accept(row)

    def test_countdown_first_bound_gaps_repeat_and_foreign_owner_fail(self):
        for mutation in ('first29','over30','gap','duplicate_tick','repeat','jump','foreignindex','missing'):
            r=self.through_a();row=deepcopy(fixture()['rows'][4])
            if mutation in ('gap','duplicate_tick','repeat','jump'):
                r.accept(row);r.accept(deepcopy(fixture()['rows'][5]));row=deepcopy(fixture()['rows'][6])
            if mutation=='gap':row['source_tick']+=1
            if mutation=='duplicate_tick':row['source_tick']-=1
            if mutation in ('first29','over30','repeat','jump'):
                value={'first29':29,'over30':31,'repeat':30,'jump':28}[mutation]
                next(s for s in row['payload']['slices'] if s['tag']==55)['hex']=value.to_bytes(4,'big').hex()
            if mutation=='foreignindex':
                next(s for s in row['payload']['slices'] if s['tag']==41)['hex']='18'
                next(s for s in row['payload']['slices'] if s['tag']==42)['address']=0x803f06d0+24*0x1c+0xb
            if mutation=='missing':row['payload']['slices']=[s for s in row['payload']['slices'] if s['tag']!=55]
            with self.subTest(mutation=mutation),self.assertRaises(SdDiagnosticError):r.accept(row)

    def test_exact_held_a_may_drain_but_no_new_a_after_neutral(self):
        r=self.through_a();r.accept(deepcopy(fixture()['rows'][4]))
        held=deepcopy(fixture()['rows'][3]);held['seq']=r.seq;held['source_tick']=152;held['payload']['menu_consumed']=466
        r.accept(held)
        self.assertEqual(r.sss_confirmation['seq'],1537)
        neutral=deepcopy(fixture()['rows'][5]);neutral['seq']=r.seq;neutral['payload']['menu_consumed']=467;r.accept(neutral)
        held['seq']=r.seq;held['payload']['menu_consumed']=468
        with self.assertRaises(SdDiagnosticError):r.accept(held)
