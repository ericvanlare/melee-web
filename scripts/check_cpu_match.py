#!/usr/bin/env python3
"""Join a complete CPU match's existing retail, port, draw and browser evidence.

The manifest contains paths, not executable commands. Every check is retained,
including native or browser divergences; no failing run is shortened or hidden.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from browser_replay_validation import BUILD_ARTIFACTS, validate_report
from cpu_observation_validation import (compare_observations, load_observation,
                                        load_preparation_observations)
from cpu_reference_scenarios import (canonical as canonical_scenario,
                                     get_scenario, validate_scenario)
from port_replay_validation import compare_paths as compare_port
from retail_draw_audit import load_draw_audit
from retail_input_plan import load_plan, verify_capture
from retail_match_completion import load_match_completion
from retail_replay_recipe import encode_mwrc
from retail_replay_validation import (compare as compare_retail, load_capture,
                                     read_jsonl, _first_difference)
from retail_setup_validation import _decode_setup
from replay_coverage import analyze_cpu_capture
from native_capture_identity import runtime_inputs


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def _read_json(path):
    """Read one owned JSON artifact while rejecting duplicate object keys."""
    def unique(pairs):
        value = {}
        for key, item in pairs:
            if key in value:
                raise ValueError(f'{path}: duplicate JSON key {key!r}')
            value[key] = item
        return value
    return json.loads(Path(path).read_text(), object_pairs_hook=unique)


def _validate_scene_teardown(path, capture, setup, observation):
    """Require the source scene ownership reset after terminal publication."""
    path = Path(path)
    value = _read_json(path)
    expected_keys = {
        'schema', 'version', 'phase', 'address', 'source_ticks', 'source_draws',
        'released_fighter_slots', 'entity_list_count', 'entity_heads_hex',
        'routing_hex', 'result',
    }
    if set(value) != expected_keys:
        raise ValueError('Scene teardown artifact has missing or unexpected fields')
    if (value['schema'] != 'melee-web-retail-scene-teardown' or
            value['version'] != 1 or value['phase'] != 'HSD_GObj_80391304_return'):
        raise ValueError('Scene teardown artifact is not the original GObj reset boundary')
    if value['address'] != '8039157c':
        raise ValueError('Scene teardown reset address differs from pinned GALE01r2')
    frame_count = len(capture.frames)
    draw_count = len(observation.draws)
    if (type(value['source_ticks']) is not int or value['source_ticks'] != frame_count or
            type(value['source_draws']) is not int or value['source_draws'] != draw_count):
        raise ValueError('Scene teardown source clocks do not cover the captured timeline')
    active_slots = list(range(len(setup['players'])))
    if value['released_fighter_slots'] != active_slots:
        raise ValueError('Scene teardown did not release exactly the active fighter slots')
    count = value['entity_list_count']
    if type(count) is not int or not 1 <= count <= 64:
        raise ValueError('Scene teardown entity-list count is invalid')
    heads = value['entity_heads_hex']
    if (not isinstance(heads, str) or len(heads) != count * 8 or
            any(c not in '0123456789abcdef' for c in heads) or set(heads) != {'0'}):
        raise ValueError('Scene teardown retained nonempty or malformed entity heads')
    routing = value['routing_hex']
    if (not isinstance(routing, str) or len(routing) != 12 or
            any(c not in '0123456789abcdef' for c in routing)):
        raise ValueError('Scene teardown routing bytes are invalid')
    if value['result'] != observation.end['result'] or value['result'] is None:
        raise ValueError('Scene teardown result differs from published source result')
    return value


def check_manifest(manifest_path):
    manifest_path = Path(manifest_path).resolve()
    manifest = _read_json(manifest_path)
    expected = {'schema', 'version', 'scenario_id', 'scenario', 'input_plan', 'recipe',
                'reference_a', 'reference_b', 'native', 'browser', 'build_directory'}
    if set(manifest) != expected or manifest['schema'] != 'melee-web-cpu-match-run' or manifest['version'] != 1:
        raise ValueError('Unsupported CPU match manifest')
    resolve = lambda name: (manifest_path.parent / name).resolve()
    references = [manifest['reference_a'], manifest['reference_b']]
    for reference in references:
        if set(reference) != {'trace', 'run_directory'}:
            raise ValueError('Each reference requires trace and owned run_directory')
    a, b = [resolve(reference['trace']) for reference in references]
    runs = [resolve(reference['run_directory']) for reference in references]
    if a == b or runs[0] == runs[1]:
        raise ValueError('Two independent original executions are required')
    plan_path, recipe_path = resolve(manifest['input_plan']), resolve(manifest['recipe'])
    scenario_path = resolve(manifest['scenario'])
    scenario = _read_json(scenario_path)
    if scenario.get('scenario_id') != manifest['scenario_id']:
        raise ValueError('Scenario identity disagrees with manifest')
    validate_scenario(scenario)
    canonical_reference = get_scenario(manifest['scenario_id'])
    if canonical_scenario(scenario) != canonical_scenario(canonical_reference):
        raise ValueError('Scenario differs from the canonical CPU corpus definition')
    scenario_canonical_sha256 = hashlib.sha256(canonical_scenario(scenario)).hexdigest()
    plan, plan_hash = load_plan(plan_path)
    if plan['version'] != 3:
        raise ValueError('Complete CPU corpus requires the v3 input-only plan')
    if plan.get('source_sha256') != scenario_canonical_sha256:
        raise ValueError('Input plan is not bound to the canonical scenario')
    result = {'schema': 'melee-web-cpu-match-result', 'version': 1,
              'scenario_id': manifest['scenario_id'], 'checks': {},
              'manifest_sha256': sha(manifest_path),
              'scenario_sha256': scenario_canonical_sha256,
              'scenario_file_sha256': sha(scenario_path),
              'input_plan_sha256': plan_hash, 'recipe_sha256': sha(recipe_path),
              'performance': 'not_evaluated', 'pixels': 'not_compared',
              'gold_admitted': False, 'development_evidence': True}
    checks = result['checks']

    def record(name, function):
        try:
            value = function()
            checks[name] = value
            return value
        except (OSError, ValueError, KeyError, TypeError, AssertionError) as error:
            checks[name] = {'status': 'invalid_or_incomplete', 'error': str(error)}
            return None

    captures = []
    for label, trace, run in zip(('a', 'b'), (a, b), runs):
        def validate_reference(trace=trace, run=run):
            capture = load_capture(trace)
            verify_capture(plan, capture)
            if capture.header['provenance'].get('input_plan_sha256') != plan_hash:
                raise ValueError('Reference is not bound to the exact input plan')
            metadata = _read_json(run / 'run-metadata.json')
            if metadata['status'] != 'captured' or metadata['output_sha256'] != capture.sha256:
                raise ValueError('Reference run metadata does not bind this successful capture')
            if metadata['capture_id'] != capture.header['capture_id']:
                raise ValueError('Reference capture identity differs from owned process metadata')
            setup = _decode_setup(capture.match_enter['start_melee_hex'])
            if setup['stage'] != scenario['stage_kind']:
                raise ValueError('Observed stage differs from scenario')
            if setup['match_kind'] != scenario['match_kind']:
                raise ValueError('Observed match kind differs from scenario')
            for key in ('is_stock', 'is_teams', 'timer_enabled', 'timer_counts_up',
                        'time_limit_seconds', 'item_frequency', 'item_mask_hex',
                        'disable_pausing', 'damage_ratio_bits', 'game_speed_bits'):
                if setup[key] != scenario['rules'][key]:
                    raise ValueError('Observed rule differs from scenario: ' + key)
            if len(setup['players']) != len(scenario['players']):
                raise ValueError('Observed player count differs from scenario')
            raw_setup = bytes.fromhex(capture.match_enter['start_melee_hex'])
            for slot, (actual, expected_player) in enumerate(zip(setup['players'], scenario['players'])):
                if raw_setup[0x69 + slot * 0x24] != expected_player['team']:
                    raise ValueError('Observed team differs from scenario')
                for key in ('port', 'character_kind', 'costume', 'stocks', 'rumble_enabled'):
                    if actual[key] != expected_player[key]:
                        raise ValueError('Observed player setting differs from scenario: ' + key)
                if actual['player_type'] != expected_player['player_type_code']:
                    raise ValueError('Observed player role differs from scenario')
                if actual['player_type'] == 1:
                    for key in ('cpu_kind', 'cpu_level'):
                        if actual[key] != expected_player[key]:
                            raise ValueError('Observed CPU setting differs from scenario: ' + key)
            observation = load_observation(run / 'evidence/cpu-observation.jsonl', capture)
            if observation.end['result'] is None:
                raise ValueError('Original result publication was not observed')
            completion = load_match_completion(capture, run / 'evidence/match-completion.json', require_complete=True)
            if observation.end['result']['outcome'] != completion['match_result']:
                raise ValueError('Original published result disagrees with final source outcome')
            draw = load_draw_audit(capture, run / 'evidence/draw-audit.jsonl')
            teardown_path = run / 'evidence/scene-teardown.json'
            teardown = _validate_scene_teardown(teardown_path, capture, setup, observation)
            return {'status': 'validated', 'capture_sha256': capture.sha256,
                    'capture_id': capture.header['capture_id'], 'initial_rng': capture.match_enter['rng'],
                    'provenance': capture.header['provenance'], 'setup': setup,
                    'identity': metadata['identity'],
                    'run_metadata_sha256': sha(run / 'run-metadata.json'),
                    'observation_sha256': observation.sha256, 'completion': completion,
                    'result': observation.end['result'],
                    'complete_match': len(observation.end['result']['winners']) == 1,
                    'source_draw_audit': draw, 'ticks': len(capture.frames),
                    'scene_teardown_sha256': sha(teardown_path),
                    'scene_teardown': teardown}
        captures.append(record('reference_' + label, validate_reference))
    core_pair = record('reference_repeatability', lambda: compare_retail(a, b))
    audits = [run / 'evidence/cpu-observation.jsonl' for run in runs]
    cpu_pair = record('reference_cpu_observation', lambda: compare_observations(*audits))

    def compare_draws():
        paths = [run / 'evidence/draw-audit.jsonl' for run in runs]
        difference = _first_difference(read_jsonl(paths[0]), read_jsonl(paths[1]))
        return {'status': 'divergence' if difference else 'exact_match',
                'first_divergence': difference, 'hashes': [sha(path) for path in paths]}
    draw_pair = record('reference_draw_repeatability', compare_draws)
    accepted = (all(captures) and core_pair and core_pair.get('repeatable') is True and
                cpu_pair and cpu_pair.get('reference_repeatability') == 'pass' and
                draw_pair and draw_pair['status'] == 'exact_match')
    result['reference_accepted'] = bool(accepted)
    result['natural_match_complete'] = bool(all(captures) and
        all(capture['complete_match'] for capture in captures))
    if not accepted:
        result['status'] = 'reference_not_accepted'
        return result

    def verify_disc_identity():
        identities = [capture['identity'] for capture in captures]
        for identity in identities:
            digest = identity.get('disc_image_sha256')
            if (not isinstance(digest, str) or len(digest) != 64 or
                    any(c not in '0123456789abcdef' for c in digest)):
                raise ValueError('Original run did not bind the complete disc image')
        for key in ('disc_image_sha256', 'disc_image_bytes', 'disc_dol_sha1'):
            if identities[0][key] != identities[1][key]:
                raise ValueError('Original disc identities differ: ' + key)
        return {'status': 'validated', 'disc_image_sha256': identities[0]['disc_image_sha256']}
    if not record('disc_identity', verify_disc_identity):
        result['reference_accepted'] = False
        result['status'] = 'reference_not_accepted'
        return result

    def verify_recipe():
        encoded, _ = encode_mwrc(load_capture(a))
        if recipe_path.read_bytes() != encoded:
            raise ValueError('Recipe differs from independently paired original inputs/setup')
        return {'status': 'exact_input_recipe'}
    if not record('recipe', verify_recipe):
        result['status'] = 'recipe_not_accepted'
        return result

    native = manifest['native']
    browser = resolve(manifest['browser'])
    def verify_native_capture():
        if set(native) != {'trace', 'observation', 'build_directory', 'menu_assets',
                           'game_assets', 'node_executable'}:
            raise ValueError('Native capture requires explicit build, Node and asset inputs')
        trace, observation = resolve(native['trace']), resolve(native['observation'])
        if trace.parent != observation.parent:
            raise ValueError('Native trace and CPU observation require one owned capture directory')
        directory = trace.parent
        config_path = directory / 'capture-config.json'
        config = _read_json(config_path)
        receipt = _read_json(directory / 'capture-result.json')
        if (config.get('schema') != 'melee-web-cpu-native-capture' or config.get('version') != 1 or
                config.get('require_match_complete') is not True or
                receipt.get('schema') != 'melee-web-cpu-native-capture-result' or
                receipt.get('version') != 1 or receipt.get('status') != 'completed' or
                type(receipt.get('returncode')) is not int or receipt['returncode'] != 0 or
                receipt.get('full_completion') is not True or receipt.get('artifact_change') is not False):
            raise ValueError('Native capture did not complete against unchanged build and recipe bytes')
        if receipt.get('capture_config_sha256') != sha(config_path):
            raise ValueError('Native receipt does not bind its invocation')
        for digest in (config.get('recipe_sha256'), receipt.get('recipe_sha256_before'),
                       receipt.get('recipe_sha256_after')):
            if digest != result['recipe_sha256']:
                raise ValueError('Native invocation differs from accepted recipe')
        artifacts = config.get('artifacts', {})
        if set(artifacts) != {'gameplay_retail_trace.js', 'gameplay_retail_trace.wasm'}:
            raise ValueError('Native executable identity is incomplete')
        for digest in artifacts.values():
            if (not isinstance(digest, str) or len(digest) != 64 or
                    any(c not in '0123456789abcdef' for c in digest)):
                raise ValueError('Native executable hash is invalid')
        if receipt.get('artifacts_before') != artifacts or receipt.get('artifacts_after') != artifacts:
            raise ValueError('Native executable changed during capture')
        native_build = resolve(native['build_directory'])
        for name, digest in artifacts.items():
            if sha(native_build / name) != digest:
                raise ValueError('Native executable differs from declared build: ' + name)
        browser_build = resolve(manifest['build_directory'])
        for name in BUILD_ARTIFACTS:
            if sha(native_build / name) != sha(browser_build / name):
                raise ValueError('Native and browser build inventories disagree: ' + name)
        expected_inputs = runtime_inputs(resolve(native['node_executable']),
                                         resolve(native['menu_assets']), resolve(native['game_assets']))
        for observed in (config.get('runtime_inputs'), receipt.get('runtime_inputs_before'),
                         receipt.get('runtime_inputs_after')):
            if observed != expected_inputs:
                raise ValueError('Native Node or owned assets differ from declared inputs')
        output_paths = {
            'trace.jsonl': trace,
            'cpu-observation.jsonl': observation,
            'stderr.log': directory / 'stderr.log',
            'hitlag-audit.log': directory / 'hitlag-audit.log',
            'matrix-audit.jsonl': directory / 'matrix-audit.jsonl',
        }
        outputs = receipt.get('outputs')
        if not isinstance(outputs, dict) or set(outputs) != set(output_paths):
            raise ValueError('Native output receipt has missing or unexpected files')
        for name, path in output_paths.items():
            if outputs[name] != sha(path):
                raise ValueError('Native output differs from capture receipt: ' + name)
        return {'status': 'validated', 'artifacts': artifacts,
                'runtime_inputs': expected_inputs,
                'capture_config_sha256': sha(config_path), 'outputs': receipt['outputs']}
    record('native_capture', verify_native_capture)
    record('native_state', lambda: compare_port(a, b, resolve(native['trace'])))
    record('native_cpu_observation', lambda: compare_observations(*audits, resolve(native['observation'])))
    record('browser_state', lambda: compare_port(a, b, browser / 'retail-port.jsonl'))
    record('browser_cpu_observation', lambda: compare_observations(*audits, browser / 'cpu-observation.jsonl'))

    def verify_browser():
        config = _read_json(browser / 'capture-config.json')
        for key in ('disc_image_sha256', 'disc_image_bytes'):
            if config.get(key) != captures[0]['identity'][key]:
                raise ValueError('Browser disc differs from original: ' + key)
        if config.get('recipe_sha256') != result['recipe_sha256']:
            raise ValueError('Browser invocation differs from accepted recipe')
        report = _read_json(browser / 'retail-browser-report.json')
        validate_report(report, result['recipe_sha256'], captures[0]['ticks'], 'state_capture')
        metrics = report.get('metrics')
        if not isinstance(metrics, dict):
            raise ValueError('Browser report metrics are missing')
        for key in ('sourceFrames', 'sourceSteps', 'sourceDraws'):
            if type(metrics.get(key)) is not int or metrics[key] != captures[0]['ticks']:
                raise ValueError('Browser source timeline count mismatch: ' + key)
        preparation_report = report.get('preparation')
        if (not isinstance(preparation_report, dict) or
                type(preparation_report.get('source_draws')) is not int or
                preparation_report['source_draws'] < 0):
            raise ValueError('Browser preparation source-draw count is missing')
        if not report['source_match']['complete']:
            raise ValueError('Browser did not reach the source ending')
        if report['source_match']['outcome'] != captures[0]['completion']['match_result']:
            raise ValueError('Browser outcome differs from original')
        winners = captures[0]['result']['winners']
        if report['source_match']['winner'] != (winners[0] if len(winners) == 1 else -1):
            raise ValueError('Browser winner differs from original published standings')
        errors = _read_json(browser / 'browser-errors.json')
        if errors['errors'] or errors['requests']:
            raise ValueError('Browser errors or unexpected non-GET requests were retained')
        return {'status': 'validated', 'report_sha256': sha(browser / 'retail-browser-report.json'),
                'source_match': report['source_match'], 'source_steps': report['metrics']['sourceSteps'],
                'source_draws': report['metrics']['sourceDraws'],
                'preparation_source_draws': preparation_report['source_draws'],
                'timing_resumes': report['instrumented_timing_resumes'],
                'timing': {key: report['metrics'].get(key) for key in
                    ('nativeCallbacksOverBudget', 'nativeCallbacksOver33ms', 'browserCallbackGaps',
                     'worstNativeCallbackMs', 'worstBrowserCallbackMs')},
                'user_agent': report['user_agent']}
    record('browser_completion', verify_browser)
    def verify_browser_phases():
        path = browser / 'preparation-observation.jsonl'
        types = [player['player_type'] for player in captures[0]['setup']['players']]
        preparation = load_preparation_observations(path, types)
        browser_completion = checks.get('browser_completion', {})
        if browser_completion.get('status') != 'validated':
            raise ValueError('Browser completion must be validated before source-phase alignment')
        if browser_completion.get('preparation_source_draws') != len(preparation):
            raise ValueError('Browser preparation report disagrees with preparation observation rows')
        return {'status': 'additional_source_draws' if preparation else 'aligned',
                'preparation_sha256': sha(path),
                'source_preparation_draws': len(preparation),
                'source_replay_draws': captures[0]['ticks'],
                'all_observed_source_draws': len(preparation) + captures[0]['ticks'],
                'first_unmatched_preparation_draw': preparation[0] if preparation else None}
    record('browser_source_phases', verify_browser_phases)
    coverage = record('coverage', lambda: analyze_cpu_capture(a, input_plan=plan_path,
        observation=audits[0], completion=runs[0] / 'evidence/match-completion.json'))
    build = resolve(manifest['build_directory'])
    result['build_artifacts'] = {name: sha(build / name) for name in BUILD_ARTIFACTS}
    def verify_served_build():
        served = json.loads((browser / 'served-artifacts.json').read_text())
        served_after = _read_json(browser / 'served-artifacts-after.json')
        expected_names = set(BUILD_ARTIFACTS)
        if (not isinstance(served, dict) or not isinstance(served_after, dict) or
                set(served) != expected_names or set(served_after) != expected_names):
            raise ValueError('Browser HTTP artifact inventory is incomplete or has extras')
        if served != served_after:
            raise ValueError('Browser HTTP build changed or was not verified after capture')
        for name, expected_hash in result['build_artifacts'].items():
            if served.get(name) != expected_hash:
                raise ValueError('Browser HTTP artifact differs from declared build: ' + name)
        return {'status': 'validated', 'artifacts': served}
    record('browser_build', verify_served_build)
    exact = lambda name: checks.get(name, {}).get('status') == 'declared_state_match'
    result['browser_agrees'] = (exact('browser_state') and exact('browser_cpu_observation') and
                               checks.get('browser_completion', {}).get('status') == 'validated' and
                               checks.get('browser_build', {}).get('status') == 'validated' and
                               checks.get('browser_source_phases', {}).get('status') == 'aligned')
    result['native_agrees'] = (exact('native_state') and exact('native_cpu_observation') and
                              checks.get('native_capture', {}).get('status') == 'validated')
    result['status'] = ('all_declared_state_matches' if result['browser_agrees'] and result['native_agrees']
                        else 'browser_matches_native_red' if result['browser_agrees'] else 'divergence_or_incomplete')
    if not result['natural_match_complete']:
        result['status'] = 'source_tie_requires_sudden_death'
    elif coverage is None:
        result['status'] = 'coverage_incomplete'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.resolve() == args.manifest.resolve():
        parser.error('output must not replace manifest')
    try:
        result = check_manifest(args.manifest)
    except (OSError, ValueError, KeyError, TypeError) as error:
        result = {'status': 'invalid_or_incomplete', 'error': str(error), 'gold_admitted': False}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    print(json.dumps({'output': str(args.output), 'status': result['status']}))
    return 0 if result['status'] == 'all_declared_state_matches' else 1


if __name__ == '__main__':
    raise SystemExit(main())
