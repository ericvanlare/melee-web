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
policy_path = TOOLS / 'stadium_standings_input.py'
policy_spec = importlib.util.spec_from_file_location('stadium_standings_input', policy_path)
policy_module = importlib.util.module_from_spec(policy_spec)
sys.modules['stadium_standings_input'] = policy_module
policy_spec.loader.exec_module(policy_module)
assert Path(policy_module.__file__).resolve() == policy_path.resolve()
SOURCE = Path(os.environ.get('MELEE_STANDINGS_SOURCE',
                             ROOT / 'scripts/capture_sd_reference_prefix.py'))
spec = importlib.util.spec_from_file_location('standings_input_control', SOURCE)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
from retail_input_plan import DISCONNECTED_PAD
from reference_versus_sequence_capture import raw_pad
from stadium_whole_long_idle import LongIdleBObservations
import stadium_go_prefix


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

    def observe(self, policy, tick, *, first_form=None, **kwargs):
        row, snapshot, sample = self.operands(tick, **kwargs)
        if first_form is not None:
            snapshot['first_form'] = dict(first_form)
        policy.observe(row, snapshot, sample, dict(source_tick=84))

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

    def test_post_transform_profile_crosses_old_cap_and_requires_mode14_after_transform(self):
        profile = policy_module.StadiumStandingsInputPolicy.POST_TRANSFORM_PROFILE
        p = policy_module.StadiumStandingsInputPolicy(profile)
        self.observe(p, 84, mode=1)
        self.observe(p, 85, mode=1, phase=1)
        self.assertIsNone(p.stop)
        self.assertEqual(p.first_transform['source_tick'], 85)
        for tick in range(86, 3585):
            self.observe(p, tick, mode=1, phase=1)
        self.assertIsNone(p.stop)  # GO+3500 is only the unchanged native window.
        self.observe(p, 3585, mode=14, phase=1)
        self.assertEqual(p.stop['reason'], 'observed_post_transform_mode14')
        self.assertEqual(p.mode14_witness['source_tick'], 3585)
        self.assertEqual(p.mode14_witness['consumed_pad']['ports'],
                         [m.NEUTRAL_PAD, m.NEUTRAL_PAD,
                          DISCONNECTED_PAD, DISCONNECTED_PAD])
        result = p.summary()
        self.assertEqual(result['profile'], profile)
        self.assertEqual(result['event_stop'], 'observed_post_transform_mode14')
        self.assertFalse(result['C3'])

    def test_post_transform_profile_does_not_accept_pretransform_mode14(self):
        p = policy_module.StadiumStandingsInputPolicy(
            policy_module.StadiumStandingsInputPolicy.POST_TRANSFORM_PROFILE)
        self.observe(p, 84, mode=14)
        self.assertEqual(p.stop['reason'], 'mode14_before_transform')
        self.assertEqual(p.stop['result'], 'INCONCLUSIVE_MODE14_BEFORE_TRANSFORM')
        self.assertFalse(p.summary()['C3'])

    def _first_form_point(self, phase, selected_form, *, active_map_id=None, replaced=None):
        return dict(profile='first_form_v1', phase=phase,
            active_map_id=(selected_form if phase >= 6 else 5)
                if active_map_id is None else active_map_id,
            selected_form=selected_form,
            root_replacement_observed=(phase >= 6) if replaced is None else replaced)

    def test_first_form_latency_mode14_first_forces_fresh_neutral_and_waits_for_settlement(self):
        profile = policy_module.StadiumStandingsInputPolicy.FIRST_FORM_LATENCY_PROFILE
        p = policy_module.StadiumStandingsInputPolicy(profile)
        neutral = [m.NEUTRAL_PAD, m.NEUTRAL_PAD]
        self.observe(p, 84, mode=14, focus=0, phase=0, form=5)
        self.assertEqual(p.neutralization_trigger['kind'], 'pretransform_mode14')
        self.assertEqual(p.mode14_witness['source_tick'], 84)
        self.assertTrue(p.mode14_witness['before_first_transform'])
        self.assertIsNone(p.stop)  # mode14 is an input trigger, not a host exit.
        self.assertTrue(p.neutralization_issue_requested)
        self.assertTrue(p.requires_issue(neutral))  # write even if last desired value was neutral.
        p.issue(neutral)
        self.assertFalse(p.neutralization_issue_requested)
        self.assertEqual(p.neutralization_issue['sequence'], p.neutralization_trigger['sequence'])

        phases = ((85, 0, 5, False), (86, 1, 6, False), (87, 2, 6, False),
                  (88, 3, 6, False), (89, 4, 6, False), (90, 5, 6, False),
                  (91, 6, 6, True), (92, 0, 6, True))
        for tick, phase, form, replaced in phases:
            self.observe(p, tick, mode=14, focus=0, phase=phase, form=form,
                ports=neutral, first_form=dict(profile='first_form_v1', phase=phase,
                    active_map_id=form if replaced else 5, selected_form=form,
                    root_replacement_observed=replaced))
            if phase != 0 or not replaced:
                self.assertIsNone(p.stop)
        self.assertEqual(p.stop['reason'], 'first_form_phase6_to_settled_phase0')
        summary = p.summary()
        self.assertEqual(summary['profile'], profile)
        self.assertEqual(summary['neutralization_trigger']['kind'], 'pretransform_mode14')
        self.assertEqual(summary['neutralization_release']['ports'],
                         [m.NEUTRAL_PAD, m.NEUTRAL_PAD,
                          DISCONNECTED_PAD, DISCONNECTED_PAD])
        self.assertEqual(summary['first_form_latency']['phase1_source_tick_dwell'], 1)
        self.assertFalse(summary['C4'])
        self.assertFalse(summary['C3'])

    def test_first_form_latency_transform_first_needs_no_mode14_witness(self):
        p = policy_module.StadiumStandingsInputPolicy(
            policy_module.StadiumStandingsInputPolicy.FIRST_FORM_LATENCY_PROFILE)
        focused = [raw_pad(x=80), m.NEUTRAL_PAD]
        self.observe(p, 84, mode=8, focus=0, phase=0, form=5)
        p.issue(focused)
        self.observe(p, 85, mode=1, phase=1, form=5, ports=focused,
            first_form=self._first_form_point(1, 6, active_map_id=5, replaced=False))
        self.assertEqual(p.neutralization_trigger['kind'], 'first_transformation')
        self.assertIsNone(p.mode14_witness)
        self.assertTrue(p.requires_issue(focused))
        p.issue([m.NEUTRAL_PAD, m.NEUTRAL_PAD])
        phases = ((86, 2, 6, 5, False), (87, 3, 6, 5, False),
                  (88, 4, 6, 5, False), (89, 5, 6, 5, False),
                  (90, 6, 6, 6, True), (91, 0, 6, 6, True))
        for tick, phase, selected, active, replaced in phases:
            self.observe(p, tick, mode=14 if tick == 86 else 1,
                focus=0 if tick == 86 else 99, phase=1, form=5,
                first_form=self._first_form_point(phase, selected,
                    active_map_id=active, replaced=replaced))
        self.assertEqual(p.stop['reason'], 'first_form_phase6_to_settled_phase0')
        result = p.summary()
        self.assertEqual(result['mode14_witness']['source_tick'], 86)
        self.assertFalse(result['mode14_witness']['before_first_transform'])
        self.assertEqual(result['neutralization_trigger']['kind'], 'first_transformation')
        self.assertLessEqual(result['neutralization_trigger']['sequence'],
                             result['neutralization_issue']['sequence'])
        self.assertLess(result['neutralization_release']['sequence'],
                        result['first_form_latency']['settled_phase0']['sequence'])

    def test_first_form_latency_same_row_trigger_does_not_claim_event_order(self):
        p = policy_module.StadiumStandingsInputPolicy(
            policy_module.StadiumStandingsInputPolicy.FIRST_FORM_LATENCY_PROFILE)
        self.observe(p, 84, mode=14, focus=0, phase=1, form=5,
            first_form=self._first_form_point(1, 6, active_map_id=5, replaced=False))
        self.assertEqual(p.neutralization_trigger['kind'],
                         'mode14_and_first_transform_same_source_row')
        self.assertFalse(p.mode14_witness['before_first_transform'])
        p.issue([m.NEUTRAL_PAD, m.NEUTRAL_PAD])
        self.assertIsNone(p.stop)

    def test_first_form_latency_issue_records_latest_source_after_trigger(self):
        p = policy_module.StadiumStandingsInputPolicy(
            policy_module.StadiumStandingsInputPolicy.FIRST_FORM_LATENCY_PROFILE)
        neutral = [m.NEUTRAL_PAD, m.NEUTRAL_PAD]
        self.observe(p, 84, mode=14, focus=0, phase=0, form=5)
        trigger_sequence = p.neutralization_trigger['sequence']
        # Model the source reader returning another complete row before the
        # Pipe owner handles the latched issue; do not relabel the write at the
        # older trigger coordinate.
        self.observe(p, 85, mode=1, focus=99, phase=0, form=5)
        p.issue(neutral)
        self.assertGreater(p.neutralization_issue['sequence'], trigger_sequence)
        self.assertEqual(p.neutralization_issue['source_tick'], 85)
        self.assertEqual(p.neutralization_issue['trigger_sequence'], trigger_sequence)

    def test_first_form_latency_neutral_release_requires_exact_four_ports(self):
        p = policy_module.StadiumStandingsInputPolicy(
            policy_module.StadiumStandingsInputPolicy.FIRST_FORM_LATENCY_PROFILE)
        neutral = [m.NEUTRAL_PAD, m.NEUTRAL_PAD]
        self.observe(p, 84, mode=14, focus=0, phase=0, form=5)
        p.issue(neutral)
        row, snapshot, sample = self.operands(85, mode=1, focus=0,
                                               phase=0, form=5)
        sample['ports'][2] = m.NEUTRAL_PAD
        with self.assertRaisesRegex(ValueError, 'exact four-port consumed input'):
            p.observe(row, snapshot, sample, dict(source_tick=84))
        self.assertIsNone(p.neutralization_release)

    def test_first_form_latency_wrong_form_or_root_cannot_settle(self):
        p = policy_module.StadiumStandingsInputPolicy(
            policy_module.StadiumStandingsInputPolicy.FIRST_FORM_LATENCY_PROFILE)
        self.observe(p, 84, mode=1, phase=1, form=5,
            first_form=self._first_form_point(1, 6, active_map_id=5, replaced=False))
        p.issue([m.NEUTRAL_PAD, m.NEUTRAL_PAD])
        row, snapshot, sample = self.operands(85, mode=1, phase=1, form=5,
            ports=[m.NEUTRAL_PAD, m.NEUTRAL_PAD])
        snapshot['first_form'] = self._first_form_point(2, 4, active_map_id=5, replaced=False)
        with self.assertRaisesRegex(ValueError, 'selected form changed'):
            p.observe(row, snapshot, sample, dict(source_tick=84))

        q = policy_module.StadiumStandingsInputPolicy(
            policy_module.StadiumStandingsInputPolicy.FIRST_FORM_LATENCY_PROFILE)
        self.observe(q, 84, mode=1, phase=1, form=5,
            first_form=self._first_form_point(1, 6, active_map_id=5, replaced=False))
        q.issue([m.NEUTRAL_PAD, m.NEUTRAL_PAD])
        row, snapshot, sample = self.operands(85, mode=1, phase=1, form=5,
            ports=[m.NEUTRAL_PAD, m.NEUTRAL_PAD])
        snapshot['first_form'] = self._first_form_point(6, 6, active_map_id=6, replaced=False)
        with self.assertRaisesRegex(ValueError, 'form/root phase relation'):
            q.observe(row, snapshot, sample, dict(source_tick=84))

    def test_first_form_latency_missing_or_late_neutral_consume_never_settles(self):
        p = policy_module.StadiumStandingsInputPolicy(
            policy_module.StadiumStandingsInputPolicy.FIRST_FORM_LATENCY_PROFILE)
        focused = [raw_pad(x=80), m.NEUTRAL_PAD]
        self.observe(p, 84, mode=8, focus=0, phase=0, form=5)
        p.issue(focused)
        self.observe(p, 85, mode=1, phase=1, form=5, ports=focused,
            first_form=self._first_form_point(1, 6, active_map_id=5, replaced=False))
        p.issue([m.NEUTRAL_PAD, m.NEUTRAL_PAD])
        phases = ((86, 2, 6, 5, False), (87, 3, 6, 5, False),
                  (88, 4, 6, 5, False), (89, 5, 6, 5, False),
                  (90, 6, 6, 6, True), (91, 0, 6, 6, True))
        for tick, phase, selected, active, replaced in phases:
            self.observe(p, tick, mode=1, focus=99, phase=1, form=5,
                ports=focused, first_form=self._first_form_point(phase, selected,
                    active_map_id=active, replaced=replaced))
        self.assertIsNone(p.neutralization_release)
        self.assertIsNone(p.stop)
        with self.assertRaisesRegex(ValueError, 'evidence stop/release has not settled'):
            p.summary()
        # A neutral consumed only after phase6->0 is too late for this record.
        with self.assertRaisesRegex(ValueError, 'ordered phase1/phase6/root-settlement'):
            self.observe(p, 92, mode=1, focus=99, phase=1, form=5,
                ports=[m.NEUTRAL_PAD, m.NEUTRAL_PAD],
                first_form=self._first_form_point(0, 6, active_map_id=6, replaced=True))
        self.assertIsNotNone(p.neutralization_release)
        self.assertIsNone(p.stop)
        with self.assertRaisesRegex(ValueError, 'evidence stop/release has not settled'):
            p.summary()

    def test_first_form_latency_timeout_cannot_be_summarized_as_a_sample(self):
        p = policy_module.StadiumStandingsInputPolicy(
            policy_module.StadiumStandingsInputPolicy.FIRST_FORM_LATENCY_PROFILE)
        self.observe(p, 84, mode=14, focus=0, phase=0, form=5)
        with self.assertRaisesRegex(ValueError, 'evidence stop/release has not settled'):
            p.summary()

    def test_first_form_latency_requires_phase6_then_settled_phase0(self):
        p = policy_module.StadiumStandingsInputPolicy(
            policy_module.StadiumStandingsInputPolicy.FIRST_FORM_LATENCY_PROFILE)
        neutral = [m.NEUTRAL_PAD, m.NEUTRAL_PAD]
        self.observe(p, 84, mode=14, focus=0, phase=0, form=5)
        p.issue(neutral)
        for tick, phase, form, replaced in ((85, 0, 5, False),
                                             (86, 1, 6, False),
                                             (87, 2, 6, False),
                                             (88, 3, 6, False),
                                             (89, 4, 6, False),
                                             (90, 5, 6, False)):
            self.observe(p, tick, mode=14, focus=0, phase=phase, form=form,
                ports=neutral, first_form=dict(profile='first_form_v1', phase=phase,
                    active_map_id=5, selected_form=form,
                    root_replacement_observed=replaced))
        self.assertIsNone(p.stop)
        with self.assertRaisesRegex(ValueError, 'evidence stop/release has not settled'):
            p.summary()

    def test_post_window_raw_pad_must_pair_to_next_contiguous_source_tick(self):
        profile = policy_module.StadiumStandingsInputPolicy.POST_TRANSFORM_PROFILE

        def receiver():
            observed = policy_module.StandingsBObservations([], [], profile=profile)
            observed.closed = True
            observed.previous = 1000
            observed.last_tick = 3500
            observed.post_window_tick = 3500
            observed.go = dict(source_tick=84)
            observed.policy.go_tick = 84
            observed.policy.initial_form = 0
            observed.policy.last = dict(source_tick=3500, mode=1, focus=99)
            observed.owner = ((67, 1, '00000001'), (68, 1, '00000002'))
            observed.post_window_expected_draw = 451
            return observed

        consumed = dict(seq=1001, event='boundary', source_tick=3501, draw_ordinal=451,
            payload=dict(boundary='pad_consume', whole_session=True, match_index=1,
                         audio_owner_epoch=1,
                         pc=0x80377584, gprs=[0]*32))
        state_row = dict(seq=1002, event='boundary', source_tick=3501, draw_ordinal=451,
            payload=dict(boundary='source_tick', whole_session=True, match_index=1,
                         audio_owner_epoch=1,
                         pc=stadium_go_prefix.SOURCE_TICK_PC, gprs=[0]*32,
                         slices=[dict(tag=67, flags=1, hex='00000001'),
                                 dict(tag=68, flags=1, hex='00000002')]))
        def add_boundary_slices(row):
            tick = row['source_tick']
            rng_pointer, camera, camera_object = 0x80401000, 0x80402000, 0x80403000
            specs = [(14, 46, 0x8046b6a0, bytes(46)),
                     (21, 856, 0x804c1f84, bytes(856)),
                     (30, 4, 0x80479d58, tick.to_bytes(4, 'big')),
                     (19, 4, 0x804d5f94, rng_pointer.to_bytes(4, 'big')),
                     (20, 4, rng_pointer, bytes.fromhex('12345678')),
                     (10, 76, 0x80452c68, camera.to_bytes(4, 'big') + bytes(72)),
                     (28, 4, camera + 0x28, camera_object.to_bytes(4, 'big')),
                     (11, 81, camera_object, bytes(81))]
            row['payload']['slices'].extend(dict(tag=tag, flags=0, size=size,
                address=address, hex=raw.hex()) for tag, size, address, raw in specs)
            return row
        add_boundary_slices(state_row)
        snapshot = dict(sequence=1002, source_tick=3501, draw_ordinal=451,
            display=dict(mode_s16=14, focused_player_s16=0),
            map2=dict(phase_s16=1, gate_mask_80=False, form_s16=0))
        literal = [m.NEUTRAL_PAD, m.NEUTRAL_PAD, DISCONNECTED_PAD, DISCONNECTED_PAD]
        accepted = receiver()
        with patch.object(policy_module, '_consumed_ports', return_value=literal), \
             patch('stadium_go_prefix.StadiumFirstFormObservations.accept', return_value=snapshot):
            accepted.accept(consumed)
            accepted.accept(state_row)
        self.assertEqual(accepted.post_window_ticks, 1)
        self.assertTrue(accepted.post_window_complete)
        self.assertEqual(accepted.policy.stop['reason'], 'observed_post_transform_mode14')

        rejected = receiver()
        wrong_tick = dict(state_row, source_tick=3502)
        wrong_tick['payload'] = copy.deepcopy(state_row['payload'])
        next(x for x in wrong_tick['payload']['slices'] if x['tag'] == 30)['hex'] = (3502).to_bytes(4, 'big').hex()
        with patch.object(policy_module, '_consumed_ports', return_value=literal), \
             patch('stadium_go_prefix.StadiumFirstFormObservations.accept', return_value=snapshot):
            rejected.accept(consumed)
            with self.assertRaises(ValueError):
                rejected.accept(wrong_tick)

        changed_root = receiver()
        changed_root_row = copy.deepcopy(state_row)
        changed_root_row['payload']['slices'][0]['hex'] = '00000003'
        with patch.object(policy_module, '_consumed_ports', return_value=literal), \
             patch('stadium_go_prefix.StadiumFirstFormObservations.accept', return_value=snapshot):
            changed_root.accept(consumed)
            with self.assertRaisesRegex(ValueError, 'map root owner changed'):
                changed_root.accept(changed_root_row)

    def test_post_window_source_draw_order_and_root_owner(self):
        profile = policy_module.StadiumStandingsInputPolicy.POST_TRANSFORM_PROFILE
        observed = policy_module.StandingsBObservations([], [], profile=profile)
        observed.closed = True
        observed.previous = 1000
        observed.last_tick = 3500
        observed.post_window_tick = 3500
        observed.post_window_expected_draw = 451
        observed.go = dict(source_tick=84)
        observed.policy.go_tick = 84
        observed.policy.initial_form = 0
        observed.policy.last = dict(source_tick=3500, mode=1, focus=99)
        observed.owner = ((67, 1, '00000001'), (68, 1, '00000002'))

        def add_source_slices(row):
            tick = row['source_tick']
            rng_pointer, camera, camera_object = 0x80401000, 0x80402000, 0x80403000
            specs = [(14, 46, 0x8046b6a0, bytes(46)),
                     (21, 856, 0x804c1f84, bytes(856)),
                     (30, 4, 0x80479d58, tick.to_bytes(4, 'big')),
                     (19, 4, 0x804d5f94, rng_pointer.to_bytes(4, 'big')),
                     (20, 4, rng_pointer, bytes.fromhex('12345678')),
                     (10, 76, 0x80452c68, camera.to_bytes(4, 'big') + bytes(72)),
                     (28, 4, camera + 0x28, camera_object.to_bytes(4, 'big')),
                     (11, 81, camera_object, bytes(81))]
            row['payload']['slices'].extend(dict(tag=tag, flags=0, size=size,
                address=address, hex=raw.hex()) for tag, size, address, raw in specs)
            return row

        def tick_row(seq, tick, draw):
            row = dict(seq=seq, event='boundary', source_tick=tick, draw_ordinal=draw,
                payload=dict(boundary='source_tick', whole_session=True, match_index=1,
                    audio_owner_epoch=1,
                    pc=stadium_go_prefix.SOURCE_TICK_PC, gprs=[0]*32,
                    slices=[dict(tag=67, flags=1, size=4, address=0x8049e9cc, hex='00000001'),
                            dict(tag=68, flags=1, size=4, address=0x8049e9cc, hex='00000002')]))
            # Raw root bytes are compared below; this lower-bound test checks
            # the existing SourceTick/RNG/camera identity decoder as well.
            add_source_slices(row)
            return row

        def consume_row(seq, tick, draw):
            return dict(seq=seq, event='boundary', source_tick=tick, draw_ordinal=draw,
                payload=dict(boundary='pad_consume', whole_session=True, match_index=1,
                             audio_owner_epoch=1,
                             pc=0x80377584, gprs=[0]*32))

        def draw_row(seq, kind, tick, draw):
            return dict(seq=seq, event='boundary', source_tick=tick, draw_ordinal=draw,
                payload=dict(boundary=kind, whole_session=True, match_index=1,
                    audio_owner_epoch=1,
                    pc=0x80390fc0 if kind == 'draw_enter' else 0x80391040,
                    gprs=[0]*32, slices=[dict(tag=1)]))

        snapshots = {
            3501: dict(sequence=1002, source_tick=3501, draw_ordinal=451,
                display=dict(mode_s16=1, focused_player_s16=99),
                map2=dict(phase_s16=1, gate_mask_80=False, form_s16=0)),
            3502: dict(sequence=1006, source_tick=3502, draw_ordinal=452,
                display=dict(mode_s16=14, focused_player_s16=0),
                map2=dict(phase_s16=1, gate_mask_80=False, form_s16=0)),
        }
        literal = [m.NEUTRAL_PAD, m.NEUTRAL_PAD, DISCONNECTED_PAD, DISCONNECTED_PAD]
        with patch.object(policy_module, '_consumed_ports', return_value=literal), \
             patch('stadium_go_prefix.StadiumFirstFormObservations.accept',
                   side_effect=lambda row, owner=None: snapshots[row['source_tick']]):
            observed.accept(consume_row(1001, 3501, 451))
            observed.accept(tick_row(1002, 3501, 451))
            observed.accept(draw_row(1003, 'draw_enter', 3502, 451))
            wrong_return = draw_row(1004, 'draw_return', 3502, 452)
            with self.assertRaisesRegex(ValueError, 'draw ordinal'):
                observed.accept(wrong_return)

        # A failed draw must leave the stream state untouched; use a fresh
        # receiver to prove the actual ordered source/draw bracket succeeds.
        observed = policy_module.StandingsBObservations([], [], profile=profile)
        observed.closed = True
        observed.previous = 1000
        observed.last_tick = 3500
        observed.post_window_tick = 3500
        observed.post_window_expected_draw = 451
        observed.go = dict(source_tick=84)
        observed.policy.go_tick = 84
        observed.policy.initial_form = 0
        observed.policy.last = dict(source_tick=3500, mode=1, focus=99)
        observed.owner = ((67, 1, '00000001'), (68, 1, '00000002'))
        with patch.object(policy_module, '_consumed_ports', return_value=literal), \
             patch('stadium_go_prefix.StadiumFirstFormObservations.accept',
                   side_effect=lambda row, owner=None: snapshots[row['source_tick']]):
            observed.accept(consume_row(1001, 3501, 451))
            observed.accept(tick_row(1002, 3501, 451))
            observed.accept(draw_row(1003, 'draw_enter', 3502, 451))
            observed.accept(draw_row(1004, 'draw_return', 3502, 451))
            observed.accept(consume_row(1005, 3502, 452))
            observed.accept(tick_row(1006, 3502, 452))
        self.assertTrue(observed.post_window_complete)
        self.assertEqual(observed.post_window_expected_draw, 452)

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

    def test_final_c_uses_saved_closed_b_window_without_relabeling(self):
        window = dict(schema='melee-web-stadium-whole-long-idle-window-v1',
            profile='selected_b_3500_v1', match_index=1, comparison_post_go_ticks=3500,
            comparison_typed_state_count=3501, typed_state_count_including_natural_tail=3501,
            four_port_consumed_pad_count=3501, go=dict(sequence=10),
            target_source_tick=dict(sequence=100), first_later_natural_draw_return=dict(sequence=101))
        class FinalC:
            match_index = 2
            long_idle_window_result = window
            def long_idle_window_summary(self):
                raise AssertionError('post-C live summary must not relabel B')
        receiver = FinalC()
        self.assertIs(m.saved_stadium_b_long_idle_window(receiver), window)
        self.assertEqual(window['match_index'], 1)
        for change in ({'match_index': 2}, {'profile': 'wrong'},
                       {'typed_state_count_including_natural_tail': 3500},
                       {'four_port_consumed_pad_count': 3500}, {'go': None}):
            receiver.long_idle_window_result = dict(window, **change)
            with self.assertRaises(ValueError): m.saved_stadium_b_long_idle_window(receiver)


if __name__ == '__main__': unittest.main()
