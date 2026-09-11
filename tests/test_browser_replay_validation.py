"""A reported pass cannot hide missing counters, wrong inputs or instrumentation."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from browser_replay_validation import validate_report, ZERO_GATES


def report():
    return {'schema': 'melee-web-browser-retail-replay', 'version': 1,
            'recipe_sha256': 'a' * 64, 'frames': 686, 'mode': 'performance',
            'complete': True, 'pass': True, 'failures': [], 'gold_admitted': False,
            'pixels': 'not_compared', 'performance': 'measured',
            'instrumented_timing_resumes': 0,
            'metrics': {**dict.fromkeys(ZERO_GATES, 0), 'sourceFrames': 686,
                        'browserCallbacks': 686, 'nativeCallbacks': 686,
                        'worstBrowserCallbackMs': 20, 'worstNativeCallbackMs': 8,
                        'browserLongTaskWorstMs': 0, 'focusLost': False,
                        'wasmHeapGrowthBytes': 0, 'liveTextureUploadBytes': 100},
            'preparation': {'total_ms': 220},
            'cache': {'cleared_on_startup': True, 'driver_cache': 'uncontrolled', 'state': 'cleared', 'bytes': 0},
            'user_agent': 'test', 'resolution': [640, 480], 'device_pixel_ratio': 2}


class BrowserReplayValidationTests(unittest.TestCase):
    def check(self, value):
        return validate_report(value, 'a' * 64, 686, 'performance', True)

    def test_scoped_timing_report_retains_memory_growth_measurement(self):
        value = report()
        value['metrics']['wasmHeapGrowthBytes'] = 86441984
        self.assertIs(self.check(value), value)
        self.assertFalse(value['gold_admitted'])

    def test_every_missing_or_nonzero_hard_counter_rejects_even_with_pass_true(self):
        for key in ZERO_GATES:
            for replacement in (None, 1, False):
                with self.subTest(key=key, replacement=replacement):
                    value = report()
                    if replacement is None: del value['metrics'][key]
                    else: value['metrics'][key] = replacement
                    with self.assertRaises(ValueError): self.check(value)

    def test_malformed_nested_reports_reject_cleanly(self):
        for key in ('metrics', 'preparation', 'cache'):
            for value in (None, [], 1, 'missing'):
                with self.subTest(key=key, value=value):
                    candidate = report(); candidate[key] = value
                    with self.assertRaises(ValueError): self.check(candidate)

    def test_wrong_input_scope_instrumentation_or_incomplete_timeline_rejects(self):
        for key, replacement in (('recipe_sha256', 'b' * 64), ('mode', 'state_capture'),
                                 ('frames', 685), ('complete', False),
                                 ('instrumented_timing_resumes', 1), ('gold_admitted', True)):
            with self.subTest(key=key):
                value = report(); value[key] = replacement
                with self.assertRaises(ValueError): self.check(value)
        value = report(); value['metrics']['sourceFrames'] = 685
        with self.assertRaises(ValueError): self.check(value)

    def test_missing_nonfinite_or_contradictory_timing_rejects(self):
        for field in ('worstBrowserCallbackMs', 'worstNativeCallbackMs'):
            for number in (None, float('nan'), float('inf'), -1, 34, False):
                with self.subTest(field=field, number=number):
                    value = report(); value['metrics'][field] = number
                    with self.assertRaises(ValueError): self.check(value)

    def test_wrong_cache_profile_and_visibility_reject(self):
        for group, key, replacement in (('cache', 'cleared_on_startup', False),
                                        ('cache', 'bytes', 100), ('cache', 'driver_cache', 'cold'),
                                        ('metrics', 'focusLost', True)):
            with self.subTest(key=key):
                value = copy.deepcopy(report()); value[group][key] = replacement
                with self.assertRaises(ValueError): self.check(value)
