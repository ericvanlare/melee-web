"""Canonical controller derivation and failure controls, never oracle state."""
from copy import deepcopy
from dataclasses import replace
import json
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
sys.path.insert(0, str(ROOT/'scripts'))
from retail_input_plan import (
    PAD, SCHEMA, POLICY, DISCONNECTED_PAD, validate_plan, load_plan, plan_from_timeline,
    pipe_commands, verify_capture,
)
from capture_retail_replay import require_raw_pipe_config, CaptureRunnerError
from retail_replay_validation import _validate_capture
from test_retail_replay_validation import candidate
from test_slippi_format import timeline_fixture
from slippi_format import decode_timeline


def plan():
    return {'schema':SCHEMA, 'version':1, 'policy':POLICY, 'source_sha256':'a'*64,
            'first_frame':-123, 'source_stage':32, 'source_characters':[8,8],
            'frames':[['00'*11, '00'*11] for _ in range(3)]}


class RetailInputPlanTests(unittest.TestCase):
    def test_normalized_donor_exports_inputs_without_observed_state(self):
        timeline = decode_timeline(timeline_fixture(fixes=(1, 1)))
        value = plan_from_timeline(timeline, 'a'*64)
        self.assertEqual(value['source_characters'], [2, 18])
        self.assertEqual(value['source_stage'], 31)
        self.assertEqual(value['first_frame'], -123)
        self.assertEqual(len(value['frames']), 3)
        self.assertEqual(PAD.unpack(bytes.fromhex(value['frames'][0][0])),
                         (256, 64, -32, 127, -127, 35, 105, 255, 0, 0))
        self.assertEqual(PAD.unpack(bytes.fromhex(value['frames'][1][0]))[0], 512)
        players = tuple(dict(p) for p in timeline.header.players)
        players[1]['player_type'] = 1
        cpu = replace(timeline, header=replace(timeline.header, players=players))
        with self.assertRaisesRegex(ValueError, 'human singles'):
            plan_from_timeline(cpu, 'a'*64)
        gap = replace(timeline, frames=(timeline.frames[0], timeline.frames[2]))
        with self.assertRaisesRegex(ValueError, 'contiguous'):
            plan_from_timeline(gap, 'a'*64)

    def test_final_input_waits_for_final_draw_without_requesting_another_input(self):
        # Exercise the collector's source-tick/draw lifecycle with a fake GDB
        # transport. Real retail repeatability is measured separately.
        class Command:
            def __init__(self, *args, **kwargs): pass
        fake = SimpleNamespace(Command=Command, Breakpoint=Command, COMMAND_USER=0)
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
        with tempfile.TemporaryDirectory() as directory:
            collector.ROOT=Path(directory)
            for tick in range(2):
                sample['scene_frame']=tick
                collector.pending_inputs=[['00'*11]*2+[DISCONNECTED_PAD]*2]
                self.assertFalse(collector.scheduler_return())
                self.assertTrue(collector.active)
                sample['scene_frame']=tick+1
                self.assertFalse(collector.draw_enter())
                self.assertEqual(collector.draw_return(),tick==1)
        collector.supply_input.assert_called_once_with(1)
        self.assertFalse(collector.active)
        self.assertEqual(collector.emit.call_args.args[0], {'record':'end','frames':2,'status':'captured'})

    def test_pipe_policy_preserves_signed_axes_and_declares_pressure(self):
        raw = PAD.pack(256|64, -95, 87, 0, -127, 255, 19, 255, 0, 0).hex()
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
                        PAD.pack(256,0,0,0,0,0,0,0,0,0)):
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
