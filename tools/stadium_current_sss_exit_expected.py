"""Project only missing recorded first-SSS fields from the admitted host packet.
No native state, original writes, menu-draw expectations or new route validator.
"""
import argparse
import hashlib
import json
from pathlib import Path
import stadium_current_context_packet as packet
from transition_trace_format import decode_start_melee_data
from reference_capture_semantics import pad_snapshot_bytes


def build_expected(binding):
    current = packet.build_packet(binding)
    path = Path(binding['files']['primary']['path'])
    rows = list(packet.replay.iter_records(path))
    selected = [r for r in rows if r['event'] == 'boundary' and
                r['payload'].get('match_index') == 0 and
                r['payload'].get('boundary') == 'sss_exit']
    packet.require(len(selected) == 1, 'First SSS exit missing/duplicated')
    row = selected[0]
    packet.require(row['payload']['pc'] == 0x8025BBD0,
                   'First SSS exit escaped actual original return PC')
    state, start = packet.replay._slice(row, 'menu_sss_state', 0xF0, 'SSS exit')
    route_byte, start_game = packet.replay._slice(row, 'menu_sss_route', 1, 'SSS exit')
    route, raw_route = packet.replay._slice(row, 'scene_routing', 6, 'SSS exit')
    frame, raw_frame = packet.replay._slice(row, 'scene_frame', 4, 'SSS exit')
    kind, raw_kind = packet.replay._slice(row, 'scene_kind', 1, 'SSS exit')
    pad, raw_pad = packet.replay._slice(row, 'pad_snapshot', 0x358, 'SSS exit')
    packet.require(state['address'] - 0x10 + 4 == route_byte['address'] and
                   start_game in (b'\x00', b'\x01'), 'SSS payload/route owner differs')
    spans = current['spans']
    packet.require(len(spans) >= 3 and [s['scene'] for s in spans[:3]] == [1, 2, 3],
                   'Current route must begin CSS/SSS/Match')
    cursor = spans[2]['first_frame']
    packet.require(spans[0]['first_frame'] == 0 and
                   spans[0]['last_frame'] + 1 == spans[1]['first_frame'] and
                   spans[1]['last_frame'] + 1 == cursor and
                   current['inputs'][cursor-1]['sequence'] < row['seq'] <
                   current['matches'][0]['entry']['sequence'],
                   'SSS exit is not after literal final SSS input before Entry')
    capture = packet.replay.capture_from_records(rows)
    recipe, _ = packet.replay.encode_v8(capture)
    rng = packet.seed(row)
    return {'schema': 'melee-web-current-sss-exit-host-expectation-v1',
            'source_binding': binding, 'primary_sha256': packet.sha(path),
            'source': {**packet.coordinate(row), 'rng': rng,
                       'raw_start_melee_hex': start.hex(),
                       'slices': row['payload']['slices']},
            'recipe': {'sha256': hashlib.sha256(recipe).hexdigest(), 'bytes': len(recipe),
                       'frames': len(current['inputs']), 'version': 8},
            'notification_cursor': cursor,
            'literal_inputs': [{'index': r['index'], 'scene': r['scene_code'], 'pads': r['pads']}
                               for r in current['inputs'][:cursor]],
            'expected': {'phase': 'sss_exit', 'scene_frame': int.from_bytes(raw_frame, 'big'),
                         'scene_kind': raw_kind[0], 'random_seed_hex': rng['value_hex'],
                         'pad_state_hex': pad_snapshot_bytes(raw_pad),
                         'scene_routing_getters': dict(zip(
                             ('current_game_mode','previous_game_mode','current_scene_index','previous_scene_index'),
                             (raw_route[0],raw_route[2],raw_route[3],raw_route[4]))),
                         'sss': {'header': {'start_game': start_game[0]},
                                 'vs': {'start': decode_start_melee_data(start)}}},
            'unpaired': ['other_sss_header_fields', 'vs_leading_eight_bytes',
                         'native_ABI_bytes', 'native_pointer_addresses'],
            'menu_draw_reference': 'UNKNOWN', 'Entry': False, 'C3': False,
            'expected_runtime_refusal': 'Original VS entry produced an unsupported Stadium diagnostic payload'}


def build_stock4_expected(binding):
    # Reuse the admitted packet and exact recorded first-SSS projection. This
    # does not rewrite its seed, PAD, input coordinates, or original operands.
    value = build_expected(binding)
    # Rules are applied AFTER the SSS exit note. The earlier SSS payload is
    # left entirely literal; admit the prepared profile from the strict packet's
    # actual later Entry/Setup, never by rewriting earlier source operands.
    current = packet.build_packet(binding)
    first = current['matches'][0]
    entry, setup = first['entry'], first['setup']
    want = entry['declared_setup']
    packet.require(value['recipe']['frames'] == 2006 and
                   want['match_kind'] == 1 and want['is_stock'] is True and
                   want['stage'] == 3 and want['time_limit_seconds'] == 0 and
                   want['item_frequency'] == -1 and not want['timer_enabled'] and
                   not want['timer_counts_up'] and not want['disable_pausing'] and
                   not want['is_teams'] and
                   [(p['character_kind'], p['costume'], p['stocks'], p['player_type'])
                    for p in want['players']] == [(8, 1, 4, 0), (8, 0, 4, 0)] and
                   entry['declared_setup'] == setup['declared_setup'] and
                   entry['start_melee_hex'] == setup['start_melee_hex'] and
                   value['source']['sequence'] < entry['sequence'] < setup['sequence'],
                   'Recorded prepared Entry/Setup stock4/None profile differs')
    keys = ('sequence', 'source_tick', 'draw_ordinal', 'match_index', 'boundary', 'pc',
            'start_melee_hex', 'start_melee_sha256', 'declared_setup')
    value['prepared_profile'] = {
        'scope': 'original_prepared_Entry_Setup_profile_not_SSS_payload',
        'source_primary_sha256': value['primary_sha256'],
        'entry': {k: entry[k] for k in keys},
        'setup': {k: setup[k] for k in keys}}
    value['schema'] = 'melee-web-stock4-none-sss-exit-host-expectation-v1'
    value['observation_profile'] = 1
    del value['expected_runtime_refusal']
    value['expected_replay_stop'] = 'Manual unload stopped the replay'
    return value


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binding', type=Path, required=True)
    p.add_argument('--binding-sha256', required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--observation-profile', choices=('time120', 'stock4-none'), default='time120')
    a = p.parse_args()
    packet.require(packet.sha(a.binding) == a.binding_sha256, 'Binding hash mismatch')
    builder = build_stock4_expected if a.observation_profile == 'stock4-none' else build_expected
    value = builder(json.loads(a.binding.read_text()))
    with a.out.open('x') as f:
        f.write(json.dumps(value, indent=2) + '\n')


if __name__ == '__main__':
    main()
