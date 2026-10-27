"""Synthetic controls for the strict natural-zero-stock witness only.

These exercise the reducer fed by actual MWRO SourceTick records; they are not
an original runtime result or a stockout gameplay success claim.
"""
import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

S = Path(__file__).resolve().parents[1]
if not (S / 'scripts' / 'agent_workspace.py').is_file():
    S = Path.cwd()
if not (S / 'scripts' / 'agent_workspace.py').is_file():
    raise RuntimeError('stockout fixture must run with the authorized Stadium checkout as its working directory')
sys.path.insert(0, str(S / 'scripts'))
sys.path.insert(0, str(S / 'tools'))
MODULE = Path(__file__).resolve().parents[1] / 'scripts' / 'capture_sd_reference_prefix.py'
spec = importlib.util.spec_from_file_location('stockout_candidate', MODULE)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class NaturalStockoutWitnessTests(unittest.TestCase):
    def test_driver_leaves_observed_platform_then_requires_consumed_fall_and_landing(self):
        # Exercise the real driver against a modeled transport. The retained
        # original failure supplies the 0 -> 12 -> 13 respawn sequence; this
        # control is input-policy evidence, not a successful original run.
        for missing_consumed_stick in (False, True):
            with self.subTest(missing_consumed_stick=missing_consumed_stick):
                neutral = module.NEUTRAL_PAD
                witness = SimpleNamespace(last_stocks=(4, 4), last_tick=179,
                    last_fighters=self.fighters(4), transitions=[], mode9=None)
                receiver = SimpleNamespace(stockout_witness=witness, seq=100,
                    selected_stockout=lambda index: index == 1, ended=False,
                    last_pad=[neutral, neutral], outcome_seen=False, stockout_summary=None)
                pending, calls = [], []

                def next_row():
                    self.assertTrue(pending, 'driver waited without a source/input transition')
                    state = pending.pop(0)
                    receiver.seq += 5
                    witness.last_tick += 1
                    if state == 'exit':
                        witness.mode9 = {'mode': 9}
                        receiver.outcome_seen = True
                        receiver.stockout_summary = {'result_outcome': 2}
                        return
                    stock, motion, air, pad = state
                    if stock != witness.last_stocks[0]:
                        witness.transitions.append({'from_stocks': witness.last_stocks,
                                                    'to_stocks': (stock, 4)})
                    witness.last_stocks = (stock, 4)
                    witness.last_fighters = dict(stocks=(stock, 4), motions=(motion, 14),
                                                 ground_air=(air, 0))
                    receiver.last_pad = [pad, neutral]

                class Controller:
                    def hold(self, p1, p2, seconds, *, action):
                        calls.append(action)
                        if action.endswith('-jump'):
                            stock = witness.last_stocks[0] - 1
                            pending.append((stock, 0, 1, neutral))
                            pending.extend([(stock, 12, 1, neutral), (stock, 13, 1, neutral)]
                                           if stock else ['exit'])

                    def set_both(self, p1, p2, *, action):
                        calls.append(action)
                        if action.endswith('-leave-platform'):
                            pending.append((witness.last_stocks[0], 29, 1,
                                            neutral if missing_consumed_stick else p1))
                        elif action.endswith('-leave-platform:release'):
                            pending.append((witness.last_stocks[0], 14, 0, neutral))

                if missing_consumed_stick:
                    with self.assertRaisesRegex(module.SdDiagnosticError, 'consumed stick and source Fall'):
                        module.drive_stadium_natural_stockout(receiver, next_row, Controller(),
                                                              module.time.monotonic() + 1)
                    self.assertTrue(calls[-1].endswith('-leave-platform:release'))
                else:
                    result = module.drive_stadium_natural_stockout(receiver, next_row, Controller(),
                                                                  module.time.monotonic() + 1)
                    self.assertEqual(result, {'result_outcome': 2})
                    self.assertEqual(sum(x.endswith('-leave-platform') for x in calls), 3)
                    self.assertEqual(sum(x.endswith('-leave-platform:release') for x in calls), 3)
                    self.assertEqual([x['to_stocks'][0] for x in witness.transitions], [3, 2, 1, 0])

    @staticmethod
    def row(sequence, tick):
        return {'event': 'boundary', 'seq': sequence, 'source_tick': tick,
                'payload': {'boundary': 'source_tick'}}

    @staticmethod
    def fighters(p1, p2=4):
        return {'stocks': (p1, p2), 'motions': (14, 14), 'ground_air': (0, 0)}

    def valid_prefix(self):
        witness = module.StadiumNaturalStockoutWitness()
        witness.begin(84)
        for tick, stock in enumerate((4, 3, 2, 1, 0), 84):
            witness.observe(self.row(100 + tick, tick), self.fighters(stock), 8)
        return witness

    def test_actual_feedback_receiver_binds_first_row_to_actual_go_tick(self):
        receiver = object.__new__(module.StadiumWholeFeedbackReceiver)
        receiver.stadium_whole_b_state = True
        receiver.b_state_matches = (1,)
        receiver.stadium_whole_stockout = True
        receiver.stadium_whole_long_idle = False
        receiver.stadium_whole_paired_idle = False
        receiver.match_index = 1
        receiver.b_entry_seen = True
        receiver.setup_seen = True
        receiver.b_go = None
        receiver.b_constructor = SimpleNamespace(finish=lambda: None)
        receiver.stockout_witness = module.StadiumNaturalStockoutWitness()
        receiver.progress_scope = 'stadium_whole3_feedback_v1'
        receiver.receipt = 'a' * 64
        row = {'event': 'progress', 'seq': 21767, 'source_tick': 84, 'draw_ordinal': 84,
               'payload': {'stadium_b_state_observation': True, 'phase': 'go_after'}}
        with patch('stadium_go_prefix._validate_progress', return_value=row['payload']), \
             patch('stadium_go_prefix._validate_stadium_idle_state_record',
                   return_value={'source_tick': 84}):
            receiver.accept_b_progress(row)
        self.assertEqual(receiver.stockout_witness.expected_start_tick, 84)
        receiver.stockout_witness.observe(self.row(21768, 84), self.fighters(4), 8)

    def test_fighter_projection_ignores_other_validated_slice_flags(self):
        head = bytearray(0x100)
        head[0x10:0x14] = (14).to_bytes(4, 'big')
        heads = [dict(name='fighter_head', flags=slot, size=0x100,
                      hex=bytes(head).hex()) for slot in (0, 1)]
        stocks = [dict(name='fighter_stocks', flags=slot, size=1, hex='04')
                  for slot in (0, 1)]
        row = {'event': 'boundary', 'seq': 21768, 'source_tick': 84,
               'payload': {'boundary': 'source_tick', 'slices': [
                   *heads, dict(name='map_display', flags=2, size=2, hex='0000'),
                   *stocks, dict(name='map_display', flags=5, size=2, hex='0000') ]}}
        state = module._stockout_source_fighter_state(row)
        self.assertEqual(state['stocks'], (4, 4))
        self.assertEqual(state['motions'], (14, 14))
        self.assertEqual(state['ground_air'], (0, 0))
        witness = module.StadiumNaturalStockoutWitness()
        witness.begin(84)
        witness.observe(row, state, 8)

    def test_actual_receiver_closes_gameplay_pairing_at_validated_exit_only(self):
        # Model only the decoded PAD leaf and already-observed stock witness;
        # execute the real receiver's tick/exit/Results control flow.
        receiver = object.__new__(module.StadiumWholeFeedbackReceiver)
        receiver.__dict__.update(ended=False, seq=190, announcements=['handshake', 'start'],
            match_index=1, stadium_whole_stockout=True, stadium_whole_b_state=False,
            stadium_whole_long_idle=False, stadium_whole_paired_idle=False,
            b_state_matches=(1,), boundaries=[], long_idle_window=None, setup_seen=True, last_pad=None,
            pending_neutral_sample=None, chord_requested=False, outcome_seen=False,
            start_allowed=False, previous_tick=88, tick_count=5, consumed_samples=[],
            menu_consumed=0, exit_requested=False, pause_consumed=None,
            stockout_witness=self.valid_prefix(), stockout_summary=None)
        receiver.stockout_witness.observe(self.row(189, 89), self.fighters(0), 9)
        from reference_versus_sequence_capture import raw_pad
        from retail_input_plan import DISCONNECTED_PAD
        neutral = module.NEUTRAL_PAD
        ports = [neutral, neutral, DISCONNECTED_PAD, DISCONNECTED_PAD]
        def row(name, tick=89, **payload):
            return {'event': 'boundary', 'seq': receiver.seq, 'source_tick': tick,
                    'payload': dict(boundary=name, whole_session=True, match_index=1, **payload)}
        def consume(p1=neutral, p2=neutral, errors=None):
            decoded = ([p1, p2, *ports[2:]], errors or [0, 0, -1, -1], {})
            with patch.object(module, '_transform_consumed_ports', return_value=decoded):
                receiver.accept(row('pad_consume', pc=1, lr=2, gprs=[]))
        consume()
        receiver.accept(row('source_tick'))
        self.assertIsNone(receiver.pending_neutral_sample)
        raw = bytearray(0x28); raw[4] = 2
        receiver.accept(row('vs_exit_return', slices=[dict(name='result', tag=15,
            flags=0, address=0x80479da4, size=0x28, hex=raw.hex())]))
        self.assertTrue(receiver.outcome_seen)
        self.assertFalse(receiver.chord_requested)
        consume(); consume()
        receiver.start_allowed = True
        start = raw_pad(buttons=['START'])
        consume(start, start); consume()
        self.assertIsNone(receiver.pending_neutral_sample)
        with self.assertRaisesRegex(module.SdDiagnosticError, 'escaped current phase'):
            consume(raw_pad(x=80))
        with self.assertRaisesRegex(module.SdDiagnosticError, 'escaped current phase'):
            consume(errors=[0, -1, -1, -1])
        with self.assertRaisesRegex(module.SdDiagnosticError, 'followed its validated exit'):
            receiver.accept(row('source_tick', tick=90))

    def test_requires_four_source_observed_stock_decrements_then_mode9_before_elimination(self):
        witness = self.valid_prefix()
        witness.observe(self.row(189, 89), self.fighters(0), 9)
        result = witness.finish({'seq': 200}, 2)
        self.assertEqual([x['to_stocks'][0] for x in result['p1_stock_transitions']], [3, 2, 1, 0])
        self.assertEqual(result['mode9_source_tick']['source_tick'], 89)
        self.assertFalse(result['standings_observed'])
        self.assertFalse(result['C3'])

    def test_refuses_skipped_stock_decrement(self):
        witness = module.StadiumNaturalStockoutWitness()
        witness.begin(84)
        witness.observe(self.row(100, 84), self.fighters(4), 8)
        with self.assertRaisesRegex(module.SdDiagnosticError, 'skipped/reordered'):
            witness.observe(self.row(101, 85), self.fighters(2), 8)

    def test_refuses_p2_stock_change(self):
        witness = module.StadiumNaturalStockoutWitness()
        witness.begin(84)
        witness.observe(self.row(100, 84), self.fighters(4), 8)
        with self.assertRaisesRegex(module.SdDiagnosticError, 'neutral P2 stocks'):
            witness.observe(self.row(101, 85), self.fighters(4, 3), 8)

    def test_refuses_mode9_before_zero(self):
        witness = module.StadiumNaturalStockoutWitness()
        witness.begin(84)
        witness.observe(self.row(100, 84), self.fighters(4), 8)
        with self.assertRaisesRegex(module.SdDiagnosticError, 'mode9 appeared before P1 zero'):
            witness.observe(self.row(101, 85), self.fighters(4), 9)

    def test_refuses_result_without_mode9_or_elimination_outcome(self):
        witness = self.valid_prefix()
        with self.assertRaisesRegex(module.SdDiagnosticError, 'mode9-before-exit'):
            witness.finish({'seq': 200}, 2)
        witness.observe(self.row(189, 89), self.fighters(0), 9)
        with self.assertRaisesRegex(module.SdDiagnosticError, 'mode9-before-exit'):
            witness.finish({'seq': 200}, 7)

    def test_refuses_first_source_tick_not_bound_to_actual_go(self):
        witness = module.StadiumNaturalStockoutWitness()
        witness.begin(84)
        with self.assertRaisesRegex(module.SdDiagnosticError, 'skipped/reordered'):
            witness.observe(self.row(100, 0), self.fighters(4), 8)


if __name__ == '__main__':
    unittest.main()
