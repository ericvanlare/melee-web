"""The diagnostic may find agreement, but never admit incomplete/gold evidence."""
from copy import deepcopy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from port_replay_validation import compare_rows, validate_port
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

if __name__=='__main__': unittest.main()
