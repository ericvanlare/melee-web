"""Focused CLI checks for immutable Slippi normalization products."""

from contextlib import redirect_stderr, redirect_stdout
import io
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests"))

import normalize_slippi as normalizer  # noqa: E402
from replay_transport import HEADER  # noqa: E402
from slippi_format import GAME_END, FIRST_FRAME  # noqa: E402
from test_slippi_timeline import (  # noqa: E402
    _post,
    _pre,
    _replay,
    _two_player_frame,
)


def _complete_replay(*, pre_size=0x42):
    events = [*_two_player_frame(FIRST_FRAME),
              bytes((GAME_END, 7, 0, 0, 0, 0, 0))]
    if pre_size != 0x42:
        events = [_pre(FIRST_FRAME, 0, payload_size=pre_size),
                  _pre(FIRST_FRAME, 1, payload_size=pre_size),
                  _post(FIRST_FRAME, 0), _post(FIRST_FRAME, 1),
                  bytes((GAME_END, 7, 0, 0, 0, 0, 0))]
    return _replay(events, pre_size=pre_size)


class SlippiNormalizeCliTests(unittest.TestCase):
    def test_digest_covers_parsed_bytes_without_rereading_them(self):
        with tempfile.TemporaryDirectory() as root:
            path=Path(root)/"source.slp"
            payload=_complete_replay()
            path.write_bytes(payload)
            original=normalizer.read_timeline
            def read_then_change(stream):
                timeline=original(stream)
                # Change already-read bytes in place. A second pass would
                # incorrectly label the parsed timeline with the new digest.
                with path.open('r+b') as writer: writer.write(b'XXXX')
                return timeline
            with patch.object(normalizer,'read_timeline',side_effect=read_then_change):
                _,digest=normalizer.load(path)
            self.assertEqual(digest,hashlib.sha256(payload).hexdigest())

    def test_one_immutable_load_publishes_json_and_transport(self):
        with tempfile.TemporaryDirectory(prefix="slippi normalize cli ") as root:
            directory = Path(root)
            source = directory / "source.slp"
            output = directory / "normalized.json"
            transport = directory / "source.mwrp"
            source.write_bytes(_complete_replay())
            timeline_ids = []
            original_record = normalizer.normalized_record
            original_transport = normalizer.encode_transport

            def record_wrapper(timeline, digest, **kwargs):
                timeline_ids.append(id(timeline))
                return original_record(timeline, digest, **kwargs)

            def transport_wrapper(timeline, digest, **kwargs):
                timeline_ids.append(id(timeline))
                return original_transport(timeline, digest, **kwargs)

            arguments = ["normalize_slippi.py", str(source), "--output",
                         str(output), "--transport-output", str(transport)]
            stdout = io.StringIO()
            with patch.object(normalizer, "load", wraps=normalizer.load) as load, \
                    patch.object(normalizer, "normalized_record",
                                 side_effect=record_wrapper), \
                    patch.object(normalizer, "encode_transport",
                                 side_effect=transport_wrapper), \
                    patch.object(sys, "argv", arguments), \
                    redirect_stdout(stdout):
                normalizer.main()

            self.assertEqual(load.call_count, 1)
            self.assertEqual(timeline_ids[0], timeline_ids[1])
            record = json.loads(output.read_text())
            transport_header = HEADER.unpack(transport.read_bytes()[:HEADER.size])
            self.assertEqual(transport_header[4], record["frame_count"])
            summary = json.loads(stdout.getvalue())
            self.assertEqual(summary["comparison"], "not_run")

    def test_transport_validation_failure_does_not_publish_json(self):
        with tempfile.TemporaryDirectory(prefix="slippi normalize failure ") as root:
            directory = Path(root)
            source = directory / "incomplete-input.slp"
            output = directory / "normalized.json"
            transport = directory / "source.mwrp"
            source.write_bytes(_complete_replay(pre_size=0x40))
            arguments = ["normalize_slippi.py", str(source), "--output",
                         str(output), "--transport-output", str(transport)]
            stderr = io.StringIO()
            with patch.object(sys, "argv", arguments), redirect_stderr(stderr):
                with self.assertRaises(SystemExit) as raised:
                    normalizer.main()
            self.assertEqual(raised.exception.code, 2)
            self.assertIn("cannot reconstruct raw PAD input", stderr.getvalue())
            self.assertFalse(output.exists())
            self.assertFalse(transport.exists())

    def test_cli_summary_reports_comparison_not_run(self):
        with tempfile.TemporaryDirectory(prefix="slippi normalize summary ") as root:
            directory = Path(root)
            source = directory / "source.slp"
            output = directory / "normalized.json"
            source.write_bytes(_complete_replay())
            arguments = ["normalize_slippi.py", str(source), "--output",
                         str(output)]
            stdout = io.StringIO()
            with patch.object(sys, "argv", arguments), redirect_stdout(stdout):
                normalizer.main()
            summary = json.loads(stdout.getvalue())
            self.assertEqual(summary["execution"], "source_input_workload_only")
            self.assertEqual(summary["comparison"], "not_run")
            self.assertEqual(summary["frame_count"], 1)


if __name__ == "__main__":
    unittest.main()
