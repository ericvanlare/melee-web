"""Deterministic synthetic v2/v3/v4 loader graph; never original-game evidence.

Use the same input layout as ARM_MAIN/FINITE_ARM_MAIN in the fresh-row controls.
All bindings are hashes of files generated in the caller's owned workspace.
Host expectations are explicit doubles, kept separate from input envelopes.
"""
from copy import deepcopy
from hashlib import sha256
import json
import struct

TICK_FIELDS = ['source_stream_sha256', 'consumed_pad_sequence', 'source_tick_sequence',
               'source_tick_value', 'source_draw_ordinal', 'original_source_frame',
               'phase_relation', 'source_scene_kind', 'host_source_scene', 'host_menu_phase',
               'native_post_host_tick_frame', 'random_seed_hex', 'pad_state_hex']
DRAW_FIELDS = ['source_tick', 'draw_ordinal', 'pad_state_hex', 'random_seed_hex',
               'scene_frame', 'scene_kind', 'scene_routing_getters']
BOUNDARIES = ['pad_consume', 'source_tick', 'draw_enter', 'draw_return', 'pad_poll']
REF_FIELDS = ['consumed_pad_sequence', 'source_tick_sequence', 'draw_enter_sequence',
              'draw_return_sequence', 'pad_poll_sequence']


def write_fixture(base, count=7):
    """Write a small complete receipt graph accepted by the actual loaders."""
    base.mkdir()

    def pin(name, data):
        target = base / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        return {'path': str(target), 'bytes': len(data), 'sha256': sha256(data).hexdigest()}

    def jp(name, value):
        return pin(name, json.dumps(value, separators=(',', ':')).encode())

    roles = ['observer_stream', 'observer_status', 'capture_report', 'capture_packet',
             'mwri', 'input_status', 'input_intentions', 'menu_recipe', 'profile_gci',
             'profile_dolphin_ini', 'profile_gcpad_ini', 'offline_reducer']
    producers = {role: pin('pins/' + role, ('synthetic fixture: ' + role).encode())
                 for role in roles}
    source = producers['observer_stream']['sha256']
    # Versioned context layout from stadium_first_css_diagnostic_input.hpp and
    # the existing ARM_MAIN fixture. Initial callback region stays null.
    payload = bytearray(4 + 822 + 0x148 + 6 + 0x18 + 0x55E8)
    struct.pack_into('>I', payload, 0, 17)
    struct.pack_into('>I', payload, 4 + 822 + 4, 0x804D6730)
    context = b'STC2CSSR' + struct.pack('>I', 2) + bytes.fromhex(source)
    context += struct.pack('>II', 10, 20) + payload
    context_binding = pin('context.bin', context)
    context_binding.update(magic='STC2CSSR', version=2, source_sha256=source,
                           entry_sequence=10, return_sequence=20, payload_bytes=len(payload))
    role_map = dict(observer='observer_stream', status='observer_status',
                    capture_report='capture_report', packet='capture_packet', mwri='mwri',
                    mwri_status='input_status', input_intentions='input_intentions',
                    menu_recipe='menu_recipe', profile_gci='profile_gci')
    offline = {'schema': 'melee-web-stadium-fresh-css-context-reduction-v1',
               'result': 'PASS_OFFLINE_INITIAL_CSS_PAYLOAD_REPRESENTABILITY_AND_DYNAMIC_TOPOLOGY_ONLY',
               'producer': {key: producers[role] for key, role in role_map.items()},
               'loader_payload_contract': {'candidate_payload': {
                   'bytes': len(payload), 'sha256': sha256(payload).hexdigest()}},
               'source_semantics': {'no_claims': ['full raw 312-byte or whole-session equivalence']}}
    offline['producer']['profile_files'] = {'Config/Dolphin.ini': producers['profile_dolphin_ini'],
                                            'Config/GCPadNew.ini': producers['profile_gcpad_ini']}
    producers['offline_result'] = jp('offline.json', offline)
    pointers = ['on_unpause_override', 'on_pause_override', 'check_for_pauser_override',
                'on_match_start', 'on_frame_start', 'on_frame_end', 'on_match_end',
                'x54_pointer', 'x58_pointer']
    comparison = ['source_scene', 'scene_kind', 'random_seed_hex', 'pad_state_hex', 'ko_counts_hex', 'css']
    returned = {'schema': 'melee-web-stadium-first-css-constructor-return-expected-v2',
                'version': 2, 'source_sha256': source, 'entry_sequence': 10,
                'return_sequence': 20, 'comparison_fields': comparison,
                'expected_return': {'scene_kind': 8, 'random_seed_hex': '00000011',
                                    'pad_state_hex': '00' * 822, 'ko_counts_hex': '00' * 6,
                                    'css': {'match_type': 0, 'vs': {'start': {'rules': dict.fromkeys(pointers)}}}}}
    expected_binding = jp('constructor-expected.json', returned)
    expected_binding.update({key: returned[key] for key in returned if key != 'expected_return'})
    manifest = {'schema': 'melee-web-stadium-first-css-constructor-return-v2', 'version': 2,
                'scope': 'Recorded original CSS context and immediate constructor return only. source_scene is the fixed local CSS-owner assertion, not a raw original enum observation. No tick, draw, input schedule, continuation, whole-session or C3 claim. Runtime-generated config copies remain separately bound in offline_result.',
                'producer_pins': producers, 'context_input': context_binding,
                'expected_return': expected_binding, 'source_boundaries': {}}
    for phase, sequence in [('css_entry', 10), ('css_return', 20)]:
        manifest['source_boundaries'][phase] = dict(phase=phase, event='progress', sequence=sequence,
                                                   pc='0x80000000', word='0x60000000', source_tick=0, draw_ordinal=0)
    manifest_pin = jp('producer-v1/manifest.json', manifest)
    topology = jp('topology.json', {'scope': 'synthetic fixture topology only'})
    rows, pairs, all_raw = [], [], bytearray()
    route = dict(current_game_mode=2, previous_game_mode=0, current_scene_index=0, previous_scene_index=0)
    for i in range(count):
        # Include nonzero values, signed axes/errors and disconnected ports.
        raw = bytes([0, i + 1, 127, 128, 255, 1, 2, 3, 4, 5, 0,
                     0, 0, 129, 1, 2, 3, 4, 5, 6, 7, 0] + [0] * 10 + [255] + [0] * 10 + [255])
        all_raw += raw
        refs = [21 + i * 5 + j for j in range(5)]
        row = dict(ordinal=i, **dict(zip(REF_FIELDS, refs)), consume_source_tick=i,
                   consume_draw_ordinal=i, tick_source_tick=i, tick_draw_ordinal=i,
                   draw_source_tick=i + 1, draw_ordinal=i, poll_source_tick=i + 1,
                   poll_draw_ordinal=i + 1, raw_four_port_hex=raw.hex(),
                   intervening_non_target_record_counts=[0, 0, 0, 0])
        tick = dict(zip(TICK_FIELDS, [source, refs[0], refs[1], i, i, i,
            f'native host post-tick sample at frame {i+1} versus original scheduler-end SourceTick {refs[1]} before frame increment at frame {i}',
            8, 1, 1, i + 1, f'{0x12345678+i:08x}', bytes([i+1]).hex() * 822]))
        tick['scene_routing_getters'] = deepcopy(route)
        pair = dict(metadata=row, input_port_status_hex=[raw[p*11:(p+1)*11].hex() for p in range(4)])
        for name, seq, boundary in [('expected_draw_enter', refs[2], 'draw_enter'),
                                     ('expected_draw_return', refs[3], 'draw_return')]:
            pair[name] = dict(sequence=seq, boundary=boundary, source_tick=i+1, draw_ordinal=i,
                              scene_frame=i+1, scene_kind=8, random_seed_hex=tick['random_seed_hex'],
                              pad_state_hex=tick['pad_state_hex'], scene_routing_getters=deepcopy(route))
        if i == count-1:
            pair.update(original_scheduler_end_unpaired=tick, terminal_tick_equality_proposed=False,
                        native_result3_required_but_unrun=True)
        else:
            pair['expected_post_tick'] = tick
        rows.append(row)
        pairs.append(pair)
    row_expected = dict(schema='stadium-fresh-first-css-row-host-expected-v1', source_admission=False,
        whole_session_equivalent=False, provenance=dict(constructor_manifest=manifest_pin, topology=topology,
        stream_sha256=source, row=rows[0]), comparison_fields=DRAW_FIELDS,
        tick_scalar_comparison_fields=TICK_FIELDS, tick_deep_comparison_fields=['scene_routing_getters'],
        **{key: pairs[0][key] for key in ['expected_post_tick', 'expected_draw_enter', 'expected_draw_return']})
    row_receipt = dict(schema='stadium-fresh-first-css-consumed-row-receipt-v1',
        result='PASS_ORIGINAL_ONLY_FIRST_ROW_PREPARATION', constructor_manifest=manifest_pin,
        producer_pins=producers, topology=topology, raw_row=pin('row.raw', all_raw[:44]),
        expected_host_only=jp('row-expected.json', row_expected),
        selected_source_rows=jp('selected.json', dict(stream_sha256=source,
            rows=[dict(seq=rows[0][key], payload=dict(boundary=name)) for key, name in zip(REF_FIELDS, BOUNDARIES)])),
        literal_port_status_hex=pairs[0]['input_port_status_hex'],
        source_pairing=[dict(seq=rows[0][key], boundary=name) for key, name in zip(REF_FIELDS, BOUNDARIES)])
    row_pin = jp('row-receipt.json', row_receipt)
    prefix = 'validation-v1/first-css-finite-preparation-v1/extraction-v1/'
    raw_pin = pin(prefix + 'literal-input-rows.raw', all_raw)
    descriptor = dict(schema='stadium-fresh-css-finite-input-metadata-v1', version=1, source_sha256=source,
        context_entry_sequence=10, context_return_sequence=20, exclusive_phase_end_sequence=21+count*5,
        row_count=count, raw_rows=raw_pin, baseline_index=0, terminal_index=count-1, rows=rows,
        native_codec_implemented=False, contains_expected_state=False)
    expected = dict(schema='stadium-fresh-css-finite-host-expected-v1', version=1,
        terminal_tick='EXPLICITLY_UNPAIRED', all_draw_equality='PROPOSED_ONLY_NOT_EXECUTED', source_admission=False,
        rows=pairs, tick_scalar_comparison_fields=TICK_FIELDS, tick_deep_comparison_fields=['scene_routing_getters'],
        draw_comparison_fields=DRAW_FIELDS, baseline_reviewed_expected=row_expected)
    receipt = dict(schema='stadium-fresh-css-finite-original-only-receipt-v1',
        result='PASS_ORIGINAL_ONLY_FINITE_CSS_PREPARATION', native_or_browser_executed=False,
        terminal_tick='EXPLICITLY_UNPAIRED', reviewed_row_receipt=row_pin, constructor_manifest=manifest_pin,
        producer_pins=producers, topology=topology, literal_rows=raw_pin,
        counts=dict(actual_source_rows=count, remainder_rows=count-1, normal_tick_comparisons_proposed=count-1,
                    draw_comparisons_proposed=count),
        input_only_descriptor=jp(prefix+'input-only-descriptor.json', descriptor),
        expected_host_only=jp('finite-expected.json', expected),
        source_pairings=jp('pairings.json', dict(rows=[[dict(event='boundary', seq=row[key], boundary=name)
            for key, name in zip(REF_FIELDS, BOUNDARIES)] for row in rows])),
        derivation_lock=jp('derivation.json', {'scope': 'synthetic fixture only'}))
    jp(prefix+'receipt.json', receipt)
    jp('first-row-capture-v1/preflight.json', dict(first_css_constructor_return=dict(manifest=manifest_pin),
                                                first_css_fresh_row=dict(receipt=row_pin)))
    return base
