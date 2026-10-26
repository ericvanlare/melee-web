"""Strict retained-reference projection controls; no runtime is exercised."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(os.environ.get('MELEE_LATER_B_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'scripts'),
               str(ROOT / 'reference-capture/dolphin')]
MODULE = Path(os.environ.get('MELEE_LATER_B_MODULE',
                             ROOT / 'tools/stadium_later_b_reference.py'))
spec = importlib.util.spec_from_file_location('later_b_gate2_reference_control', MODULE)
operand = importlib.util.module_from_spec(spec)
spec.loader.exec_module(operand)


class LaterBGate2ReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        required = ('MELEE_LATER_B_BINDING', 'MELEE_LATER_B_OLD_PRIMARY')
        present = [name in os.environ for name in required]
        if not any(present):
            raise unittest.SkipTest('optional retained original-B fixtures are not configured')
        if not all(present) or not all(os.environ[name] for name in required):
            raise ValueError('partial retained original-B fixture configuration refuses')
        cls.binding_path = Path(os.environ['MELEE_LATER_B_BINDING'])
        cls.binding_bytes = cls.binding_path.read_bytes()
        cls.binding = json.loads(cls.binding_bytes)
        cls.paths = operand.verified_files(cls.binding)
        cls.rows = list(operand.context.replay.iter_records(cls.paths['primary']))
        cls.launch = json.loads(cls.paths['launch'].read_text())
        cls.report = json.loads(cls.paths['report'].read_text())
        cls.operands = operand.load(cls.binding)

    def derive(self, rows=None, report=None):
        return operand.derive(self.rows if rows is None else rows,
            self.launch['expected'], self.launch['setup']['sha256'],
            self.report if report is None else report)

    def write_binding(self, folder, value, name='binding.json'):
        path = Path(folder) / name
        path.write_text(json.dumps(value, sort_keys=True) + '\n')
        return path

    def test_exact_gate2_projection_and_exact_byte_binding_identity(self):
        value = operand.build_gate2_projection(self.binding_path)
        self.assertEqual(value['schema'], 'melee-web-stadium-later-b-gate2-reference-projection-v1')
        self.assertEqual(value['reference_id'], 'original-b-2013-943-v1')
        self.assertEqual(value['binding_sha256'], hashlib.sha256(self.binding_bytes).hexdigest())
        self.assertEqual(value['recipe_sha256'], operand.RECIPE_SHA256)
        self.assertEqual(value['selected_match'], 1)
        self.assertEqual(value['setup_global_cursor'], 943)
        self.assertEqual(value['menu_prefix']['input_count'], 943)
        self.assertEqual(value['menu_prefix']['inputs'],
                         self.operands['whole_session_context']['inputs'][:943])
        self.assertEqual(value['source_spans'], self.operands['whole_session_context']['spans'])
        self.assertEqual(value['entry']['sequence'], 4242)
        self.assertEqual(value['entry']['boundary'], 'entry')
        self.assertEqual(value['entry']['seed'], 'ba5dac05')
        self.assertEqual(value['setup']['sequence'], 4331)
        self.assertEqual(value['setup']['boundary'], 'setup')
        self.assertEqual(value['setup']['seed'], '4b25c79b')

        actions = value['actions']
        steps = self.operands['source_steps_before_pause']
        self.assertEqual(len(steps), 180)
        self.assertEqual(len(actions), 360)
        for local_tick, step in enumerate(steps):
            tick_action, draw_action = actions[local_tick * 2:local_tick * 2 + 2]
            self.assertEqual((tick_action['action_index'], tick_action['kind']),
                             (local_tick * 2, 'tick'))
            self.assertEqual(tick_action['source_tick'], local_tick)
            self.assertEqual(tick_action['consume'], step['consume'])
            self.assertEqual(tick_action['tick'], step['tick'])
            self.assertEqual(tick_action['pads'], step['ports'])
            self.assertEqual(len(tick_action['pads']), 4)
            self.assertTrue(all(len(port) == 22 for port in tick_action['pads']))
            self.assertLess(tick_action['consume']['sequence'], tick_action['tick']['sequence'])
            self.assertEqual((draw_action['action_index'], draw_action['kind']),
                             (local_tick * 2 + 1, 'draw'))
            self.assertEqual(draw_action['source_tick'], local_tick + 1)
            self.assertEqual(draw_action['draw_enter'], step['draw_enter'])
            self.assertEqual(draw_action['draw_return'], step['draw_return'])
            self.assertLess(draw_action['draw_enter']['sequence'],
                            draw_action['draw_return']['sequence'])
        self.assertEqual(actions[0]['kind'], 'tick')  # no synthetic initial draw

        go = value['go']
        go_coordinate = go['coordinate']
        go_tick = go_coordinate['source_tick']
        self.assertEqual(go_tick, 84)  # retained observation, not a fallback
        self.assertLess(steps[go_tick]['consume']['sequence'], go_coordinate['sequence'])
        self.assertLess(go_coordinate['sequence'], steps[go_tick]['tick']['sequence'])
        self.assertEqual(go['state'], self.operands['go'])
        self.assertEqual(go['state']['label'], 'GO')
        self.assertEqual(go['rng'], self.operands['go_rng'])

        states = value['states']
        self.assertEqual(len(states), 96)
        self.assertEqual([row['tick']['source_tick'] for row in states], list(range(84, 180)))
        self.assertEqual([row['state'] for row in states], self.operands['pre_pause_states'])
        self.assertEqual([row['rng'] for row in states], self.operands['pre_pause_state_rng'])
        self.assertTrue(all(row['state']['label'] == 'IDLE' for row in states))
        self.assertEqual(states[0]['tick']['sequence'], steps[84]['tick']['sequence'])
        self.assertNotEqual(go_coordinate['sequence'], states[0]['tick']['sequence'])
        stop = value['stop_before_consume']
        self.assertEqual(stop, self.operands['stop_before_consume'])
        self.assertEqual(stop['source_tick'], 180)
        self.assertLess(actions[-1]['draw_return']['sequence'], stop['sequence'])

        constructor = value['constructor_shared_projection']
        self.assertEqual({key: constructor[key] for key in
            ('entry_count', 'min_parameter', 'max_parameter', 'random_bound', 'random_result',
             'seed_before', 'seed_after')},
            {'entry_count': 1, 'min_parameter': 3600, 'max_parameter': 3800,
             'random_bound': 200, 'random_result': 142,
             'seed_before': 'ba5dac05', 'seed_after': 'b6cdeeb4'})
        self.assertEqual(constructor['parameter_roles'],
                         {'min_parameter': 'parameter_x0', 'max_parameter': 'parameter_x4'})
        witnesses = value['constructor_original_witnesses']
        self.assertEqual(witnesses['scope'],
                         'original-only order; no browser phase-dispatch pairing')
        self.assertEqual([row['sequence'] for row in witnesses['coordinates']],
                         [4303, 4304, 4305, 4306, 4307])
        self.assertEqual(len(witnesses['coordinates']), 5)
        for flag in ('runtime_admission', 'same_run_provenance_verified',
                     'browser_parity', 'C3'):
            self.assertIs(value[flag], False)
        self.assertEqual(value['unpaired'], ['original_constructor_phase_dispatch',
            'per_owner_rng', 'registration', 'all_writers', 'C3'])

        with tempfile.TemporaryDirectory() as folder:
            pretty = Path(folder) / 'whitespace-binding.json'
            pretty.write_text(json.dumps(self.binding, indent=2, sort_keys=True) + '\n')
            pretty_bytes = pretty.read_bytes()
            pretty_value = operand.build_gate2_projection(pretty)
        self.assertNotEqual(pretty_value['binding_sha256'], value['binding_sha256'])
        self.assertEqual(pretty_value['binding_sha256'], hashlib.sha256(pretty_bytes).hexdigest())
        original_without_hash = {key: item for key, item in value.items()
                                 if key != 'binding_sha256'}
        pretty_without_hash = {key: item for key, item in pretty_value.items()
                               if key != 'binding_sha256'}
        self.assertEqual(pretty_without_hash, original_without_hash)

    def test_constructor_phase_sidecar_and_modeled_native_receipt_comparator(self):
        expected = operand.build_gate2_constructor_phase_projection(self.binding_path)
        self.assertEqual(expected['schema'],
                         'melee-web-stadium-original-b-constructor-phase-projection-v1')
        self.assertEqual(expected['branch'], 'up')
        self.assertEqual(expected['semantic_owner'], {
            'id': 'selected-match-1/map2/constructor-callback-1',
            'selected_match': 1, 'native_role': 'map2/constructor-callback-1',
            'map_id': 2, 'constructor_callback_ordinal': 1})
        self.assertEqual([event['phase'] for event in expected['events']],
                         ['entry', 'parameters', 'up_call', 'up_return', 'commit'])
        self.assertEqual([row['sequence'] for row in expected['source_coordinates']],
                         [4303, 4304, 4305, 4306, 4307])
        self.assertEqual((expected['events'][1]['parameter_x0'],
                          expected['events'][1]['parameter_x4']), (3600, 3800))
        self.assertEqual((expected['events'][2]['scalar_kind'],
                          expected['events'][2]['scalar_value']), ('randi_bound', 200))
        self.assertEqual((expected['events'][3]['scalar_kind'],
                          expected['events'][3]['scalar_value']), ('randi_result', 142))
        self.assertEqual((expected['events'][4]['scalar_kind'],
                          expected['events'][4]['scalar_value']), ('stored_countdown', 3742))
        self.assertEqual([event['source_seed_hex'] for event in expected['events']],
                         ['ba5dac05', 'ba5dac05', 'ba5dac05', 'b6cdeeb4', 'b6cdeeb4'])
        self.assertIs(expected['runtime_admission'], False)
        self.assertIs(expected['browser_parity'], False)
        self.assertIs(expected['C3'], False)

        # This fixture exercises the consumer's strict join shape; it models an
        # actual Setup receipt and is not evidence from a running native game.
        events = copy.deepcopy(expected['events'])
        setup = {
            'schema': 'melee-web-stadium-zero-tick-setup-state-v1',
            'world_generation': 77, 'world_ticks': 0, 'source_frame': 0,
            'map_id': 2, 'initial_countdown': 3742, 'current_countdown': 3742,
            'parameter_x0': 3600, 'parameter_x4': 3800,
            'constructor_callback_count': 1, 'source_parameter_roots_agree': True,
            'constructor_randi_call_count': 1, 'constructor_randi_bound': 200,
            'constructor_randi_result': 142,
            'source_seed_before_hex': 'ba5dac05', 'source_seed_after_hex': 'b6cdeeb4',
            'constructor_phase_trace': {
                'schema': 'melee-web-stadium-constructor-phase-trace-v1',
                'scope': 'diagnostic-zero-tick-setup-owner-observation-only',
                'owner': {'semantic_id': expected['semantic_owner']['native_role'],
                          'world_generation': 77, 'map_id': 2,
                          'constructor_callback_ordinal': 1},
                'event_count': len(events), 'events': events,
            },
        }
        compared = operand.compare_gate2_constructor_phase_trace(expected, setup)
        self.assertEqual(compared['decision'],
                         'PASS_EXACT_ORIGINAL_PHASES_AND_NATIVE_SETUP_SCALARS')
        self.assertFalse(compared['source_coordinates_compared'])
        self.assertFalse(compared['runtime_admission'])

        for change in (
            lambda state: state['constructor_phase_trace']['events'].reverse(),
            lambda state: state['constructor_phase_trace']['events'][2].update(scalar_value=201),
            lambda state: state['constructor_phase_trace']['events'][3].update(source_seed_hex='00000000'),
            lambda state: state['constructor_phase_trace']['owner'].update(world_generation=78),
            lambda state: state.update(current_countdown=3743),
            lambda state: state['constructor_phase_trace']['owner'].update(semantic_id='foreign/map2/callback-1'),
        ):
            changed = copy.deepcopy(setup)
            change(changed)
            self.assertEqual(operand.compare_gate2_constructor_phase_trace(expected, changed)['decision'],
                             'REFUSE_CONSTRUCTOR_PHASE_TRACE')

        misbound = copy.deepcopy(expected)
        misbound['semantic_owner']['native_role'] = 'foreign/map2/constructor-callback-1'
        self.assertEqual(operand.compare_gate2_constructor_phase_trace(misbound, setup)['decision'],
                         'REFUSE_CONSTRUCTOR_PHASE_TRACE')

    def test_changed_role_pin_and_mixed_setup_owner_refuse(self):
        with tempfile.TemporaryDirectory() as folder:
            changed_pin = copy.deepcopy(self.binding)
            changed_pin['files']['primary']['sha256'] = '0' * 64
            with self.assertRaisesRegex(ValueError, 'changed consumed operand: primary'):
                operand.build_gate2_projection(self.write_binding(folder, changed_pin, 'pin.json'))

            mixed = copy.deepcopy(self.binding)
            primary = mixed['files']['primary']
            mixed['files']['setup_receipt'] = copy.deepcopy(primary)
            with self.assertRaisesRegex(ValueError, 'launch producer/setup joins differ'):
                operand.build_gate2_projection(self.write_binding(folder, mixed, 'mixed.json'))

            extra_role = copy.deepcopy(self.binding)
            extra_role['files']['unreviewed_role'] = copy.deepcopy(primary)
            with self.assertRaisesRegex(ValueError, 'unknown or missing consumed operand role'):
                operand.build_gate2_projection(self.write_binding(folder, extra_role, 'role.json'))

            old_primary = Path(os.environ['MELEE_LATER_B_OLD_PRIMARY'])
            with old_primary.open('rb') as stream:
                old_sha256 = hashlib.file_digest(stream, 'sha256').hexdigest()
            self.assertEqual(old_primary.stat().st_size, 15239799)
            self.assertEqual(old_sha256,
                             '982e6c2022efe270be1e973b68e663392423c858b8bc6b8ff3ddbdb49e8c4fc9')
            legacy = copy.deepcopy(self.binding)
            legacy['files']['primary'] = {'path': str(old_primary),
                'bytes': old_primary.stat().st_size, 'sha256': old_sha256}
            with self.assertRaisesRegex(ValueError, 'same-run report/raw identities differ'):
                operand.build_gate2_projection(self.write_binding(folder, legacy, 'legacy.json'))

    def test_changed_actual_pad_and_go_order_or_constructor_roles_refuse(self):
        rows = copy.deepcopy(self.rows)
        consume = next(row for row in rows if row['event'] == 'boundary' and
                       row['payload'].get('boundary') == 'pad_consume' and
                       row['payload'].get('match_index') == 1)
        pad = next(item for item in consume['payload']['slices'] if item.get('tag') == 3)
        pad['hex'] = ('01' if pad['hex'][:2] != '01' else '00') + pad['hex'][2:]
        with self.assertRaisesRegex(ValueError, 'not the exact new 2013-input reference corpus'):
            self.derive(rows)

        go_index = next(index for index, row in enumerate(self.rows)
                        if row['event'] == 'progress' and
                        row['payload'].get('match_index') == 1 and
                        row['payload'].get('phase') == 'go_after')
        missing_go = copy.deepcopy(self.rows)
        missing_go[go_index]['payload']['phase'] = 'changed_go_phase_control'
        with self.assertRaisesRegex(ValueError, 'missing actual GO marker'):
            self.derive(missing_go)

        reordered_go = copy.deepcopy(self.rows)
        reordered_go[go_index - 1], reordered_go[go_index] = (
            reordered_go[go_index], reordered_go[go_index - 1])
        with self.assertRaises(ValueError):
            self.derive(reordered_go)

        duplicated_go = copy.deepcopy(self.rows)
        duplicated_go.insert(go_index, copy.deepcopy(duplicated_go[go_index]))
        with self.assertRaises(ValueError):
            self.derive(duplicated_go)

        rows = copy.deepcopy(self.rows)
        witness = next(row for row in rows if row['event'] == 'progress' and
                       row['payload'].get('match_index') == 1 and
                       row['payload'].get('phase') == 'constructor_parameters')
        parameters = next(item for item in witness['payload']['slices']
                          if item.get('tag') == 94)
        raw = bytes.fromhex(parameters['hex'])
        parameters['hex'] = (raw[4:8] + raw[0:4]).hex()
        with self.assertRaises(ValueError):
            self.derive(rows)

    def test_binding_mutation_and_symlink_refuse(self):
        original_load = operand.load
        with tempfile.TemporaryDirectory() as folder:
            mutable = Path(folder) / 'mutable.json'
            mutable.write_bytes(self.binding_bytes)

            def mutate_after_load(binding):
                result = original_load(binding)
                mutable.write_bytes(self.binding_bytes + b' ')
                return result

            with mock.patch.object(operand, 'load', side_effect=mutate_after_load):
                with self.assertRaisesRegex(ValueError, 'binding file changed during Gate2 projection'):
                    operand.build_gate2_projection(mutable)

            link = Path(folder) / 'binding-link.json'
            link.symlink_to(self.binding_path)
            with self.assertRaisesRegex(ValueError, 'regular non-symlink'):
                operand.build_gate2_projection(link)


if __name__ == '__main__':
    unittest.main()
