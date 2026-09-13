"""Canonical controller derivation and failure controls, never oracle state."""
from copy import deepcopy
from dataclasses import replace
import hashlib
import json
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
sys.path.insert(0, str(ROOT/'scripts'))
from retail_input_plan import (
    PAD, SCHEMA, POLICY, PROCESSED_POLICY, LEGACY_RAW_POLICY, DISCONNECTED_PAD, validate_plan, load_plan,
    plan_from_timeline, prefix_timeline,
    pipe_commands, verify_capture, verify_entry,
)
from capture_retail_replay import require_raw_pipe_config, CaptureRunnerError
from retail_replay_validation import _validate_capture
from test_retail_replay_validation import candidate
from test_slippi_format import timeline_fixture
from slippi_format import SlippiFormatError, decode_timeline


def plan():
    return {'schema':SCHEMA, 'version':1, 'policy':POLICY, 'source_sha256':'a'*64,
            'first_frame':-123, 'source_stage':32, 'source_characters':[8,8],
            'frames':[['00'*11, '00'*11] for _ in range(3)]}


def cpu_plan():
    value = plan()
    value.update({
        'version': 2,
        'source_player_types': [0, 1],
        'source_cpu_kinds': [None, 4],
        'source_cpu_levels': [None, 9],
        'source_cpu_pad_modes': [None, 'disconnected'],
        'controlled_ports': [1],
    })
    value['frames'] = [[frame[0], DISCONNECTED_PAD] for frame in value['frames']]
    return value


def float_bits(value):
    return struct.unpack('>I', struct.pack('>f', value))[0]


def legacy_timeline():
    timeline = decode_timeline(timeline_fixture())
    frames = tuple(replace(frame, inputs=tuple(
        replace(value, raw_stick=(None, None), raw_cstick=(None, None))
        for value in frame.inputs)) for frame in timeline.frames)
    return replace(timeline, frames=frames)


def replace_port1(timeline, value):
    frame = timeline.frames[0]
    return replace(timeline, frames=(replace(frame, inputs=(value, frame.inputs[1]),),
                                     *timeline.frames[1:]))


class RetailInputPlanTests(unittest.TestCase):
    def test_export_frames_prefix_keeps_full_source_provenance(self):
        spec = importlib.util.spec_from_file_location(
            'export_retail_input_plan', ROOT/'scripts/export_retail_input_plan.py')
        exporter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(exporter)
        source_bytes = timeline_fixture()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root/'donor.slp'
            output = root/'prefix.json'
            source.write_bytes(source_bytes)
            with mock.patch.object(sys, 'argv', [
                    'export_retail_input_plan.py', str(source), '--output',
                    str(output), '--frames', '2']):
                exporter.main()
            value, _ = load_plan(output)
            self.assertEqual(len(value['frames']), 2)
            self.assertEqual(value['first_frame'], -123)
            self.assertEqual(value['policy'], POLICY)
            self.assertEqual(value['source_sha256'],
                             hashlib.sha256(source_bytes).hexdigest())

    def test_export_frames_prefix_requires_positive_count_within_full_source(self):
        spec = importlib.util.spec_from_file_location(
            'export_retail_input_plan_bounds', ROOT/'scripts/export_retail_input_plan.py')
        exporter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(exporter)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root/'donor.slp'
            source.write_bytes(timeline_fixture())
            for count in ('0', '4'):
                output = root/f'bad-{count}.json'
                with self.subTest(count=count), mock.patch.object(sys, 'argv', [
                        'export_retail_input_plan.py', str(source), '--output',
                        str(output), '--frames', count]):
                    with self.assertRaises(SystemExit) as error:
                        exporter.main()
                    self.assertEqual(error.exception.code, 2)
                self.assertFalse(output.exists())

    def test_export_frames_prefix_parses_complete_source_before_slicing(self):
        spec = importlib.util.spec_from_file_location(
            'export_retail_input_plan_complete', ROOT/'scripts/export_retail_input_plan.py')
        exporter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(exporter)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root/'truncated.slp'
            output = root/'prefix.json'
            source_bytes = timeline_fixture()
            source.write_bytes(source_bytes[:-(len(b'ignored metadata') + 1)])
            with mock.patch.object(sys, 'argv', [
                    'export_retail_input_plan.py', str(source), '--output',
                    str(output), '--frames', '1']):
                with self.assertRaises(SystemExit) as error:
                    exporter.main()
            self.assertEqual(error.exception.code, 2)
            self.assertFalse(output.exists())

    def test_prefix_timeline_is_initial_only_and_does_not_change_source_identity(self):
        timeline = decode_timeline(timeline_fixture())
        prefix = prefix_timeline(timeline, 2)
        self.assertEqual([frame.number for frame in prefix.frames], [-123, -122])
        for count in (0, -1, 4):
            with self.assertRaises(ValueError):
                prefix_timeline(timeline, count)
        shifted = replace(timeline, frames=tuple(
            replace(frame, number=frame.number + 1) for frame in timeline.frames))
        with self.assertRaisesRegex(ValueError, 'frame -123'):
            prefix_timeline(shifted, 1)

    def test_normalized_donor_exports_inputs_without_observed_state(self):
        timeline = decode_timeline(timeline_fixture(fixes=(1, 1)))
        value = plan_from_timeline(timeline, 'a'*64)
        self.assertEqual(value['source_characters'], [2, 18])
        self.assertEqual(value['source_stage'], 31)
        self.assertEqual(value['first_frame'], -123)
        self.assertEqual(len(value['frames']), 3)
        self.assertEqual(PAD.unpack(bytes.fromhex(value['frames'][0][0])),
                         (256, 64, -32, 127, -127, 35, 105, 0, 0, 0))
        self.assertEqual(PAD.unpack(bytes.fromhex(value['frames'][1][0]))[0], 512)
        players = tuple(dict(p) for p in timeline.header.players)
        players[1]['player_type'] = 1
        cpu = replace(timeline, header=replace(timeline.header, players=players))
        with self.assertRaisesRegex(ValueError, 'human singles'):
            plan_from_timeline(cpu, 'a'*64)
        gap = replace(timeline, frames=(timeline.frames[0], timeline.frames[2]))
        with self.assertRaisesRegex(ValueError, 'contiguous'):
            plan_from_timeline(gap, 'a'*64)

    def test_cpu_plan_binds_ordinary_vs_setup_and_disconnects_cpu_pad(self):
        value = cpu_plan()
        self.assertIs(validate_plan(value), value)
        setup = bytearray(0x138)
        setup[14:16] = (32).to_bytes(2, 'big')
        setup[0x60], setup[0x84] = 8, 8
        setup[0x61], setup[0x85] = 0, 1
        setup[0x6e], setup[0x92] = 0, 4
        setup[0x6f], setup[0x93] = 0, 9
        verify_entry(value, setup.hex())

        changed = bytearray(setup)
        changed[0x93] = 1
        with self.assertRaisesRegex(ValueError, 'CPU level'):
            verify_entry(value, changed.hex())

        changed = bytearray(setup)
        changed[0x92] = 0
        with self.assertRaisesRegex(ValueError, 'CPU kind'):
            verify_entry(value, changed.hex())

        changed_plan = deepcopy(value)
        changed_plan['frames'][0][1] = '00' * 11
        with self.assertRaisesRegex(ValueError, 'disconnected PAD'):
            validate_plan(changed_plan)

        neutral = cpu_plan()
        neutral['source_cpu_pad_modes'][1] = 'neutral'
        neutral['frames'] = [[frame[0], '00' * 11] for frame in neutral['frames']]
        self.assertIs(validate_plan(neutral), neutral)

    def test_plan_export_requires_a_complete_source_game_end(self):
        timeline = decode_timeline(timeline_fixture())
        incomplete = replace(timeline, game_end_method=None)
        with self.assertRaisesRegex(ValueError, 'complete Slippi source with Game End'):
            plan_from_timeline(incomplete, 'a'*64)

    def test_modern_default_and_explicit_raw_policy_have_identical_output(self):
        timeline = decode_timeline(timeline_fixture())
        implicit = plan_from_timeline(timeline, 'a'*64)
        explicit = plan_from_timeline(timeline, 'a'*64, policy=POLICY)
        self.assertEqual(implicit, explicit)
        self.assertEqual(json.dumps(implicit, sort_keys=True,
                                    separators=(',', ':')),
                         json.dumps(explicit, sort_keys=True,
                                    separators=(',', ':')))

    def test_legacy_source_is_rejected_by_default(self):
        with self.assertRaisesRegex(SlippiFormatError, 'cannot reconstruct raw PAD'):
            plan_from_timeline(legacy_timeline(), 'a'*64)

    def test_processed_policy_derives_axes_with_pinned_rounding_and_triggers(self):
        timeline = legacy_timeline()
        source = timeline.frames[0].inputs[0]
        source = replace(source, processed_stick_bits=(float_bits(1), float_bits(-1)),
                         processed_cstick_bits=(float_bits(.03125),
                                                 float_bits(-.03125)))
        result = plan_from_timeline(replace_port1(timeline, source), 'a'*64,
                                    policy=PROCESSED_POLICY)
        self.assertEqual(result['policy'], PROCESSED_POLICY)
        self.assertEqual(PAD.unpack(bytes.fromhex(result['frames'][0][0])),
                         (256, 80, -80, 3, -3, 35, 105, 0, 0, 0))

        digital = replace(source, physical_buttons=256 | 64 | 32 | 512)
        result = plan_from_timeline(replace_port1(timeline, digital), 'a'*64,
                                    policy=PROCESSED_POLICY)
        self.assertEqual(PAD.unpack(bytes.fromhex(result['frames'][0][0]))[5:9],
                         (255, 255, 0, 0))

    def test_processed_policy_requires_physical_fields_and_valid_axes(self):
        timeline = legacy_timeline()
        source = timeline.frames[0].inputs[0]
        cases = (
            (replace(source, physical_buttons=None), 'physical buttons'),
            (replace(source, physical_trigger_bits=(None,
                                                     source.physical_trigger_bits[1])),
             'exact invertible physical triggers'),
            (replace(source, physical_trigger_bits=(float_bits(.123),
                                                     source.physical_trigger_bits[1])),
             'exact invertible physical triggers'),
            (replace(source, processed_stick_bits=(float_bits(1.0001),
                                                   source.processed_stick_bits[1])),
             r'outside \[-1,1\]'),
            (replace(source, processed_stick_bits=(float_bits(float('nan')),
                                                   source.processed_stick_bits[1])),
             'finite'),
        )
        for value, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, message):
                    plan_from_timeline(replace_port1(timeline, value), 'a'*64,
                                       policy=PROCESSED_POLICY)

    def test_processed_plan_rejects_axes_outside_derived_scale(self):
        value = plan()
        value['policy'] = PROCESSED_POLICY
        value['frames'][0][0] = PAD.pack(256, 81, 0, 0, 0, 0, 0, 0, 0, 0).hex()
        with self.assertRaisesRegex(ValueError, r'Processed-v2 PAD axes.*\[-80,80\]'):
            validate_plan(value)

    def test_unknown_policy_is_rejected_at_conversion_and_schema_boundaries(self):
        with self.assertRaisesRegex(ValueError, 'Unsupported'):
            plan_from_timeline(decode_timeline(timeline_fixture()), 'a'*64,
                               policy='guess')
        bad = plan()
        bad['policy'] = 'guess'
        with self.assertRaisesRegex(ValueError, 'Unsupported'):
            validate_plan(bad)

    def test_mode3_pressure_and_historical_movement_policy(self):
        old = plan()
        old['policy'] = LEGACY_RAW_POLICY
        validate_plan(old)
        old['frames'][0][0] = PAD.pack(256, 0, 0, 0, 0, 0, 0, 255, 0, 0).hex()
        with self.assertRaises(ValueError):
            validate_plan(old)
        current = deepcopy(old)
        current['policy'] = POLICY
        current['frames'][0][0] = PAD.pack(256 | 512, 0, 0, 0, 0, 0, 0, 0, 0, 0).hex()
        validate_plan(current)
        commands = pipe_commands(current['frames'][0][0]).decode()
        self.assertIn('PRESS A', commands)
        self.assertIn('PRESS B', commands)

    def test_final_input_waits_for_final_draw_without_requesting_another_input(self):
        # Exercise the collector's source-tick/draw lifecycle with a fake GDB
        # transport. Real retail repeatability is measured separately.
        class Command:
            def __init__(self, *args, **kwargs): pass
        fake = SimpleNamespace(Command=Command, Breakpoint=Command, COMMAND_USER=0,
                               parse_and_eval=lambda expression: {
                                   '$r6': 0, '$r25': 0x80001000,
                               }[expression])
        spec=importlib.util.spec_from_file_location('collector_lifecycle', ROOT/'tools/reference_replay_capture.py')
        collector=importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {'gdb':fake}), mock.patch.dict('os.environ', {'MELEE_REPLAY_INPUT_PLAN':''}):
            spec.loader.exec_module(collector)
        collector.active=collector.ready=collector.DRAW_AUDIT=True
        collector.LIMIT=2;collector.fighters={0:1,1:2}
        collector.input_plan=plan();collector.input_plan['frames']=collector.input_plan['frames'][:2]
        sample={'scene_frame':0, 'rng':1, 'match_frame':0, 'fighters':[]}
        collector.state=lambda:dict(sample)
        collector.machine_context=lambda:[]
        collector.emit=mock.Mock();collector.supply_input=mock.Mock()
        vector = ['00'*11]*2 + [DISCONNECTED_PAD]*2
        queue = bytearray(12)
        queue[0] = 3
        queue[1] = 1
        queue[3] = 0
        struct.pack_into('>I', queue, 8, 0x80001000)
        raw = b''.join(bytes.fromhex(pad) + b'\0' for pad in vector)
        collector.memory=lambda address,size:bytes(queue) if address == 0x804c1f78 else raw
        collector.word=lambda address:sample['scene_frame']
        with tempfile.TemporaryDirectory() as directory:
            collector.ROOT=Path(directory)
            collector.OUTPUT=collector.ROOT/'capture.jsonl'
            def write_emit(row):
                with collector.OUTPUT.open('a', encoding='utf-8') as stream:
                    stream.write(json.dumps(row) + '\n')
            collector.emit.side_effect=write_emit
            for tick in range(2):
                sample['scene_frame']=tick
                collector.pad_consume()
                # The next input is published before the current scheduler
                # work, and identical debugger repeats never republish it.
                collector.supply_input.assert_called_once_with(1)
                collector.pad_consume()
                self.assertEqual(len(collector.pending_inputs), 1)
                self.assertFalse(collector.scheduler_return())
                self.assertTrue(collector.active)
                sample['scene_frame']=tick+1
                self.assertFalse(collector.draw_enter())
                self.assertEqual(collector.draw_return(),tick==1)
            completion = json.loads((collector.ROOT/'match-completion.json').read_text())
            self.assertEqual(completion['frames'], 2)
            self.assertEqual(completion['phase'], 'after_final_source_draw')
            self.assertEqual(completion['final_draw_source_index'], 1)
        collector.supply_input.assert_called_once_with(1)
        self.assertFalse(collector.active)
        self.assertEqual(collector.emit.call_args.args[0], {'record':'end','frames':2,'status':'captured'})
        self.assertEqual(collector.draw_count, 2)
        self.assertEqual(collector.last_drawn_source_index, 1)

    def test_collector_publishes_first_input_at_match_entry(self):
        class Command:
            def __init__(self, *args, **kwargs): pass
        fake = SimpleNamespace(Command=Command, Breakpoint=Command, COMMAND_USER=0,
                               parse_and_eval=mock.Mock(return_value=0))
        spec = importlib.util.spec_from_file_location(
            'collector_entry_publication', ROOT/'tools/reference_replay_capture.py')
        collector = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {'gdb':fake}):
            spec.loader.exec_module(collector)
        collector.input_plan = plan()
        collector.machine_context = lambda: []
        collector.word = lambda address: 0
        collector.memory = lambda address, size: bytes(size)
        start = bytearray(0x138)
        start[14:16] = (32).to_bytes(2, 'big')
        start[0x60] = start[0x84] = 8
        collector.memory = lambda address, size: bytes(start) if size == 0x138 else bytes(size)
        collector.pad_state = lambda: '00'
        collector.emit = mock.Mock()
        collector.supply_input = mock.Mock()
        self.assertFalse(collector.active)
        collector.enter()
        self.assertTrue(collector.active)
        collector.supply_input.assert_called_once_with(0)

    def test_cpu_collector_writes_only_declared_human_pipe(self):
        class Command:
            def __init__(self, *args, **kwargs): pass
        fake = SimpleNamespace(Command=Command, Breakpoint=Command, COMMAND_USER=0)
        spec = importlib.util.spec_from_file_location(
            'collector_cpu_publication', ROOT/'tools/reference_replay_capture.py')
        collector = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {'gdb': fake}):
            spec.loader.exec_module(collector)
        collector.input_plan = cpu_plan()
        collector.published_inputs = 0
        with mock.patch.object(collector.os, 'open', return_value=17) as opened, \
                mock.patch.object(collector.os, 'write', side_effect=lambda fd, data: len(data)) as written, \
                mock.patch.object(collector.os, 'close') as closed:
            collector.supply_input(0)
        self.assertEqual(collector.published_inputs, 1)
        self.assertEqual(opened.call_count, 1)
        self.assertIn('/pad1', str(opened.call_args.args[0]))
        self.assertNotIn('/pad2', str(opened.call_args.args[0]))
        self.assertTrue(written.call_args.args[1].endswith(b'\n'))
        closed.assert_called_once_with(17)

    def test_cpu_collector_publishes_declared_neutral_cpu_status(self):
        class Command:
            def __init__(self, *args, **kwargs): pass
        fake = SimpleNamespace(Command=Command, Breakpoint=Command, COMMAND_USER=0)
        spec = importlib.util.spec_from_file_location(
            'collector_cpu_neutral_publication', ROOT/'tools/reference_replay_capture.py')
        collector = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {'gdb': fake}):
            spec.loader.exec_module(collector)
        value = cpu_plan()
        value['source_cpu_pad_modes'][1] = 'neutral'
        value['frames'] = [[frame[0], '00' * 11] for frame in value['frames']]
        collector.input_plan = value
        collector.published_inputs = 0
        with mock.patch.object(collector.os, 'open', return_value=17) as opened, \
                mock.patch.object(collector.os, 'write', side_effect=lambda fd, data: len(data)), \
                mock.patch.object(collector.os, 'close'):
            collector.supply_input(0)
        self.assertEqual(collector.published_inputs, 1)
        self.assertEqual({str(call.args[0]).rsplit('/', 1)[-1] for call in opened.call_args_list},
                         {'pad1', 'pad2'})

    def test_collector_pad_read_before_restore_publishes_next_vector(self):
        class Command:
            def __init__(self, *args, **kwargs): pass
        stack = 0x80001000
        status_end = 0x80002030
        fake = SimpleNamespace(Command=Command, Breakpoint=Command, COMMAND_USER=0,
                               parse_and_eval=mock.Mock(side_effect=lambda name: {
                                   '$r1': stack, '$r31': status_end}[name]))
        spec = importlib.util.spec_from_file_location(
            'collector_pad_read_publication', ROOT/'tools/reference_replay_capture.py')
        collector = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {'gdb':fake}):
            spec.loader.exec_module(collector)
        collector.active = collector.ready = True
        collector.LIMIT = 3
        collector.input_plan = plan()
        collector.pad_bootstrapped = True
        collector.pad_sample_index = 0
        collector.machine_context = lambda: []
        queue = bytes(12)
        vector = collector.input_plan['frames'][1] + [DISCONNECTED_PAD, DISCONNECTED_PAD]
        raw = b''.join(bytes.fromhex(pad) + b'\0' for pad in vector)
        collector.memory = lambda address, size: (
            raw if address == status_end - 0x30 else
            queue if address == 0x804c1f78 else bytes(size))
        collector.word = lambda address: 0x80376A28 if address == stack + 0x54 else 0
        collector.supply_input = mock.Mock()
        self.assertFalse(collector.pad_read_before_interrupt_restore())
        self.assertEqual(collector.pad_sample_index, 1)
        collector.supply_input.assert_called_once_with(2)

    def test_collector_pad_read_before_restore_ignores_non_hsd_caller(self):
        class Command:
            def __init__(self, *args, **kwargs): pass
        stack = 0x80001000
        status_end = 0x80002030
        fake = SimpleNamespace(Command=Command, Breakpoint=Command, COMMAND_USER=0,
                               parse_and_eval=mock.Mock(side_effect=lambda name: {
                                   '$r1': stack, '$r31': status_end}[name]))
        spec = importlib.util.spec_from_file_location(
            'collector_pad_read_other_caller', ROOT/'tools/reference_replay_capture.py')
        collector = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {'gdb':fake}):
            spec.loader.exec_module(collector)
        collector.active = collector.ready = True
        collector.LIMIT = 3
        collector.input_plan = plan()
        collector.pad_bootstrapped = True
        collector.pad_sample_index = 0
        collector.word = lambda address: 0x8034DA14 if address == stack + 0x54 else 0
        collector.memory = lambda address, size: (_ for _ in ()).throw(
            AssertionError('non-HSD PADRead must not inspect the status buffer'))
        collector.supply_input = mock.Mock()
        self.assertFalse(collector.pad_read_before_interrupt_restore())
        self.assertEqual(collector.pad_sample_index, 0)
        collector.supply_input.assert_not_called()

    def test_collector_pad_read_before_restore_rejects_mismatched_vector(self):
        class Command:
            def __init__(self, *args, **kwargs): pass
        stack = 0x80001000
        status_end = 0x80002030
        fake = SimpleNamespace(Command=Command, Breakpoint=Command, COMMAND_USER=0,
                               parse_and_eval=mock.Mock(side_effect=lambda name: {
                                   '$r1': stack, '$r31': status_end}[name]))
        spec = importlib.util.spec_from_file_location(
            'collector_pad_read_mismatch', ROOT/'tools/reference_replay_capture.py')
        collector = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {'gdb':fake}):
            spec.loader.exec_module(collector)
        collector.active = collector.ready = True
        collector.LIMIT = 3
        collector.input_plan = plan()
        collector.pad_bootstrapped = True
        collector.pad_sample_index = 0
        collector.machine_context = lambda: []
        changed = bytearray(b''.join(bytes.fromhex(pad) + b'\0' for pad in
                                     collector.input_plan['frames'][1] +
                                     [DISCONNECTED_PAD, DISCONNECTED_PAD]))
        changed[0] = 1
        collector.memory = lambda address, size: (
            bytes(changed) if address == status_end - 0x30 else bytes(size))
        collector.word = lambda address: 0x80376A28 if address == stack + 0x54 else 0
        collector.supply_input = mock.Mock()
        with self.assertRaises(ValueError):
            collector.pad_read_before_interrupt_restore()
        collector.supply_input.assert_not_called()

    def test_collector_pad_read_before_restore_ignores_queue_bookkeeping(self):
        class Command:
            def __init__(self, *args, **kwargs): pass
        stack = 0x80001000
        status_end = 0x80002030
        fake = SimpleNamespace(Command=Command, Breakpoint=Command, COMMAND_USER=0,
                               parse_and_eval=mock.Mock(side_effect=lambda name: {
                                   '$r1': stack, '$r31': status_end}[name]))
        spec = importlib.util.spec_from_file_location(
            'collector_pad_read_queue_state', ROOT/'tools/reference_replay_capture.py')
        collector = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {'gdb':fake}):
            spec.loader.exec_module(collector)
        collector.active = collector.ready = True
        collector.LIMIT = 3
        collector.input_plan = plan()
        collector.pad_bootstrapped = True
        collector.pad_sample_index = 0
        collector.machine_context = lambda: []
        queue = bytearray(12)
        queue[0] = queue[3] = 1
        vector = collector.input_plan['frames'][1] + [DISCONNECTED_PAD, DISCONNECTED_PAD]
        raw = b''.join(bytes.fromhex(pad) + b'\0' for pad in vector)
        collector.memory = lambda address, size: (
            raw if address == status_end - 0x30 else
            bytes(queue) if address == 0x804c1f78 else bytes(size))
        collector.word = lambda address: 0x80376A28 if address == stack + 0x54 else 0
        collector.supply_input = mock.Mock()
        self.assertFalse(collector.pad_read_before_interrupt_restore())
        collector.supply_input.assert_called_once_with(2)

    def test_collector_publication_rejects_skipped_or_reordered_index(self):
        class Command:
            def __init__(self, *args, **kwargs): pass
        fake = SimpleNamespace(Command=Command, Breakpoint=Command, COMMAND_USER=0)
        spec = importlib.util.spec_from_file_location(
            'collector_publication_order', ROOT/'tools/reference_replay_capture.py')
        collector = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {'gdb':fake}):
            spec.loader.exec_module(collector)
        collector.input_plan = plan()
        collector.published_inputs = 1
        with self.assertRaisesRegex(RuntimeError, 'skipped or reordered'):
            collector.supply_input(2)

    def test_collector_duplicate_pad_observation_is_suppressed(self):
        class Command:
            def __init__(self, *args, **kwargs): pass
        fake = SimpleNamespace(Command=Command, Breakpoint=Command, COMMAND_USER=0,
                               parse_and_eval=lambda expression: {
                                   '$r6': 0, '$r25': 0x80001000,
                               }[expression])
        spec = importlib.util.spec_from_file_location(
            'collector_duplicate_pad', ROOT/'tools/reference_replay_capture.py')
        collector = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {'gdb':fake}):
            spec.loader.exec_module(collector)
        collector.active = collector.ready = True
        collector.frame_index = 0
        collector.LIMIT = 4
        collector.machine_context = lambda: []
        collector.word = lambda address: 0
        collector.input_plan = plan()
        queue = bytearray(12)
        queue[0] = 3
        queue[1] = 1
        queue[3] = 0
        queue[8:12] = (0x80001000).to_bytes(4, 'big')
        vector = ['00'*11, '00'*11, DISCONNECTED_PAD, DISCONNECTED_PAD]
        raw = b''.join(bytes.fromhex(pad) + b'\0' for pad in vector)
        collector.memory = lambda address, size: bytes(queue) if address == 0x804c1f78 else raw
        collector.supply_input = mock.Mock()
        collector.pad_consume()
        collector.pad_consume()
        self.assertEqual(len(collector.pending_inputs), 1)
        self.assertEqual(collector.observations.duplicates, {'pad': 1})
        collector.supply_input.assert_called_once_with(1)

    def test_pipe_policy_preserves_signed_axes_and_declares_pressure(self):
        raw = PAD.pack(256|64, -95, 87, 0, -127, 255, 19, 0, 0, 0).hex()
        commands = pipe_commands(raw).decode().splitlines()
        self.assertIn('PRESS A', commands)
        self.assertIn('PRESS L', commands)
        self.assertIn('RELEASE R', commands)
        axes = next(x.split()[2:] for x in commands if x.startswith('SET MAIN '))
        self.assertEqual([round((float(v)*2-1)*127) for v in axes], [-95,87])
        self.assertIn('SET L 1', commands)
        for changed in (PAD.pack(64,0,0,0,0,140,0,0,0,0),
                        PAD.pack(0,-128,0,0,0,0,0,0,0,0),
                        PAD.pack(0,0,0,0,0,0,0,0,0,-1),
                        PAD.pack(256,0,0,0,0,0,0,255,0,0)):
            with self.assertRaises(ValueError): pipe_commands(changed.hex())

    def test_complete_consumption_and_same_matchup_required(self):
        rows=candidate()
        setup=bytearray.fromhex(rows[1]['start_melee_hex'])
        setup[14:16]=(32).to_bytes(2,'big');setup[0x60]=setup[0x84]=8
        rows[1]['start_melee_hex']=setup.hex()
        value=plan()
        for frame in rows[3:-1]:
            frame['consumed_inputs']=[['00'*11,'00'*11,'00'*10+'ff','00'*10+'ff']]
        capture=_validate_capture(rows,'synthetic')
        verify_capture(value,capture)
        wrong=deepcopy(value);wrong['source_characters'][1]=20
        with self.assertRaisesRegex(ValueError,'characters/stage'):verify_capture(wrong,capture)
        truncated=deepcopy(value);truncated['frames'].pop()
        with self.assertRaisesRegex(ValueError,'complete'):verify_capture(truncated,capture)
        wrong=deepcopy(value);wrong['frames'][1][0]=PAD.pack(2048,0,0,0,0,0,0,0,0,0).hex()
        with self.assertRaisesRegex(ValueError,'tick 1'):verify_capture(wrong,capture)
        for raw in ('00'*11, PAD.pack(256,0,0,0,0,0,0,255,0,-1).hex()):
            changed=deepcopy(rows);changed[4]['consumed_inputs'][0][2]=raw
            with self.assertRaisesRegex(ValueError,'tick 1'):
                verify_capture(value,_validate_capture(changed,'synthetic'))

    def test_schema_rejects_ambiguous_or_state_bearing_plans(self):
        examples=[]
        for key,val in (('version',True),('policy','guess'),('source_sha256','0'*64),
                        ('frames',[]),('first_frame',0),('source_characters',[8]),('expected_state',{})):
            bad=plan();bad[key]=val;examples.append(bad)
        bad=plan();bad['frames'][1]=bad['frames'][1][:1];examples.append(bad)
        for bad in examples:
            with self.assertRaises(ValueError):validate_plan(bad)
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'plan.json'
            path.write_text(json.dumps(plan())[:-1]+',"version":1}')
            with self.assertRaisesRegex(ValueError,'Duplicate'):load_plan(path)

    def test_raw_pipe_profile_rejects_implicit_axis_remapping(self):
        config='\n'.join(f'[GCPad{i}]\nDevice = Pipe/0/pad{i}\nMain Stick/Calibration =\nC-Stick/Calibration =\n' for i in (1,2))
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'GCPadNew.ini';path.write_text(config)
            require_raw_pipe_config(path)
            for setting in ('Main Stick/Calibration = 100 141.42', 'Main Stick/Dead Zone = 10',
                            'C-Stick/Virtual Notches = 45', 'Main Stick/Modifier = `Button X`',
                            'Triggers/Dead Zone = 1'):
                bad=config.replace('Main Stick/Calibration =\n','') if setting.startswith('Main Stick/Calibration') else config
                path.write_text(bad+'\n'+setting+'\n')
                with self.assertRaises(CaptureRunnerError):require_raw_pipe_config(path)
