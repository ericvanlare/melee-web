"""Debugger repeats must not conceal changed source state or inputs."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from reference_replay_boundary import BoundaryObservations


class BoundaryTests(unittest.TestCase):
    def test_identical_trap_repeats_and_ready_scene_progression(self):
        gate = BoundaryObservations()
        state = {'match_frame': 0, 'context': [1, 2], 'fighter_bits': '12345678'}
        self.assertTrue(gate.accept('scheduler', 0, state))
        self.assertFalse(gate.accept('scheduler', 0, state.copy()))
        self.assertTrue(gate.accept('scheduler', 1, state))
        self.assertEqual(gate.duplicates, {'scheduler': 1})

    def test_changed_context_state_or_inputs_never_discarded(self):
        for field in ('context', 'state', 'input'):
            with self.subTest(field=field):
                gate = BoundaryObservations()
                sample = {'context': 1, 'state': 2, 'input': 3}
                gate.accept('scheduler', 9, sample)
                changed = dict(sample, **{field: 100})
                with self.assertRaisesRegex(ValueError, 'Conflicting'):
                    gate.accept('scheduler', 9, changed)

    def test_distinct_queue_consumptions_remain_distinct(self):
        gate = BoundaryObservations()
        for key in ((7, 0), (7, 1)):
            self.assertTrue(gate.accept('pad', key, b'neutral'))
        self.assertFalse(gate.accept('pad', (7, 1), b'neutral'))

    def test_older_boundary_cannot_recur_after_progression(self):
        gate = BoundaryObservations()
        gate.accept('scheduler', 2, b'old')
        gate.accept('scheduler', 3, b'new')
        with self.assertRaisesRegex(ValueError, 'Out-of-order'):
            gate.accept('scheduler', 2, b'old')


if __name__ == '__main__':
    unittest.main()
