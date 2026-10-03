# SPDX-License-Identifier: MIT
"""Focused checks for the local Slippi scenario's state and network gates."""

import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import Mock, patch

from run_local import (
    PairRun,
    _menu_state,
    _menu_state_observation,
    _mario_cursor_confirmed,
    _parse_lsof_output,
    _game_frame_delta,
    _gameplay_controller_port,
    SCRIPTED_INPUT_START_FRAME,
    SLIPPI_UNFREEZE_INPUT_FRAME,
    SSS_CURSOR_MAX_STEP_PER_FRAME,
    SSS_SELECTION_SETTLE_ATTEMPTS,
    SSS_SELECTION_SETTLE_FRAMES,
    SSS_CURSOR_SWEEP_X_FRAMES,
    SSS_CURSOR_SWEEP_Y_ROWS,
    SSS_CURSOR_X_BOUND,
    SSS_CURSOR_Y_BOUND,
    _stage_selection_settle,
    SCENE_GAME,
    SCENE_SSS,
    _slippi_players_are_mario_mario,
    _stage_is_valid_for_game,
    _ticket_accepted,
    _run_interruption_probe,
)


class LocalScenarioGateTests(unittest.TestCase):
    def test_ordinary_clients_do_not_inherit_external_diagnostic_controls(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run = PairRun.__new__(PairRun)
            run.rollback_diagnostic = None
            run.work = root
            run.disc = root / "disc"
            run.profiles = {name: SimpleNamespace(user_root=root / name) for name in ("p1", "p2")}
            run.evidence = {"clients": {name: {} for name in run.profiles}}
            run.children = {}
            run.supervisor = Mock()
            run.supervisor.start.return_value = SimpleNamespace(pid=123)
            run.timeouts = {"boot": 1}
            run._wait_until = Mock()
            run._record_menu_observation = Mock()
            with patch.dict("os.environ", {"SLIPPI_ROLLBACK_DIAGNOSTIC_CONFIG": "external-fault-config"}):
                run._start_clients(root / "client")
            for call in run.supervisor.start.call_args_list:
                self.assertNotIn("SLIPPI_ROLLBACK_DIAGNOSTIC_CONFIG", call.kwargs["env"])
            self.assertFalse(list(root.glob("*-rollback-config.json")))

    def test_interruption_child_keeps_explicit_artifacts_and_owned_profile_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            child_work = root / "interruption-child/cycle-01"
            child_work.mkdir(parents=True)
            (child_work / "paired.ready").touch()
            (child_work / "evidence.json").write_text(json.dumps({
                "result": "interrupted", "ports_released": True,
                "cleanup": [{"process_group_released": True}],
            }))
            options = {name: root / name for name in
                       ("client_binary", "matchmaker_binary", "dolphin_build",
                        "matchmaker_build", "dolphin_source", "enet_source", "profile_temp_root")}
            supervisor = Mock()
            supervisor.stop.return_value = {"process_group_released": True, "returncode": 130}
            with patch("run_local.ProcessSupervisor", return_value=supervisor):
                receipt = _run_interruption_probe(
                    disc=root / "disc", parent_root=root,
                    timeouts={"boot": 1, "pair": 1, "game": 1, "rematch": 1},
                    artifact_options=options)
            argv = supervisor.start.call_args.args[1]
            for name, path in options.items():
                self.assertEqual(argv[argv.index("--" + name.replace("_", "-")) + 1], str(path))
            self.assertIn("--pause-after-pair", argv)
            self.assertEqual(receipt["result"], "passed")
            supervisor.close.assert_called_once()

    def test_button_pulse_drains_pre_edge_observations(self):
        run = PairRun.__new__(PairRun)
        watcher = Mock()
        pad = Mock()
        events = []
        watcher.receive.side_effect = lambda timeout: events.append("old frame")
        pad.set_button.side_effect = lambda button, held: events.append((button, held))
        run.watchers = {"p1": watcher}
        run.pads = {"p1": pad}
        run._wait_frames = lambda name, count: events.append(("wait", count))
        with patch("run_local.select.select", side_effect=[
            ([watcher.socket], [], []), ([watcher.socket], [], []), ([], [], [])
        ]):
            run._pulse("p1", "A")
        self.assertEqual(events, ["old frame", "old frame", ("A", True),
                                  ("wait", 3), ("A", False), ("wait", 7)])

    def test_gameplay_controller_port_uses_local_si_slot_for_both_clients(self):
        self.assertEqual(_gameplay_controller_port("p1"), 1)
        self.assertEqual(_gameplay_controller_port("p2"), 1)
        with self.assertRaises(ValueError):
            _gameplay_controller_port("p3")

    def test_scripted_remote_input_starts_after_the_pinned_sync_freeze(self):
        self.assertGreater(SCRIPTED_INPUT_START_FRAME, SLIPPI_UNFREEZE_INPUT_FRAME)

    def test_stage_sweep_covers_the_source_cursor_clamp_with_sub_icon_steps(self):
        self.assertAlmostEqual(SSS_CURSOR_MAX_STEP_PER_FRAME, 2.91)
        self.assertEqual(SSS_CURSOR_SWEEP_X_FRAMES, 19)
        self.assertEqual(SSS_CURSOR_SWEEP_Y_ROWS, 15)
        self.assertEqual((SSS_CURSOR_X_BOUND, SSS_CURSOR_Y_BOUND), (27.0, 19.0))

    def test_final_destination_settle_requires_exact_tile_for_every_source_frame(self):
        expected = (25, 32)
        self.assertEqual(SSS_SELECTION_SETTLE_FRAMES, 6)
        self.assertEqual(SSS_SELECTION_SETTLE_ATTEMPTS, 2)
        stable = _stage_selection_settle(expected, [expected] * SSS_SELECTION_SETTLE_FRAMES)
        self.assertTrue(stable["stable"])
        drift = _stage_selection_settle(
            expected, [expected] * (SSS_SELECTION_SETTLE_FRAMES - 1) + [(26, 28)]
        )
        self.assertFalse(drift["stable"])
        self.assertEqual(drift["observed"][-1], [26, 28])
        self.assertFalse(_stage_selection_settle(expected, [])["stable"])

    def test_final_destination_reacquires_after_transient_settle_drift_without_early_a(self):
        expected = (25, 32)

        class FakeWatcher:
            def __init__(self, run):
                self.run = run
                self.values = {"80479d58": 0}
                self.stage = expected

            def selected_stage(self):
                return self.stage

            def online_scene_code(self):
                return SCENE_GAME if self.run.pulses else SCENE_SSS

        class FakePad:
            def set_axis(self, *_args):
                return None

            def neutral(self):
                return None

        def advance(run, watcher, count):
            run.wait_call += 1
            run.source_frame += count
            watcher.values["80479d58"] = run.source_frame
            if run.wait_call == 3:
                watcher.stage = run.first_settle[0]
            elif 3 <= run.wait_call <= 8:
                watcher.stage = run.first_settle[run.wait_call - 3]
            elif run.wait_call == 10:
                watcher.stage = run.second_settle[0]
            elif 10 <= run.wait_call <= 15:
                watcher.stage = run.second_settle[run.wait_call - 10]

        def make_run(first_settle, second_settle):
            run = PairRun.__new__(PairRun)
            run.pulses = 0
            run.wait_call = 0
            run.source_frame = 0
            run.first_settle = first_settle
            run.second_settle = second_settle
            watcher = FakeWatcher(run)
            peer = FakeWatcher(run)
            pad = FakePad()
            run.watchers = {"p1": watcher, "p2": peer}
            run.pads = {"p1": pad}
            run.evidence = {}
            run.replay_baselines = {}
            run._wait_frames = lambda _name, count: advance(run, watcher, count)
            run._wait_until = lambda predicate, **_kwargs: self.assertTrue(predicate())
            run._replay_file_snapshot = lambda: {"fresh": True}
            run._pulse = lambda _name, _button: setattr(run, "pulses", run.pulses + 1)
            return run

        run = make_run(
            [expected] * (SSS_SELECTION_SETTLE_FRAMES - 1) + [(26, 28)],
            [expected] * SSS_SELECTION_SETTLE_FRAMES,
        )
        run._select_final_destination(picker="p1")
        trace = run.evidence["stage_selection_trace"]
        self.assertEqual(run.pulses, 1)
        self.assertEqual(len(trace["settle_attempts"]), 2)
        self.assertFalse(trace["settle_attempts"][0]["stable"])
        self.assertTrue(trace["settle_attempts"][1]["stable"])
        self.assertEqual(
            [row["source_frame"] for row in trace["settle_attempts"][0]["observations"]],
            list(range(32, 38)),
        )
        self.assertEqual(
            trace["settle_attempts"][0]["observations"][-1]["selected_stage"],
            (26, 28),
        )

        run = make_run(
            [expected] * (SSS_SELECTION_SETTLE_FRAMES - 1) + [(26, 28)],
            [expected] * (SSS_SELECTION_SETTLE_FRAMES - 1) + [(26, 28)],
        )
        with self.assertRaisesRegex(RuntimeError, "did not remain selected"):
            run._select_final_destination(picker="p1")
        self.assertEqual(run.pulses, 0)

    def test_stage_gate_scopes_random_direct_opening_and_final_destination_rematch(self):
        self.assertTrue(_stage_is_valid_for_game(2, 1))
        self.assertTrue(_stage_is_valid_for_game(31, 1))
        self.assertFalse(_stage_is_valid_for_game(2, 2))
        self.assertTrue(_stage_is_valid_for_game(32, 2))
        self.assertFalse(_stage_is_valid_for_game(32, 3))

    def test_slippi_character_gate_uses_its_published_mario_id(self):
        self.assertTrue(_slippi_players_are_mario_mario([
            {"character_id": 8}, {"character_id": 8},
        ]))
        self.assertFalse(_slippi_players_are_mario_mario([
            {"character_id": 0}, {"character_id": 0},
        ]))

    def test_game_frame_counter_reset_is_not_treated_as_uint32_wrap(self):
        self.assertEqual(_game_frame_delta(142, 1), 1)
        self.assertEqual(_game_frame_delta(30, 36), 6)

    def test_mario_confirmation_requires_the_cursor_inside_the_authored_icon_bounds(self):
        state = {
            "held": -1,
            "selected": 8,
            "cursor": (-31.0, -2.5),
            "model": (-21.0, 17.0),
            "bounds": (-24.4, -17.4, 20.0, 13.0),
        }
        self.assertFalse(_mario_cursor_confirmed(state))
        state["cursor"] = (-22.0, 17.0)
        self.assertTrue(_mario_cursor_confirmed(state))

    def test_menu_state_decodes_source_menu_and_selection(self):
        class Watcher:
            values = {"804a04f0": 0x08080002}

        self.assertEqual(_menu_state(Watcher()), (8, 8, 2))

    def test_menu_state_requires_an_observed_source_word(self):
        class Watcher:
            values = {}

        self.assertIsNone(_menu_state(Watcher()))

    def test_direct_code_confirmation_uses_source_menu_item_57(self):
        class Watcher:
            values = {"804a04f0": 0x08080039}

        self.assertEqual(_menu_state(Watcher()), (8, 8, 57))

    def test_ticket_acknowledgement_is_scoped_to_the_expected_local_identity(self):
        rows = [
            {"event": "ticket_accepted", "uid": "local-cycle-1-p1"},
        ]
        self.assertTrue(_ticket_accepted(rows, "local-cycle-1-p1"))
        self.assertFalse(_ticket_accepted(rows, "local-cycle-1-p2"))

    def test_menu_observation_preserves_locked_login_default_for_failure_evidence(self):
        class Watcher:
            values = {
                "804a04f0": 0x08080005,
                "80479d58": 17,
            }

            @staticmethod
            def scene_kind():
                return 1

            @staticmethod
            def online_scene_code():
                return 1

        self.assertEqual(
            _menu_state_observation(Watcher()),
            {
                "menu_word_hex": "08080005",
                "submenu": 8,
                "previous_submenu": 8,
                "selected_option": 5,
                "scene_kind": 1,
                "online_scene_code": 1,
                "source_frame": 17,
            },
        )

    def test_network_gate_accepts_loopback_and_unconnected_local_listener(self):
        output = (
            "COMMAND PID USER FD TYPE DEVICE SIZE/OFF NODE NAME\n"
            "dolphin 123 user 17u IPv4 0x01 0t0 TCP "
            "127.0.0.1:41301->127.0.0.1:41302 (ESTABLISHED)\n"
            "dolphin 123 user 18u IPv4 0x02 0t0 UDP 127.0.0.1:41301\n"
        )
        self.assertEqual(
            _parse_lsof_output(output),
            [
                {"protocol": "TCP", "endpoint":
                 "127.0.0.1:41301->127.0.0.1:41302"},
                {"protocol": "UDP", "endpoint": "127.0.0.1:41301"},
            ],
        )

    def test_network_gate_rejects_external_peer(self):
        output = (
            "COMMAND PID USER FD TYPE DEVICE SIZE/OFF NODE NAME\n"
            "dolphin 123 user 17u IPv4 0x01 0t0 TCP "
            "127.0.0.1:41301->198.51.100.7:443 (ESTABLISHED)\n"
        )
        with self.assertRaisesRegex(RuntimeError, "non-loopback"):
            _parse_lsof_output(output)

    def test_network_gate_rejects_wildcard_listener(self):
        output = (
            "COMMAND PID USER FD TYPE DEVICE SIZE/OFF NODE NAME\n"
            "dolphin 123 user 17u IPv4 0x01 0t0 UDP *:41301\n"
        )
        with self.assertRaisesRegex(RuntimeError, "not bound to loopback"):
            _parse_lsof_output(output)


if __name__ == "__main__":
    unittest.main()
