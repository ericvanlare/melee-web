"""Raw-source replay admission and modeled transport controls, never a capture."""
from pathlib import Path
import copy
import hashlib
import importlib
import json
import os
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'scripts'),str(ROOT/'tools'),str(ROOT/'reference-capture/dolphin')]
import capture_sd_reference_prefix as driver
import stadium_later_b_reference as reference


def pin(path):
    path=Path(path);data=path.read_bytes()
    return dict(path=str(path),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())


class StadiumWholeRecordedInputTests(unittest.TestCase):
    def test_replay_policy_sink_never_dispatches_input(self):
        with tempfile.TemporaryDirectory(prefix='whole-replay-intent-') as temporary:
            path=Path(temporary)/'intentions.jsonl'
            sink=driver.StadiumReplayIntentObserver(path)
            with patch.object(driver.DualPipeController,'set_both',side_effect=AssertionError('input write')):
                sink.set_both(driver.NEUTRAL_PAD,driver.NEUTRAL_PAD,action='read-only-policy')
            row=json.loads(path.read_text())
            self.assertFalse(row['input_written']);self.assertEqual(row['index'],0)
            with self.assertRaises(ValueError):sink.set_both('bad',driver.NEUTRAL_PAD,action='invalid')
            self.assertEqual(len(path.read_text().splitlines()),1)

    def test_replay_option_requires_whole_source_tuple_before_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            out=Path(temporary)/'absent'
            with self.assertRaisesRegex(ValueError,'requires the whole3 input tuple'):
                driver.run(dolphin=None,disc=None,profile=None,input_plan=None,menu_recipe=None,
                    output=out,build_manifest=None,stadium_whole_replay_binding='binding')
            self.assertFalse(out.exists())

    def test_explicit_v5_coverage_without_window_refuses(self):
        with tempfile.TemporaryDirectory() as temporary:
            out=Path(temporary)/'absent'
            with self.assertRaisesRegex(ValueError,'coverage requires'):
                driver.run_stadium_whole_three(dolphin=None,disc=None,profile=None,menu_recipe=None,
                    output=out,build_manifest=None,target_path=None,setup_path=None,checkpoint_gc=None,
                    dol=None,provenance=None,gci=None,timeout=180,stadium_whole_rng_coverage='rng_updates')
            self.assertFalse(out.exists())

    def retained(self):
        setting=os.environ.get('MELEE_STADIUM_WHOLE_REPLAY_SOURCE_BINDING')
        if not setting:self.skipTest('Complete raw original source binding not configured')
        path=Path(setting);binding=json.loads(path.read_text());paths=reference.verified_files(binding)
        launch=json.loads(paths['launch'].read_text())
        return path,binding,paths,launch

    def source(self, directory):
        path,binding,paths,launch=self.retained()
        source=driver.stadium_whole_replay_preflight(path,output=directory/'fresh-output',
            setup_path=paths['setup_receipt'],expected=launch['expected'],
            profile_inventory=launch['profile_inventory'],disc=Path(launch['command'][launch['command'].index('-e')+1]),
            dol=Path(json.loads(paths['setup_receipt'].read_text())['dol']['path']))
        return source,binding,paths,launch

    def test_actual_raw_source_admission_and_identity_refusals(self):
        with tempfile.TemporaryDirectory(prefix='whole-replay-bind-') as temporary:
            directory=Path(temporary);source,binding,paths,launch=self.source(directory)
            self.assertGreater(source['summary']['events'],0)
            self.assertEqual(source['source'],str(paths['si']))
            bad=copy.deepcopy(binding);bad['files']['si']['sha256']='0'*64
            changed=directory/'changed-binding.json';changed.write_text(json.dumps(bad))
            args=dict(output=directory/'fresh-output',setup_path=paths['setup_receipt'],expected=launch['expected'],
                profile_inventory=launch['profile_inventory'],disc=Path(launch['command'][launch['command'].index('-e')+1]),
                dol=Path(json.loads(paths['setup_receipt'].read_text())['dol']['path']))
            with self.assertRaisesRegex(ValueError,'changed consumed operand'):
                driver.stadium_whole_replay_preflight(changed,**args)
            recursive=copy.deepcopy(binding);recursive['schema']=reference.REPLAY_SCHEMA
            changed.write_text(json.dumps(recursive))
            with self.assertRaisesRegex(ValueError,'original record binding'):
                driver.stadium_whole_replay_preflight(changed,**args)
            alias=directory/'alias.json';alias.symlink_to(Path(source['binding_path']))
            with self.assertRaises(ValueError):driver.stadium_whole_replay_preflight(alias,**args)
            args['profile_inventory']={}
            with self.assertRaisesRegex(ValueError,'Setup/profile differs'):
                driver.stadium_whole_replay_preflight(source['binding_path'],**args)

    def test_actual_retained_recipe_completion_and_mode_refusal(self):
        with tempfile.TemporaryDirectory(prefix='whole-replay-complete-') as temporary:
            directory=Path(temporary);source,_,paths,launch=self.source(directory)
            status=directory/'replay-status.json'
            record=json.loads(paths['si_status'].read_text());record['mode']='replay'
            status.write_text(json.dumps(record))
            # Replay completion boundary only: identical retained raw source is
            # the control, not evidence that a new native run has occurred.
            result=driver.stadium_whole_replay_complete(source,status,paths['primary'],paths['primary_status'],launch['expected'])
            self.assertEqual(result,source['summary'])
            record['mode']='record';status.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError,'mode disagrees'):
                driver.stadium_whole_replay_complete(source,status,paths['primary'],paths['primary_status'],launch['expected'])
            record['mode']='replay';record['events']-=1;status.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError,'event count disagrees'):
                driver.stadium_whole_replay_complete(source,status,paths['primary'],paths['primary_status'],launch['expected'])

    def test_explicit_replay_roles_and_current_status_are_required(self):
        with tempfile.TemporaryDirectory(prefix='whole-replay-export-') as temporary:
            directory=Path(temporary);source,binding,paths,launch=self.source(directory)
            report=json.loads(paths['report'].read_text())
            status=json.loads(paths['si_status'].read_text());status['mode']='replay'
            status_path=directory/'replay-status.json';status_path.write_text(json.dumps(status))
            launch.update(input_mode='replay',input_replay=source)
            launch['environment_delta'].pop('MWRC_INPUT_RECORD')
            launch['environment_delta'].update(MWRC_INPUT_REPLAY=str(paths['si']),
                MWRC_INPUT_STATUS=str(status_path),MWRC_STADIUM_WHOLE_REPLAY_BINDING=source['binding_sha256'])
            report.update(input_replay=source,input_replay_status=status)
            launch_path=directory/'launch.json';launch_path.write_text(json.dumps(launch))
            report_path=directory/'report.json';report_path.write_text(json.dumps(report))
            replay=copy.deepcopy(binding);replay['schema']=reference.REPLAY_SCHEMA
            replay['files'].update(launch=pin(launch_path),report=pin(report_path),
                replay_source_binding=pin(source['binding_path']),replay_status=pin(status_path))
            actual_iter=reference.context.replay.iter_records
            def announced(path):
                # Explicit modeled transport metadata only; every source row,
                # input, phase, state and seed remains actual retained data.
                for row in actual_iter(path):
                    row=copy.deepcopy(row)
                    if Path(path)==paths['primary'] and row['event'] in ('handshake','start'):
                        row['payload']['stadium_whole_replay_binding_sha256']=source['binding_sha256']
                    yield row
            with patch.object(reference.context.replay,'iter_records',side_effect=announced):
                self.assertEqual(reference.load(replay)['recipe_sha256'],reference.RECIPE_SHA256)
                bad=copy.deepcopy(replay);bad['files'].pop('replay_status')
                with self.assertRaisesRegex(ValueError,'missing consumed operand'):reference.load(bad)
                with patch.object(reference.context.replay,'iter_records',side_effect=actual_iter):
                    with self.assertRaisesRegex(ValueError,'raw announcement lacks'):
                        reference.load(replay)
            # Replay metadata under the historical record schema is forbidden.
            bad=copy.deepcopy(binding);bad['files'].update(launch=pin(launch_path),report=pin(report_path))
            with self.assertRaisesRegex(ValueError,'explicit record or replay binding'):
                reference.load(bad)


if __name__=='__main__':unittest.main()
