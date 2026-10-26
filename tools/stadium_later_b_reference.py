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
from pathlib import Path
import stat

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
GATE2_SCHEMA = 'melee-web-stadium-later-b-gate2-reference-projection-v1'
GATE2_REFERENCE_ID = 'original-b-2013-943-v1'
GATE2_ACTION_STEPS = 180
GATE2_GO_STATES = 96
GATE2_PREFIX_INPUTS = 943
MAX_BINDING_BYTES = 64 * 1024


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
