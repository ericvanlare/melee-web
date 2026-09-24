import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.compaction_manager_state import CompactionManagerState, FIELDS


class CompactionManagerStateTests(unittest.TestCase):
    def test_ram_chunks_completion_and_nested_noop_preserve_source_words(self):
        manager = CompactionManagerState(dict.fromkeys(FIELDS, 0))
        state = dict(callback_arg=7, cursor=0x81000000, callback=0x80017A80,
                     phase='awaiting_callback')
        manager.begin(state)
        state.update(phase='waiting_ram_alarm', cursor=0x81020000,
                     move=dict(generation=2, source=0x81010000, destination=0x81000000,
                               size=0x20000, offset=0, next=0x804318C8))
        manager.callback(state, 0x80015320)
        self.assertEqual(manager.snapshot()['chunk'], 0x19000)
        state['move']['offset'] = 0x19000
        manager.alarm(state)
        self.assertEqual(manager.snapshot()['remaining'], 0x7000)
        state['move']['offset'] = 0x20000
        state['phase'] = 'awaiting_callback'
        manager.alarm(state)
        self.assertEqual(manager.snapshot()['size'], 0)
        self.assertEqual(manager.snapshot()['offset'], 0x20000)
        before = manager.snapshot()
        state.update(phase='idle', cursor=0x81030000)
        manager.begin(state)
        self.assertEqual(manager.snapshot(), dict(before, x6E4=0x81030000))
        with self.assertRaisesRegex(ValueError, 'active manager generation'):
            manager.alarm(state)

    def test_devcom_does_not_replace_previous_ram_manager(self):
        initial = dict.fromkeys(FIELDS, 0)
        initial.update(src=0x81001000, dst=0x81000000, offset=32,
                       callback=0x80015320, callback_arg=0x804318C8)
        manager = CompactionManagerState(initial)
        state = dict(callback_arg=0, callback=0x80017A80, cursor=0x600020,
                     phase='waiting_devcom', move=dict(generation=4))
        manager.begin(state)
        manager.callback(state, 0x80015320)
        self.assertEqual(manager.snapshot()['src'], initial['src'])
        self.assertEqual(manager.snapshot()['offset'], 32)

    def test_missing_independent_roots_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'roots'):
            CompactionManagerState({})


if __name__ == '__main__':
    unittest.main()
