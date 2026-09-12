"""A reported pass cannot hide missing counters, wrong inputs or instrumentation."""
import copy
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
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

    def test_timer_paths_must_be_complete_and_timed_setup_requires_them(self):
        values = dict(reference_a=None, reference_b=None, recipe=None, port=None,
                      state=None, cold=None, warm=None, profile=None,
                      browser_errors=None, build_directory=None)
        with self.assertRaisesRegex(ValueError, 'timer-a, timer-b, and timer-port'):
            check_evidence(**values, timer_a='timer-a.json')

        capture = type('Capture', (), {
            'match_enter': {'start_melee_hex': ('02' + '00' * 0x137)},
        })()
        comparison = {
            'status': 'declared_state_match', 'source_drawing': 'source_draws',
            'frames_compared': 0, 'capture_hashes': {'port': 'p' * 64},
        }
        with patch('browser_replay_validation.compare_paths', return_value=comparison), \
             patch('browser_replay_validation.load_capture', return_value=capture), \
             patch('browser_replay_validation.encode_mwrc', return_value=(b'', '')):
            with self.assertRaisesRegex(ValueError, 'required for a timed'):
                check_evidence(**values)

    def test_untimed_setup_rejects_stray_timer_sidecars(self):
        values = dict(reference_a='reference-a.json', reference_b='reference-b.json',
                      recipe=None, port=None, state=None, cold=None, warm=None,
                      profile=None, browser_errors=None, build_directory=None)
        capture = type('Capture', (), {
            'match_enter': {'start_melee_hex': ('00' + '00' * 0x137)},
        })()
        comparison = {
            'status': 'declared_state_match', 'source_drawing': 'source_draws',
            'frames_compared': 0, 'capture_hashes': {'port': 'p' * 64},
        }
        with patch('browser_replay_validation.compare_paths', return_value=comparison), \
             patch('browser_replay_validation.load_capture', return_value=capture), \
             patch('browser_replay_validation.encode_mwrc', return_value=(b'', '')):
            with self.assertRaisesRegex(ValueError, 'untimed reference setup'):
                check_evidence(**values, timer_a='timer-a.json',
                               timer_b='timer-b.json', timer_port='timer-port.json')

    def test_timed_result_contains_timer_hashes_and_binds_state_report(self):
        expected_recipe = b'expected recipe'
        recipe_hash = hashlib.sha256(expected_recipe).hexdigest()
        port_trace_hash = 'p' * 64
        timer_port_hash = 't' * 64
        capture = type('Capture', (), {
            'match_enter': {'start_melee_hex': ('02' + '00' * 0x137)},
        })()
        comparison = {
            'status': 'declared_state_match', 'source_drawing': 'source_draws',
            'frames_compared': 686, 'capture_hashes': {'port': port_trace_hash},
        }
        timer_comparison = {
            'status': 'pass',
            'captures': {
                'a': {'sha256': 'a' * 64}, 'b': {'sha256': 'b' * 64},
                'port': {'sha256': timer_port_hash},
            },
        }
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            recipe_path = root / 'recipe.mwrc'
            recipe_path.write_bytes(expected_recipe)
            reports = {}
            for name, mode in (('state', 'state_capture'), ('cold', 'performance'),
                                ('warm', 'performance')):
                value = report()
                value['recipe_sha256'] = recipe_hash
                value['frames'] = 686
                value['mode'] = mode
                value['performance'] = 'not_evaluated' if mode == 'state_capture' else 'measured'
                value['trace_sha256'] = port_trace_hash
                value['cache']['cleared_on_startup'] = name == 'cold'
                value['cache']['state'] = 'cleared' if name == 'cold' else 'ready'
                value['cache']['bytes'] = 0 if name == 'cold' else 1
                if name == 'state':
                    value['timer_trace_sha256'] = timer_port_hash
                path = root / f'{name}.json'
                path.write_text(__import__('json').dumps(value), encoding='utf-8')
                reports[name] = path
            profile_path = root / 'profile.json'
            artifacts = {}
            for name in BUILD_ARTIFACTS:
                payload = name.encode()
                (root / name).write_bytes(payload)
                artifacts[name] = hashlib.sha256(payload).hexdigest()
            profile_path.write_text(__import__('json').dumps({
                'build': 'Release', 'hardware': 'test', 'power': 'test',
                'display': 'test', 'artifacts': artifacts,
            }), encoding='utf-8')
            errors_path = root / 'errors.json'
            errors_path.write_text('[]', encoding='utf-8')
            values = dict(reference_a='reference-a.json', reference_b='reference-b.json',
                          recipe=recipe_path, port='port.json', state=reports['state'],
                          cold=reports['cold'], warm=reports['warm'], profile=profile_path,
                          browser_errors=errors_path, build_directory=root)
            with patch('browser_replay_validation.compare_paths', return_value=comparison), \
                 patch('browser_replay_validation.load_capture', return_value=capture), \
                 patch('browser_replay_validation.encode_mwrc', return_value=(expected_recipe, 'ignored')), \
                 patch('browser_replay_validation.compare_timer_paths', return_value=timer_comparison) as timer_compare:
                result = check_evidence(
                    **values, timer_a='timer-a.json', timer_b='timer-b.json',
                    timer_port='timer-port.json')
            timer_compare.assert_called_once_with(
                'timer-a.json', 'timer-b.json', 'timer-port.json',
                reference_capture='reference-a.json', cpu='Interpreter64')
            self.assertEqual(result['timer_comparison'], timer_comparison)
            self.assertEqual(result['evidence_hashes']['timer_port'], timer_port_hash)

    def test_timed_state_report_cannot_claim_a_different_timer_trace(self):
        expected_recipe = b'expected recipe'
        recipe_hash = hashlib.sha256(expected_recipe).hexdigest()
        capture = type('Capture', (), {
            'match_enter': {'start_melee_hex': ('02' + '00' * 0x137)},
        })()
        comparison = {
            'status': 'declared_state_match', 'source_drawing': 'source_draws',
            'frames_compared': 686, 'capture_hashes': {'port': 'p' * 64},
        }
        timer_comparison = {
            'status': 'pass',
            'captures': {'a': {'sha256': 'a' * 64}, 'b': {'sha256': 'b' * 64},
                         'port': {'sha256': 't' * 64}},
        }
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            recipe_path = root / 'recipe.mwrc'; recipe_path.write_bytes(expected_recipe)
            for name, mode in (('state', 'state_capture'), ('cold', 'performance'),
                                ('warm', 'performance')):
                value = report(); value.update(recipe_sha256=recipe_hash, mode=mode,
                    performance='not_evaluated' if mode == 'state_capture' else 'measured',
                    trace_sha256='p' * 64)
                value['cache']['cleared_on_startup'] = name == 'cold'
                value['cache']['state'] = 'cleared' if name == 'cold' else 'ready'
                value['cache']['bytes'] = 0 if name == 'cold' else 1
                if name == 'state': value['timer_trace_sha256'] = 'wrong' * 16
                (root / f'{name}.json').write_text(__import__('json').dumps(value), encoding='utf-8')
            artifacts = {}
            for name in BUILD_ARTIFACTS:
                payload = name.encode(); (root / name).write_bytes(payload)
                artifacts[name] = hashlib.sha256(payload).hexdigest()
            (root / 'profile.json').write_text(__import__('json').dumps({
                'build': 'Release', 'hardware': 'test', 'power': 'test',
                'display': 'test', 'artifacts': artifacts}), encoding='utf-8')
            (root / 'errors.json').write_text('[]', encoding='utf-8')
            values = dict(reference_a='reference-a.json', reference_b='reference-b.json',
                          recipe=recipe_path, port='port.json', state=root / 'state.json',
                          cold=root / 'cold.json', warm=root / 'warm.json',
                          profile=root / 'profile.json', browser_errors=root / 'errors.json',
                          build_directory=root)
            with patch('browser_replay_validation.compare_paths', return_value=comparison), \
                 patch('browser_replay_validation.load_capture', return_value=capture), \
                 patch('browser_replay_validation.encode_mwrc', return_value=(expected_recipe, 'ignored')), \
                 patch('browser_replay_validation.compare_timer_paths', return_value=timer_comparison):
                with self.assertRaisesRegex(ValueError, 'different timer trace'):
                    check_evidence(**values, timer_a='timer-a.json', timer_b='timer-b.json',
                                   timer_port='timer-port.json')
