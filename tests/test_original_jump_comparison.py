import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('jump_compare',Path(__file__).resolve().parents[1]/'tools/compare_jump_trace.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class OriginalJumpComparison(unittest.TestCase):
    def fixture(self):
        source=[{'tick':i,'motion':25,'frame':i-1,'vy_bits':'3f800000'} for i in range(1,103)]
        retail=[{'game_frame':i+199,'fighters':[{'slot':0,'motion':25,
                 'frame':{'bits':str(i),'value':i-1},'position':[{}, {'bits':'00000000'}],
                 'velocity':[{}, {'bits':'3f800000'}]}]} for i in range(1,103)]
        return source,retail
    def test_same_frame_duplicates_preserve_exact_comparison(self):
        source,retail=self.fixture()
        self.assertEqual(module.compare(retail+retail,source,200)['matched'],102)
    def test_conflicting_duplicate_is_not_silently_dropped(self):
        import copy
        source,retail=self.fixture();bad=copy.deepcopy(retail[0])
        bad['fighters'][0]['velocity'][1]['bits']='40000000'
        with self.assertRaisesRegex(ValueError,'Conflicting original states'):
            module.compare(retail+[bad],source,200)
    def test_missing_frames_reject_and_velocity_mismatch_fails(self):
        source,retail=self.fixture()
        with self.assertRaisesRegex(ValueError,'Missing original game frame'):
            module.compare(retail[1:],source,200)
        source[3]['vy_bits']='40000000'
        result=module.compare(retail,source,200)
        self.assertEqual(result['matched'],101)
        self.assertEqual(result['differences'][0]['input_frame'],4)
