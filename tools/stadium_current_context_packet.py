"""Host-only current Stadium context/expectations; no guest writes or replay.

Reuse strict whole-session, StartMeleeData, consumed queue and original RNG
endpoint decoders. Historical MWRC and Stadium fixture adapters stay unchanged.
Every expectation remains host-side; this is not browser admission or C3.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
from pathlib import Path
import sys

import whole_session_replay as replay
from retail_setup_validation import _decode_setup, _validate_expected_setup

SCHEMA = 'melee-web-stadium-current-context-host-packet'
FILES = {'primary', 'primary_status', 'allocations', 'allocation_profile',
         'target', 'setup_receipt', 'gci', 'launch', 'offline_report'}

class CurrentContextError(ValueError):
    pass

def require(condition, message):
    if not condition:
        raise CurrentContextError(message)

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def coordinate(row):
    p = row['payload']
    return {'sequence': row['seq'], 'source_tick': row['source_tick'],
            'draw_ordinal': row['draw_ordinal'], 'match_index': p['match_index'],
            'boundary': p['boundary'], 'pc': p['pc'], 'lr': p['lr'],
            'gprs': [replay._gpr(p, i, p['boundary']) for i in range(32)]}

def seed(row):
    # Reuse the strict single-slice byte decoder. Unlike historical fixtures,
    # the value address is read from the actual original pointer slot.
    pointer, raw_pointer = replay._slice(row, 'rng_pointer', 4, 'RNG endpoint')
    value, raw_value = replay._slice(row, 'rng_value', 4, 'RNG endpoint')
    address = int.from_bytes(raw_pointer, 'big')
    require(pointer['address'] == 0x804D5F94 and address % 4 == 0 and
            0x80000000 <= address <= 0x817FFFFC and value['address'] == address,
            'RNG endpoint escaped its original pointer slot/value owner')
    return {'pointer_slot_address': pointer['address'], 'pointer_hex': raw_pointer.hex(),
            'value_address': value['address'], 'value_hex': raw_value.hex()}

def match_steps(rows, match, setup, exit_row):
    """Current observed one-tick/one-draw intervals, keeping both coordinates.

    Source semantics: reference_capture_semantics.py source_tick increments
    frame_count after checking scene_frame; DrawReturn checks scene_frame ==
    frame_count. Thus source tick0 and the following draw at frame_count1 are
    distinct actual coordinates. This adapter doesn't change draw scheduling.
    """
    steps = []
    pending = None
    drawing = None
    for row in rows:
        p = row.get('payload', {})
        if row['event'] != 'boundary' or p.get('match_index') != match:
            continue
        if not setup['seq'] < row['seq'] < exit_row['seq']:
            continue
        name = p['boundary']
        if name in ('pad_consume', 'source_tick', 'draw_enter', 'draw_return'):
            require(replay.semantics.BOUNDARIES.get(p['pc']) == name,
                    'Step boundary disagrees with original source PC')
        if name == 'pad_consume':
            require(pending is None and drawing is None, 'Repeated/stale in-match consume')
            pending = row
        elif name == 'source_tick':
            require(pending is not None and drawing is None and
                    (not steps or 'draw_return' in steps[-1]), 'Tick missing fresh consume/draw closure')
            require(row['source_tick'] == len(steps) == pending['source_tick'],
                    'Tick/consume source order differs')
            item, raw = replay._slice(row, 'scene_frame', 4, 'source tick')
            require(item['address'] == 0x80479D58 and int.from_bytes(raw, 'big') == len(steps),
                    'Source tick disagrees with recorded scene-frame owner')
            steps.append({'consume': coordinate(pending),
                          'ports': replay._consumed_ports(pending, pending['seq']),
                          'tick': coordinate(row)})
            pending = None
        elif name == 'draw_enter':
            require(steps and pending is None and drawing is None and
                    'draw_enter' not in steps[-1], 'Missing tick or nested/duplicate draw')
            require(row['source_tick'] == len(steps) and row['draw_ordinal'] == len(steps)-1,
                    'Draw differs from actual source frame-count relation')
            drawing = row
            steps[-1]['draw_enter'] = coordinate(row)
        elif name == 'draw_return':
            require(drawing is not None and row['source_tick'] == drawing['source_tick'] and
                    row['draw_ordinal'] == drawing['draw_ordinal'], 'Unpaired/reordered draw return')
            item, raw = replay._slice(row, 'scene_frame', 4, 'draw return')
            require(item['address'] == 0x80479D58 and int.from_bytes(raw, 'big') == len(steps),
                    'Draw return disagrees with recorded scene-frame owner')
            steps[-1]['draw_return'] = coordinate(row)
            drawing = None
    require(steps and pending is None and drawing is None and 'draw_return' in steps[-1],
            'Incomplete final tick/draw/consume interval')
    require(exit_row['source_tick'] == len(steps), 'Exit does not close the observed tick interval')
    return steps

def packet_from_records(rows, expected):
    """Pure actual-decoder core, also used by retained-row falsification tests."""
    expected = _validate_expected_setup(expected)
    capture = replay.capture_from_records(rows)
    require(capture['identity']['match_count'] == 3, 'Current packet requires three recorded lifetimes')
    require(capture['declared_setups'] == [expected]*3, 'Target differs from original typed Entry')
    require(len(set(capture['setup_hexes'])) == 1, 'Repeated raw Entry setups differ')
    by_seq = {row['seq']: row for row in rows}
    require(len(by_seq) == len(rows), 'Duplicate raw sequence')
    all_inputs = []
    for frame in capture['frames']:
        row = by_seq[frame['source_seq']]
        require(replay.semantics.BOUNDARIES.get(row['payload']['pc']) == 'pad_consume',
                'Consumed input disagrees with original source PC')
        # _timeline already decoded every lifecycle-owned queue exactly once.
        queue, raw_queue = replay._slice(row, 'pad_queue', 0xC, 'lifecycle consume')
        slot, raw_slot = replay._slice(row, 'pad_slot', 0x30, 'lifecycle consume')
        all_inputs.append({**frame, **coordinate(row), 'queue_address': queue['address'],
                           'queue_hex': raw_queue.hex(), 'slot_address': slot['address'],
                           'slot_hex': raw_slot.hex()})
    matches = []
    for match in range(3):
        boundaries = {}
        for name in ('entry', 'setup', 'vs_exit', 'vs_exit_return'):
            selected = [r for r in rows if r['event'] == 'boundary' and
                        r['payload'].get('match_index') == match and r['payload'].get('boundary') == name]
            require(len(selected) == 1, f'Missing/duplicate {name} for lifetime {match}')
            boundaries[name] = selected[0]
        endpoints = {}
        for name in ('entry', 'setup'):
            row = boundaries[name]
            item, raw = replay._slice(row, 'match_setup', replay.GAME_INFO_SIZE, name)
            require(item['address'] == replay._gpr(boundaries['entry']['payload'], 3, 'entry'),
                    'Entry/Setup raw block changed source owner')
            require(raw.hex() == capture['setup_hexes'][match] and _decode_setup(raw.hex()) == expected,
                    'Entry/Setup full raw or typed StartMeleeData differs')
            endpoints[name] = {**coordinate(row), 'start_melee_hex': raw.hex(),
                               'start_melee_sha256': hashlib.sha256(raw).hexdigest(),
                               'declared_setup': _decode_setup(raw.hex()), 'rng': seed(row),
                               'raw_slices': row['payload']['slices']}
        matches.append({'match_index': match, **endpoints,
                        'steps': match_steps(rows, match, boundaries['setup'], boundaries['vs_exit']),
                        'exit': coordinate(boundaries['vs_exit']),
                        'exit_return': coordinate(boundaries['vs_exit_return'])})
    css_row = by_seq[capture['first_css']['observer_seq']]
    return {'schema': SCHEMA, 'version': 1, 'scope': 'host-only current original context and expectations',
            'first_css': capture['first_css'], 'first_css_source': {**coordinate(css_row), 'rng': seed(css_row),
            'raw_slices': css_row['payload']['slices']}, 'identity': capture['identity'],
            'lifecycle': capture['lifecycle'], 'inputs': all_inputs, 'spans': capture['spans'],
            'matches': matches, 'repeated_raw_setup_equal': True,
            'mwrc_v8_same_setup_prerequisite': True,
            'menu_draw_schedule': 'UNKNOWN', 'runtime_admission': False, 'C3': False}

def build_packet(binding):
    require(set(binding['files']) == FILES, 'Missing/extra packet input binding')
    paths = {}
    for name, spec in binding['files'].items():
        path = Path(spec['path']).resolve()
        require(path.is_file() and sha(path) == spec['sha256'], 'Source identity mismatch: '+name)
        paths[name] = path
    dependency_root = Path(replay.__file__).resolve().parents[1]
    required_source = ['tools/whole_session_replay.py', 'tools/retail_setup_validation.py',
        'tools/reference_capture_semantics.py', 'reference-capture/dolphin/reference_observer_stream.py',
        'tools/allocation_lifetime_replay.py', 'tools/compaction_manager_state.py',
        'tools/allocation_history_replay.py', 'scripts/capture_allocation_history.py',
        'reference-capture/dolphin/source/Core/PowerPC/ReferenceAllocationProfile.h']
    require(all(str(dependency_root/name) in binding['source_sha256'] for name in required_source),
            'Required actual decoder source binding missing')
    require(binding['adapter_sha256'] == sha(Path(__file__)), 'Adapter source identity mismatch')
    for path, digest in binding['source_sha256'].items():
        require(sha(Path(path)) == digest, 'Decoder source identity mismatch: '+path)
    target = json.loads(paths['target'].read_text())['expected_setup']
    launch = json.loads(paths['launch'].read_text())
    receipt = json.loads(paths['setup_receipt'].read_text())
    require(launch['expected'] == target and launch['setup']['sha256'] == binding['files']['setup_receipt']['sha256'],
            'Launch/target/setup-receipt identity mismatch')
    gci_sha = binding['files']['gci']['sha256']
    require(launch['owned_gci']['sha256'] == gci_sha == launch['setup']['initial_profile_sha256'] ==
            receipt['initial_card']['gci_sha256'] == receipt['source_profile_export']['exported_gci_sha256'],
            'Admitted original GCI/profile identity mismatch')
    # Existing decoder validates CRC, contiguous raw records, whole lifecycle,
    # firstCSS, exact typed/raw Entry and every lifecycle-owned four-port queue.
    capture = replay.capture_from_path(paths['primary'], paths['primary_status'])
    rows = list(replay.iter_records(paths['primary']))
    result = packet_from_records(rows, target)
    require(result['first_css'] == capture['first_css'], 'FirstCSS decode disagreement')
    sys.path[:0] = [str(dependency_root), str(dependency_root/'scripts')]
    from capture_allocation_history import join_stadium_allocation_samples
    from tools.allocation_lifetime_replay import validate_original_rng_scene_boundaries
    allocations = [json.loads(line) for line in paths['allocations'].read_text().splitlines()]
    require(allocations[0].get('vs_target') == 2 and allocations[0].get('stop_at') == 'exit',
            'Allocation scope differs from firsttwo exits')
    profile = json.loads(paths['allocation_profile'].read_text())
    # The producer's compiled profile identity is distinct from the amended
    # retained JSON profile file hash; preserve both, as the existing finalizer
    # does. Bind the actual producer header rather than equate those hashes.
    header_path = dependency_root/'reference-capture/dolphin/source/Core/PowerPC/ReferenceAllocationProfile.h'
    declared = re.findall(r'Profile SHA-256: ([0-9a-f]{64})', header_path.read_text())
    require(declared == [binding['producer_profile_sha256']] and
            allocations[0]['profile_sha256'] == declared[0], 'Allocation producer profile identity mismatch')
    rng = validate_original_rng_scene_boundaries(profile, allocations[0], allocations[-1], allocations)
    selected = [r for r in allocations if r.get('record') in ('enter', 'return') and
                r.get('function') in ('gm_Scene_Vs_OnEnter', 'gm_Scene_Vs_OnExit')]
    join_stadium_allocation_samples(selected, [r for r in rows if r['event'] == 'boundary'])
    require(rng['sample_count'] == 8, 'Missing original RNG endpoints')
    labels = {('gm_Scene_Vs_OnEnter', 'enter'): 'entry', ('gm_Scene_Vs_OnEnter', 'return'): 'setup',
              ('gm_Scene_Vs_OnExit', 'enter'): 'vs_exit', ('gm_Scene_Vs_OnExit', 'return'): 'vs_exit_return'}
    joined = []
    for i, side in enumerate(selected):
        name = labels[(side['function'], side['record'])]
        primary = next(r for r in rows if r['event'] == 'boundary' and
                       r['payload'].get('match_index') == i//4 and r['payload'].get('boundary') == name)
        require(seed(primary)['value_hex'] == f"{rng['samples'][i]['value']:08x}" and
                seed(primary)['value_address'] == rng['samples'][i]['value_address'],
                'Primary/sidecar dynamic RNG value differs')
        joined.append({'original_sample': rng['samples'][i], 'primary': coordinate(primary)})
    offline = json.loads(paths['offline_report'].read_text())
    require(offline['result'] == 'PASS' and offline['native']['primary_sha256'] == binding['files']['primary']['sha256'] and
            offline['native']['native_allocations_sha256'] == binding['files']['allocations']['sha256'],
            'Offline endpoint validation refers to other bytes')
    result.update(provenance=binding, original_rng_endpoints=joined,
                  rng_scope=rng['scope'], all_rng_writers_observed=False,
                  port_wrapper_correspondence='not claimed',
                  expectations_host_only=True)
    for name, spec in binding['files'].items():
        require(sha(paths[name]) == spec['sha256'], 'Input changed during packet construction: '+name)
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binding', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(not args.output.exists(), 'Fresh host packet output required')
    packet = build_packet(json.loads(args.binding.read_text()))
    with args.output.open('x') as stream:
        json.dump(packet, stream, indent=2)
        stream.write('\n')

if __name__ == '__main__':
    main()
