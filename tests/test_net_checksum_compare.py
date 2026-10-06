import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from tools import net_checksum_compare as comparator


def record(tick, **changes):
    values = {
        "tick": tick, "scene": 1, "seed": 0x12345678, "frame": tick,
        "flags": 0, "objects": 0, "input": 0, "pad": 0,
        "scene_state": 0, "object_state": 0, "total": 0,
    }
    values.update(changes)
    return comparator.RECORD.pack(*(values[name] for name in comparator.FIELDS))


def complete_instance(directory, data, start_record):
    directory.mkdir()
    (directory / "checksums.bin").write_bytes(data)
    rows = len(data) // comparator.RECORD_BYTES
    (directory / "instance.json").write_text(json.dumps({
        "outcome": "complete", "records": rows, "total_ticks": rows,
        "first_error": None, "browser_closed": True,
        "start_record": {"recorded": 1, "scene": 1, "seed": 1, "frame": 0,
                         "total": "0000000000000000", "pad": "0000000000000000",
                         "scene_state": "0000000000000000", "object_state": "0000000000000000",
                         "objects": 0, "flags": 0, **start_record},
        "arena": [],
    }))


class NetChecksumComparatorTests(unittest.TestCase):
    def test_empty_streams_cannot_pass(self):
        with self.assertRaisesRegex(comparator.StreamError, "empty"):
            comparator.read_records(b"")
        with self.assertRaisesRegex(comparator.StreamError, "at least one"):
            comparator.compare([], [])

    def test_instance_directory_requires_complete_metadata_and_records(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "failed"
            directory.mkdir()
            (directory / "checksums.bin").write_bytes(record(0))
            (directory / "instance.json").write_text(json.dumps({
                "outcome": "fail", "records": 0, "total_ticks": 1,
                "first_error": "navigation failed",
            }))
            with self.assertRaisesRegex(comparator.StreamError, "not complete"):
                comparator.load(directory)

            (directory / "instance.json").unlink()
            with self.assertRaisesRegex(comparator.StreamError, "missing instance.json"):
                comparator.load(directory)

    def test_complete_outcome_cannot_hide_incomplete_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "incomplete"
            complete_instance(directory, record(0), {})
            meta_path = directory / "instance.json"
            meta = json.loads(meta_path.read_text())
            meta["total_ticks"] = 2
            meta_path.write_text(json.dumps(meta))
            with self.assertRaisesRegex(comparator.StreamError, "total_ticks"):
                comparator.load(directory)
            meta["total_ticks"] = 1
            meta["browser_closed"] = False
            meta_path.write_text(json.dumps(meta))
            with self.assertRaisesRegex(comparator.StreamError, "browser cleanup"):
                comparator.load(directory)
            meta["browser_closed"] = True
            meta["start_record"]["recorded"] = 0
            meta_path.write_text(json.dumps(meta))
            with self.assertRaisesRegex(comparator.StreamError, "recorded agreed start context"):
                comparator.load(directory)

    def test_start_context_mismatch_blocks_an_identical_stream_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            complete_instance(root / "a", record(0), {"seed": 1, "rules": "a"})
            complete_instance(root / "b", record(0), {"seed": 1, "rules": "b"})
            report = comparator.compare_paths(root / "a", root / "b")
            self.assertTrue(report["stream_identical"])
            self.assertFalse(report["identical"])
            self.assertFalse(report["gating_identical"])
            self.assertFalse(report["start_record_equal"])
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(comparator.main([str(root / "a"), str(root / "b")]), 1)

    def test_object_mismatch_precedes_and_does_not_hide_gating_divergence(self):
        first = comparator.read_records(b"".join(record(tick) for tick in range(3)))
        second = comparator.read_records(b"".join((
            record(0, objects=1, object_state=1),
            record(1, input=1, total=1),
            record(2),
        )))
        report = comparator.compare(first, second)
        self.assertEqual(report["first_divergence"]["tick"], 0)
        self.assertEqual(report["first_divergence"]["channels"], ["objects"])
        self.assertEqual(report["first_gating_divergence"]["tick"], 1)
        self.assertIn("input", report["first_gating_divergence"]["channels"])
        self.assertFalse(report["identical"])

    def test_flipped_input_control_names_its_first_tick(self):
        first = comparator.read_records(b"".join(record(tick) for tick in range(4)))
        second = comparator.read_records(b"".join((
            record(0), record(1), record(2, input=1, total=1), record(3, input=1, total=1),
        )))
        report = comparator.compare(first, second)
        self.assertEqual(report["first_divergence"]["tick"], 2)
        self.assertIn("input", report["first_divergence"]["channels"])

    def test_matching_complete_instances_can_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            context = {"seed": 1, "rules": "same"}
            complete_instance(root / "a", record(0), context)
            complete_instance(root / "b", record(0), context)
            report = comparator.compare_paths(root / "a", root / "b")
            self.assertTrue(report["identical"])
            self.assertTrue(report["start_record_equal"])


if __name__ == "__main__":
    unittest.main()
