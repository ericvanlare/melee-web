"""Join independent state and visible timing evidence without widening its scope."""
import hashlib
import json
import math
from pathlib import Path

from port_replay_validation import compare_paths
from retail_replay_recipe import encode_mwrc
from retail_match_completion import load_match_completion
from retail_replay_validation import load_capture
from retail_timer_validation import compare_paths as compare_timer_paths

ZERO_GATES = ('browserCallbackGaps', 'browserLongTasks', 'nativeCallbacksOver33ms',
              'livePipelinesQueued', 'livePipelinesCreated', 'preparationPauses',
              'audioUnderrunFrames', 'audioOverflowFrames')

# Include the separately imported asset loader, disc/PAD utilities and audio
# transport modules; freezing only the Wasm and HTML leaves executable inputs
# outside the development/held-out build identity.
BUILD_ARTIFACTS = tuple(json.loads(
    Path(__file__).with_name('browser_build_artifacts.json').read_text(encoding='utf-8')
))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate_build_artifacts(artifacts, build_directory):
    require(isinstance(artifacts, dict) and set(artifacts) == set(BUILD_ARTIFACTS),
            'Build artifact inventory is incomplete')
    for name in BUILD_ARTIFACTS:
        require(hashlib.sha256((Path(build_directory) / name).read_bytes()).hexdigest() == artifacts[name],
                'Build artifact changed: ' + name)


def finite(value):
    return type(value) in (int, float) and math.isfinite(value) and value >= 0


def validate_cache_sync_capture(capture):
    """Auxiliary I/O records cannot replace native/browser failure evidence."""
    capabilities = capture.get('capabilities', {})
    require(isinstance(capabilities, dict), 'Malformed hitch capabilities')
    capability = capabilities.get('cache_sync', {})
    require(isinstance(capability, dict), 'Malformed cache sync capability')
    fields = ('cache_syncs', 'cache_sync_cap', 'cache_sync_count', 'cache_sync_overflow_count')
    if not any(key in capture for key in fields) and capability.get('requested') is not True:
        return  # Earlier reports predate the optional mount instrumentation.
    rows = capture.get('cache_syncs')
    require(isinstance(rows, list), 'Missing cache sync records')
    require(type(capture.get('cache_sync_cap')) is int and 0 < capture['cache_sync_cap'] <= 1024
            and len(rows) <= capture['cache_sync_cap'], 'Invalid cache sync bound')
    require(type(capture.get('cache_sync_count')) is int and capture['cache_sync_count'] == len(rows),
            'Cache sync count mismatch')
    require(type(capture.get('cache_sync_overflow_count')) is int
            and capture['cache_sync_overflow_count'] == 0, 'Overflowed cache sync capture')
    require(type(capability.get('requested')) is bool, 'Missing cache sync request state')
    if capability['requested']:
        require(capability.get('installed') is True and capability.get('enabled') is True,
                'Requested cache sync hook unavailable')
    else:
        require(not rows, 'Unexpected cache sync records without opt-in')
    ids = set()
    counts = {'calls': len(rows), 'pending': 0, 'errors': 0}
    for row in rows:
        require(isinstance(row, dict) and isinstance(row.get('id'), str) and row['id']
                and row['id'] not in ids, 'Invalid or duplicate cache sync record')
        ids.add(row['id'])
        require(row.get('clock') == 'performance.now' and finite(row.get('started')),
                'Missing cache sync clock')
        require(row.get('status') in ('pending', 'completed', 'error'), 'Invalid cache sync status')
        if row['status'] == 'pending':
            counts['pending'] += 1
            require(row.get('ended') is None and row.get('duration_ms') is None,
                    'Pending cache sync has a completion')
        else:
            require(finite(row.get('ended')) and row['ended'] >= row['started']
                    and finite(row.get('duration_ms'))
                    and math.isclose(row['duration_ms'], row['ended'] - row['started'], abs_tol=0.01),
                    'Invalid cache sync completion interval')
            counts['errors'] += int(row['status'] == 'error')
    for key, count in counts.items():
        require(type(capability.get(key)) is int and capability[key] == count,
                'Cache sync records disagree with counter: ' + key)
    require(counts['pending'] == 0 and counts['errors'] == 0, 'Incomplete or failed cache sync evidence')


def validate_hitch_capture(capture, metrics):
    """Optional diagnostic evidence must agree with the independent counters."""
    require(isinstance(capture, dict), 'Malformed hitch capture')
    require(capture.get('schema') == 'melee-web-diagnostic-hitch-capture'
            and type(capture.get('version')) is int and capture['version'] == 1,
            'Unsupported hitch capture schema')
    require(type(capture.get('enabled')) is bool, 'Missing hitch capture enablement')
    if not capture['enabled']:
        return
    require(capture.get('valid') is True and capture.get('invalid') is False
            and capture.get('overflowed') is False
            and type(capture.get('overflow_count')) is int and capture['overflow_count'] == 0,
            'Incomplete or overflowed hitch capture')
    events, observers = capture.get('events'), capture.get('observers')
    require(isinstance(events, list) and isinstance(observers, list), 'Missing hitch events')
    require(type(capture.get('cap')) is int and 0 < capture['cap'] <= 1024
            and len(events) + len(observers) <= capture['cap'], 'Invalid hitch event bound')
    require(capture.get('event_count') == len(events)
            and capture.get('observer_count') == len(observers), 'Hitch event count mismatch')
    counts = {'nativeCallbacksOverBudget': 0, 'nativeCallbacksOver33ms': 0, 'browserCallbackGaps': 0}
    ids = set()
    for event in events:
        require(isinstance(event, dict) and isinstance(event.get('id'), str), 'Invalid hitch event')
        require(event['id'] not in ids, 'Duplicate hitch event id')
        ids.add(event['id'])
        require(finite(event.get('duration_ms')), 'Missing hitch event duration')
        duration = event['duration_ms']
        if event.get('kind') == 'native_deadline':
            require(duration > 1000 / 60, 'Native hitch does not miss its deadline')
            require(finite(metrics.get('worstNativeCallbackMs')) and duration <= metrics['worstNativeCallbackMs'],
                    'Native hitch exceeds reported maximum')
            counts['nativeCallbacksOverBudget'] += 1
            counts['nativeCallbacksOver33ms'] += int(duration > 1000 / 30)
        else:
            require(event.get('kind') == 'browser_gap' and duration > 1000 / 30,
                    'Invalid browser hitch')
            require(finite(metrics.get('worstBrowserCallbackMs')) and duration <= metrics['worstBrowserCallbackMs'],
                    'Browser hitch exceeds reported maximum')
            counts['browserCallbackGaps'] += 1
    for key, count in counts.items():
        require(type(metrics.get(key)) is int and metrics[key] == count,
                'Hitch events disagree with counter: ' + key)
    validate_cache_sync_capture(capture)


def validate_report(report, recipe_hash, frames, mode, cold=None, *, expected_winner=None,
                    expected_draws=None):
    require(isinstance(report, dict), 'Browser report must be an object')
    paint = report.get('diagnostic_page_paint')
    require(paint is None or (isinstance(paint, dict) and paint.get('mode') == 'normal'
                             and paint.get('diagnostic_only') is False
                             and paint.get('restored') is True),
            'Diagnostic page-paint control is not normal-page acceptance evidence')
    if paint is not None:
        require(finite(paint.get('started_ms')) and finite(paint.get('ended_ms'))
                and paint['ended_ms'] >= paint['started_ms'], 'Invalid page-paint interval')
        geometry = paint.get('geometry_after')
        require(isinstance(geometry, dict) and paint.get('geometry_before') == geometry,
                'Page-paint geometry changed or is missing')
        require(all(type(geometry.get(k)) in (int, float) and math.isfinite(geometry[k])
                    for k in ('x', 'y', 'width', 'height', 'buffer_width', 'buffer_height', 'dpr'))
                and geometry['width'] > 0 and geometry['height'] > 0 and geometry['dpr'] > 0
                and geometry['dpr'] == report.get('device_pixel_ratio')
                and geometry['buffer_width'] == 640 * geometry['dpr']
                and geometry['buffer_height'] == 480 * geometry['dpr'],
                'Invalid page-paint canvas geometry')
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
    if report.get('diagnostic_capture') is not None:
        validate_hitch_capture(report['diagnostic_capture'], metrics)
    require(type(metrics.get('sourceFrames')) is int and metrics['sourceFrames'] == frames,
            'Browser input timeline is incomplete')
    # Earlier v1 evidence predates per-tick traversal accounting. New reports
    # carry both counters; never accept a partial or mismatched pair.
    if 'sourceSteps' in metrics or 'sourceDraws' in metrics:
        for key in ('sourceSteps', 'sourceDraws'):
            expected_count = (expected_draws if key == 'sourceDraws' and
                              expected_draws is not None else frames)
            require(type(expected_count) is int and 1 <= expected_count <= frames,
                    'Invalid independently computed source count')
            require(type(metrics.get(key)) is int and metrics[key] == expected_count,
                    'Source tick/draw count mismatch: ' + key)
    for key in ('browserCallbacks', 'nativeCallbacks'):
        require(type(metrics.get(key)) is int and metrics[key] > 0, 'Missing active callbacks')
    require(isinstance(report.get('user_agent'), str) and report['user_agent'], 'Missing browser identity')
    require(report.get('resolution') == [640, 480] and finite(report.get('device_pixel_ratio'))
            and report['device_pixel_ratio'] > 0, 'Missing rendering configuration')
    preparation = report.get('preparation')
    require(isinstance(preparation, dict) and finite(preparation.get('total_ms')), 'Missing preparation timing')
    if expected_winner is not None:
        source_match = report.get('source_match')
        require(isinstance(source_match, dict), 'Missing source match completion observation')
        require(type(source_match.get('complete')) is bool and source_match['complete'] is True,
                'Browser source match did not complete')
        require(type(source_match.get('outcome')) is int and source_match['outcome'] == 2,
                'Browser source match did not report elimination outcome')
        require(type(source_match.get('winner')) is int and source_match['winner'] == expected_winner,
                'Browser source match winner disagrees with reference final stocks')
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


def _json(path, limit=8 * 1024 * 1024):
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


def _reference_winner(capture, context):
    try:
        fighters = capture.frames[-1]['fighters']
        stocks = [fighter['stocks'] for fighter in fighters]
    except (AttributeError, IndexError, KeyError, TypeError) as error:
        raise ValueError(f'{context}: missing final fighter stocks') from error
    require(len(stocks) == 2 and all(type(stock) is int for stock in stocks),
            f'{context}: invalid final fighter stocks')
    require(stocks.count(0) == 1 and any(stock > 0 for stock in stocks),
            f'{context}: final stocks do not identify one winner')
    return 1 if stocks[0] == 0 else 0


def check_evidence(reference_a, reference_b, recipe, port, state, cold, warm, profile,
                   browser_errors, build_directory, *, cpu="Interpreter64",
                   completion_a=None, completion_b=None,
                   timer_a=None, timer_b=None, timer_port=None):
    require((completion_a is None) == (completion_b is None),
            'completion-a and completion-b must be supplied together')
    timer_paths = (timer_a, timer_b, timer_port)
    timer_supplied = tuple(path is not None for path in timer_paths)
    require(len(set(timer_supplied)) == 1,
            'timer-a, timer-b, and timer-port must be supplied together')
    comparison = compare_paths(reference_a, reference_b, port, cpu=cpu)
    require(comparison['status'] == 'declared_state_match' and comparison['source_drawing'] == 'source_draws',
            'Rendered source comparison failed: ' + json.dumps(comparison))
    reference_capture = load_capture(reference_a, cpu=cpu)
    expected, _ = encode_mwrc(reference_capture)
    setup = bytes.fromhex(reference_capture.match_enter['start_melee_hex'])
    timer_enabled = bool(setup[0] & 2)
    if timer_enabled:
        require(all(timer_supplied),
                'timer-a, timer-b, and timer-port are required for a timed reference setup')
    else:
        require(not any(timer_supplied),
                'timer sidecars are not valid for an untimed reference setup')
    actual = Path(recipe).read_bytes()
    require(actual == expected, 'Browser recipe differs from paired retail input/setup')
    recipe_hash = hashlib.sha256(actual).hexdigest()
    frames = comparison['frames_compared']
    completion_reports = None
    expected_winner = None
    if completion_a is not None:
        capture_a = load_capture(reference_a, cpu=cpu)
        capture_b = load_capture(reference_b, cpu=cpu)
        completion_reports = {
            'a': load_match_completion(capture_a, completion_a, require_complete=True),
            'b': load_match_completion(capture_b, completion_b, require_complete=True),
        }
        expected_winner = _reference_winner(capture_a, 'reference-a')
        require(_reference_winner(capture_b, 'reference-b') == expected_winner,
                'Reference final stocks identify different winners')
    reports, hashes = {}, {}
    for name, path in (('state', state), ('cold', cold), ('warm', warm)):
        reports[name], hashes[name] = _json(path)
        validate_report(reports[name], recipe_hash, frames,
                        'state_capture' if name == 'state' else 'performance', name == 'cold',
                        expected_winner=expected_winner)
    require(reports['state'].get('trace_sha256') == comparison['capture_hashes']['port'], 'State report names a different trace')
    timer_comparison = None
    if timer_enabled:
        timer_comparison = compare_timer_paths(
            timer_a, timer_b, timer_port, reference_capture=reference_a, cpu=cpu)
        require(timer_comparison.get('status') == 'pass',
                'Timer comparison failed: ' + json.dumps(timer_comparison, sort_keys=True))
        timer_port_sha256 = timer_comparison['captures']['port'].get('sha256')
        require(isinstance(timer_port_sha256, str) and len(timer_port_sha256) == 64,
                'Timer comparison omitted the port sidecar hash')
        require(reports['state'].get('timer_trace_sha256') == timer_port_sha256,
                'State report names a different timer trace')
    for key in ('user_agent', 'resolution', 'device_pixel_ratio'):
        require(reports['state'][key] == reports['cold'][key] == reports['warm'][key], 'Browser configurations differ')
    config, hashes['profile'] = _json(profile)
    require(config.get('build') == 'Release' and config.get('hardware') and config.get('power') and config.get('display'),
            'A named Release machine/power/display profile is required')
    artifacts = config.get('artifacts', {})
    validate_build_artifacts(artifacts, build_directory)
    errors, hashes['browser_errors'] = _json(browser_errors)
    require(errors == [], 'Browser error log must be an inspected empty error list')
    result = {'schema': 'melee-web-scoped-replay-evidence', 'version': 1,
            'status': 'scoped_replay_gates_passed', 'gold_admitted': False,
            'content_admitted': False, 'frames': frames, 'recipe_sha256': recipe_hash,
            'evidence_hashes': {**comparison['capture_hashes'], **hashes},
            'state_comparison': comparison, 'cold': reports['cold'], 'warm': reports['warm'],
            'build_artifacts': artifacts,
            'scope': 'This bounded input donor and the declared state fields on the named visible Release configuration only. '
                     'The profile/error inspection is an operator attestation, not browser build attestation. '
                     'No full-match/content admission, driver-cold, pixel/audio-reference or hardware-input claim.'}
    if timer_comparison is not None:
        result['timer_comparison'] = timer_comparison
        result['evidence_hashes'].update({
            'timer_a': timer_comparison['captures']['a']['sha256'],
            'timer_b': timer_comparison['captures']['b']['sha256'],
            'timer_port': timer_comparison['captures']['port']['sha256'],
        })
    if completion_reports is not None:
        result['evidence_hashes'].update({
            'completion_a': completion_reports['a']['completion_sha256'],
            'completion_b': completion_reports['b']['completion_sha256'],
        })
        result['match_completion'] = completion_reports
        result['completion_a'] = completion_reports['a']
        result['completion_b'] = completion_reports['b']
    return result
