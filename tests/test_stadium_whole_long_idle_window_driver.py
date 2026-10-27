"""Focused modeled controls for the opt-in B long-idle receiver gate.

These controls exercise the actual driver methods with explicitly modeled rows.
They do not represent an original-game capture or establish source parity.
"""
from pathlib import Path
import hashlib
import importlib.util
import importlib
import os
import sys
import tempfile
import unittest
from unittest.mock import patch


SOURCE_ROOT = Path(os.environ.get(
    "MELEE_STADIUM_FEEDBACK_SOURCE_ROOT", Path(__file__).resolve().parents[1]
)).resolve()
DRIVER_PATH = Path(os.environ.get(
    "MELEE_STADIUM_WHOLE_LONG_IDLE_DRIVER",
    SOURCE_ROOT / "scripts" / "capture_sd_reference_prefix.py",
)).resolve()
sys.path[:0] = [
    str(SOURCE_ROOT / "scripts"),
    str(SOURCE_ROOT / "tools"),
    str(SOURCE_ROOT / "reference-capture" / "dolphin"),
]


def load_exact(name, path):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    if Path(module.__file__).resolve() != path.resolve():
        raise AssertionError("loaded driver path changed")
    if hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() != digest:
        raise AssertionError("loaded driver bytes changed")
    return module


decoder = importlib.import_module("stadium_go_prefix")
if Path(decoder.__file__).resolve() != (SOURCE_ROOT / "tools" / "stadium_go_prefix.py").resolve():
    raise AssertionError("loaded Stadium decoder path changed")
driver = load_exact("_candidate_capture_sd_reference_prefix_long_idle", DRIVER_PATH)
from sd_original_menu_plan import stadium_go_feedback_packet


class StadiumWholeLongIdleWindowDriverTests(unittest.TestCase):
    def test_compact_launch_builder_uses_fresh_selected_b_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "owned-output"
            output.mkdir()
            environment, receipt = driver.build_stadium_whole_long_idle_compact_launch(output)
            expected = output.resolve() / driver.STADIUM_WHOLE_LONG_IDLE_COMPACT_BASENAME
            self.assertEqual(environment, {
                "MWRC_STADIUM_COMPACT_RNG_OUTPUT": str(expected),
                "MWRC_STADIUM_COMPACT_RNG_MATCHES": "1",
            })
            self.assertEqual(receipt["matches"], [1])
            self.assertEqual(receipt["output_path"], str(expected))
            self.assertTrue(receipt["output_absent_at_prepare"])
            self.assertFalse(receipt["lifecycle_parity"])
            self.assertFalse(expected.exists())

            expected.write_bytes(b"occupied")
            with self.assertRaisesRegex(ValueError, "must be fresh"):
                driver.build_stadium_whole_long_idle_compact_launch(output)

    @staticmethod
    def new_receiver():
        receiver = driver.StadiumWholeFeedbackReceiver(
            stadium_go_feedback_packet(), "a" * 64, {},
            stadium_whole_b_state=True, stadium_whole_long_idle=True,
        )
        receiver.match_index = 1
        receiver.b_entry_seen = True
        receiver.setup_seen = True
        receiver.b_constructor.finish = lambda *args, **kwargs: None
        go = dict(sequence=100, source_tick=900, draw_ordinal=700)
        payload = dict(
            whole_session=True,
            match_index=1,
            stadium_b_state_observation=True,
            phase="go_after",
        )
        row = dict(
            seq=go["sequence"], source_tick=go["source_tick"],
            draw_ordinal=go["draw_ordinal"], event="progress", payload=payload,
        )
        with patch.object(decoder, "_validate_progress", return_value=payload), \
                patch.object(decoder, "_validate_stadium_idle_state_record", return_value=go):
            receiver.accept_b_progress(row)
        return receiver

    @staticmethod
    def modeled_tick(receiver, ordinal, sequence, *, ports=None, errors=None,
                     draw_ordinal=None):
        window = receiver.long_idle_window
        source_tick = window["go"]["source_tick"] + ordinal
        if draw_ordinal is None:
            draw_ordinal = window["expected_draw_ordinal"]
        consume = dict(
            seq=sequence, source_tick=source_tick, draw_ordinal=draw_ordinal,
        )
        receiver._observe_long_idle_boundary(consume, "pad_consume")
        tick = dict(
            seq=sequence + 1, source_tick=source_tick,
            draw_ordinal=draw_ordinal,
        )
        receiver._observe_long_idle_boundary(tick, "source_tick")
        sample = dict(
            sequence=sequence, source_tick=source_tick,
            draw_ordinal=draw_ordinal, match_index=receiver.match_index,
            ports=(ports if ports is not None else
                   [driver.NEUTRAL_PAD, driver.NEUTRAL_PAD,
                    "00" * 10 + "ff", "00" * 10 + "ff"]),
            errors=errors if errors is not None else [0, 0, -1, -1],
        )
        snapshot = dict(
            sequence=sequence + 1, source_tick=source_tick,
            draw_ordinal=draw_ordinal, modeled_tick_ordinal=ordinal,
        )
        receiver._observe_long_idle_tick(tick, snapshot, sample)
        return tick

    @staticmethod
    def modeled_draw_pair(receiver, source_tick, draw_ordinal, sequence):
        enter = dict(seq=sequence, source_tick=source_tick,
                     draw_ordinal=draw_ordinal)
        returned = dict(seq=sequence + 1, source_tick=source_tick,
                        draw_ordinal=draw_ordinal)
        receiver._observe_long_idle_boundary(enter, "draw_enter")
        receiver._observe_long_idle_boundary(returned, "draw_return")
        return returned

    def test_actual_gate_methods_accept_3501_typed_ticks_and_first_later_draw(self):
        receiver = self.new_receiver()
        sequence = receiver.long_idle_window["go"]["sequence"] + 1
        go_tick = receiver.long_idle_window["go"]["source_tick"]
        first_draw = receiver.long_idle_window["expected_draw_ordinal"]
        for ordinal in range(3501):
            tick = self.modeled_tick(receiver, ordinal, sequence)
            self.modeled_draw_pair(receiver, tick["source_tick"] + 1,
                                   first_draw + ordinal, sequence + 2)
            sequence += 4
        self.assertTrue(receiver.long_idle_window_complete)
        summary = receiver.long_idle_window_summary()
        self.assertEqual(summary["profile"], "selected_b_3500_v1")
        self.assertEqual(summary["comparison_post_go_ticks"], 3500)
        self.assertEqual(summary["comparison_typed_state_count"], 3501)
        self.assertEqual(summary["four_port_consumed_pad_count"], 3501)
        self.assertEqual(summary["target_source_tick"]["source_tick"], go_tick + 3500)
        self.assertEqual(summary["target_source_tick"]["ordinal"], 3500)
        self.assertEqual(summary["first_later_natural_draw_return"]["draw_ordinal"],
                         first_draw + 3500)
        self.assertEqual(summary["natural_tail_source_ticks"], [])
        self.assertFalse(summary["source_schedule_verified"])
        self.assertFalse(summary["lifecycle_parity"])

    def test_missing_tick_bad_pad_and_bad_draw_coordinates_refuse(self):
        receiver = self.new_receiver()
        go = receiver.long_idle_window["go"]
        bad_tick = dict(seq=go["sequence"] + 2,
                        source_tick=go["source_tick"] + 2,
                        draw_ordinal=go["draw_ordinal"])
        sample = dict(sequence=go["sequence"] + 1,
                      source_tick=bad_tick["source_tick"],
                      draw_ordinal=bad_tick["draw_ordinal"],
                      ports=[driver.NEUTRAL_PAD] * 4,
                      errors=[0, 0, 0, 0])
        snapshot = dict(sequence=bad_tick["seq"],
                        source_tick=bad_tick["source_tick"],
                        draw_ordinal=bad_tick["draw_ordinal"])
        with self.assertRaisesRegex(ValueError, "typed SourceTick is missing"):
            receiver._observe_long_idle_tick(bad_tick, snapshot, sample)

        receiver = self.new_receiver()
        with self.assertRaisesRegex(ValueError, "exact four-port consumed PAD"):
            self.modeled_tick(receiver, 0, 101, ports=[driver.NEUTRAL_PAD] * 4,
                              errors=[0, 0, 0, 0])

        receiver = self.new_receiver()
        malformed = dict(seq=101, source_tick=900, draw_ordinal="700")
        with self.assertRaisesRegex(ValueError, "lacks actual source coordinates"):
            receiver._observe_long_idle_boundary(malformed, "draw_enter")

    def test_draw_return_requires_matching_actual_enter_and_window_cannot_skip_close(self):
        receiver = self.new_receiver()
        go = receiver.long_idle_window["go"]
        unpaired = dict(seq=go["sequence"] + 1,
                        source_tick=go["source_tick"] + 1,
                        draw_ordinal=go["draw_ordinal"])
        with self.assertRaisesRegex(ValueError, "DrawReturn lacks its actual DrawEnter"):
            receiver._observe_long_idle_boundary(unpaired, "draw_return")

        receiver = self.new_receiver()
        for ordinal in range(3501):
            self.modeled_tick(receiver, ordinal, 101 + ordinal * 2)
        self.assertFalse(receiver.long_idle_window_complete)
        with self.assertRaisesRegex(ValueError, r"lacks its actual GO\+3500/natural DrawReturn"):
            receiver.long_idle_window_summary()

    def test_profile_handshake_is_exact_and_default_record_cap_is_unchanged(self):
        receiver = self.new_receiver()
        payload = dict(
            whole_session=True, match_count=3, stadium_whole_feedback=True,
            setup_receipt_sha256="a" * 64, stadium_whole_b_state=True,
            stadium_observed_match=1, stadium_whole_replay_binding_sha256=None,
            stadium_whole_long_idle=dict(driver.STADIUM_WHOLE_LONG_IDLE_PROFILE,
                                         comparison_post_go_ticks=3499),
        )
        with self.assertRaisesRegex(ValueError, "source profile announcement differs"):
            receiver.accept(dict(seq=0, event="handshake", payload=payload))

        receiver = self.new_receiver()
        payload["stadium_whole_long_idle"] = dict(driver.STADIUM_WHOLE_LONG_IDLE_PROFILE)
        receiver.accept(dict(seq=0, event="handshake", payload=payload))
        receiver.accept(dict(seq=1, event="start", payload=payload))
        self.assertEqual(receiver.announcements, ["handshake", "start"])

        deadline = driver.time.monotonic() + 10
        with self.assertRaisesRegex(ValueError, "observer record cap"):
            driver.check_stadium_capture_bounds(
                Path("absent-primary.mwro"), Path("absent-input.mwri"),
                deadline, 16385,
            )
        driver.check_stadium_capture_bounds(
            Path("absent-primary.mwro"), Path("absent-input.mwri"),
            deadline, driver.STADIUM_WHOLE_LONG_IDLE_RECORD_CAP,
            whole_long_idle=True,
        )
        with self.assertRaisesRegex(ValueError, "observer record cap"):
            driver.check_stadium_capture_bounds(
                Path("absent-primary.mwro"), Path("absent-input.mwri"),
                deadline, driver.STADIUM_WHOLE_LONG_IDLE_RECORD_CAP + 1,
                whole_long_idle=True,
            )


if __name__ == "__main__":
    unittest.main()
