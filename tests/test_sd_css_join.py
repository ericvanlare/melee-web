"""Actual initial NA inventory; later source autojoin remains synthetic."""
from copy import deepcopy
import json
import hashlib
from pathlib import Path
from types import SimpleNamespace
import unittest

from test_sd_reference_diagnostic import index  # Shared tools/scripts import paths.
from capture_sd_reference_prefix import drive_authored_css_sss, require_css_join_owner
from sd_original_menu_plan import gci_sd_prefix_packet, gci_competitive_entry_packet
from sd_reference_diagnostic import css_state, slices, SdDiagnosticError
from retail_input_plan import NEUTRAL_PAD


def actual_css_row(filename='sd-css-initial-na-order.json'):
    fixture=json.loads((Path(__file__).parent/'fixtures'/filename).read_text())
    row=deepcopy(fixture['rows'][0])
    owner=next(s for s in row['payload']['slices'] if s['tag']==48 and s['flags']==0)
    css=owner.pop('css_data')
    prefix=bytes.fromhex(css['prefix_hex'])
    players=css['players']
    if len(prefix)!=0x70 or [p['index'] for p in players]!=list(range(6)):
        raise ValueError('Actual CSSData fixture structure differs')
    data=[bytes.fromhex(p['player_init_hex']) for p in players]
    if any(len(p)!=0x24 for p in data):
        raise ValueError('Actual PlayerInitData fixture extent differs')
    owner['hex']=(prefix+b''.join(data)).hex()
    digest=hashlib.sha256(json.dumps(row,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    if digest!=fixture['row_sha256']:
        raise ValueError('Actual CSS row bytes/source identity changed')
    return row


def actual_css():
    return css_state(slices(actual_css_row()['payload']))


class CssJoinTests(unittest.TestCase):
    def driver(self,version=7,css=None,join=True):
        """Portable driver ordering control, not a native observer/runner test."""
        r=SimpleNamespace(css=deepcopy(css if css is not None else actual_css()),
                          menu_polls=0,menu_consumed=0,last_pad=[NEUTRAL_PAD]*2,stage=None)
        trace=[]
        def wait(predicate,label,cap):
            # Explicit synthetic source progress; no real elapsed-time claim.
            for _ in range(cap):
                if predicate():return
                r.menu_polls+=1
            raise SdDiagnosticError('synthetic source cap: '+label)
        def set_both(p1,p2,*,action):
            trace.append(('intent',action))
            r.menu_consumed+=1;r.menu_polls+=1;r.last_pad=[p1,p2]
            if action in ('Mario-P1','Mario-P2'):
                port=int(action[-1])-1
                r.css['cursors'][port].update(x=-20.9,y=16.5)
                if join:
                    r.css['players'][port]['kind']=0;r.css['doors'][port]['kind']=0
                    r.css['cursors'][port].update(state=1,held=port)
                    r.css['models'][port]['owner']=port+1
                    trace.append(('synthetic-own-Human',port))
        def tap(action,label,cap):
            if label.startswith('Mario-place-'):
                port=int(label[-1])-1
                require_css_join_owner(r.css,port)
                trace.append(('placement',port))
                r.css['players'][port]['character']=8
                r.css['doors'][port].update(icon=1,costume=(1,0)[port])
                r.css['cursors'][port]['state']=2
            elif label=='CSS-start-SSS':r.stage={'kind':32,'cooldown':0}
            else:raise AssertionError('unexpected synthetic tap '+label)
        self.trace=trace
        packet=gci_competitive_entry_packet() if version==8 else gci_sd_prefix_packet(version)
        drive_authored_css_sss(r,packet,
                              SimpleNamespace(set_both=set_both),lambda:None,wait,tap)
        return r,trace

    def test_actual_initial_inventory_and_legacy_pre_move_failure(self):
        css=actual_css()
        self.assertEqual([p['kind'] for p in css['players']],[3,3])
        self.assertEqual([c['port'] for c in css['cursors']],[0,1])
        for port in (0,1):require_css_join_owner(css,port,initial=True)
        with self.assertRaisesRegex(SdDiagnosticError,'two original humans'):
            self.driver(version=5)
        self.assertEqual(self.trace,[])

    def test_actual_initial_then_synthetic_movement_join_before_placement(self):
        r,trace=self.driver()
        for port in (0,1):
            self.assertLess(trace.index(('intent','Mario-P'+str(port+1))),
                            trace.index(('synthetic-own-Human',port)))
            self.assertLess(trace.index(('synthetic-own-Human',port)),
                            trace.index(('placement',port)))
        self.assertEqual([p['kind'] for p in r.css['players']],[0,0])
        self.assertEqual([p['character'] for p in r.css['players']],[8,8])
        self.assertEqual([d['costume'] for d in r.css['doors']],[1,0])

    def test_actual_competitive_css_reuses_same_join_before_placement(self):
        row=actual_css_row('competitive-css-initial-na-order.json')
        self.assertEqual((row['seq'],row['source_tick'],row['payload']['menu_consumed']),(1299,1,360))
        css=css_state(slices(row['payload']))
        self.assertEqual(css,actual_css())
        self.assertEqual(gci_competitive_entry_packet()['css'],gci_sd_prefix_packet(7)['css'])
        # Only the initial inventory is actual. All later autojoin/movement
        # here is the portable synthetic source-ordering control.
        r,trace=self.driver(version=8,css=css)
        for port in (0,1):
            self.assertLess(trace.index(('intent','Mario-P'+str(port+1))),trace.index(('synthetic-own-Human',port)))
            self.assertLess(trace.index(('synthetic-own-Human',port)),trace.index(('placement',port)))
        self.assertEqual([p['kind'] for p in r.css['players']],[0,0])
        with self.assertRaisesRegex(SdDiagnosticError,'two original humans'):self.driver(version=5,css=css)
        self.assertEqual(self.trace,[])

    def test_competitive_join_keeps_foreign_owner_and_finite_readiness_guards(self):
        css=css_state(slices(actual_css_row('competitive-css-initial-na-order.json')['payload']))
        for group,key,value in (('players','kind',1),('players','slot',2),('doors','kind',0),
                                ('cursors','port',1),('cursors','state',3),('cursors','x',0),
                                ('models','owner',2)):
            invalid=deepcopy(css);invalid[group][0][key]=value
            with self.subTest(group=group,key=key),self.assertRaises(SdDiagnosticError):self.driver(version=8,css=invalid)
            self.assertEqual(self.trace,[])
        with self.assertRaisesRegex(SdDiagnosticError,'CSS own Human join'):self.driver(version=8,css=css,join=False)
        self.assertFalse(any(kind=='placement' for kind,_ in self.trace))

    def test_missing_join_hits_existing_source_cap_without_placement(self):
        with self.assertRaisesRegex(SdDiagnosticError,'CSS own Human join'):
            self.driver(join=False)
        self.assertFalse(any(kind=='placement' for kind,_ in self.trace))

    def test_cpu_foreign_and_unconstructed_initial_owners_reject(self):
        mutations=(('players','kind',1),('doors','kind',0),('players','slot',2),
                   ('cursors','port',1),('cursors','state',3),('cursors','x',0),
                   ('cursors','y',0),('models','owner',2),('doors','icon',1))
        for group,key,value in mutations:
            with self.subTest(group=group,key=key):
                css=actual_css();css[group][0][key]=value
                with self.assertRaises(SdDiagnosticError):self.driver(css=css)
                self.assertEqual(self.trace,[])
        css=actual_css();css['players'][0]['kind']=0;css['doors'][0]['kind']=0
        css['cursors'][0].update(state=1,held=1)
        with self.assertRaises(SdDiagnosticError):self.driver(css=css)
        css['cursors'][0]['held']=0;css['models'][0]['owner']=2
        with self.assertRaises(SdDiagnosticError):self.driver(css=css)

    def test_missing_cursor_or_model_extent_rejects_before_driver(self):
        for tag in (43,47,48):
            with self.subTest(tag=tag):
                row=actual_css_row()
                field=next(s for s in row['payload']['slices'] if s['tag']==tag and s['flags']==0)
                field['hex']=field['hex'][:-2]
                with self.assertRaises(SdDiagnosticError):css_state(slices(row['payload']))
