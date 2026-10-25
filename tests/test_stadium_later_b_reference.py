"""Actual retained-reference controls; these do not exercise a game runtime."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import sys
import unittest

ROOT = Path(os.environ.get('MELEE_LATER_B_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'scripts'), str(ROOT / 'reference-capture/dolphin')]
MODULE = Path(os.environ.get('MELEE_LATER_B_MODULE', ROOT / 'tools/stadium_later_b_reference.py'))
spec = importlib.util.spec_from_file_location('later_b_reference_control', MODULE)
operand = importlib.util.module_from_spec(spec)
spec.loader.exec_module(operand)


class LaterBReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        required = ('MELEE_LATER_B_BINDING', 'MELEE_LATER_B_OLD_PRIMARY')
        present = [name in os.environ for name in required]
        if not any(present):
            raise unittest.SkipTest('optional retained original-B fixtures are not configured')
        if not all(present) or not all(os.environ[name] for name in required):
            raise ValueError('partial retained original-B fixture configuration refuses')
        cls.binding = json.loads(Path(os.environ['MELEE_LATER_B_BINDING']).read_text())
        cls.paths = operand.verified_files(cls.binding)
        cls.rows = list(operand.context.replay.iter_records(cls.paths['primary']))
        cls.launch = json.loads(cls.paths['launch'].read_text())
        cls.report = json.loads(cls.paths['report'].read_text())

    def derive(self, rows=None, report=None, receipt=None):
        return operand.derive(self.rows if rows is None else rows,
            self.launch['expected'], self.launch['setup']['sha256'] if receipt is None else receipt,
            self.report if report is None else report)

    def test_actual_exact_operands_and_runtime_refusal(self):
        result = operand.load(self.binding)
        self.assertEqual(result['setup_global_cursor'], 943)
        self.assertEqual(len(result['whole_session_context']['inputs']), 2013)
        self.assertEqual(len(result['source_steps_before_pause']), 180)
        self.assertEqual(len(result['pre_pause_states']), 96)
        self.assertEqual(len(result['natural_pause_tail_unrun']), 12)
        self.assertEqual(result['go']['source_tick'], 84)  # observed fixture assertion, not admission logic
        self.assertEqual(result['entry']['rng']['value_hex'], 'ba5dac05')
        self.assertEqual(result['setup']['rng']['value_hex'], '4b25c79b')
        self.assertEqual(result['go_rng']['value_hex'], '1bc52546')
        for flag in ('runtime_admission', 'same_run_provenance_verified', 'browser_parity', 'C3'):
            self.assertIs(result[flag], False)
        with self.assertRaisesRegex(ValueError, 'reference-only operands'):
            operand.require_runtime_admission(result)

    def test_gate1_projection_preserves_exact_recipe_and_phase_witnesses(self):
        value = operand.build_gate1_expected(self.binding)
        self.assertEqual(value['observation_profile'], 3)
        self.assertEqual(value['notification_cursor'], 943)
        self.assertEqual(len(value['literal_inputs']), 943)
        self.assertEqual(value['recipe']['frames'], 2013)
        self.assertEqual(value['source']['sequence'], 4241)
        self.assertEqual(value['prepared_profile']['entry']['sequence'], 4242)
        self.assertEqual(value['prepared_profile']['setup']['sequence'], 4331)
        self.assertEqual(value['prepared_profile']['entry']['rng']['value_hex'], 'ba5dac05')
        self.assertEqual(value['prepared_profile']['setup']['rng']['value_hex'], '4b25c79b')
        self.assertIs(value['runtime_admission'], False)
        self.assertIsNone(value['reference_context']['notification_order'])
        self.assertEqual(value['reference_order_contract'],
                         'captured_native_complete_trace_not_original_draw_ordinal')
        strict = operand.context.replay.capture_from_path(self.paths['primary'], self.paths['primary_status'])
        recipe, _ = operand.context.replay.encode_v8(strict)
        self.assertEqual(len(recipe), value['recipe']['bytes'])
        import hashlib
        self.assertEqual(hashlib.sha256(recipe).hexdigest(), value['recipe']['sha256'])
        # This optional output is a host operand fixture, never a runtime result.
        if 'MELEE_LATER_B_GATE1_FIXTURE_OUT' in os.environ:
            target = Path(os.environ['MELEE_LATER_B_GATE1_FIXTURE_OUT'])
            self.assertFalse(target.is_symlink())
            self.assertFalse(target.exists())
            target.mkdir(parents=True)
            (target / 'expected.json').write_text(json.dumps(value, indent=2) + '\n')
            (target / 'recipe.mwrc').write_bytes(recipe)

    def test_legacy_corpus_is_not_new_variant(self):
        old = Path(os.environ['MELEE_LATER_B_OLD_PRIMARY'])
        rows = list(operand.context.replay.iter_records(old))
        with self.assertRaisesRegex(ValueError, 'exact new 2013-input'):
            self.derive(rows)

    def test_changed_consumed_pad_and_source_order_refuse(self):
        rows = copy.deepcopy(self.rows)
        consumed = next(row for row in rows if row['event'] == 'boundary' and
                        row['payload'].get('boundary') == 'pad_consume')
        # Removing a real consumed event must not be repaired with a neutral row.
        rows.remove(consumed)
        with self.assertRaises(ValueError):
            self.derive(rows)
        with self.assertRaises(ValueError):
            self.derive(list(reversed(self.rows)))

    def test_missing_repeated_and_changed_constructor_refuse(self):
        index = next(i for i, row in enumerate(self.rows) if row['event'] == 'progress' and
                     row['payload'].get('match_index') == 1 and
                     row['payload'].get('phase') in operand.CONSTRUCTOR_POINTS)
        for kind in ('omit', 'repeat', 'foreign'):
            with self.subTest(kind=kind):
                rows = copy.deepcopy(self.rows)
                if kind == 'omit':
                    rows.pop(index)
                elif kind == 'repeat':
                    rows.insert(index, copy.deepcopy(rows[index]))
                else:
                    rows[index]['payload']['match_index'] = 0
                with self.assertRaises(ValueError):
                    self.derive(rows)

    def test_removed_go_retains_contiguous_sequence_refusal(self):
        rows = [row for row in self.rows if not (row['event'] == 'progress' and
                row['payload'].get('match_index') == 1 and row['payload'].get('phase') == 'go_after')]
        with self.assertRaisesRegex(ValueError, 'observer sequence is not contiguous at record 4753'):
            self.derive(rows)

    def test_missing_go_marker_foreign_receipt_and_tail_claim_refuse(self):
        rows = copy.deepcopy(self.rows)
        go = next(row for row in rows if row['event'] == 'progress' and
                  row['payload'].get('match_index') == 1 and
                  row['payload'].get('phase') == 'go_after')
        # Preserve every record coordinate so the actual missing-marker guard runs.
        go['payload']['phase'] = 'missing_go_marker_control'
        with self.assertRaisesRegex(ValueError, 'missing actual GO marker'):
            self.derive(rows)
        with self.assertRaises(ValueError):
            self.derive(receipt='0' * 64)
        report = copy.deepcopy(self.report)
        selected = next(item for item in report['actual_inputs'] if item['match_index'] == 1)
        selected['actual_pause_chord']['source_tick'] -= 1
        with self.assertRaisesRegex(ValueError, 'pause receipt'):
            self.derive(report=report)

    def test_changed_role_pin_unknown_schema_and_report_refuse(self):
        for mutation in ('digest', 'role', 'schema'):
            binding = copy.deepcopy(self.binding)
            if mutation == 'digest':
                binding['files']['primary']['sha256'] = '0' * 64
            elif mutation == 'role':
                binding['files']['historical_maybe_current'] = binding['files']['primary']
            else:
                binding['schema'] = 'legacy-2006-940'
            with self.assertRaises(ValueError):
                operand.verified_files(binding)
        report = copy.deepcopy(self.report)
        report['stadium_b_observation']['go']['map2']['countdown_s32'] += 1
        with self.assertRaisesRegex(ValueError, 'report differs'):
            self.derive(report=report)


if __name__ == '__main__':
    unittest.main()
