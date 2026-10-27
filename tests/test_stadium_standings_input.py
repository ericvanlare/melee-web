"""Actual host-policy controls using labeled modeled transport/state leaves.

These controls establish no original standings outcome or native parity.
"""
import copy
import importlib.util
import os
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
import tempfile
from unittest.mock import patch

ROOT = Path.cwd()
if not (ROOT / 'scripts/agent_workspace.py').is_file():
    ROOT = Path(__file__).resolve().parents[1]
TOOLS = Path(os.environ.get('MELEE_STANDINGS_TOOLS', ROOT / 'tools'))
sys.path[:0] = [str(TOOLS), str(ROOT / 'scripts'), str(ROOT / 'tools'),
                str(ROOT / 'reference-capture/dolphin')]
# Legacy imports may prepend their repository paths. Bind the actual selected
# validator explicitly before importing the driver; never silently test old S.
validator_path = TOOLS / 'stadium_whole_long_idle.py'
validator_spec = importlib.util.spec_from_file_location('stadium_whole_long_idle', validator_path)
validator_module = importlib.util.module_from_spec(validator_spec)
sys.modules['stadium_whole_long_idle'] = validator_module
validator_spec.loader.exec_module(validator_module)
assert Path(validator_module.__file__).resolve() == validator_path.resolve()
SOURCE = Path(os.environ.get('MELEE_STANDINGS_SOURCE',
                             ROOT / 'scripts/capture_sd_reference_prefix.py'))
spec = importlib.util.spec_from_file_location('standings_input_control', SOURCE)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
from retail_input_plan import DISCONNECTED_PAD
from reference_versus_sequence_capture import raw_pad
import stadium_standings_input as policy_module
from stadium_whole_long_idle import LongIdleBObservations


class StandingsInputPolicyTests(unittest.TestCase):
    def operands(self, tick, mode=1, focus=99, ports=None, *, phase=0, form=0):
        seq = 100 + tick * 5
        heads = []
        for slot in (0, 1):
            head = bytearray(0x100)
            head[12] = slot
            heads.append(dict(name='fighter_head', flags=slot, size=0x100, hex=head.hex()))
        row = dict(seq=seq, source_tick=tick, draw_ordinal=tick,
                   payload=dict(match_index=1, slices=heads))
        snapshot = dict(sequence=seq, source_tick=tick, draw_ordinal=tick,
            display=dict(mode_s16=mode, focused_player_s16=focus),
            map2=dict(phase_s16=phase, form_s16=form, gate_mask_80=False))
        sample = dict(sequence=seq-1, source_tick=tick, draw_ordinal=tick, match_index=1,
            ports=(ports or [m.NEUTRAL_PAD]*2) + [DISCONNECTED_PAD]*2,
            errors=[0, 0, -1, -1])
        return row, snapshot, sample

    def observe(self, policy, tick, **kwargs):
        policy.observe(*self.operands(tick, **kwargs), dict(source_tick=84))

    def test_each_mode8_uses_actual_focus_and_requires_consumed_release(self):
        p = m.StadiumStandingsInputPolicy()
        self.observe(p, 84, mode=8, focus=0)
        first = [raw_pad(x=80), m.NEUTRAL_PAD]
        self.assertEqual(p.desired, first)
        p.issue(first)
        self.observe(p, 85, mode=7, focus=0, ports=first)
        self.assertEqual(p.desired, [m.NEUTRAL_PAD]*2)
        p.issue(p.desired)
        self.observe(p, 86, mode=8, focus=1, ports=first)
        self.assertEqual(p.desired, [m.NEUTRAL_PAD]*2)
        self.observe(p, 87, mode=8, focus=1)
        second = [m.NEUTRAL_PAD, raw_pad(x=80)]
        self.assertEqual(p.desired, second)
        p.issue(second)
        self.assertFalse(p.allowed(first))
        self.assertTrue(p.allowed(second))

    def test_invalid_focus_fighter_or_consumed_owner_refuses(self):
        for mutation in ('focus', 'fighter_slot', 'fighter_kind', 'consume_tick',
                         'consume_draw', 'consume_port', 'consume_error', 'consume_sequence'):
            with self.subTest(mutation=mutation):
                p = m.StadiumStandingsInputPolicy()
                row, snapshot, sample = self.operands(84, mode=8, focus=0)
                if mutation == 'focus': snapshot['display']['focused_player_s16'] = 4
                elif mutation.startswith('fighter'):
                    h = bytearray.fromhex(row['payload']['slices'][0]['hex'])
                    h[12 if mutation == 'fighter_slot' else 7] = 1
                    row['payload']['slices'][0]['hex'] = h.hex()
                elif mutation == 'consume_tick': sample['source_tick'] += 1
                elif mutation == 'consume_draw': sample['draw_ordinal'] += 1
                elif mutation == 'consume_port': sample['ports'][1] = raw_pad(x=80)
                elif mutation == 'consume_error': sample['errors'][0] = -1
                else: sample['sequence'] = row['seq']
                with self.assertRaises(ValueError):
                    p.observe(row, snapshot, sample, dict(source_tick=84))
                self.assertIsNone(p.inflight)

    def test_stop_reasons_are_actual_mode_transform_or_inconclusive_cap(self):
        for mode, phase, target, reason in ((14, 0, 84, 'observed_mode14'),
                                           (1, 1, 84, 'first_transform'),
                                           (1, 0, 3584, 'go_plus_3500_cap')):
            p = m.StadiumStandingsInputPolicy()
            # Exercise the actual contiguous cap, no synthetic jump to endpoint.
            for tick in range(84, target + 1):
                self.observe(p, tick, mode=mode if tick == target else 1,
                             phase=phase if tick == target else 0)
            self.assertEqual(p.stop['reason'], reason)
            self.assertEqual(p.stop['result'], 'OBSERVED_PRETRANSFORM_MODE14' if
                             reason == 'observed_mode14' else 'INCONCLUSIVE_PRETRANSFORM')
            self.assertFalse(p.summary()['C3'])
            self.assertEqual(p.desired, [m.NEUTRAL_PAD]*2)
        p = m.StadiumStandingsInputPolicy()
        self.observe(p, 84, mode=14, phase=1)
        self.assertEqual(p.stop['reason'], 'first_transform')

    def test_exact_declared_human_controller_setup(self):
        setup = dict(players=[dict(port=i+1, player_type=0, character_kind=8, stocks=4)
                              for i in range(2)])
        m.StadiumStandingsInputPolicy.validate_setup(setup)
        for key, value in (('port', 3), ('player_type', 1), ('character_kind', 0), ('stocks', 3)):
            bad = copy.deepcopy(setup)
            bad['players'][1][key] = value
            with self.assertRaises(ValueError): m.StadiumStandingsInputPolicy.validate_setup(bad)

    def test_no_unissued_stick_or_input_mutation(self):
        p = m.StadiumStandingsInputPolicy()
        row, state, sample = self.operands(84)
        originals = copy.deepcopy((row, state, sample))
        p.observe(row, state, sample, dict(source_tick=84))
        self.assertEqual((row, state, sample), originals)
        with self.assertRaises(ValueError): p.issue([raw_pad(x=80), m.NEUTRAL_PAD])
        row, state, sample = self.operands(85, ports=[raw_pad(x=80), m.NEUTRAL_PAD])
        with self.assertRaises(ValueError): p.observe(row, state, sample, dict(source_tick=84))

    def test_actual_driver_releases_before_same_route_exit(self):
        p = m.StadiumStandingsInputPolicy()
        self.observe(p, 84, mode=8, focus=1)
        calls = []
        scratch = tempfile.TemporaryDirectory(prefix='standings-issue-control-')
        self.addCleanup(scratch.cleanup)
        receiver = SimpleNamespace(standings_policy=p, match_index=1, ended=False,
            seq=p.last['sequence']+1, standings_issues=[],
            standings_issue_path=Path(scratch.name)/'issues.json',
            selected_standings=lambda index: index == 1)
        active = [m.NEUTRAL_PAD, raw_pad(x=80)]
        pending = [(85, 8, 1, active), (86, 7, 1, active),
                   (87, 14, 1, [m.NEUTRAL_PAD]*2)]
        def next_row():
            tick, mode, focus, ports = pending.pop(0)
            self.observe(p, tick, mode=mode, focus=focus, ports=ports)
            receiver.seq = 101 + tick*5
        class Controller:
            def __init__(self): self.controller = SimpleNamespace(intent_records=0)
            def set_both(self, p1, p2, *, action):
                calls.append((p1, p2, action))
                self.controller.intent_records += 2
        result = m.drive_stadium_standings_input(receiver, next_row, Controller(), float('inf'))
        self.assertEqual(calls[0][:2], tuple(active))
        self.assertTrue(all(x[:2] == (m.NEUTRAL_PAD,)*2 for x in calls[1:]))
        self.assertTrue(result['neutral_release_observed'])
        self.assertEqual(result['stop']['reason'], 'observed_mode14')
        self.assertFalse(pending)

    def issue_operands(self):
        source = dict(sequence=520, source_tick=84, draw_ordinal=84, mode=8, focus=0)
        ports = [raw_pad(x=80), m.NEUTRAL_PAD]
        issue = dict(schema='melee-web-stadium-standings-input-issue-v1', index=0,
                     source=source, after_sequence=520, ports=ports,
                     action='standings-mode8-focused-horizontal', pipe_row_indices=[0, 1])
        pipes = [dict(host_monotonic_ns=i+1, port=i+1, action=issue['action'],
                      intended_pad=ports[i]) for i in range(2)]
        return issue, pipes

    def test_issue_requires_actual_pipe_records_and_exact_source_point(self):
        issue, pipes = self.issue_operands()
        for mutation in ('missing_issue', 'wrong_port', 'wrong_pad', 'missing_source'):
            with self.subTest(mutation=mutation):
                candidate, journal = copy.deepcopy(issue), copy.deepcopy(pipes)
                if mutation == 'missing_source':
                    candidate['source']['focus'] = 1
                    observed = policy_module.StandingsBObservations([candidate], journal)
                    observed.policy.last = dict(issue['source'])
                    observed.policy.desired = issue['ports']
                    row = dict(seq=520, event='boundary', payload=dict(boundary='source_tick', match_index=1))
                    with patch.object(LongIdleBObservations, 'accept', return_value=None):
                        with self.assertRaises(ValueError): observed.accept(row)
                    continue
                if mutation == 'wrong_port': journal[0]['port'] = 2
                elif mutation == 'wrong_pad': journal[1]['intended_pad'] = raw_pad(x=80)
                with self.assertRaises(ValueError):
                    policy_module.StandingsBObservations([] if mutation == 'missing_issue' else [candidate], journal)
        observed = policy_module.StandingsBObservations([issue], pipes)
        observed.policy.last = dict(issue['source'])
        observed.policy.desired = issue['ports']
        with patch.object(LongIdleBObservations, 'accept', return_value=None):
            observed.accept(dict(seq=520, event='boundary',
                                 payload=dict(boundary='source_tick', match_index=1)))
        self.assertEqual(observed.policy.inflight, issue['ports'])
        self.assertEqual(observed.issue_cursor, 1)

    def test_default_idle_hooks_still_refuse_stick_and_transform(self):
        idle = LongIdleBObservations()
        with patch('stadium_whole_long_idle._consumed_ports',
                   return_value=[raw_pad(x=80), m.NEUTRAL_PAD]+[DISCONNECTED_PAD]*2):
            with self.assertRaises(ValueError): idle.validate_consumed_pad(dict(seq=1))
        row, state, sample = self.operands(84, phase=1)
        idle.owner = ()
        idle.countdown = 3600
        idle.samples = 0
        idle.map2_history = dict(state['map2'])
        with self.assertRaises(ValueError): idle.validate_map_state(row, state, (), sample)

    def test_standings_transform_stop_retains_neutral_structural_suffix(self):
        observed = policy_module.StandingsBObservations([], [])
        observed.owner = ()
        observed.go = dict(source_tick=84)
        row, state, sample = self.operands(84, phase=1)
        consumed = dict(seq=sample['sequence'], source_tick=84, draw_ordinal=84,
                        payload=dict(match_index=1))
        with patch.object(policy_module, '_consumed_ports', return_value=sample['ports']):
            observed.validate_map_state(row, state, (), consumed)
        self.assertEqual(observed.policy.stop['reason'], 'first_transform')
        self.assertEqual(observed.policy.stop['result'], 'INCONCLUSIVE_PRETRANSFORM')
        row, state, sample = self.operands(85, phase=2)
        consumed.update(seq=sample['sequence'], source_tick=85, draw_ordinal=85)
        with patch.object(policy_module, '_consumed_ports', return_value=sample['ports']):
            observed.validate_map_state(row, state, (), consumed)
        with patch.object(policy_module, '_consumed_ports',
                          return_value=[raw_pad(x=80), m.NEUTRAL_PAD]+[DISCONNECTED_PAD]*2):
            with self.assertRaises(ValueError): observed.validate_consumed_pad(dict(seq=1))
        self.assertFalse(observed.policy.summary()['C3'])
        # _consumed_ports validates queue ownership, not signed PAD error.
        # The new hook derives the literal signed byte, never supplies success.
        invalid = [m.NEUTRAL_PAD[:-2]+'fe', m.NEUTRAL_PAD]+[DISCONNECTED_PAD]*2
        with patch.object(policy_module, '_consumed_ports', return_value=invalid), \
             patch.object(observed.policy, 'observe', wraps=observed.policy.observe) as checked:
            with self.assertRaises(ValueError): observed.validate_map_state(row, state, (), consumed)
            self.assertEqual(checked.call_args.args[2]['errors'], [-2, 0, -1, -1])


if __name__ == '__main__': unittest.main()
