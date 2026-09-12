"""The diagnostic may find agreement, but never admit incomplete/gold evidence."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from port_replay_validation import compare_paths, compare_rows, validate_port
from retail_replay_validation import CaptureError, _validate_capture
from test_retail_replay_validation import candidate


def fixture():
    rows=candidate()
    reference=_validate_capture(rows,"test")
    port=[{'record':'header','schema':'melee-web-port-replay-candidate','version':1,
           'frames_requested':3,'phase':'after_source_tick_before_audio_transport',
           'rendering':'excluded','comparison':'not_run'},
          {key:rows[1][key] for key in ('record','rng','start_melee_hex')}]
    port.append({key:value for key,value in rows[2].items() if key!='scene_frame'})
    for row in rows[3:-1]:
        port.append({**{key:value for key,value in row.items() if key not in ('scene_frame','consumed_inputs')},
                     'supplied_inputs':row['consumed_inputs'][0]})
    port.append(deepcopy(rows[-1]))
    return reference,deepcopy(port)


def _write(path, rows):
    path.write_text(''.join(json.dumps(row, separators=(',', ':')) + '\n'
                         for row in rows), encoding='utf-8')


def _paths(port_rows):
    directory = tempfile.TemporaryDirectory(prefix='port-validation-')
    root = Path(directory.name)
    reference_a = candidate()
    reference_b = candidate()
    paths = (root / 'reference-a.jsonl', root / 'reference-b.jsonl', root / 'port.jsonl')
    _write(paths[0], reference_a)
    _write(paths[1], reference_b)
    _write(paths[2], port_rows)
    return directory, paths


class PortReplayTests(unittest.TestCase):
    def test_complete_agreement_is_scoped_and_not_gold(self):
        ref,port=fixture(); result=compare_rows(ref,port)
        self.assertEqual(result['status'],'declared_state_match')
        self.assertEqual(result['frames_compared'],3)
        self.assertFalse(result['gold_admitted'])
        self.assertEqual(result['performance'],'not_evaluated')

    def test_rendered_state_evidence_does_not_claim_pixel_or_performance_agreement(self):
        ref, port = fixture()
        port[0]['rendering'] = 'source_draws'
        result = compare_rows(ref, port)
        self.assertEqual(result['status'], 'declared_state_match')
        self.assertEqual(result['source_drawing'], 'source_draws')
        self.assertIn('pixel agreement', result['scope'])
        self.assertFalse(result['gold_admitted'])
        self.assertEqual(result['performance'], 'not_evaluated')
        port[0]['rendering'] = 'unknown'
        with self.assertRaises(CaptureError): validate_port(port)

    def test_first_rng_difference_is_not_hidden_by_matching_fighters(self):
        ref,port=fixture();port[3]['rng']+=1
        result=compare_rows(ref,port)
        self.assertEqual(result['status'],'diverged')
        self.assertEqual(result['first_divergence']['frame'],0)
        self.assertEqual(result['first_divergence']['field'],'rng')
        self.assertEqual(result['checks']['fighters'],'pass')

    def test_late_input_and_state_differences_are_reported(self):
        ref,port=fixture();port[-2]['supplied_inputs'][0]='01'+'00'*10
        port[-2]['fighters'][1]['stocks']=3
        result=compare_rows(ref,port)
        self.assertEqual(result['first_divergence']['frame'],2)
        self.assertEqual(result['checks']['inputs'],'diverged')
        self.assertEqual(result['checks']['fighters'],'diverged')

    def test_incomplete_extra_reordered_and_wrong_phase_rejected(self):
        _,good=fixture()
        mutations=[lambda p:p.pop(),lambda p:p.append(p[-1]),
                   lambda p:p[3].update(index=1),
                   lambda p:p[0].update(version=True),
                   lambda p:p[0].update(phase='after_render'),
                   lambda p:p[-1].update(status='aborted'),
                   lambda p:p[-2]['fighters'][0].update(damage_bits='7f800000'),
                   lambda p:p[-2].pop('rng')]
        for mutate in mutations:
            port=deepcopy(good);mutate(port)
            with self.assertRaises(CaptureError): validate_port(port)

    def test_incomplete_early_exit_keeps_first_divergence_diagnostic(self):
        _, port = fixture()
        prefix = deepcopy(port[:-1])
        prefix[4]['supplied_inputs'][0] = '01' + '00' * 10
        directory, paths = _paths(prefix)
        try:
            result = compare_paths(*paths)
        finally:
            directory.cleanup()
        self.assertEqual(result['status'], 'invalid_capture')
        self.assertEqual(result['diagnostic_status'], 'validated_prefix')
        self.assertEqual(result['first_divergence']['record'], 'frame')
        self.assertEqual(result['first_divergence']['frame'], 1)
        self.assertEqual(result['first_divergence']['field'], '[0]')
        self.assertEqual(result['checks']['inputs'], 'diverged')
        self.assertFalse(result['gold_admitted'])
        self.assertEqual(result['performance'], 'not_evaluated')

    def test_matching_prefix_stays_invalid_and_reports_missing_teardown(self):
        _, port = fixture()
        directory, paths = _paths(port[:-1])
        try:
            result = compare_paths(*paths)
        finally:
            directory.cleanup()
        self.assertEqual(result['status'], 'invalid_capture')
        self.assertEqual(result['diagnostic_status'], 'validated_prefix')
        self.assertIsNone(result['first_divergence'])
        self.assertEqual(result['observed_frames'], 3)
        self.assertEqual(result['frames_compared'], 3)
        self.assertFalse(result['gold_admitted'])
        self.assertEqual(result['performance'], 'not_evaluated')
        self.assertIn('teardown/end', result['error'])

    def test_malformed_prefix_has_no_misleading_comparison(self):
        _, port = fixture()
        prefix = deepcopy(port[:-1])
        prefix[4]['index'] = 0
        directory, paths = _paths(prefix)
        try:
            result = compare_paths(*paths)
        finally:
            directory.cleanup()
        self.assertEqual(result['status'], 'invalid_capture')
        self.assertNotIn('diagnostic_status', result)
        self.assertIsNone(result.get('first_divergence'))
        self.assertNotIn('frames_compared', result)
        self.assertIn('incomplete or extra records', result['error'])

    def test_duplicate_json_key_is_rejected_without_prefix_diagnostic(self):
        _, port = fixture()
        directory = tempfile.TemporaryDirectory(prefix='port-validation-duplicate-')
        try:
            root = Path(directory.name)
            paths = (root / 'reference-a.jsonl', root / 'reference-b.jsonl', root / 'port.jsonl')
            _write(paths[0], candidate())
            _write(paths[1], candidate())
            paths[2].write_text(
                json.dumps(port[0], separators=(',', ':')) + '\n' +
                '{"record":"header","record":"header"}\n',
                encoding='utf-8')
            result = compare_paths(*paths)
        finally:
            directory.cleanup()
        self.assertEqual(result['status'], 'invalid_capture')
        self.assertNotIn('diagnostic_status', result)
        self.assertIsNone(result.get('first_divergence'))
        self.assertIn('duplicate JSON key', result['error'])

if __name__=='__main__': unittest.main()
