from __future__ import annotations

import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
import io
from contextlib import redirect_stdout
from unittest.mock import patch

ROOT = Path(__file__).parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from stadium_go_prefix import (CONSTRUCTOR_POINTS, StadiumConstructorObservations,
                               StadiumGoPrefixError, EXPECTED_SETUP_RECEIPT_SHA256,
                               DIAGNOSTIC, _ppc_randi_result, validate_stadium_go_prefix,
                               validate_stadium_idle_3500_reference)
from test_stadium_go_prefix import _fixture, observer_stream


def witnesses(x0=3600, x4=3800):
    obj, ground, entities, params, r13, rng = 0x81001000, 0x81002000, 0x81003000, 0x81004000, 0x804db6a0, 0x81005000
    seed = 0x3bb84c53
    after = (seed * 214013 + 2531011) & 0xffffffff
    branch = 'up' if x4 > x0 else 'down' if x4 < x0 else 'equal'
    bound = abs(x4-x0) & 0xffffffff
    result = _ppc_randi_result(after, bound)
    final = x4 if branch == 'equal' else ((x0 if branch == 'up' else x4) + result) & 0xffffffff
    phases = ['constructor_entry', 'constructor_parameters']
    if branch != 'equal': phases += [f'constructor_{branch}_call', f'constructor_{branch}_return']
    phases += ['constructor_commit']
    header = bytearray(0x38); header[:2] = b'\0\3'; header[2] = 5; header[7] = 3
    header[0x2c:0x30] = struct.pack('>I', ground)
    gr = bytearray(0x18); gr[4:8] = struct.pack('>I', obj); gr[0x14:] = struct.pack('>I', 2)
    rows = []
    def sl(tag, address, raw, flags=0):
        return dict(tag=tag, address=address, hex=raw.hex(), size=len(raw), flags=flags)
    u = lambda n: struct.pack('>I', n & 0xffffffff)
    for i, phase in enumerate(phases):
        pc, word = CONSTRUCTOR_POINTS[phase]
        returned = phase.endswith('_return') or phase == 'constructor_commit'
        value = after if returned and branch != 'equal' else seed
        g = [obj if i == 0 else params if i == 1 else bound if phase.endswith('_call') else result,
             r13, x4 & 0xffffffff, x0 & 0xffffffff, obj, ground]
        if phase == 'constructor_commit': g[2] = final
        slices = [sl(67,0x8049e850,u(obj),2),sl(68,obj+0x2c,u(ground),2),
                  sl(90,obj,bytes(header)),sl(91,ground,bytes(gr)),
                  sl(92,entities+20,u(obj)),sl(98,0x804d782c,u(entities)),
                  sl(95,0x804ce380,b'\x05'),sl(96,obj,bytes(header)),
                  sl(17,0x80479d30,b'\x02\0\0\0\0\0'),sl(30,0x80479d58,u(40)),
                  sl(19,0x804d5f94,u(rng)),sl(20,rng,u(value))]
        if i: slices += [sl(93,r13-0x4d28,u(params)),sl(94,params,u(x0)+u(x4))]
        if phase == 'constructor_commit': slices += [sl(70,ground+0xd8,u(final)),sl(97,0x801d14c4,bytes.fromhex('937f00d8'))]
        p = dict(diagnostic=DIAGNOSTIC, phase=phase, setup_receipt_sha256=EXPECTED_SETUP_RECEIPT_SHA256,
                 setup_profile_verified_by_observer=False, pc=f'0x{pc:08x}', word=word,
                 argument=f'0x{obj:08x}', lr='0x801d1138', source_tick=40, draw_ordinal=0, constructor_gprs=g, slices=slices)
        rows.append(dict(event='progress',seq=100+i,source_tick=40,draw_ordinal=0,payload=p))
    return rows


def decode(rows, route_ready=True):
    tracker = StadiumConstructorObservations()
    for row in rows: tracker.accept(row, route_ready=route_ready)
    return tracker.finish()


def literal_frames(raw):
    """Retain the actual wire header and payload, including binary boundaries."""
    result = []
    offset = 0
    while offset < len(raw):
        header = observer_stream.HEADER.unpack_from(raw, offset)
        start = offset + observer_stream.HEADER.size
        end = start + header[-2]
        if end > len(raw):
            raise ValueError('truncated fixture payload')
        result.append((header, raw[start:end]))
        offset = end
    return result


def stored_frame(header, literal_payload, *, sequence=None, replacement_payload=None):
    values = list(header)
    if sequence is not None:
        values[3] = sequence
    payload = literal_payload if replacement_payload is None else replacement_payload
    if replacement_payload is not None:
        values[-2] = len(payload)
        values[-1] = zlib.crc32(payload) & 0xffffffff
    return observer_stream.HEADER.pack(*values) + payload


def with_constructor(raw, status, constructor_rows=None):
    constructor_rows = witnesses() if constructor_rows is None else constructor_rows
    original=[observer_stream._decode_record(header,payload,"constructor fixture")
              for header,payload in literal_frames(raw)]
    for row, (header, payload) in zip(original, literal_frames(raw), strict=True):
        row['_stored_header'] = header
        row['_literal_payload'] = payload
    added=copy.deepcopy(constructor_rows); rows=[]
    for row in original:
        if row['event']=='boundary' and row['payload']['boundary']=='setup': rows+=added
        rows.append(copy.deepcopy(row))
    chunks=[]
    for i,row in enumerate(rows):
        if row['event'] in ('handshake','start','end'):
            row['payload']['constructor_owner_observations']=True
        if row['event']=='end':
            row['payload'].update(constructor_observations_complete=True,registration_event_observed=False,
                                  allocation_generation_observed=False,lifetime_pairing_observed=False)
        replacement = None
        if '_stored_header' in row:
            header = row['_stored_header']
            literal = row['_literal_payload']
            if row['event'] in ('handshake', 'start', 'end'):
                replacement = json.dumps(row['payload']).encode()
        else:
            # New Progress witnesses carry explicit synthetic header fields.
            # Existing headers are never reconstructed from decoded rows.
            literal = json.dumps(row['payload']).encode()
            header = (observer_stream.MAGIC_U32, observer_stream.SCHEMA_VERSION,
                      4, row['seq'], 100, int(row['payload']['pc'], 16),
                      row['source_tick'], row['draw_ordinal'], len(literal),
                      zlib.crc32(literal) & 0xffffffff)
        chunks.append(stored_frame(header, literal, sequence=i,
                                   replacement_payload=replacement))
    status = {**status, 'last_seq':len(rows)-1, 'event_count':len(rows)}
    return b''.join(chunks), status


class StadiumConstructorDecoderTests(unittest.TestCase):
    def test_both_branches_equal_and_signed_machine_product(self):
        for x0,x4,branch in [(3600,3800,'up'),(3800,3600,'down'),(3600,3600,'equal'),
                             (-2147483648,2147483647,'up')]:
            with self.subTest(branch=branch,x0=x0):
                r = decode(witnesses(x0,x4))
                self.assertEqual(r['branch'],branch)
                self.assertFalse(r['allocation_generation_observed'])
                self.assertFalse(r['registration_event_observed'])
        self.assertEqual(_ppc_randi_result(0xffff0000, 0xffffffff), 0)
        self.assertEqual(_ppc_randi_result(0xffff0000, 0x10000), 0xffffffff)
        self.assertEqual(_ppc_randi_result(0xffff0000, 200), 199)

    def test_missing_duplicate_reordered_context_and_literal_mutations_refuse(self):
        original=witnesses()
        bads=[original[1:], original[:-1], original[:2]+[original[2]]+original[2:],
              [original[0],original[2],original[1]]+original[3:]]
        def mutation(index, fn):
            rows=copy.deepcopy(original); fn(rows[index]['payload']); bads.append(rows)
        mutation(2,lambda p:p.update(phase='constructor_down_call',pc='0x801d14b4',word='0x481af0cd'))
        mutation(3,lambda p:p['constructor_gprs'].__setitem__(0,p['constructor_gprs'][0]+1))
        mutation(3,lambda p:p['constructor_gprs'].__setitem__(4,0x81001004))
        mutation(1,lambda p:p.update(word='0x00000000'))
        mutation(4,lambda p:p['slices'].append(copy.deepcopy(p['slices'][0])))
        for tag,index,hex_value in [(20,3,'00000000'),(19,3,'81005004'),(94,3,'00000e1000000ed9'),
                                    (67,1,'81001004'),(17,1,'180000000000'),(97,4,'00000000')]:
            mutation(index,lambda p,t=tag,h=hex_value:next(s for s in p['slices'] if s['tag']==t).update(hex=h))
        for rows in bads:
            with self.subTest(case=bads.index(rows)):
                with self.assertRaises(StadiumGoPrefixError): decode(rows)
        with self.assertRaises(StadiumGoPrefixError): decode(original,route_ready=False)

    def test_membership_missing_cycle_and_capacity(self):
        for kind in ('missing','cycle','overcapacity'):
            rows=witnesses(); p=rows[0]['payload']; node=next(s for s in p['slices'] if s['tag']==96)
            if kind=='missing': p['slices'].remove(node)
            elif kind=='cycle':
                extra=copy.deepcopy(node);extra['flags']=1;p['slices'].append(extra)
            else:
                p['slices'] += [{**node,'flags':i} for i in range(1,50)]
            with self.subTest(kind=kind):
                with self.assertRaises(StadiumGoPrefixError): decode(rows)

    def test_original_fixture_roundtrip_preserves_every_header_and_payload(self):
        with tempfile.TemporaryDirectory() as d:
            raw, status = _fixture()
            frames = literal_frames(raw)
            roundtrip = b''.join(stored_frame(header, payload) for header, payload in frames)
            self.assertEqual(roundtrip, raw)
            p = Path(d) / 'observer.bin'
            p.write_bytes(roundtrip)
            Path(str(p) + '.status.json').write_text(json.dumps(status))
            result = validate_stadium_go_prefix(p)
            self.assertEqual(result['decision'], 'PASS_ORIGINAL_RAW_GO_PREFIX_ONLY')

    def test_whole_production_route_explicit_scope_and_unchanged_terminal(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'observer.bin'; raw,status=_fixture();p.write_bytes(raw)
            augmented,status=with_constructor(raw,status)
            p.write_bytes(augmented)
            status_path=Path(str(p)+'.status.json');status_path.write_text(json.dumps(status))
            result=validate_stadium_go_prefix(p,constructor_observations=True)
            self.assertEqual(result['constructor_observations']['branch'],'up')
            self.assertEqual(result['first_full_post_go_source_tick_F'],126)
            with self.assertRaises(StadiumGoPrefixError): validate_stadium_go_prefix(p)
            # Early constructor end is neither a complete GO route nor a completed replay.
            frames=literal_frames(augmented)
            setup_index=next(i for i,(h,b) in enumerate(frames)
                             if observer_stream._decode_record(h,b,'fixture')['payload'].get('boundary')=='setup')
            p.write_bytes(b''.join(stored_frame(h,b) for h,b in frames[:setup_index]))
            with self.assertRaises(StadiumGoPrefixError): validate_stadium_go_prefix(p,constructor_observations=True)





from owned_test_workspace import OwnedWorkspaceTests
from stadium_go_prefix import validate_stadium_idle_state_snapshot
from stadium_recorded_prefix_compare import compare_stadium_common_wire, StadiumCommonWireMismatch
import test_stadium_idle_state_contract as state_fixtures
from test_stadium_recorded_prefix_compare import _write_case, _change_header, _rewrite_json_payload
from test_stadium_recorded_replay import driver


class StadiumConstructorWiringTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.workspace=cls.new_workspace(ROOT,'stadium-constructor-wire-')
        cls.case=0

    def pair(self, constructor_rows=None, mutate=None):
        directory=self.workspace/f'case-{type(self).case}'
        type(self).case+=1
        directory.mkdir()
        original_data,original_status=_fixture(draw_between_c_f=True)
        candidate_data,candidate_status=state_fixtures._stateful_route_fixture(directory,mutate)
        candidate_data,candidate_status=with_constructor(candidate_data,candidate_status,constructor_rows)
        original=_write_case(directory/'original','observer',original_data,original_status)
        candidate=_write_case(directory/'candidate','observer',candidate_data,candidate_status)
        return original,candidate,candidate_data,candidate_status

    def compare(self,original,candidate,**options):
        return compare_stadium_common_wire(original[0],candidate[0],
            original_status_path=original[1],candidate_status_path=candidate[1],**options)

    def test_literal_equal_and_call_insertions_validate_before_projection(self):
        for x0,x4,count in [(3600,3600,3),(3600,3800,5),(3800,3600,5)]:
            original,candidate,_,_=self.pair(witnesses(x0,x4))
            receipt=self.compare(original,candidate,constructor_observations=True)
            self.assertEqual(receipt['decision'],'PASS_SEQUENCE_MAPPED_STADIUM_COMMON_WIRE_WITH_CONSTRUCTOR_OBSERVATIONS')
            self.assertEqual(receipt['version'],2)
            self.assertNotIn('sequence',receipt['comparison']['exact_wire_fields'])
            mapping=receipt['comparison']['sequence_mapping']
            self.assertEqual(len(mapping['removed_candidate_constructor_records']),count)
            self.assertEqual(mapping['all_candidate_records_accounted'],mapping['all_original_records_compared']+count)
            pairs=[(o,c) for a,b,c,d in mapping['inclusive_ranges'] for o,c in zip(range(a,b+1),range(c,d+1),strict=True)]
            self.assertEqual([o for o,c in pairs],list(range(mapping['all_original_records_compared'])))
            removed={r['candidate_sequence'] for r in mapping['removed_candidate_constructor_records']}
            self.assertEqual([c for o,c in pairs],[c for c in range(mapping['all_candidate_records_accounted']) if c not in removed])
            companion=validate_stadium_idle_state_snapshot(candidate[0],status_path=candidate[1],constructor_observations=True)
            self.assertEqual(len(companion['snapshots']),3)
            with self.assertRaises(StadiumGoPrefixError): self.compare(original,candidate)
            with self.assertRaises(StadiumGoPrefixError): validate_stadium_idle_state_snapshot(candidate[0],status_path=candidate[1])

    def test_malformed_ctor_and_declaration_refuse_without_projection(self):
        for kind in ('missing','duplicate','reorder','unknown','unknown_slice','bool_slice','missing_lr','bad_lr','bool_tick','float_draw','declaration','raw_pc','sequence'):
            rows=witnesses()
            if kind=='missing': rows.pop(2)
            elif kind=='duplicate': rows.insert(2,copy.deepcopy(rows[2]))
            elif kind=='reorder': rows[1],rows[2]=rows[2],rows[1]
            elif kind=='unknown': rows[2]['payload']['unapproved']=0
            elif kind=='unknown_slice': rows[2]['payload']['slices'][0]['unapproved']=False
            elif kind=='bool_slice': rows[2]['payload']['slices'][2]['flags']=False
            elif kind=='missing_lr': del rows[2]['payload']['lr']
            elif kind=='bad_lr': rows[2]['payload']['lr']='not hex'
            elif kind=='bool_tick': rows[2]['payload']['source_tick']=True
            elif kind=='float_draw': rows[2]['payload']['draw_ordinal']=0.0
            original,candidate,data,status=self.pair(rows)
            frames=literal_frames(data)
            ctor_seq=next(h[3] for h,b in frames if h[2]==4 and json.loads(b).get('phase')=='constructor_entry')
            if kind=='declaration': data=_rewrite_json_payload(data,0,lambda p:p.update(constructor_owner_observations=False))
            elif kind=='raw_pc': data=_change_header(data,ctor_seq,5,0x81234567)
            elif kind=='sequence': data=_change_header(data,ctor_seq,3,ctor_seq+1)
            candidate[0].write_bytes(data)
            with self.subTest(kind=kind):
                with self.assertRaises(ValueError): self.compare(original,candidate,constructor_observations=True)

    def test_actual_literal_common_mutation_targets_exist(self):
        original,candidate,data,status=self.pair()
        receipt=self.compare(original,candidate,constructor_observations=True)
        self.assertEqual(receipt['decision'],'PASS_SEQUENCE_MAPPED_STADIUM_COMMON_WIRE_WITH_CONSTRUCTOR_OBSERVATIONS')
        frames=literal_frames(data)
        header,payload=next((h,b) for h,b in frames if h[2]==4 and json.loads(b).get('phase')=='css_return')
        row=json.loads(payload)
        for tag,size in ((21,0x358),(19,4),(20,4)):
            observed=[s for s in row['slices'] if s['tag']==tag]
            self.assertEqual(len(observed),1)
            self.assertEqual(len(bytes.fromhex(observed[0]['hex'])),size)
        self.assertEqual(stored_frame(header,payload),next(stored_frame(h,b) for h,b in literal_frames(candidate[0].read_bytes()) if h[3]==header[3]))

    def test_exact_common_pc_tick_pad_rng_and_order_still_refuse(self):
        for kind in ('pc','tick','pad','rng','order','extra_metadata'):
            def mutate(label,p):
                if kind=='order' and label=='GO':
                    p['slices'][0],p['slices'][1]=p['slices'][1],p['slices'][0]
            original,candidate,data,status=self.pair(mutate=mutate)
            if kind in ('pad','rng'):
                # Actual existing CSS Progress carries both source observations.
                # No synthetic common slice is inserted to manufacture coverage.
                frames=literal_frames(data)
                css_seq=next(h[3] for h,b in frames if h[2]==4 and json.loads(b).get('phase')=='css_return')
                def change_literal(p):
                    item=next(s for s in p['slices'] if s['tag']==(21 if kind=='pad' else 20))
                    raw=bytearray.fromhex(item['hex']);raw[-1]^=1;item['hex']=raw.hex()
                data=_rewrite_json_payload(data,css_seq,change_literal)
            frames=literal_frames(data)
            go_seq=next(h[3] for h,b in frames if h[2]==4 and json.loads(b).get('phase')=='go_after')
            if kind=='pc': data=_change_header(data,go_seq,5,0x81234567)
            elif kind=='tick': data=_change_header(data,go_seq,6,999)
            elif kind=='extra_metadata': data=_rewrite_json_payload(data,0,lambda p:p.update(unapproved=True))
            candidate[0].write_bytes(data)
            with self.subTest(kind=kind):
                with self.assertRaises(ValueError) as raised:
                    self.compare(original,candidate,constructor_observations=True)
                if isinstance(raised.exception,StadiumCommonWireMismatch):
                    self.assertEqual(raised.exception.receipt['version'],2)
                    difference=raised.exception.receipt['first_difference']
                    self.assertEqual(difference['candidate_sequence']-difference['original_sequence'],0 if kind in ('extra_metadata','pad','rng') else 5)
                    self.assertEqual(raised.exception.receipt['original']['stream_sha256'],__import__('hashlib').sha256(original[0].read_bytes()).hexdigest())

    def test_actual_cli_default_and_explicit_option_forwarding_and_scope_refusal(self):
        args=[]
        for name in ('dolphin','disc','profile','menu-recipe','output','build-manifest'):
            args += ['--'+name,'unused']
        for extra,enabled in [([],False),(['--input-replay','retained','--stadium-constructor-owner'],True)]:
            with patch.object(driver,'run',return_value={}) as run, redirect_stdout(io.StringIO()):
                driver.main(args+extra)
            self.assertIs(run.call_args.kwargs['stadium_constructor_owner'],enabled)
            if enabled: self.assertIs(run.call_args.kwargs['common_wire_comparator'],compare_stadium_common_wire)
        for scope,replay,value in [('stadium_go_prefix',None,True),('transform_prefix',{},True),('stadium_go_prefix',{},1)]:
            with patch.object(driver,'validate_reference_build_manifest',return_value={}), patch.object(driver,'menu_actions',return_value=({'scope':scope},'hash')):
                with self.assertRaisesRegex(ValueError,'explicit checked capture mode'):
                    driver._run(dolphin='unused',disc='unused',profile='unused',menu_recipe='unused',output=self.workspace,build_manifest='unused',timeout=180,replay_receipt=replay,stadium_constructor_owner=value)

    def test_idle3500_pre_go_draw_return_short_circuits_post_go_target(self):
        # This actual idle fixture includes the authored DrawReturn before GO,
        # when there is no C tick yet. It must reach the later scoped terminal
        # refusal rather than evaluating the post-GO 3500-tick target early.
        import test_stadium_idle_3500_reference as idle

        raw, status = idle.fixture(end_ordinal=0)
        with tempfile.TemporaryDirectory() as directory:
            stream = Path(directory) / 'idle3500-observer.bin'
            stream.write_bytes(raw)
            status_path = Path(str(stream) + '.status.json')
            status_path.write_text(json.dumps(status))
            with self.assertRaisesRegex(
                    StadiumGoPrefixError,
                    'observer stream lacks containing C, next full F, or finishing DrawReturn'):
                validate_stadium_idle_3500_reference(stream, status_path=status_path)


if __name__=='__main__': unittest.main()
