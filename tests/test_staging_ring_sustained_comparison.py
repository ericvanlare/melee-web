"""Offline interpretation contracts, independent of browser/runtime acceptance."""
import copy
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sustained_comparison', ROOT / 'scripts/assess_staging_ring_sustained_comparison.py')
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)


class SustainedComparisonTests(unittest.TestCase):
    def test_fraction_is_reconstructed_only_for_complete_active_steps(self):
        previous = {'sample_replay_cursor': 281, 'sample_source_frame': 157}
        row = {'sample_replay_cursor': 289, 'sample_source_frame': 165,
               'source_steps': 8, 'source_draws': 8, 'sample_pending_ticks': 0.0007,
               'sample_running': 1, 'sample_scene': 7, 'preparation_ms': 0}
        self.assertEqual(comparison.reconstruct(row, previous)['accepted_pre_consumption_pending_ticks'], 8.0007)
        for change in [{'sample_running': 0}, {'preparation_ms': 1}, {'source_steps': 7},
                       {'sample_pending_ticks': 1}, {'source_draws': 7}]:
            with self.subTest(change=change):
                result = comparison.reconstruct({**row, **change}, previous)
                self.assertFalse(result['reconstruction_valid_under_no_break_no_reset_conditions'])
                self.assertIsNone(result['accepted_pre_consumption_pending_ticks'])

    def test_comparability_boundaries_are_frozen(self):
        before = {'mean_ms': 10, 'p95_ms': 10, 'batches_per_enabled_second': 10}
        for ratio, valid in [(0.8, True), (1.2, True), (0.799, False), (1.201, False)]:
            with self.subTest(ratio=ratio):
                checks = comparison.metric_check('window', before, {name: value*ratio for name, value in before.items()})
                self.assertTrue(all(check['within_frozen_bounds'] == valid for check in checks))

    def test_incident_boundary_distinguishes_guard_from_preceding_callback(self):
        columns = ['hook_at_ms', 'started_ms', 'sample_running', 'sample_scene',
                   'sample_source_frame', 'sample_replay_cursor', 'source_steps',
                   'source_draws', 'sample_pending_ticks', 'preparation_ms', 'total_ms',
                   'staging_slot_wait_ms']
        rows = [[9, 8, 1, 7, 157, 281, 5, 5, .9426, 0, 113.09, 84.74],
                [10, 9, 1, 7, 165, 289, 8, 8, .0007, 0, 158.985, 111.28],
                [11, 10.5, 0, 7, 165, 289, 0, 0, 0, 0, 2.205, 0]]
        incident = {'at_ms': 10.7, 'row': 2, 'reason': 1, 'source_frame': 165,
                    'value': 9, 'threshold': 8}
        capture = {'columns': columns, 'table': [value for row in rows for value in row],
                   'rows': 3, 'incidents': [incident]}
        window = {'assessment': {'active_rows': [{'row': 1}]},
                  'terminal': {'pause_incident': incident},
                  'start': {'load_start_epoch_ms': 1008, 'time_origin': 1000},
                  'gpu_load': {'gpu_stop_state': {'time_origin_ms': 1000, 'stopped_at_ms': 11.2}}}
        result = comparison.describe_window({'treatment_window': window}, capture, 'treatment_window')
        self.assertEqual(result['actual_guard_callback']['row'], 2)
        self.assertEqual(result['actual_guard_callback']['source_steps'], 0)
        self.assertEqual(result['preceding_active_callback']['row'], 1)
        self.assertEqual(result['tail'][0]['accepted_pre_consumption_pending_ticks'], 8.0007)
        self.assertEqual(result['incidents_through_actual_load_off'], [incident])
        # A guard after the logical incident cutoff remains visible through actual off.
        later = {**incident, 'at_ms': 11.1}
        amended = copy.deepcopy(capture)
        amended['incidents'].append(later)
        self.assertEqual(len(comparison.describe_window({'treatment_window': window}, amended,
                                                       'treatment_window')['incidents_through_actual_load_off']), 2)

    def test_capture_shape_loss_rejects(self):
        with self.assertRaisesRegex(ValueError, 'shape mismatch'):
            comparison.decoded({'columns': ['row'], 'table': [], 'rows': 1})


if __name__ == '__main__':
    unittest.main()
