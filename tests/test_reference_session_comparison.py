from copy import deepcopy
from pathlib import Path
import sys,unittest
from unittest import mock
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import reference_session_comparison as C

class SessionComparisonTests(unittest.TestCase):
    def session(self, name='a'):
        rows=[{'seq':0,'source_tick':0,'draw_ordinal':0,'event':'fighter_create','payload':{'slot':0,'allocation_identity':'81000100'}},
              {'seq':1,'source_tick':0,'draw_ordinal':0,'event':'source_tick','payload':{'retail':{'rng':45,'fighters':[{'damage_bits':'00000000'}]},'cpu':{'players':[{'cpu':{'buttons':1}}]}}}]
        return C._LoadedSession(Path(name),{'session_id':name,'run_id':name,'created_at_unix':1 if name=='a' else 2,'environment':{'disc':{'hash':'same'}},'replay_source_manifest_sha256':None if name=='a' else 'a'}, {'manifest_sha256':name},rows,rows,{'complete':True},name)
    def compare(self,a,b):
        with mock.patch.object(C,'_load_bundle',side_effect=[a,b]):return C.compare_bundles('a','b')
    def test_distinct_runs_compare_all_observations(self):
        r=self.compare(self.session('a'),self.session('b'))
        self.assertEqual(r['status'],'matched');self.assertFalse(r['semantic']['addresses_used_as_replay_inputs'])
    def test_changed_address_and_cpu_command_are_not_ignored(self):
        for index,key,value in [(0,'allocation_identity','81000200'),(1,'cpu',{'players':[{'cpu':{'buttons':2}}]})]:
            a,b=self.session('a'),self.session('b');b.semantic_rows[index]['payload'][key]=value
            r=self.compare(a,b);self.assertEqual(r['status'],'diverged');self.assertEqual(r['semantic']['first_divergence']['index'],index)
    def test_same_capture_cannot_claim_repeatability(self):
        a=self.session('a');self.assertEqual(self.compare(a,a)['status'],'invalid')
    def test_missing_event_and_environment_drift_fail(self):
        a,b=self.session('a'),self.session('b');b.semantic_rows.pop();self.assertEqual(self.compare(a,b)['status'],'diverged')
        a,b=self.session('a'),self.session('b');b.header['environment']['disc']['hash']='changed';self.assertEqual(self.compare(a,b)['status'],'diverged')
    def test_invalid_bundle_not_diagnostic_match(self):
        with mock.patch.object(C,'_load_bundle',side_effect=C.SessionComparisonError('truncated')):
            self.assertEqual(C.compare_bundles('a','b')['status'],'invalid')
