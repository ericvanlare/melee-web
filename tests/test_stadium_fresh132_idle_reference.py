"""Explicitly SYNTHETIC positive component controls; no positive fresh recording exists."""
import copy
import json
import os
from pathlib import Path
import tempfile
import unittest

import test_stadium_idle_3500_reference as fixture
from test_stadium_constructor_observations import literal_frames, stored_frame
from reference_observer_stream import _decode_record, iter_records
from stadium_go_prefix import validate_stadium_idle_3500_reference, StadiumGoPrefixError
from stadium_fresh132_idle_reference import extract_records, read_bound, FreshIdleError


def synthetic_full(tail=0):
    """Complete old synthetic wire fixture's intentionally sparse pre-GO schedule."""
    raw,status=fixture.fixture(tail=tail)
    out=[]; setup=False; go=False; ticks=draws=0
    def add(raw):
        nonlocal draws,ticks
        h,b=literal_frames(raw)[0]; row=_decode_record(h,b,'synthetic full idle');p=row['payload']
        header=list(h)
        if setup and row['event']=='boundary':
            header[6]=ticks; header[7]=draws
            if p['boundary'] in ('draw_enter','draw_return'):
                next(s for s in p['slices'] if s['tag']==30)['hex']=ticks.to_bytes(4,'big').hex()
                kind=7 if p['boundary']=='draw_enter' else 8
                _,b=literal_frames(fixture.frame(kind,ticks,p['slices']))[0]
            if p['boundary']=='source_tick':ticks+=1
            if p['boundary']=='draw_return':draws+=1
        elif setup and p.get('phase')=='go_after':
            header[7]=draws;p['draw_ordinal']=draws;b=json.dumps(p).encode()
        out.append(stored_frame(tuple(header),b,replacement_payload=b))
    for h,b in literal_frames(raw):
        row=_decode_record(h,b,'synthetic construction');p=row['payload']
        if row['event']=='boundary' and p['boundary']=='setup':
            out.append(stored_frame(h,b));setup=True
            for tick in range(125):
                for part in (fixture.consume(tick),fixture.frame(6,tick,[fixture.route._slice(2,12)]+fixture.match_slices(tick)),
                             fixture.frame(7,tick,[fixture.route._slice(2,12)]+fixture.match_slices(tick)),
                             fixture.frame(8,tick,[fixture.route._slice(2,12)]+fixture.match_slices(tick))):add(part)
            continue
        if p.get('phase')=='go_after':
            go=True;add(fixture.consume(125))
            for sl in p['slices']:
                if sl['tag']==19:sl.update(address=0x804d5f94,hex='804d5f90')
                if sl['tag']==20:sl.update(address=0x804d5f90,hex='263cc5ca')
            b=json.dumps(p).encode()
        if setup and not go:continue
        add(stored_frame(h,b,replacement_payload=b))
    raw=b''.join(stored_frame(h,b,sequence=i) for i,part in enumerate(out) for h,b in literal_frames(part))
    status.update(event_count=len(out),last_seq=len(out)-1)
    return raw,status

class FreshIdleControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw,cls.status=synthetic_full(tail=2)

    def parsed(self, raw=None,status=None):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'observer.bin';p.write_bytes(self.raw if raw is None else raw)
            q=Path(d)/'status.json';q.write_text(json.dumps(self.status if status is None else status))
            idle=validate_stadium_idle_3500_reference(p,status_path=q,extract_inputs=True,include_constructor_state=True)
            return list(iter_records(p)),idle

    def test_synthetic_actual_wire_parser_and_pairing_with_two_natural_tails(self):
        rows,idle=self.parsed();result=extract_records(rows,idle['route'],idle)
        self.assertIs(result['same_run_provenance_verified'],False)
        self.assertEqual(idle['route']['constructor_observations']['state_projection']['constructor_entry_count'],1)
        self.assertEqual(idle['route']['go_source_tick_C'],125) # fixture, deliberately not historical84
        self.assertEqual(result['host_only']['natural_tail_ticks'],2)
        states=result['host_only']['typed_states']
        self.assertEqual([s['scope'] for s in states[-3:]],['comparison','natural_tail','natural_tail'])
        self.assertEqual(len(result['runtime_payload']['bundles']),3628)
        kinds=[r['action'] for r in result['runtime_payload']['schedule']]
        self.assertIn(['tick','tick'],[kinds[i:i+2] for i in range(len(kinds)-1)])

    def test_synthetic_order_go_four_ports_and_tail_negatives(self):
        rows,idle=self.parsed()
        for change in ('remove_consume','duplicate_tick','reorder','go','four_ports','tail'):
            bad=copy.deepcopy(rows);bad_idle=copy.deepcopy(idle)
            if change=='tail':bad_idle['natural_tail_ticks']=1
            elif change=='go':next(r for r in bad if r['payload'].get('phase')=='go_after')['payload']['source_tick']+=1
            else:
                i=next(i for i,r in enumerate(bad) if r['payload'].get('boundary')=='pad_consume' and r['source_tick']>130)
                if change=='remove_consume':bad.pop(i)
                elif change=='duplicate_tick':bad.insert(i+2,copy.deepcopy(bad[i+1]))
                elif change=='reorder':bad[i],bad[i+1]=bad[i+1],bad[i]
                else:
                    sl=next(s for s in bad[i]['payload']['slices'] if s['tag']==3)
                    sl['hex']='01'+sl['hex'][2:]
            with self.subTest(change=change),self.assertRaises(ValueError):extract_records(bad,idle['route'],bad_idle)

    def test_synthetic_constructor_omitted_repeated_reordered_witnesses_refuse(self):
        parts=literal_frames(self.raw)
        positions=[i for i,(h,b) in enumerate(parts) if _decode_record(h,b,'witness')['payload'].get('phase','').startswith('constructor_')]
        self.assertEqual(len(positions),5)
        for change in ('omit','repeat','reorder'):
            bad=list(parts)
            if change=='omit':bad.pop(positions[1])
            elif change=='repeat':bad.insert(positions[1],bad[positions[1]])
            else:bad[positions[1]],bad[positions[2]]=bad[positions[2]],bad[positions[1]]
            raw=b''.join(stored_frame(h,b,sequence=i) for i,(h,b) in enumerate(bad))
            status=dict(self.status,event_count=len(bad),last_seq=len(bad)-1)
            with self.subTest(change=change),self.assertRaises(StadiumGoPrefixError):self.parsed(raw,status)

    def test_identity_mutation_and_duplicate_schema_refuse(self):
        import hashlib
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'input';p.write_bytes(b'producer1')
            item=dict(path=str(p),bytes=9,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
            self.assertEqual(read_bound(item),b'producer1')
            p.write_bytes(b'producer2')
            with self.assertRaises(FreshIdleError):read_bound(item)
            with self.assertRaises(FreshIdleError):read_bound(dict(item,extra=True))

    def test_synthetic_metadata_producer_joins_and_actual_short_span_refusal(self):
        import hashlib
        from stadium_fresh132_idle_reference import validate_binding,extract,PRODUCER_KEYS
        base=os.environ.get('MELEE_FRESH132_GO2_CAPTURE')
        if not base:self.skipTest('explicit retained GO2 capture not provided')
        base=Path(base);run=Path(os.environ['MELEE_FRESH132_RUN_ROOT'])
        def identity(path):
            data=path.read_bytes()
            return dict(path=str(path),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
        with tempfile.TemporaryDirectory() as d:
            # SYNTHETIC metadata-only idle launch; underlying actual GO2 never passes idle parsing.
            launch=json.loads((base/'launch.json').read_text())
            launch.pop('original_post_go_target_ordinal');launch.pop('stadium_go_ordinal2')
            launch['original_idle_post_go_ticks']=3500
            launch['caps'].update(observer_records=77602,pre_C_records=16384)
            q=Path(d)/'synthetic-launch.json';q.write_text(json.dumps(launch))
            paths={name:base/file for name,file in dict(observer='observer.bin',observer_status='observer-status.json',
                inputs='inputs.mwri',input_status='input-status.json',menu_recipe='menu-recipe.json',intentions='input-intentions.jsonl').items()}
            paths.update(launch=q,ready_expected=run/'fresh-sss-ready-start-implementation-candidate-v1/data/fresh-sss-132-ready-start-expected-v1.json',
                recipe_input=Path('/Volumes/AgentStorage/melee-web/runs/stadium-c3-lifetime-20261009-074216-4d7cf7d1/original-stadium-feedback-proposal-v2/stadium-go-feedback-menu-recipe-v1.json'))
            binding=dict(schema='melee-web-fresh132-idle-reference-binding',version=1,
                files={name:identity(path) for name,path in paths.items()},producer={k:launch[k] for k in PRODUCER_KEYS})
            self.assertEqual(set(validate_binding(binding)),set(paths))
            with self.assertRaises(StadiumGoPrefixError):extract(binding)
            for key in PRODUCER_KEYS:
                changed=copy.deepcopy(binding);changed['producer'][key]='invalid'
                with self.subTest(producer=key),self.assertRaises(FreshIdleError):validate_binding(changed)
            launch['wall_seconds']=181;q.write_text(json.dumps(launch));binding['files']['launch']=identity(q)
            with self.assertRaisesRegex(FreshIdleError,'deadline'):validate_binding(binding)

    def test_actual_short_context_matches_retained_fresh_ready_fields_only(self):
        from stadium_fresh132_idle_reference import validate_fresh_context
        from stadium_go_prefix import validate_stadium_go_ordinal2_reference
        base=os.environ.get('MELEE_FRESH132_GO2_CAPTURE')
        if not base:self.skipTest('explicit retained GO2 capture not provided')
        base=Path(base);run=Path(os.environ['MELEE_FRESH132_RUN_ROOT'])
        route=validate_stadium_go_ordinal2_reference(base/'observer.bin',input_path=base/'inputs.mwri',
            input_status_path=base/'input-status.json',status_path=base/'observer-status.json')['route']
        # Derive constructor projection from the actual five witnesses, not an expected count.
        from stadium_go_prefix import StadiumConstructorObservations
        rows=list(iter_records(base/'observer.bin'));ctor=StadiumConstructorObservations()
        for row in rows:
            if row.get('payload',{}).get('phase','').startswith('constructor_'):ctor.accept(row,route_ready=True)
        route['constructor_observations']=ctor.finish(include_state_projection=True)
        expected=json.loads((run/'fresh-sss-ready-start-implementation-candidate-v1/data/fresh-sss-132-ready-start-expected-v1.json').read_text())['expected']
        context=validate_fresh_context(rows,route,expected)
        self.assertEqual(context['constructor']['constructor_entry_count'],1)
        bad=copy.deepcopy(expected);bad['random_seed_hex']='00000000'
        with self.assertRaises(FreshIdleError):validate_fresh_context(rows,route,bad)

    def test_actual_retained_go2_is_incomplete_idle_reference(self):
        base=os.environ.get('MELEE_FRESH132_GO2_CAPTURE')
        if not base:self.skipTest('explicit retained GO2 capture not provided')
        p=Path(base)
        with self.assertRaises(StadiumGoPrefixError):
            validate_stadium_idle_3500_reference(p/'observer.bin',status_path=p/'observer-status.json',
                extract_inputs=True,include_constructor_state=True)

if __name__=='__main__':unittest.main()
