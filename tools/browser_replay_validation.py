"""Join independent state and visible timing evidence without widening its scope."""
import hashlib
import json
import math
from pathlib import Path

from port_replay_validation import compare_paths
from retail_replay_recipe import encode_mwrc
from retail_replay_validation import load_capture

ZERO_GATES = ('browserCallbackGaps', 'browserLongTasks', 'nativeCallbacksOver33ms',
              'livePipelinesQueued', 'livePipelinesCreated', 'preparationPauses',
              'audioUnderrunFrames', 'audioOverflowFrames')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def finite(value):
    return type(value) in (int, float) and math.isfinite(value) and value >= 0


def validate_report(report, recipe_hash, frames, mode, cold=None):
    require(isinstance(report, dict), 'Browser report must be an object')
    for key, expected in {'schema': 'melee-web-browser-retail-replay', 'version': 1,
                          'recipe_sha256': recipe_hash, 'frames': frames,
                          'mode': mode, 'complete': True, 'pass': True,
                          'failures': [], 'gold_admitted': False,
                          'pixels': 'not_compared',
                          'performance': 'measured' if mode == 'performance' else 'not_evaluated'}.items():
        require(type(report.get(key)) is type(expected) and report[key] == expected,
                'Browser report disagrees at ' + key)
    metrics = report.get('metrics', {})
    require(isinstance(metrics, dict), 'Browser metrics must be an object')
    require(type(metrics.get('sourceFrames')) is int and metrics['sourceFrames'] == frames,
            'Browser input timeline is incomplete')
    for key in ('browserCallbacks', 'nativeCallbacks'):
        require(type(metrics.get(key)) is int and metrics[key] > 0, 'Missing active callbacks')
    require(isinstance(report.get('user_agent'), str) and report['user_agent'], 'Missing browser identity')
    require(report.get('resolution') == [640, 480] and finite(report.get('device_pixel_ratio'))
            and report['device_pixel_ratio'] > 0, 'Missing rendering configuration')
    preparation = report.get('preparation')
    require(isinstance(preparation, dict) and finite(preparation.get('total_ms')), 'Missing preparation timing')
    if mode == 'performance':
        require(type(report.get('instrumented_timing_resumes')) is int and report['instrumented_timing_resumes'] == 0,
                'Timing run resumed')
        for key in ZERO_GATES:
            require(type(metrics.get(key)) is int and metrics[key] == 0, 'Timing gate failed: ' + key)
        require(metrics.get('focusLost') is False, 'Replay lost visibility')
        for key in ('worstBrowserCallbackMs', 'worstNativeCallbackMs'):
            require(finite(metrics.get(key)) and metrics[key] <= 1000 / 30,
                    'Invalid or over-budget timing: ' + key)
        require(metrics.get('browserLongTaskWorstMs') == 0, 'Reported long task contradicts count')
        for key in ('wasmHeapGrowthBytes', 'liveTextureUploadBytes'):
            require(type(metrics.get(key)) is int and metrics[key] >= 0, 'Missing memory/resource measurement')
        cache = report.get('cache', {})
        require(isinstance(cache, dict), 'Browser cache profile must be an object')
        require(cache.get('cleared_on_startup') is cold and cache.get('driver_cache') == 'uncontrolled',
                'Wrong application/driver cache profile')
        require(cache.get('state') == ('cleared' if cold else 'ready'), 'Cache was not ready for this profile')
        require(type(cache.get('bytes')) is int and (cache['bytes'] == 0 if cold else cache['bytes'] > 0),
                'Cache byte count contradicts profile')
    return report


def _json(path, limit=65536):
    with Path(path).open('rb') as stream:
        raw = stream.read(limit + 1)
    require(len(raw) <= limit, 'Evidence JSON exceeds byte limit')
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'Duplicate evidence JSON key')
            result[key] = value
        return result
    return json.loads(raw, object_pairs_hook=unique), hashlib.sha256(raw).hexdigest()


def check_evidence(reference_a, reference_b, recipe, port, state, cold, warm, profile, browser_errors, build_directory):
    comparison = compare_paths(reference_a, reference_b, port)
    require(comparison['status'] == 'declared_state_match' and comparison['source_drawing'] == 'source_draws',
            'Rendered source comparison failed: ' + json.dumps(comparison))
    expected, _ = encode_mwrc(load_capture(reference_a))
    actual = Path(recipe).read_bytes()
    require(actual == expected, 'Browser recipe differs from paired retail input/setup')
    recipe_hash = hashlib.sha256(actual).hexdigest()
    frames = comparison['frames_compared']
    reports, hashes = {}, {}
    for name, path in (('state', state), ('cold', cold), ('warm', warm)):
        reports[name], hashes[name] = _json(path)
        validate_report(reports[name], recipe_hash, frames,
                        'state_capture' if name == 'state' else 'performance', name == 'cold')
    require(reports['state'].get('trace_sha256') == comparison['capture_hashes']['port'], 'State report names a different trace')
    for key in ('user_agent', 'resolution', 'device_pixel_ratio'):
        require(reports['state'][key] == reports['cold'][key] == reports['warm'][key], 'Browser configurations differ')
    config, hashes['profile'] = _json(profile)
    require(config.get('build') == 'Release' and config.get('hardware') and config.get('power') and config.get('display'),
            'A named Release machine/power/display profile is required')
    artifacts = config.get('artifacts', {})
    required = ('gameplay_menu_browser.js', 'gameplay_menu_browser.wasm', 'gameplay_menu_browser.data',
                'runtime.html', 'runtime-cache.js', 'audio-worklet.js')
    require(set(artifacts) == set(required), 'Build artifact inventory is incomplete')
    for name in required:
        require(hashlib.sha256((Path(build_directory) / name).read_bytes()).hexdigest() == artifacts[name],
                'Build artifact changed: ' + name)
    errors, hashes['browser_errors'] = _json(browser_errors)
    require(errors == [], 'Browser error log must be an inspected empty error list')
    return {'schema': 'melee-web-scoped-replay-evidence', 'version': 1,
            'status': 'scoped_replay_gates_passed', 'gold_admitted': False,
            'content_admitted': False, 'frames': frames, 'recipe_sha256': recipe_hash,
            'evidence_hashes': {**comparison['capture_hashes'], **hashes},
            'state_comparison': comparison, 'cold': reports['cold'], 'warm': reports['warm'],
            'build_artifacts': artifacts,
            'scope': 'This bounded input donor and the declared state fields on the named visible Release configuration only. '
                     'The profile/error inspection is an operator attestation, not browser build attestation. '
                     'No full-match/content admission, driver-cold, pixel/audio-reference or hardware-input claim.'}
