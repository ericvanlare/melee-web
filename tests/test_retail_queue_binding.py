from pathlib import Path
import hashlib
import json
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from bind_retail_queue import bind


class RetailQueueBindingTests(unittest.TestCase):
    def setUp(self):
        self.recipe = struct.pack('>4sIIIHH', b'MWRC', 4, 42, 4, 0x7ff, 0x1c0) + bytes(312 + 822 + 44 * 4)
        self.rows = [dict(source_tick=tick, payload=dict(clock_pc=0x803769d4,
                         r3=count, core_ticks=time, queue_bytes=[5, 0, 0, count]))
                     for tick, count, time in [(0, 1, 100), (1, 2, 200), (3, 1, 300)]]

    def run_bind(self, rows=None):
        clocks = ('\n'.join(json.dumps(row) for row in (rows if rows is not None else self.rows)) + '\n').encode()
        sha = lambda value: hashlib.sha256(value).hexdigest()
        return bind(self.recipe, clocks, sha(self.recipe), sha(clocks))

    def test_protected_queue_inputs_preserve_recipe_and_exclude_draw_outputs(self):
        result, proof = self.run_bind()
        self.assertEqual(struct.unpack_from('>I', result, 4)[0], 6)
        self.assertEqual(result[16:20], self.recipe[16:20])
        self.assertEqual(result[55:], self.recipe[20:])
        self.assertEqual(struct.unpack_from('>II', result, 20), (3, 0))
        self.assertEqual(struct.unpack_from('>QB', result, 37), (100, 2))
        self.assertEqual(proof['scope'], 'gameplay replay conditioned on recorded nonempty input-queue snapshots')
        # Draw rows are not used to decide batches, even when their contents change.
        self.rows.insert(1, dict(source_tick=99, payload=dict(clock_pc=0x80390fc0, expected='wrong')))
        self.assertEqual(self.run_bind()[0], result)
        # Absolute emulated epoch and host timestamps do not affect event ordering.
        for row in self.rows:
            row['host_timestamp'] = 987654321
            if 'core_ticks' in row['payload']:
                row['payload']['core_ticks'] += 100000
        self.assertEqual(self.run_bind()[0], result)

    def test_reject_incomplete_reordered_or_inconsistent_platform_input(self):
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            self.run_bind(self.rows[:-1])
        for field, value in [('source_tick', 2), ('time', 99), ('count', 0), ('queue', 1)]:
            rows = json.loads(json.dumps(self.rows))
            if field == 'source_tick': rows[1]['source_tick'] = value
            elif field == 'time': rows[1]['payload']['core_ticks'] = value
            elif field == 'count': rows[1]['payload']['r3'] = value
            else: rows[1]['payload']['queue_bytes'][3] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.run_bind(rows)
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            bind(self.recipe, b'', '0' * 64, '0' * 64)
