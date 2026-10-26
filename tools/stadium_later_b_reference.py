"""Exact new-reference operands only; deliberately no browser/native admission.

The existing 2006/940 runtime route remains separate. This adapter reuses the
strict original parsers; it neither writes guest state nor converts reference
observations into a claim about the browser.
"""
from __future__ import annotations
import copy
import hashlib
import json
import os
import re
from pathlib import Path
import stat

import stadium_current_context_packet as context
import reference_input_stream as si
from reference_observer_stream import SLICE_NAMES
from stadium_go_prefix import (CONSTRUCTOR_POINTS, StadiumConstructorObservations,
                               _validate_progress, _validate_stadium_idle_state_record)

SCHEMA = 'melee-web-stadium-later-b-reference-operands-v1'
REPLAY_SCHEMA = 'melee-web-stadium-later-b-reference-replay-operands-v2'
VARIANT = 'private-original-b-2013-943-reference-only'
ROLES = {'primary', 'primary_status', 'si', 'si_status', 'launch', 'report',
         'setup_receipt', 'producer_manifest'}
RECIPE_SHA256 = '5fb53bdc71b35b800bc3451b1d9117709edef2413a38a8f7311ee352647a6755'
GATE2_SCHEMA = 'melee-web-stadium-later-b-gate2-reference-projection-v1'
GATE2_REFERENCE_ID = 'original-b-2013-943-v1'
GATE2_ACTION_STEPS = 180
GATE2_GO_STATES = 96
GATE2_PREFIX_INPUTS = 943
REPLAY_ROLES = ROLES | {'replay_source_binding', 'replay_status'}
MAX_BINDING_BYTES = 64 * 1024


def require(value, message):
    if not value:
        raise ValueError(message)


def verified_files(binding):
    require(set(binding) == {'schema', 'files'} and binding['schema'] in (SCHEMA, REPLAY_SCHEMA),
            'unknown operand binding schema or field')
    files = binding['files']
    require(set(files) == (ROLES if binding['schema'] == SCHEMA else REPLAY_ROLES),
            'unknown or missing consumed operand role')
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


def _progress_coordinate(row):
    payload = row['payload']
    return {'sequence': row['seq'], 'source_tick': row['source_tick'],
            'draw_ordinal': row['draw_ordinal'],
            'match_index': payload['match_index'], 'phase': payload['phase'],
            'pc': payload['pc'], 'lr': payload['lr']}


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
    by_sequence = {row['seq']: row for row in rows}
    constructor_coordinates = [_progress_coordinate(by_sequence[sequence])
                              for sequence in construction['source_sequences']]
    # Retain actual consume/tick/draw coordinates. No synthetic wire action or PAD.
    return {'schema': SCHEMA, 'variant': VARIANT, 'runtime_admission': False,
            'same_run_provenance_verified': False,
            'scope': 'original-reference operand derivation; producer approval remains external',
            'recipe_sha256': RECIPE_SHA256, 'recipe_bytes': len(recipe),
            'whole_session_context': packet, 'selected_match': 1,
            'setup_global_cursor': packet['spans'][6]['first_frame'],
            'entry': entry, 'setup': setup, 'constructor': construction,
            'constructor_original_witnesses': constructor_coordinates,
            'go_source_coordinate': _progress_coordinate(go),
            'go': go_projection, 'go_rng': _named_seed(go),
            'source_steps_before_pause': steps[:stop],
            'pre_pause_states': states[:stop-go_tick],
            'pre_pause_state_rng': state_rng[:stop-go_tick],
            'natural_pause_tail_unrun': states[stop-go_tick:],
            'natural_pause_tail_rng_unrun': state_rng[stop-go_tick:],
            'stop_before_consume': steps[stop]['consume'],
            'registration_parity': False, 'per_owner_rng_parity': False,
            'browser_parity': False, 'C3': False}


def _binding_stat_identity(info):
    return (info.st_dev, info.st_ino, info.st_mode, info.st_size,
            info.st_mtime_ns, info.st_ctime_ns)


def _read_binding_snapshot(binding_path):
    """Read exact bytes from one stable regular non-symlink binding file."""
    path = Path(binding_path)
    nofollow = getattr(os, 'O_NOFOLLOW', None)
    require(nofollow is not None, 'binding no-follow file open is unavailable')
    try:
        before = os.lstat(path)
        require(stat.S_ISREG(before.st_mode) and not stat.S_ISLNK(before.st_mode),
                'binding must be a regular non-symlink file')
        require(before.st_size <= MAX_BINDING_BYTES, 'binding exceeds the bounded file size')
        fd = os.open(path, os.O_RDONLY | nofollow)
    except (OSError, TypeError, ValueError) as error:
        raise ValueError(f'cannot open exact binding file: {error}') from error
    try:
        opened = os.fstat(fd)
        require(stat.S_ISREG(opened.st_mode) and
                _binding_stat_identity(before) == _binding_stat_identity(opened),
                'binding file identity changed before read')
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            raw = stream.read(MAX_BINDING_BYTES + 1)
        after_fd = os.fstat(fd)
    finally:
        os.close(fd)
    try:
        after_path = os.lstat(path)
    except OSError as error:
        raise ValueError(f'binding file disappeared after read: {error}') from error
    require(len(raw) <= MAX_BINDING_BYTES and len(raw) == before.st_size and
            _binding_stat_identity(before) == _binding_stat_identity(after_fd) ==
            _binding_stat_identity(after_path),
            'binding file changed during read')
    return raw, _binding_stat_identity(after_path)


def build_gate2_projection(binding_path):
    """Build source-reference Gate2 rows from one exact retained binding file.

    The exact-byte hash identifies this host input file. It is not a native
    admission token; consumers must revalidate the complete sealed identity.
    """
    raw_binding, file_identity = _read_binding_snapshot(binding_path)
    try:
        binding = json.loads(raw_binding.decode('utf-8'))
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise ValueError(f'binding is not valid UTF-8 JSON: {error}') from error
    require(isinstance(binding, dict), 'binding JSON root must be an object')
    operands = load(binding)

    steps = operands['source_steps_before_pause']
    states = operands['pre_pause_states']
    state_rng = operands['pre_pause_state_rng']
    packet = operands['whole_session_context']
    stop = operands['stop_before_consume']
    go_tick = operands['go']['source_tick']
    require(operands['recipe_sha256'] == RECIPE_SHA256 and
            operands['setup_global_cursor'] == GATE2_PREFIX_INPUTS and
            len(packet['inputs']) == 2013 and
            len(packet['inputs'][:GATE2_PREFIX_INPUTS]) == GATE2_PREFIX_INPUTS,
            'strict source packet differs from exact Gate2 reference dimensions')
    require(len(steps) == GATE2_ACTION_STEPS and len(states) == GATE2_GO_STATES and
            len(state_rng) == GATE2_GO_STATES and stop['source_tick'] == GATE2_ACTION_STEPS,
            'strict source interval differs from the bounded Gate2 extent')
    require(go_tick + GATE2_GO_STATES == stop['source_tick'],
            'actual GO-to-pause state interval differs from 96 rows')

    actions = []
    for local_tick, step in enumerate(steps):
        require(step['tick']['source_tick'] == local_tick and
                step['consume']['source_tick'] == local_tick and
                step['draw_enter']['source_tick'] == local_tick + 1 and
                step['draw_return']['source_tick'] == local_tick + 1 and
                len(step['ports']) == 4 and all(len(port) == 22 for port in step['ports']),
                'actual source tick/draw/PAD tuple escaped Gate2 coordinates')
        actions.append({'action_index': 2 * local_tick, 'kind': 'tick',
                        'source_tick': step['tick']['source_tick'],
                        'consume': step['consume'], 'tick': step['tick'],
                        'pads': list(step['ports'])})
        actions.append({'action_index': 2 * local_tick + 1, 'kind': 'draw',
                        'source_tick': step['draw_return']['source_tick'],
                        'draw_enter': step['draw_enter'],
                        'draw_return': step['draw_return']})

    go_step = steps[go_tick]
    go_coordinate = operands['go_source_coordinate']
    require(go_step['consume']['sequence'] < go_coordinate['sequence'] <
            go_step['tick']['sequence'], 'actual GO marker escaped its consume/tick bracket')
    projected_states = []
    for offset, (state, rng) in enumerate(zip(states, state_rng)):
        step = steps[go_tick + offset]
        require(state['source_tick'] == go_tick + offset and
                step['tick']['source_tick'] == state['source_tick'],
                'post-tick state escaped its actual GO-to-pause source tick')
        projected_states.append({'tick': step['tick'], 'state': state, 'rng': rng})

    constructor_state = operands['constructor']['state_projection']
    require(constructor_state['constructor_entry_count'] == 1 and
            constructor_state['random_call_observed'] is True and
            constructor_state['random_bound_valid'] is True and
            constructor_state['random_result_valid'] is True,
            'checked shared constructor aggregate is incomplete')
    constructor_projection = {
        'scope': 'original-and-native-shared-aggregate-only',
        'entry_count': constructor_state['constructor_entry_count'],
        'parameter_roles': {'min_parameter': 'parameter_x0',
                            'max_parameter': 'parameter_x4'},
        'min_parameter': constructor_state['parameter_x0'],
        'max_parameter': constructor_state['parameter_x4'],
        'random_bound': constructor_state['random_bound'],
        'random_result': constructor_state['random_result'],
        'seed_before': constructor_state['seed_before_hex'],
        'seed_after': constructor_state['seed_after_hex'],
    }
    witnesses = operands['constructor_original_witnesses']
    require(len(witnesses) == 5 and
            [witness['sequence'] for witness in witnesses] ==
            operands['constructor']['source_sequences'],
            'original constructor witness order differs from strict accepted source rows')

    entry = operands['entry']
    setup = operands['setup']
    projected = {
        'schema': GATE2_SCHEMA,
        'reference_id': GATE2_REFERENCE_ID,
        'binding_sha256': hashlib.sha256(raw_binding).hexdigest(),
        'recipe_sha256': operands['recipe_sha256'],
        'selected_match': 1,
        'setup_global_cursor': GATE2_PREFIX_INPUTS,
        'menu_prefix': {
            'scope': 'global literal input prefix; distinct from local B action indices',
            'input_count': GATE2_PREFIX_INPUTS,
            'inputs': packet['inputs'][:GATE2_PREFIX_INPUTS],
        },
        'source_spans': packet['spans'],
        'entry': {key: entry[key] for key in
                  ('sequence', 'source_tick', 'draw_ordinal', 'match_index',
                   'boundary', 'pc', 'lr', 'gprs')} | {'seed': entry['rng']['value_hex']},
        'setup': {key: setup[key] for key in
                  ('sequence', 'source_tick', 'draw_ordinal', 'match_index',
                   'boundary', 'pc', 'lr', 'gprs')} | {'seed': setup['rng']['value_hex']},
        'constructor_shared_projection': constructor_projection,
        'constructor_original_witnesses': {
            'scope': 'original-only order; no browser phase-dispatch pairing',
            'coordinates': witnesses,
        },
        'actions': actions,
        'go': {'coordinate': go_coordinate, 'state': operands['go'],
               'rng': operands['go_rng']},
        'states': projected_states,
        'stop_before_consume': stop,
        'unpaired': ['original_constructor_phase_dispatch', 'per_owner_rng',
                     'registration', 'all_writers', 'C3'],
        'runtime_admission': False,
        'same_run_provenance_verified': False,
        'browser_parity': False,
        'C3': False,
    }
    final_raw, final_identity = _read_binding_snapshot(binding_path)
    require(final_identity == file_identity and final_raw == raw_binding,
            'binding file changed during Gate2 projection')
    return projected


def build_gate2_constructor_phase_projection(binding_path):
    """Derive an explicit constructor-phase sidecar from retained original rows.

    This uses the same checked operand binding and StadiumConstructorObservations
    parser as the aggregate/action projection. It adds no native/runtime authority
    and leaves the version-1 Gate2 projection bytes and meaning unchanged.
    """
    raw_binding, file_identity = _read_binding_snapshot(binding_path)
    try:
        binding = json.loads(raw_binding.decode('utf-8'))
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise ValueError(f'binding is not valid UTF-8 JSON: {error}') from error
    require(isinstance(binding, dict), 'binding JSON root must be an object')
    operands = load(binding)
    paths = verified_files(binding)
    rows = list(context.replay.iter_records(paths['primary']))
    source = StadiumConstructorObservations()
    entry, setup = operands['entry'], operands['setup']
    sequences = operands['constructor']['source_sequences']
    require(len(sequences) in (3, 5), 'strict original constructor phase count is unsupported')
    for sequence in sequences:
        require(type(sequence) is int and 0 <= sequence < len(rows) and rows[sequence]['seq'] == sequence,
                'strict constructor sequence no longer indexes its raw row')
        row = rows[sequence]
        require(entry['sequence'] < sequence < setup['sequence'],
                'constructor phase escaped the selected Entry/Setup interval')
        source.accept(row, route_ready=True, scope='stadium_whole3_feedback_v1',
                      expected_receipt=binding['files']['setup_receipt']['sha256'],
                      match_index=1)
    observed = source.finish(include_state_projection=True)
    state = observed['state_projection']
    by_phase = {row['payload']['phase']: row for row in source.rows}
    expected_phases = (['constructor_entry', 'constructor_parameters',
                        f"constructor_{observed['branch']}_call",
                        f"constructor_{observed['branch']}_return", 'constructor_commit']
                       if observed['branch'] != 'equal' else
                       ['constructor_entry', 'constructor_parameters', 'constructor_commit'])
    require([row['payload']['phase'] for row in source.rows] == expected_phases,
            'strict original constructor phase order differs')
    events = []
    for ordinal, (row, phase) in enumerate(zip(source.rows, expected_phases), 1):
        payload = row['payload']
        seed_slices = [item for item in payload['slices'] if item.get('tag') == 20]
        require(len(seed_slices) == 1 and len(bytes.fromhex(seed_slices[0]['hex'])) == 4,
                'strict constructor phase lacks its actual dynamic seed value')
        seed_hex = bytes.fromhex(seed_slices[0]['hex']).hex()
        event = {
            'ordinal': ordinal,
            'phase': phase.removeprefix('constructor_'),
            'parameter_values_valid': phase != 'constructor_entry',
            'parameter_x0': state['parameter_x0'] if phase != 'constructor_entry' else 0,
            'parameter_x4': state['parameter_x4'] if phase != 'constructor_entry' else 0,
            'scalar_value_valid': phase not in ('constructor_entry', 'constructor_parameters'),
            'scalar_value': 0,
            'source_seed_valid': True,
            'source_seed_hex': seed_hex,
        }
        if phase.endswith('_call'):
            event['scalar_value'] = payload['constructor_gprs'][0]
            event['scalar_kind'] = 'randi_bound'
        elif phase.endswith('_return'):
            event['scalar_value'] = payload['constructor_gprs'][0]
            event['scalar_kind'] = 'randi_result'
        elif phase == 'constructor_commit':
            countdown = [item for item in payload['slices'] if item.get('tag') == 70]
            require(len(countdown) == 1 and len(bytes.fromhex(countdown[0]['hex'])) == 4,
                    'strict constructor commit lacks its actual stored countdown')
            event['scalar_value'] = int.from_bytes(bytes.fromhex(countdown[0]['hex']),
                                                   'big', signed=True)
            event['scalar_kind'] = 'stored_countdown'
        events.append(event)
    require(observed['branch'] in ('up', 'down', 'equal'), 'unknown original constructor branch')
    result = {
        'schema': 'melee-web-stadium-original-b-constructor-phase-projection-v1',
        'reference_id': GATE2_REFERENCE_ID,
        'binding_sha256': hashlib.sha256(raw_binding).hexdigest(),
        'semantic_owner': {
            'id': 'selected-match-1/map2/constructor-callback-1',
            'selected_match': 1,
            'native_role': 'map2/constructor-callback-1',
            'map_id': 2,
            'constructor_callback_ordinal': 1,
        },
        'branch': observed['branch'],
        'events': events,
        'source_coordinates': [_progress_coordinate(row) for row in source.rows],
        'runtime_admission': False,
        'browser_parity': False,
        'C3': False,
    }
    final_raw, final_identity = _read_binding_snapshot(binding_path)
    require(final_identity == file_identity and final_raw == raw_binding,
            'binding file changed during constructor phase projection')
    return result


def compare_gate2_constructor_phase_trace(expected, actual_setup_state):
    """Strictly compare the explicit original sidecar to the actual Setup receipt.

    The selected original match is reference provenance; the native receipt only
    claims its generic map-2 constructor-callback role. This comparator maps
    that role to the selected original context without claiming native match
    selection. PPC/native addresses and ticks remain separate provenance.
    """
    fields = []
    try:
        require(isinstance(expected, dict) and expected.get('schema') ==
                'melee-web-stadium-original-b-constructor-phase-projection-v1',
                'original phase projection schema differs')
        require(set(expected) == {'schema', 'reference_id', 'binding_sha256',
                'semantic_owner', 'branch', 'events', 'source_coordinates',
                'runtime_admission', 'browser_parity', 'C3'},
                'original phase projection fields differ')
        require(expected['reference_id'] == GATE2_REFERENCE_ID and
                re.fullmatch(r'[0-9a-f]{64}', expected['binding_sha256']) is not None and
                expected['runtime_admission'] is False and
                expected['browser_parity'] is False and expected['C3'] is False,
                'original phase projection identity/scope differs')
        semantic = expected['semantic_owner']
        require(semantic == {'id': 'selected-match-1/map2/constructor-callback-1',
                'selected_match': 1, 'native_role': 'map2/constructor-callback-1',
                'map_id': 2, 'constructor_callback_ordinal': 1},
                'original semantic owner identity/mapping differs')
        require(expected['branch'] in ('up', 'down', 'equal') and
                isinstance(expected['events'], list) and
                len(expected['events']) == (3 if expected['branch'] == 'equal' else 5),
                'original constructor phase extent/branch differs')
        phases = (['entry', 'parameters', 'commit'] if expected['branch'] == 'equal' else
                  ['entry', 'parameters', f"{expected['branch']}_call",
                   f"{expected['branch']}_return", 'commit'])
        expected_event_keys = {'ordinal', 'phase', 'parameter_values_valid',
            'parameter_x0', 'parameter_x4', 'scalar_value_valid', 'scalar_value',
            'source_seed_valid', 'source_seed_hex'}
        for index, (event, phase) in enumerate(zip(expected['events'], phases), 1):
            keys = set(expected_event_keys)
            if phase in ('up_call', 'down_call', 'up_return', 'down_return', 'commit'):
                keys.add('scalar_kind')
            require(set(event) == keys and event['ordinal'] == index and
                    event['phase'] == phase and event['source_seed_valid'] is True and
                    re.fullmatch(r'[0-9a-f]{8}', event['source_seed_hex']) is not None,
                    'original constructor event schema/order/seed differs')
            if phase == 'entry':
                require(event['parameter_values_valid'] is False and
                        event['scalar_value_valid'] is False,
                        'original entry has unexpected constructor values')
            else:
                require(event['parameter_values_valid'] is True and
                        type(event['parameter_x0']) is int and type(event['parameter_x4']) is int,
                        'original constructor parameter phase lacks actual values')
            scalar_kind = ('randi_bound' if phase.endswith('_call') else
                           'randi_result' if phase.endswith('_return') else
                           'stored_countdown' if phase == 'commit' else None)
            if scalar_kind is None:
                require(event['scalar_value_valid'] is False and event['scalar_value'] == 0,
                        'original constructor phase has an unexpected scalar')
            else:
                require(event['scalar_value_valid'] is True and event['scalar_kind'] == scalar_kind and
                        type(event['scalar_value']) is int,
                        'original constructor scalar phase differs')
        require(isinstance(actual_setup_state, dict) and
                actual_setup_state.get('schema') == 'melee-web-stadium-zero-tick-setup-state-v1',
                'actual zero-tick Setup state schema differs')
        trace = actual_setup_state.get('constructor_phase_trace')
        require(isinstance(trace, dict) and set(trace) ==
                {'schema', 'scope', 'owner', 'event_count', 'events'} and
                trace['schema'] == 'melee-web-stadium-constructor-phase-trace-v1' and
                trace['scope'] == 'diagnostic-zero-tick-setup-owner-observation-only',
                'actual constructor phase trace schema/scope differs')
        owner = trace['owner']
        require(isinstance(owner, dict) and set(owner) ==
                {'semantic_id', 'world_generation', 'map_id', 'constructor_callback_ordinal'} and
                owner['semantic_id'] == semantic['native_role'] == 'map2/constructor-callback-1' and
                semantic['id'] == 'selected-match-1/map2/constructor-callback-1' and
                semantic['selected_match'] == 1 and owner['map_id'] == semantic['map_id'] == 2 and
                owner['constructor_callback_ordinal'] == semantic['constructor_callback_ordinal'] == 1 and
                type(owner['world_generation']) is int and owner['world_generation'] > 0 and
                owner['world_generation'] == actual_setup_state.get('world_generation'),
                'actual semantic Stage owner or generation differs')
        require(actual_setup_state.get('world_ticks') == 0 and
                actual_setup_state.get('source_frame') == 0 and
                actual_setup_state.get('map_id') == 2 and
                actual_setup_state.get('source_parameter_roots_agree') is True and
                actual_setup_state.get('constructor_callback_count') == 1,
                'actual Setup owner is not the checked zero-tick map-2 constructor')
        actual_events = trace.get('events')
        require(trace.get('event_count') == len(expected['events']) and
                isinstance(actual_events, list) and len(actual_events) == len(expected['events']),
                'actual constructor phase count differs')
        for index, (want, got) in enumerate(zip(expected['events'], actual_events)):
            require(isinstance(got, dict) and got.get('ordinal') == want['ordinal'] and
                    got.get('phase') == want['phase'] and
                    got.get('parameter_values_valid') is want['parameter_values_valid'] and
                    got.get('parameter_x0') == want['parameter_x0'] and
                    got.get('parameter_x4') == want['parameter_x4'] and
                    got.get('scalar_value_valid') is want['scalar_value_valid'] and
                    got.get('scalar_value') == want['scalar_value'] and
                    got.get('source_seed_valid') is True and
                    got.get('source_seed_hex') == want['source_seed_hex'] and
                    got.get('scalar_kind') == want.get('scalar_kind'),
                    f'constructor phase {index + 1} differs')
        parameter = expected['events'][1]
        require(actual_setup_state.get('parameter_x0') == parameter['parameter_x0'] and
                actual_setup_state.get('parameter_x4') == parameter['parameter_x4'] and
                actual_setup_state.get('source_seed_before_hex') == expected['events'][0]['source_seed_hex'] and
                actual_setup_state.get('source_seed_after_hex') == expected['events'][-1]['source_seed_hex'] and
                actual_setup_state.get('initial_countdown') == expected['events'][-1]['scalar_value'] and
                actual_setup_state.get('current_countdown') == expected['events'][-1]['scalar_value'],
                'actual Setup aggregate differs from its constructor phase trace')
        if expected['branch'] == 'equal':
            require(actual_setup_state.get('constructor_randi_call_count') == 0 and
                    actual_setup_state.get('constructor_randi_bound') is None and
                    actual_setup_state.get('constructor_randi_result') is None,
                    'equal branch unexpectedly reports a Randi call')
        else:
            call, returned = expected['events'][2:4]
            require(actual_setup_state.get('constructor_randi_call_count') == 1 and
                    actual_setup_state.get('constructor_randi_bound') == call['scalar_value'] and
                    actual_setup_state.get('constructor_randi_result') == returned['scalar_value'],
                    'actual Setup Randi aggregate differs from phase scalars')
        fields = ['semantic_owner.map_id', 'semantic_owner.constructor_callback_ordinal',
            'event.ordinal', 'event.phase', 'event.parameter_x0', 'event.parameter_x4',
            'event.randi_bound', 'event.randi_result', 'event.stored_countdown',
            'event.source_seed_hex', 'actual.world_generation']
        return {'schema': 'melee-web-stadium-constructor-phase-comparison-v1',
            'decision': 'PASS_EXACT_ORIGINAL_PHASES_AND_NATIVE_SETUP_SCALARS',
            'compared_fields': fields, 'first_mismatch': None,
            'source_coordinates_compared': False, 'runtime_admission': False, 'C3': False}
    except (KeyError, TypeError, ValueError) as error:
        return {'schema': 'melee-web-stadium-constructor-phase-comparison-v1',
            'decision': 'REFUSE_CONSTRUCTOR_PHASE_TRACE',
            'compared_fields': fields, 'first_mismatch': str(error),
            'source_coordinates_compared': False, 'runtime_admission': False, 'C3': False}


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
    mode = launch.get('input_mode', 'record')
    require(mode in ('record', 'replay') and
            (mode == 'replay') == (binding['schema'] == REPLAY_SCHEMA),
            'input mode requires its explicit record or replay binding schema')
    if mode == 'replay':
        validate_replay_operand_source(binding, paths, launch, report, strict, events)
    verified_files(binding)
    return result


def validate_replay_operand_source(binding, paths, launch, report, strict, events):
    # Retained record input and status are distinct from CURRENT replay status.
    raw, _ = _read_binding_snapshot(paths['replay_source_binding'])
    source_binding = json.loads(raw)
    require(source_binding.get('schema') == SCHEMA,
            'replay source must be an original record binding, never recursive replay')
    source_paths = verified_files(source_binding)
    source_operands = load(source_binding)  # All unchanged original context guards.
    for role in ('si', 'si_status'):
        require(binding['files'][role] == source_binding['files'][role],
                'replay retained input/status role differs from original binding')
    receipt = launch.get('input_replay')
    require(isinstance(receipt, dict) and report.get('input_replay') == receipt and
            receipt.get('binding_sha256') == hashlib.sha256(raw).hexdigest() and
            Path(receipt.get('binding_path', '')) == paths['replay_source_binding'] and
            receipt.get('source_files') == source_binding['files'] and
            Path(receipt.get('source', '')) == paths['si'] and
            receipt.get('summary') == events and report['input_stream'] == events,
            'current replay launch/report and original source binding differ')
    environment = launch['environment_delta']
    require(environment.get('MWRC_STADIUM_WHOLE_REPLAY_BINDING') == receipt['binding_sha256'] and
            environment.get('MWRC_INPUT_REPLAY') == str(paths['si']) and
            not environment.get('MWRC_INPUT_RECORD') and
            environment.get('MWRC_INPUT_STATUS') == str(paths['replay_status']),
            'current replay native input-source environment differs')
    replay_status = si.validate_status(paths['replay_status'], mode='replay',
                                      events=events['events'], require_complete=True)
    require(report.get('input_replay_status') == replay_status,
            'current replay completion status differs from report')
    previous = context.replay.capture_from_path(source_paths['primary'], source_paths['primary_status'])
    require(context.replay.encode_v8(previous)[0] == context.replay.encode_v8(strict)[0],
            'current replay consumed recipe differs from original source')
    current_rows = list(context.replay.iter_records(paths['primary']))
    current_packet = context.packet_from_records(current_rows, launch['expected'])
    previous_packet = source_operands['whole_session_context']
    for prior_match, current_match in zip(previous_packet['matches'], current_packet['matches']):
        for phase in ('entry', 'setup'):
            require(prior_match[phase]['rng']['value_hex'] == current_match[phase]['rng']['value_hex'],
                    'current replay actual Entry/Setup seed differs')
    for row in current_rows[:2]:
        require(row['payload'].get('stadium_whole_replay_binding_sha256') == receipt['binding_sha256'],
                'current replay raw announcement lacks bound source identity')
    verified_files(source_binding)


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
