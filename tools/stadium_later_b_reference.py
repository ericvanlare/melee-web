"""Exact new-reference operands only; deliberately no browser/native admission.

The existing 2006/940 runtime route remains separate. This adapter reuses the
strict original parsers; it neither writes guest state nor converts reference
observations into a claim about the browser.
"""
from __future__ import annotations
import copy
import hashlib
import json
from pathlib import Path

import stadium_current_context_packet as context
import reference_input_stream as si
from reference_observer_stream import SLICE_NAMES
from stadium_go_prefix import (CONSTRUCTOR_POINTS, StadiumConstructorObservations,
                               _validate_progress, _validate_stadium_idle_state_record)

SCHEMA = 'melee-web-stadium-later-b-reference-operands-v1'
VARIANT = 'private-original-b-2013-943-reference-only'
ROLES = {'primary', 'primary_status', 'si', 'si_status', 'launch', 'report',
         'setup_receipt', 'producer_manifest'}
RECIPE_SHA256 = '5fb53bdc71b35b800bc3451b1d9117709edef2413a38a8f7311ee352647a6755'


def require(value, message):
    if not value:
        raise ValueError(message)


def verified_files(binding):
    require(set(binding) == {'schema', 'files'} and binding['schema'] == SCHEMA,
            'unknown operand binding schema or field')
    files = binding['files']
    require(set(files) == ROLES, 'unknown or missing consumed operand role')
    paths = {}
    for role, pin in files.items():
        require(set(pin) == {'path', 'bytes', 'sha256'}, 'malformed operand pin')
        path = Path(pin['path'])
        require(not path.is_symlink() and path.is_file(), 'operand is missing or symlinked')
        with path.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        require(path.stat().st_size == pin['bytes'] and digest == pin['sha256'],
                f'changed consumed operand: {role}')
        paths[role] = path
    return paths


def _named_seed(row):
    # Progress JSON carries numeric tags; the binary decoder adds these names.
    # Representation-only copy, never a replacement pointer/value or raw slice.
    named = copy.deepcopy(row)
    for item in named['payload']['slices']:
        item['name'] = SLICE_NAMES.get(item['tag'], 'unknown')
    return context.seed(named)


def derive(rows, expected, receipt, report):
    packet = context.packet_from_records(rows, expected)
    capture = context.replay.capture_from_records(rows)
    recipe, _ = context.replay.encode_v8(capture)
    require(hashlib.sha256(recipe).hexdigest() == RECIPE_SHA256,
            'not the exact new 2013-input reference corpus')
    require(len(packet['inputs']) == 2013 and packet['spans'][6]['first_frame'] == 943,
            'new-reference dimensions differ; legacy 2006/940 is not this variant')
    match = packet['matches'][1]
    entry, setup = match['entry'], match['setup']
    progress = [row for row in rows if row['event'] == 'progress' and
                row['payload'].get('match_index') == 1]
    constructor = StadiumConstructorObservations()
    go = None
    for row in progress:
        phase = row['payload'].get('phase')
        if phase in CONSTRUCTOR_POINTS:
            require(entry['sequence'] < row['seq'] < setup['sequence'],
                    'constructor escaped selected Entry/Setup interval')
            constructor.accept(row, route_ready=True,
                scope='stadium_whole3_feedback_v1', expected_receipt=receipt, match_index=1)
        elif phase == 'go_after':
            require(go is None and row['seq'] > setup['sequence'], 'duplicate or early GO')
            constructor.finish()
            _validate_progress(row, phase, receipt,
                scope='stadium_whole3_feedback_v1', match_index=1)
            go = row
    require(go is not None, 'missing actual GO marker')
    construction = constructor.finish(include_state_projection=True)
    require(construction['state_projection']['seed_before_hex'] == entry['rng']['value_hex'],
            'constructor parent seed differs from actual Entry')
    go_projection = _validate_stadium_idle_state_record(go, 'GO', go['source_tick'])
    go_tick = go['source_tick']
    steps = match['steps']
    require(0 <= go_tick < len(steps), 'GO is outside selected actual steps')
    require(steps[go_tick]['consume']['sequence'] < go['seq'] <
            steps[go_tick]['tick']['sequence'], 'GO does not precede its SourceTick')
    actual = report['actual_inputs']
    selected = [item for item in actual if item['match_index'] == 1]
    require(len(selected) == 1, 'ambiguous selected pause receipt')
    pause = selected[0]['actual_pause_chord']
    stop = pause['source_tick']
    require(stop < len(steps) and steps[stop]['consume']['sequence'] == pause['sequence']
            and steps[stop]['ports'] == pause['ports'], 'pause receipt is not an actual consume')
    require(stop - go_tick == 96, 'reference does not contain exactly 96 pre-pause states')
    states = []
    state_rng = []
    for tick in range(go_tick, len(steps)):
        row = rows[steps[tick]['tick']['sequence']]
        require(row['seq'] == steps[tick]['tick']['sequence'], 'record sequence/index differs')
        states.append(_validate_stadium_idle_state_record(row, 'IDLE', tick))
        state_rng.append(context.seed(row))
    observations = {'constructor': construction, 'go': go_projection,
                    'source_tick_states': states,
                    'registration_event_observed': False, 'lifetime_pairing_observed': False}
    require(observations == report['stadium_b_observation'],
            'retained observation report differs from actual strict raw replay')
    require(len(states[stop-go_tick:]) == 12, 'natural pause tail differs')
    # Retain actual consume/tick/draw coordinates. No synthetic wire action or PAD.
    return {'schema': SCHEMA, 'variant': VARIANT, 'runtime_admission': False,
            'same_run_provenance_verified': False,
            'scope': 'original-reference operand derivation; producer approval remains external',
            'recipe_sha256': RECIPE_SHA256, 'recipe_bytes': len(recipe),
            'whole_session_context': packet, 'selected_match': 1,
            'setup_global_cursor': packet['spans'][6]['first_frame'],
            'entry': entry, 'setup': setup, 'constructor': construction,
            'go': go_projection, 'go_rng': _named_seed(go),
            'source_steps_before_pause': steps[:stop],
            'pre_pause_states': states[:stop-go_tick],
            'pre_pause_state_rng': state_rng[:stop-go_tick],
            'natural_pause_tail_unrun': states[stop-go_tick:],
            'natural_pause_tail_rng_unrun': state_rng[stop-go_tick:],
            'stop_before_consume': steps[stop]['consume'],
            'registration_parity': False, 'per_owner_rng_parity': False,
            'browser_parity': False, 'C3': False}


def load(binding):
    paths = verified_files(binding)
    launch = json.loads(paths['launch'].read_text())
    report = json.loads(paths['report'].read_text())
    require(report['result'] == 'pass' and
            report['native']['primary_sha256'] == binding['files']['primary']['sha256'] and
            report['input_stream']['sha256'] == binding['files']['si']['sha256'],
            'same-run report/raw identities differ')
    require(launch['setup']['sha256'] == binding['files']['setup_receipt']['sha256'] and
            launch['manifest']['sha256'] == binding['files']['producer_manifest']['sha256'],
            'launch producer/setup joins differ')
    require(Path(launch['setup']['path']) == paths['setup_receipt'] and
            Path(launch['manifest']['path']) == paths['producer_manifest'],
            'launch producer/setup role path differs')
    rows = list(context.replay.iter_records(paths['primary']))
    strict = context.replay.capture_from_path(paths['primary'], paths['primary_status'])
    result = derive(rows, launch['expected'], launch['setup']['sha256'], report)
    require(strict['frames'] == [{k: row[k] for k in
            ('index', 'scene', 'scene_code', 'source_seq', 'source_tick', 'pads')}
            for row in result['whole_session_context']['inputs']], 'strict input extraction differs')
    events = si.validate_stream(paths['si'])
    si.validate_status(paths['si_status'], mode='record', events=events['events'], require_complete=True)
    verified_files(binding)
    return result


def require_runtime_admission(_operands):
    raise ValueError('reference-only operands: native 2013/943 pause/READY/Setup and finite source-action bridge are not implemented')


def build_gate1_expected(binding):
    """Seal new-reference expectation from actual operands; no runtime actions.

    The current native profile3 must independently hash the actual recipe bytes.
    This host projection never turns its mutable result into an admission token.
    """
    from stadium_current_sss_exit_expected import (project_sss_expected_from_packet,
                                                  attach_stock4_prepared_profile)
    paths = verified_files(binding)
    operands = load(binding)
    table = json.loads(Path(__file__).with_name('stadium_later_reference_contexts.json').read_text())
    descriptor = table['contexts'][1]
    require(descriptor['id'] == 'original-b-2013-943-v1' and descriptor['profile'] == 3 and
            descriptor['recipe_sha256'] == RECIPE_SHA256 and descriptor['frames'] == 2013 and
            descriptor['first'][6] == operands['setup_global_cursor'] == 943,
            'new Gate1 descriptor differs from checked original operand identity')
    current = operands['whole_session_context']
    rows = list(context.replay.iter_records(paths['primary']))
    topology = list(zip(table['scenes'], descriptor['first'], descriptor['last']))
    value = project_sss_expected_from_packet(binding, current, rows, match_index=1,
                                              later_topology=topology)
    value = attach_stock4_prepared_profile(value, current, match_index=1, frames=descriptor['frames'])
    require(value['recipe']['sha256'] == descriptor['recipe_sha256'] and
            value['recipe']['bytes'] == descriptor['recipe_bytes'] and
            value['source']['sequence'] == descriptor['sss_sequence'] and
            value['prepared_profile']['entry']['sequence'] == descriptor['entry_sequence'] and
            value['prepared_profile']['setup']['sequence'] == descriptor['setup_sequence'] and
            value['prepared_profile']['entry']['rng']['value_hex'] == descriptor['entry_seed'] and
            value['prepared_profile']['setup']['rng']['value_hex'] == descriptor['setup_seed'],
            'new Gate1 phase coordinates/seeds are not actual same-run observations')
    value.update(schema='melee-web-stock4-none-later-sss-reference-expectation-v1',
                 observation_profile=descriptor['profile'], selected_occurrence=1,
                 preceding_spans=current['spans'][:6], menu_session_ticks=descriptor['menu_ticks'],
                 preceding_profile=current['matches'][0], reference_context=descriptor,
                 reference_order_contract=descriptor['notification_order_contract'],
                 original_B_constructor_GO=descriptor['original_B_constructor_GO'],
                 runtime_admission=False)
    verified_files(binding)
    return value
