#!/usr/bin/env python3
"""Prospective, separate assessment; never mutate frozen capture decisions."""
import argparse
import hashlib
import json
from pathlib import Path

LOW, HIGH = 0.8, 1.2
COMMON_SOURCE, COMMON_CURSOR = (133, 165), (257, 289)


def identity(path):
    raw = path.read_bytes()
    return {'path': str(path.resolve()), 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def decoded(capture):
    columns = capture['columns']
    width = len(columns)
    if len(capture['table']) != width * capture['rows']:
        raise ValueError('capture table shape mismatch')
    return [{'row': i, **dict(zip(columns, capture['table'][i*width:(i+1)*width]))}
            for i in range(capture['rows'])]


def reconstruct(row, previous):
    out = dict(row)
    out['cursor_delta'] = row['sample_replay_cursor'] - previous['sample_replay_cursor']
    out['source_delta'] = row['sample_source_frame'] - previous['sample_source_frame']
    fraction = row['sample_pending_ticks']
    valid = (row['sample_running'] == 1 and row['sample_scene'] == 7 and
             row['preparation_ms'] == 0 and 0 <= fraction < 1 and
             out['cursor_delta'] == out['source_delta'] == row['source_steps'] == row['source_draws'])
    out['reconstruction_valid_under_no_break_no_reset_conditions'] = valid
    out['accepted_pre_consumption_pending_ticks'] = row['source_steps'] + fraction if valid else None
    return out


def metric_check(label, before, after):
    rows = []
    for key in ['mean_ms', 'p95_ms', 'batches_per_enabled_second']:
        expected, actual = before[key], after[key]
        valid = expected > 0 and actual > 0 and LOW*expected <= actual <= HIGH*expected
        rows.append({'window': label, 'metric': key, 'baseline': expected, 'candidate': actual,
                     'ratio': actual / expected if expected > 0 else None, 'within_frozen_bounds': valid})
    return rows


def load_metrics(window, calibration=False):
    stats = window['batch_duration_stats_ms'] if calibration else window['gpu_load']['batch_duration_stats_ms']
    duration = window['actual_window_ms'] if calibration else window['gpu_load']['actual_gpu_load_window_ms']
    return {'mean_ms': stats['mean_ms'], 'p95_ms': stats['p95_ms'],
            'batches_per_enabled_second': 1000 * stats['count'] / duration}


def describe_window(report, capture, key):
    window = report.get(key)
    if not window:
        return None
    all_rows = decoded(capture)
    selected = window['assessment']['active_rows']
    rows = [reconstruct(all_rows[row['row']], all_rows[row['row']-1]) for row in selected]
    incident = window['terminal'].get('pause_incident')
    guard = all_rows[incident['row']] if incident and incident['row'] < len(all_rows) else None
    prior = next((row for row in reversed(all_rows[:incident['row']])
                  if row['sample_running'] == 1 and row['sample_source_frame'] > 0), None) if incident else None
    start = window['start']['load_start_epoch_ms'] - window['start']['time_origin']
    stop_state = window['gpu_load']['gpu_stop_state']
    stop = stop_state['time_origin_ms'] + stop_state['stopped_at_ms'] - window['start']['time_origin']
    active_incidents = [event for event in capture['incidents']
                        if event['source_frame'] > 0 and start <= event['at_ms'] <= stop]
    return {'first_source': rows[0]['sample_source_frame'] if rows else None,
            'last_source': rows[-1]['sample_source_frame'] if rows else None,
            'first_cursor': rows[0]['sample_replay_cursor'] if rows else None,
            'last_cursor': rows[-1]['sample_replay_cursor'] if rows else None,
            'active_rows': len(rows), 'all_reconstructions_valid': bool(rows) and all(
                row['reconstruction_valid_under_no_break_no_reset_conditions'] for row in rows),
            'tail': rows[-10:], 'incident': incident, 'actual_guard_callback': guard,
            'preceding_active_callback': prior, 'load_start_main_ms': start,
            'actual_load_stop_main_ms': stop, 'incidents_through_actual_load_off': active_incidents}


def assess(baseline, candidate, baseline_capture, candidate_capture):
    problems, metrics, offsets = [], [], []
    described = {name: describe_window(candidate, candidate_capture, name)
                 for name in ['control_window', 'treatment_window']}
    ring = candidate.get('ring_status', {})
    if (ring.get('frame_slots'), ring.get('staging_buffers'), ring.get('reserved_gpu_buffer_bytes')) != (4, 4, 264241152):
        problems.append('ring_four_identity_mismatch')
    if not candidate.get('ring_status_validation', {}).get('valid') or not candidate.get('ring_status_before_unload_validation', {}).get('valid'):
        problems.append('selected_ring_validation_missing_or_failed')
    native = candidate.get('native_before_unload', {})
    if native.get('phase') != 7 or native.get('match', {}).get('complete') is not False:
        problems.append('match_owner_or_loop_completion_exclusion_not_verified')
    if candidate.get('failure') or candidate.get('integrity_errors') or candidate.get('browser_errors'):
        problems.append('runner_or_integrity_failure')
    if candidate.get('inputs') != baseline.get('inputs') or candidate.get('gpu_config') != baseline.get('gpu_config'):
        problems.append('input_or_offered_load_identity_mismatch')
    if (candidate.get('build_artifacts_before') != baseline.get('build_artifacts_before') or
            candidate.get('build_artifacts_after') != baseline.get('build_artifacts_before')):
        problems.append('unchanged_build_bytes_mismatch')
    if not candidate.get('prospective_build_binding', {}).get('after_http_artifact_map', {}).get('matches_manifest'):
        problems.append('prospective_manifest_http_gate_failed')
    for iterations in [300, 600]:
        before = next((x for x in baseline['calibration'] if x['iterations'] == iterations), None)
        after = next((x for x in candidate.get('calibration', []) if x['iterations'] == iterations), None)
        if not before or not after or not after.get('valid'):
            problems.append(f'calibration_{iterations}_missing_or_invalid')
        else:
            metrics += metric_check(f'calibration_{iterations}', load_metrics(before, True), load_metrics(after, True))
    for name in ['control_window', 'treatment_window']:
        before, after = baseline.get(name), candidate.get(name)
        details = described[name]
        if not after or not details or after['assessment']['status'] != 'clean':
            problems.append(name + '_not_qualified')
            continue
        metrics += metric_check(name, load_metrics(before), load_metrics(after))
        if not details['all_reconstructions_valid']:
            problems.append(name + '_source_reconstruction_not_qualified')
        for field, base_field in [('first_source', 'first_original_frame'), ('first_cursor', 'first_input_cursor')]:
            offset = details[field] - before['assessment'][base_field]
            offsets.append({'window': name, 'field': field, 'offset_ticks': offset, 'within_frozen_bounds': abs(offset) <= 3})
        duration = after['gpu_load']['actual_gpu_load_window_ms']
        if not 0 < duration <= 2100:
            problems.append(name + '_actual_load_bound_failed')
    if any(not metric['within_frozen_bounds'] for metric in metrics):
        problems.append('observed_load_not_comparable')
    if any(not offset['within_frozen_bounds'] for offset in offsets):
        problems.append('source_start_offsets_not_comparable')
    control, treatment = candidate.get('control_window'), candidate.get('treatment_window')
    if control and (control['terminal']['outcome'] != 'window_complete' or
                    described['control_window']['incidents_through_actual_load_off'] or
                    not 2000 <= control['gpu_load']['actual_gpu_load_window_ms'] <= 2100):
        problems.append('control_not_continuously_clean')
    outcome = treatment['terminal']['outcome'] if treatment else None
    if outcome == 'window_complete':
        details = described['treatment_window']
        if candidate.get('native_before_unload', {}).get('source_running') != 1:
            problems.append('source_not_running_at_completed_exposure_boundary')
        if (details['first_source'] > COMMON_SOURCE[0] or details['last_source'] < COMMON_SOURCE[1] or
                details['first_cursor'] > COMMON_CURSOR[0] or details['last_cursor'] < COMMON_CURSOR[1]):
            problems.append('common_source_interval_not_covered')
        if details['incidents_through_actual_load_off']:
            problems.append('incident_before_actual_load_off')
        if not 2000 <= treatment['gpu_load']['actual_gpu_load_window_ms'] <= 2100:
            problems.append('full_requested_treatment_not_observed')
    elif outcome == 'timing_pause':
        details = described['treatment_window']
        incident, guard = details['incident'], details['actual_guard_callback']
        genuine = any(record.get('incident', {}).get('reason_code') == 1 and
                      record['incident'].get('source_frame') == incident['source_frame'] and
                      record['incident'].get('value') == incident['value'] and
                      record['incident'].get('threshold') == incident['threshold'] and
                      record['incident'].get('staging') is not None
                      for record in candidate.get('runtime_incident_recorder', {}).get('retained_records', [])) if incident else False
        if not (incident and incident['reason'] == 1 and incident['value'] > incident['threshold'] and
                guard and guard['sample_running'] == 0 and guard['source_steps'] == 0 and
                guard['sample_source_frame'] == incident['source_frame'] and
                guard['started_ms'] <= incident['at_ms'] <= guard['hook_at_ms'] and genuine):
            problems.append('actual_guard_and_genuine_incident_not_qualified')
        prior = details['preceding_active_callback']
        if not prior or not (0 < prior['staging_slot_wait_ms'] <= prior['total_ms'] and
                             prior['staging_slot_wait_ms'] > prior['total_ms'] - prior['staging_slot_wait_ms']):
            problems.append('guard_preceding_callback_not_staging_wait_dominant')
        if any(event != incident for event in details['incidents_through_actual_load_off']):
            problems.append('additional_incident_before_load_off')
    else:
        problems.append('unexpected_treatment_terminal')
    return {'schema': 'melee-web-h1-source-informed-ring-four-comparison-v1',
            'result': 'inconclusive' if problems else 'mechanism_persists' if outcome == 'timing_pause' else 'scoped_induced_mechanism_nonreproduction',
            'problems': sorted(set(problems)), 'frozen_comparability': {'metric_ratio': [LOW, HIGH], 'start_offset_ticks_max': 3,
                'common_source': COMMON_SOURCE, 'common_cursor': COMMON_CURSOR},
            'observed_metric_comparisons': metrics, 'source_start_offsets': offsets,
            'candidate_windows': described,
            'baseline_windows': {name: describe_window(baseline, baseline_capture, name) for name in described},
            'legacy_frozen_candidate_decision': candidate.get('decision_evidence'),
            'legacy_frozen_baseline_decision': baseline.get('decision_evidence'),
            'legacy_field_warning': 'pause_callback fields can describe preceding active callback; actual guard callback is separately identified here.',
            'limitations': ['One induced synthetic workload only; no natural-pause cause or H1 acceptance.',
                'Accepted demand reconstruction requires no step-loop break or later clock reset; source/cursor agreement and active no-preparation conditions checked.',
                'Wall batch statistics and declared reservation do not establish pure GPU execution or physical resident memory.',
                'Comparability bounds were frozen prospectively; no tuning or retry is authorized.']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--proposal', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    proposal = json.loads(args.proposal.read_text())
    if identity(args.baseline / 'report.json')['sha256'] != proposal['baseline_report']['sha256']:
        raise ValueError('baseline differs from frozen proposal')
    load = lambda directory, name: json.loads((directory / name).read_text())
    result = assess(load(args.baseline, 'report.json'), load(args.candidate, 'report.json'),
                    load(args.baseline, 'capture.json'), load(args.candidate, 'capture.json'))
    result['input_receipts'] = {label: identity(directory / name) for label, directory, name in [
        ('baseline_report', args.baseline, 'report.json'), ('baseline_capture', args.baseline, 'capture.json'),
        ('candidate_report', args.candidate, 'report.json'), ('candidate_capture', args.candidate, 'capture.json')]}
    result['proposal'] = identity(args.proposal)
    result['postprocessor'] = identity(Path(__file__))
    with args.out.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(json.dumps({'result': result['result'], 'problems': result['problems'], 'receipt': identity(args.out)}))
