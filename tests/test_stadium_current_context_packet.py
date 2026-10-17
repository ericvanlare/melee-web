"""Retained actual-source packet controls; no native/browser execution."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCY_ROOT = Path(os.environ.get('MELEE_STADIUM_PACKET_DEPENDENCY_ROOT', ROOT))
sys.path[:0] = [str(DEPENDENCY_ROOT), str(DEPENDENCY_ROOT/'tools'),
                str(DEPENDENCY_ROOT/'reference-capture/dolphin')]
MODULE = Path(os.environ.get('MELEE_STADIUM_PACKET_MODULE', ROOT/'tools/stadium_current_context_packet.py'))
spec = importlib.util.spec_from_file_location('stadium_current_context_packet', MODULE)
packet = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = packet
spec.loader.exec_module(packet)

class StadiumCurrentContextSyntheticTests(unittest.TestCase):
    """Always-on shape/order controls only, never original observation evidence."""
    @staticmethod
    def interval():
        # Reuse the maintained genuine queue/slot fixture shape, not a fake
        # decoder or successful native service. Remaining fields are synthetic.
        sys.path.insert(0, str(DEPENDENCY_ROOT/'tests'))
        from test_whole_session_replay import _pad_consume
        consume = _pad_consume(0, 0x10, 0)
        consume['seq'] = 1
        consume['payload']['lr'] = 0
        rows = [consume]
        for seq, name, tick in [(2, 'source_tick', 0), (3, 'draw_enter', 1),
                                (4, 'draw_return', 1)]:
            row = copy.deepcopy(consume)
            row.update(seq=seq, source_tick=tick)
            row['payload'].update(boundary=name,
                pc=next(pc for pc, label in packet.replay.semantics.BOUNDARIES.items() if label==name),
                slices=[{'name':'scene_frame','address':0x80479D58,'size':4,
                         'hex':tick.to_bytes(4,'big').hex()}])
            rows.append(row)
        return rows, {'seq':0}, {'seq':5,'source_tick':1}

    def test_synthetic_tick_zero_draw_one_relation_and_four_port_queue(self):
        rows, setup, exit_row = self.interval()
        result = packet.match_steps(rows, 0, setup, exit_row)
        self.assertEqual(len(result), 1)
        self.assertEqual((result[0]['tick']['source_tick'], result[0]['draw_return']['source_tick']), (0, 1))
        self.assertEqual(result[0]['ports'], [bytes([0x10+i]).hex()*11 for i in range(4)])

    def test_synthetic_missing_consumption_draw_and_reordered_pairs_refuse(self):
        rows, setup, exit_row = self.interval()
        for index in range(4):
            with self.subTest(missing=rows[index]['payload']['boundary']), self.assertRaises(ValueError):
                packet.match_steps(rows[:index]+rows[index+1:], 0, setup, exit_row)
        for left, right in [(0,1), (2,3)]:
            mutated = copy.deepcopy(rows)
            mutated[left], mutated[right] = mutated[right], mutated[left]
            with self.subTest(reordered=(left,right)), self.assertRaises(ValueError):
                packet.match_steps(mutated, 0, setup, exit_row)

    def test_synthetic_mismatched_source_and_queue_coordinates_refuse(self):
        rows, setup, exit_row = self.interval()
        for index, field in [(0,'source_tick'), (1,'source_tick'), (2,'source_tick'),
                             (3,'source_tick'), (2,'draw_ordinal'), (3,'draw_ordinal')]:
            mutated = copy.deepcopy(rows);mutated[index][field] += 1
            with self.subTest(index=index,field=field), self.assertRaises(ValueError):
                packet.match_steps(mutated,0,setup,exit_row)
        for index in range(4):
            mutated = copy.deepcopy(rows);mutated[index]['payload']['pc'] ^= 4
            with self.subTest(wrong_pc=index), self.assertRaisesRegex(packet.CurrentContextError,'source PC'):
                packet.match_steps(mutated,0,setup,exit_row)
        mutated = copy.deepcopy(rows);mutated[0]['payload']['gprs'][25] += 0x30
        with self.assertRaisesRegex(ValueError,'source queue slot'):
            packet.match_steps(mutated,0,setup,exit_row)

    def test_direct_script_help_uses_actual_dependency_imports(self):
        env = dict(os.environ)
        # External candidates need the actual checkout dependencies explicitly;
        # tracked tools/... direct invocation obtains tools from sys.path[0].
        env['PYTHONPATH'] = os.pathsep.join([str(DEPENDENCY_ROOT/'tools'), str(DEPENDENCY_ROOT)])
        result = subprocess.run([sys.executable, str(MODULE), '--help'], env=env,
                                cwd=DEPENDENCY_ROOT, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Host-only current Stadium', result.stdout)

    def test_synthetic_dynamic_seed_pointer_uses_recorded_value_owner(self):
        sys.path.insert(0, str(DEPENDENCY_ROOT/'tests'))
        from test_whole_session_replay import _candidate
        css = next(r for r in _candidate() if r.get('payload',{}).get('boundary')=='css_enter')
        self.assertEqual(packet.seed(css)['value_hex'], '12345678')
        for name, field, value in [('rng_pointer','address',0x804D5F90),
                                    ('rng_pointer','hex','80510001'),
                                    ('rng_pointer','hex','81800000'),
                                    ('rng_value','address',0x80510004)]:
            mutated=copy.deepcopy(css)
            next(x for x in mutated['payload']['slices'] if x['name']==name)[field]=value
            with self.subTest(name=name,field=field), self.assertRaisesRegex(ValueError,'pointer slot/value owner'):
                packet.seed(mutated)
        mutated=copy.deepcopy(css)
        mutated['payload']['slices'].append(copy.deepcopy(next(x for x in mutated['payload']['slices'] if x['name']=='rng_value')))
        with self.assertRaisesRegex(ValueError,'exactly one rng_value'):
            packet.seed(mutated)


class StadiumCurrentContextPacketTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = os.environ.get('MELEE_STADIUM_CURRENT_CONTEXT_BINDING')
        if not path:
            raise unittest.SkipTest('Retained current-context source packet binding unavailable')
        cls.binding = json.loads(Path(path).read_text())
        if os.environ.get('MELEE_STADIUM_CURRENT_CONTEXT_BINDING_SHA256'):
            assert packet.sha(Path(path)) == os.environ['MELEE_STADIUM_CURRENT_CONTEXT_BINDING_SHA256']
        cls.rows = list(packet.replay.iter_records(Path(cls.binding['files']['primary']['path'])))
        cls.expected = json.loads(Path(cls.binding['files']['target']['path']).read_text())['expected_setup']

    def row(self, name, match=0):
        return next(r for r in self.rows if r['event'] == 'boundary' and
                    r['payload'].get('match_index') == match and r['payload'].get('boundary') == name)

    def test_actual_source_packet_has_exact_context_setups_eight_joins_and_four_port_steps(self):
        result = packet.build_packet(self.binding)
        self.assertEqual(result['first_css']['observer_seq'], 560)
        self.assertEqual(result['first_css']['rng'], 0x312151C3)
        self.assertEqual(result['first_css_source']['rng']['value_hex'], '312151c3')
        self.assertEqual(result['matches'][0]['entry']['sequence'], 1507)
        self.assertEqual(result['matches'][0]['entry']['rng']['value_hex'], '3bb84c53')
        self.assertEqual(result['matches'][0]['setup']['sequence'], 1554)
        self.assertEqual(result['matches'][0]['setup']['rng']['value_hex'], '0729c198')
        self.assertEqual(len(result['original_rng_endpoints']), 8)
        self.assertEqual([len(m['steps']) for m in result['matches']], [192]*3)
        for match in result['matches']:
            self.assertEqual(match['entry']['start_melee_hex'], match['setup']['start_melee_hex'])
            self.assertEqual(match['setup']['declared_setup'], self.expected)
            for step in match['steps']:
                self.assertEqual(len(step['ports']), 4)
                self.assertTrue(all(len(p) == 22 for p in step['ports']))
                self.assertLess(step['consume']['sequence'], step['tick']['sequence'])
                self.assertLess(step['tick']['sequence'], step['draw_enter']['sequence'])
                self.assertLess(step['draw_enter']['sequence'], step['draw_return']['sequence'])
        first = result['matches'][0]['steps'][0]
        self.assertEqual((first['consume']['sequence'], first['tick']['sequence'],
                          first['draw_enter']['sequence'], first['draw_return']['sequence']),
                         (1555, 1556, 1557, 1558))
        self.assertEqual((first['tick']['source_tick'], first['draw_return']['source_tick']), (0, 1))
        self.assertEqual(result['menu_draw_schedule'], 'UNKNOWN')
        self.assertFalse(result['runtime_admission'])
        self.assertFalse(result['C3'])
        self.assertFalse(result['all_rng_writers_observed'])
        # Exact repeated raw StartMeleeData prerequisite is independently reused
        # by existing v8 encoding, without creating runtime admission or files.
        capture = packet.replay.capture_from_records(self.rows)
        encoded, metadata = packet.replay.encode_v8(capture)
        self.assertEqual(encoded[:4], b'MWRC')
        self.assertEqual(metadata['version'], 8)

    def test_source_hash_and_required_decoder_binding_refuse_before_packet(self):
        b = copy.deepcopy(self.binding)
        b['files']['primary']['sha256'] = '0'*64
        with self.assertRaisesRegex(packet.CurrentContextError, 'Source identity mismatch'):
            packet.build_packet(b)
        b = copy.deepcopy(self.binding)
        b['source_sha256'] = {}
        with self.assertRaisesRegex(packet.CurrentContextError, 'decoder source binding missing'):
            packet.build_packet(b)

    def test_wrong_target_and_raw_setup_changes_are_not_healed(self):
        expected = copy.deepcopy(self.expected)
        expected['players'][0]['costume'] = 0
        with self.assertRaisesRegex(packet.CurrentContextError, 'Target differs'):
            packet.packet_from_records(self.rows, expected)
        rows = copy.deepcopy(self.rows)
        row = next(r for r in rows if r['seq'] == 1554)
        item = next(s for s in row['payload']['slices'] if s['name'] == 'match_setup')
        raw = bytearray.fromhex(item['hex']);raw[0x1C] ^= 1;item['hex'] = raw.hex()
        with self.assertRaisesRegex(packet.CurrentContextError, 'full raw or typed'):
            packet.packet_from_records(rows, self.expected)

    def test_queue_owner_duplicate_slice_and_source_step_refusals(self):
        original = self.row('pad_consume')
        row = copy.deepcopy(original)
        row['payload']['gprs'][25] += 0x30
        with self.assertRaisesRegex(packet.replay.WholeSessionReplayError, 'source queue slot'):
            packet.replay._consumed_ports(row, row['seq'])
        row = copy.deepcopy(original)
        row['payload']['slices'].append(copy.deepcopy(next(s for s in row['payload']['slices'] if s['name']=='pad_slot')))
        with self.assertRaisesRegex(packet.replay.WholeSessionReplayError, 'exactly one pad_slot'):
            packet.replay._consumed_ports(row, row['seq'])
        for name in ('pad_consume', 'source_tick', 'draw_enter', 'draw_return'):
            rows = copy.deepcopy(self.rows)
            row = next(r for r in rows if r['seq'] == {'pad_consume':1555,'source_tick':1556,
                          'draw_enter':1557,'draw_return':1558}[name])
            row['payload']['boundary'] = 'pad_poll'
            with self.subTest(missing=name), self.assertRaises(ValueError):
                packet.packet_from_records(rows, self.expected)
        rows = copy.deepcopy(self.rows)
        rows[1556]['source_tick'] = 1
        with self.assertRaisesRegex(packet.CurrentContextError, 'source order differs'):
            packet.packet_from_records(rows, self.expected)

    def test_dynamic_seed_and_raw_sequence_refusals(self):
        row = copy.deepcopy(self.row('setup'))
        next(s for s in row['payload']['slices'] if s['name']=='rng_value')['address'] += 4
        with self.assertRaisesRegex(packet.CurrentContextError, 'pointer slot/value owner'):
            packet.seed(row)
        for kind in ('duplicate', 'reordered'):
            rows = list(self.rows)
            if kind == 'duplicate': rows[1556] = rows[1555]
            else: rows[1556], rows[1557] = rows[1557], rows[1556]
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                packet.packet_from_records(rows, self.expected)

if __name__ == '__main__':
    unittest.main()
