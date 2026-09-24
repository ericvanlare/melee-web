import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
from types import SimpleNamespace

from tools.allocation_replay_spool import JsonSpool, run_checked_file_model


class AllocationReplaySpoolTests(unittest.TestCase):
    def test_partial_write_latches_failure_and_prevents_misleading_receipt(self):
        with tempfile.TemporaryDirectory() as temp:
            spool = JsonSpool(Path(temp), 'failed')
            real_index = spool._index
            spool._index = Mock(wraps=real_index)
            spool._index.write.side_effect = OSError('injected index failure')
            try:
                with self.assertRaisesRegex(OSError, 'injected'):
                    spool.append({'orphan': 1})
                for operation in (lambda: spool.append({}), spool.receipt,
                                  spool.canonical_sha256, lambda: list(spool)):
                    with self.assertRaisesRegex(OSError, 'unusable'):
                        operation()
            finally:
                spool.close()

    def test_hard_byte_budget_rejects_without_changing_sequence(self):
        with tempfile.TemporaryDirectory() as temp:
            spool = JsonSpool(Path(temp), 'small', max_bytes=12)
            try:
                spool.append({})
                with self.assertRaisesRegex(OSError, 'byte budget'):
                    spool.append({})
                self.assertEqual(list(spool), [{}])
            finally:
                spool.close()

    def test_disk_bound_rejects_before_appending(self):
        with tempfile.TemporaryDirectory() as temp:
            spool = JsonSpool(Path(temp), 'limited')
            try:
                with patch('tools.allocation_replay_spool.shutil.disk_usage',
                           return_value=SimpleNamespace(free=1)):
                    with self.assertRaisesRegex(OSError, '2 GiB'):
                        spool.append({'retained': False})
                self.assertEqual(len(spool), 0)
                self.assertEqual(list(spool), [])
            finally:
                spool.close()

    def test_lossless_index_iteration_and_canonical_digest(self):
        with tempfile.TemporaryDirectory() as temp:
            spool = JsonSpool(Path(temp), 'values')
            try:
                values = [{'z': '\u2603', 'a': [1, None]}, {}, {'address': 0xffffffff}]
                self.assertEqual(spool.canonical_sha256(), hashlib.sha256(b'[]').hexdigest())
                for value in values:
                    spool.append(value)
                    self.assertEqual(spool[0], values[0])
                self.assertEqual(list(spool), values)
                self.assertEqual(spool[-1], values[-1])
                self.assertEqual(list(reversed(spool)), list(reversed(values)))
                expected = hashlib.sha256(json.dumps(values, sort_keys=True,
                                                    separators=(',', ':')).encode()).hexdigest()
                self.assertEqual(spool.canonical_sha256(), expected)
                self.assertEqual(spool.receipt()['count'], 3)
                with self.assertRaises(IndexError):
                    _ = spool[3]
                with self.assertRaises(FileExistsError):
                    JsonSpool(Path(temp), 'values')
            finally:
                spool.close()

    def test_checked_model_compares_all_records_and_cardinality(self):
        actions = [{'command': {'value': value}} for value in range(3)]
        outputs = [action['command'] for action in actions]
        cases = [
            ('import sys; sys.stdout.write(sys.stdin.read())', outputs, True, None),
            ('import sys; sys.stdout.write(sys.stdin.read())', [{}, *outputs[1:]], False, 0),
            ('import sys; sys.stdout.write(sys.stdin.readline())', outputs, False, 1),
            ('import sys; sys.stdout.write(sys.stdin.read()+"{}\\n")', outputs, False, 3),
            ('print("invalid")', outputs, False, 0),
        ]
        for code, expected, matches, index in cases:
            with self.subTest(code=code, index=index), tempfile.TemporaryDirectory() as temp:
                result = run_checked_file_model([sys.executable, '-c', code], temp,
                                                actions, expected, Path(temp))
                self.assertEqual(result['matches'], matches)
                self.assertEqual(result['mismatch_index'], index)

    def test_checked_model_retains_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            result = run_checked_file_model([sys.executable, '-c',
                                            'import sys; sys.stderr.write("failure"); sys.exit(2)'],
                                           temp, [], [], Path(temp))
            self.assertFalse(result['matches'])
            self.assertEqual(Path(result['stderr']).read_text(), 'failure')

    def test_checked_model_rejects_native_count_gap_and_timeout(self):
        with tempfile.TemporaryDirectory() as temp:
            result = run_checked_file_model([], temp, [{'command': {}}], [], Path(temp))
            self.assertEqual(result['error'], 'native action and output counts differ')
            result = run_checked_file_model([sys.executable, '-c', 'import time; time.sleep(2)'],
                                           temp, [], [], Path(temp), timeout=0.02)
            self.assertFalse(result['matches'])
            self.assertIn('timeout', result['error'])

    def test_checked_model_rejects_fast_output_budget_overrun(self):
        with tempfile.TemporaryDirectory() as temp:
            result = run_checked_file_model([sys.executable, '-c', 'print("x" * 1000)'],
                                           temp, [], [], Path(temp), max_bytes=100)
            self.assertFalse(result['matches'])
            self.assertIn('byte budget', result['error'])


if __name__ == '__main__':
    unittest.main()
