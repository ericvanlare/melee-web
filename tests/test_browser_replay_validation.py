"""A reported pass cannot hide missing counters, wrong inputs or instrumentation."""
import copy
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from browser_replay_validation import (check_evidence, validate_report, ZERO_GATES,
                                       BUILD_ARTIFACTS, validate_build_artifacts)


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
    def test_imported_runtime_and_audio_modules_are_bound_to_the_frozen_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifacts = {}
            for name in BUILD_ARTIFACTS:
                payload = name.encode()
                (root / name).write_bytes(payload)
                artifacts[name] = hashlib.sha256(payload).hexdigest()
            validate_build_artifacts(artifacts, root)
            incomplete = dict(artifacts)
            del incomplete['runtime-assets.mjs']
            with self.assertRaisesRegex(ValueError, 'inventory'):
                validate_build_artifacts(incomplete, root)
            for name in ('runtime-assets.mjs', 'disc-image.mjs', 'audio-ring.mjs'):
                with self.subTest(module=name):
                    (root / name).write_bytes(b'changed after freeze')
                    with self.assertRaisesRegex(ValueError, 'Build artifact changed'):
                        validate_build_artifacts(artifacts, root)
                    (root / name).write_bytes(name.encode())

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

    def test_source_match_completion_is_required_only_for_full_match_evidence(self):
        value = report()
        self.assertIs(self.check(value), value)
        value['source_match'] = {'complete': True, 'outcome': 2, 'winner': 1}
        self.assertIs(validate_report(value, 'a' * 64, 686, 'performance', True,
                                      expected_winner=1), value)
        for replacement in (
                None,
                {'complete': False, 'outcome': 2, 'winner': 1},
                {'complete': True, 'outcome': 1, 'winner': 1},
                {'complete': True, 'outcome': 2, 'winner': 0},
                {'complete': True, 'outcome': 2, 'winner': True},
        ):
            with self.subTest(source_match=replacement):
                candidate = report()
                if replacement is not None:
                    candidate['source_match'] = replacement
                with self.assertRaises(ValueError):
                    validate_report(candidate, 'a' * 64, 686, 'performance', True,
                                    expected_winner=1)

    def test_completion_paths_must_be_paired(self):
        values = dict(reference_a=None, reference_b=None, recipe=None, port=None,
                      state=None, cold=None, warm=None, profile=None,
                      browser_errors=None, build_directory=None)
        with self.assertRaisesRegex(ValueError, 'supplied together'):
            check_evidence(**values, completion_a='completion-a.json')
        with self.assertRaisesRegex(ValueError, 'supplied together'):
            check_evidence(**values, completion_b='completion-b.json')
