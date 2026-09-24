"""Stream original allocation boundaries through the independently rooted models.

Observed pointer values only compare results or select previously validated
producer labels. No observed address is sent to the native/Wasm model. Sizes,
heap selectors and pool requests remain the recorded source operation stream.
This is diagnostic replay, not an allocation provider for browser gameplay.
"""
from __future__ import annotations

import json
from pathlib import Path
import selectors
import subprocess
from typing import Any

from .compaction_manager_state import CompactionManagerState

from .allocation_history_replay import (
    ROOT, ModelDriver, ReplayProblem, call_chain, canonical_sha256, parse_result,
    sha256_file, parse_u32,
)


class StreamModel:
    def __init__(self, *, action_sink=None, output_sink=None):
        self.drivers = []
        self.processes = []
        self.actions = action_sink if action_sink is not None else []
        self.outputs = output_sink if output_sink is not None else []
        try:
            # Checked Wasm consumes the frozen command stream after native replay.
            driver = ModelDriver(source=ROOT / 'tests/allocation_lifetime_model.cpp',
                                 extra_impls=[ROOT / 'src/source_game_heap_context.cpp'])
            self.drivers.append(driver)
            self.processes.append(subprocess.Popen(driver.runner, cwd=ROOT, text=True,
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                bufsize=1))
        except Exception:
            self.close()
            raise

    def close(self):
        for process in self.processes:
            if process.poll() is None:
                process.terminate()
            try:
                process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()
        for driver in self.drivers:
            driver.close()

    def run(self, command: dict, call: dict) -> dict:
        self.actions.append({'call': call.get('call'), 'sequence': call.get('sequence'),
                             'function': call.get('function'), 'command': command})
        results = []
        for process in self.processes:
            try:
                process.stdin.write(json.dumps(command) + '\n')
                process.stdin.flush()
                with selectors.DefaultSelector() as selector:
                    selector.register(process.stdout, selectors.EVENT_READ)
                    if not selector.select(timeout=30):
                        raise ReplayProblem('model', 'allocator driver response timed out', call=call)
                line = process.stdout.readline()
            except (BrokenPipeError, OSError) as error:
                raise ReplayProblem('model', f'allocator driver pipe failed: {error}', call=call) from error
            if not line:
                error = process.stderr.read()
                raise ReplayProblem('model', f'allocator driver stopped: {error}', call=call)
            results.append(parse_result(line))
        self.outputs.append(results[0])
        accepted_statuses = {'ok'}
        if command.get('op') in {
                'game_handle_compact_begin', 'game_handle_compact_callback',
                'game_handle_compact_ram_chunk', 'game_handle_compact_devcom_complete'}:
            accepted_statuses.update({'compact_started', 'compact_in_progress',
                                      'compact_complete', 'cancelled'})
        if results[0].get('status') not in accepted_statuses:
            raise ReplayProblem('unsupported', f"model rejected source operation: {results[0].get('status')}", call=call)
        return dict(results[0])


def validate_layout(verified: dict) -> dict:
    layout = verified.get('static_layout')
    if not isinstance(layout, dict):
        raise ReplayProblem('provenance', 'independent static layout is missing')
    pools = layout.get('pool_descriptors')
    if not isinstance(pools, dict) or any(not isinstance(name, str) for name in pools):
        raise ReplayProblem('provenance', 'independent pool descriptor map is malformed')
    addresses = [parse_u32(address, 'independent pool descriptor') for address in pools.values()]
    if any(not address or address % 4 for address in addresses) or len(set(addresses)) != len(addresses):
        raise ReplayProblem('provenance', 'independent pool descriptor identities are invalid or duplicated')
    descriptors = layout.get('lbheap_descriptors')
    if (not isinstance(descriptors, list) or len(descriptors) < 2
            or descriptors[-1] != [6, 0, 0, 0]):
        raise ReplayProblem('provenance', 'original game-heap descriptor sentinel differs')
    seen = set()
    for row in descriptors[:-1]:
        if not isinstance(row, list) or len(row) != 4:
            raise ReplayProblem('provenance', 'game-heap descriptor must contain four words')
        index, kind, previous, size = [parse_u32(value, 'independent game-heap descriptor') for value in row]
        if index not in range(2, 6) or index in seen or kind not in (1, 2, 3, 4) or (previous != 6 and previous not in seen):
            raise ReplayProblem('provenance', 'game-heap descriptor order or type is invalid')
        seen.add(index)
    table = layout.get('aram_stack_table')
    if not isinstance(table, dict):
        raise ReplayProblem('provenance', 'independent ARAM stack table is missing')
    address = parse_u32(table.get('address'), 'ARAM stack table address')
    size = parse_u32(table.get('size'), 'ARAM stack table size')
    if not address or address % 4 or not size or size % 4 or address + size > 0x100000000:
        raise ReplayProblem('provenance', 'independent ARAM stack table span is invalid')
    base = parse_u32(layout.get('lbmemory_allocator'), 'independent handle allocator')
    if not base or base % 4 or base + 0x6f0 > 0x100000000:
        raise ReplayProblem('provenance', 'independent handle allocator span is invalid')
    return layout


def validate_metadata_shape(row: dict, call: dict, *, required: bool):
    observed = row.get('observed', {})
    if not isinstance(observed, dict) or (required and 'observed' not in row):
        raise ReplayProblem('stream', 'allocator boundary requires object-valued observed metadata', call=call)
    function = call['function']
    returned = row['record'] == 'return'
    words = {}
    if function in ('HSD_ObjAllocInit', 'HSD_ObjAllocAddFree', 'HSD_ObjAlloc', 'HSD_ObjFree'):
        words['pool_words'] = 11
    if function in ('HSD_ObjSetHeap', 'HSD_ObjAllocAddFree', 'HSD_ObjAlloc'):
        words['object_heap_words'] = 4
    if returned and function.startswith('lbHeap_'):
        words['game_heap_words'] = 46
    if returned and row.get('result') and function in ('lbMemory_80014E24', 'lbMemory_800154D4', 'lbMemory_80014FC8'):
        words['handle_words'] = 3 if function == 'lbMemory_80014FC8' else 4
    for key, length in words.items():
        value = observed.get(key)
        if value is None and not required:
            continue  # Explicit synthetic fixtures may test one boundary at a time.
        if not isinstance(value, list) or len(value) != length:
            raise ReplayProblem('stream', f'allocator boundary requires {length}-word {key}', call=call)
        for word in value:
            parse_u32(word, key)
    maps = {}
    if function in ('ARInit', 'ARAlloc', 'ARFree', 'ARGetSize'):
        maps['aram'] = ('__AR_BlockLength', '__AR_FreeBlocks', '__AR_Size', '__AR_StackPointer', '__AR_init_flag')
    if returned and function in ('OSInitAlloc', 'OSCreateHeap', 'OSDestroyHeap', 'OSAllocFromHeap', 'OSFreeToHeap'):
        maps['heaps'] = ('ArenaEnd', 'ArenaStart', 'HeapArray', 'NumHeaps', '__OSArenaHi', '__OSArenaLo', '__OSCurrHeap', 'current_heap')
    for key, fields in maps.items():
        value = observed.get(key)
        if value is None and not required:
            continue
        if not isinstance(value, dict) or any(field not in value for field in fields):
            raise ReplayProblem('stream', f'allocator boundary requires complete {key} metadata', call=call)
        for field in fields:
            parse_u32(value[field], field)
        if key == 'heaps':
            descriptors = value.get('descriptors')
            if value['NumHeaps'] > 32 or not isinstance(descriptors, list) or len(descriptors) != value['NumHeaps']:
                raise ReplayProblem('stream', 'SDK heap descriptor count is invalid', call=call)
            for descriptor in descriptors:
                if not isinstance(descriptor, list) or len(descriptor) != 3:
                    raise ReplayProblem('stream', 'SDK heap descriptor requires three words', call=call)
                for word in descriptor:
                    parse_u32(word, 'SDK heap descriptor')


def replay_lifetimes(trace: Path, profile_path: Path, profile: dict, header: dict,
                     rows: list[dict], enters: dict[int, dict], returns: dict[int, dict],
                     context: dict, verified: dict, *, checked_wasm: bool,
                     require_complete: bool, artifact_dir: Path | None = None) -> dict:
    layout = validate_layout(verified)
    retail = not profile['source_revision'].startswith('synthetic-')
    pool_ids = {address: index for index, (_, address) in
                enumerate(sorted(layout['pool_descriptors'].items()), 1)}
    descriptors = layout['lbheap_descriptors']
    if not descriptors or descriptors[-1] != [6, 0, 0, 0]:
        raise ReplayProblem('provenance', 'original game-heap descriptor sentinel differs')
    bounded_state = artifact_dir is not None
    action_store = output_store = identity_store = None
    if bounded_state:
        try:
            from .allocation_replay_spool import JsonSpool
        except ImportError as error:
            raise ReplayProblem('input', 'artifact replay requires allocation_replay_spool.py') from error
        artifact_dir = Path(artifact_dir)
        artifact_dir.mkdir(parents=True, exist_ok=True)
        action_store = JsonSpool(artifact_dir, 'actions')
        output_store = JsonSpool(artifact_dir, 'outputs')
        identity_store = JsonSpool(artifact_dir, 'identities')
    model = (StreamModel(action_sink=action_store, output_sink=output_store)
             if bounded_state else StreamModel())
    observed_labels: dict[int, str] = {}
    observed_heaps: dict[int, int] = {}
    observed_pools: dict[int, int] = {}
    pool_heaps: dict[int, int] = {}
    pool_backings: dict[int, list[tuple[int, int, int]]] = {}
    derived_calls: dict[int, dict] = {}
    derived_payloads: dict[int, str] = {}
    derived_handles: dict[int, str] = {}
    completed_count = 0
    identities = identity_store if identity_store is not None else []
    ownership_events = []
    fighter_owners = {}
    demo_owners = {}
    children: dict[int, list[dict]] = {}
    active_enters: dict[int, dict] = {}
    if not bounded_state:
        for call in enters.values():
            if call.get('parent') is not None:
                children.setdefault(call['parent'], []).append(call)
    pending_ids: set[int] = set()
    stage = verified['stages']
    arena_lows = iter([stage['linker_arena_lo'], stage['os_arena_lo'], stage['after_xfb'],
                       stage['os_init_alloc_lo'], context['arena_start'], context['arena_end']])
    arena_lo = profile['initial_dol_words']['__OSArenaLo']
    arena_hi = verified['boot']['arena_hi']
    os_created = 0
    hsd_heap = profile['initial_dol_words']['current_heap']
    os_initialized = False
    aram_cursor = None
    aram_depth = 0
    aram_lengths = []
    first_problem = None
    stream_error_reason = None
    repeated_counts = {}
    modeled_functions = set()
    static_pool_descriptors = {
        name: parse_u32(address, 'independent pool descriptor')
        for name, address in layout['pool_descriptors'].items()
    }
    fighter_pool_address = static_pool_descriptors.get('fighter_alloc_data')
    gobj_pool_address = static_pool_descriptors.get('gobj_alloc_data')
    fighter_initialized = False
    fighter_initialized_scene_generation = None
    fighter_pool_generation = None
    fighter_initialize_call = None
    fighter_pool_init_call = None
    demo_pool_generation = None
    demo_initialize_call = None
    demo_pool_init_call = None
    demo_initialized = False
    scene_active = False
    scene_generation = 0
    scene_enter_call = None
    scene_exit_call = None
    scene_enter_completed = False
    scene_exit_completed = False
    supported = {
        'OSSetArenaLo', 'OSSetArenaHi', 'OSAllocFromArenaLo', 'HSD_AllocateFifo',
        'HSD_AllocateXFB', 'OSInitAlloc', 'OSCreateHeap', 'OSDestroyHeap',
        'OSSetCurrentHeap', 'HSD_SetHeap', 'HSD_ObjSetHeap', 'OSAllocFromHeap',
        'OSFreeToHeap', 'HSD_MemAlloc', 'HSD_Free', 'HSD_ObjAllocInit',
        '_HSD_ObjAllocForgetMemory', 'HSD_ObjAllocAddFree', 'HSD_ObjAlloc', 'HSD_ObjFree',
        'ARInit', 'ARAlloc', 'ARFree', 'ARGetSize', 'lbMemory_80014E24',
        'lbMemory_800154D4', 'lbMemory_800155A4', 'lbMemory_80014EEC',
        'lbMemory_80014FC8', 'lbMemory_8001529C', 'lbMemory_8001564C',
        'lbHeap_80015F3C', 'lbHeap_800158D0', 'lbHeap_80015900', 'lbHeap_80015BD0',
        'lbHeap_80015D6C', 'lbHeap_80015CA8', 'lbMemFreeToHeap', 'HSD_CreateMainHeap', 'HSD_OSInit',
        'Fighter_FirstInitialize_80067A84', 'Fighter_Create',
        'ftDemo_ObjAllocInit', 'ftDemo_CreateFighter',
        'gm_Scene_Vs_OnEnter', 'gm_Scene_Vs_OnExit',
        'fn_80015184', 'lbMemory_80015320', 'lbDvd_80017A80',
        'HSD_DevComRequest', 'HSD_DevComARAMCallback',
    }
    declarations = {item['name']: item for item in profile.get('functions', [])}
    # The old handle model intentionally fails closed at the first
    # asynchronous move. Only a profile that declares every boundary needed
    # to observe the source transfer may opt into the granular state machine.
    async_compaction_functions = {
        'fn_80015184', 'lbMemory_80015320', 'lbDvd_80017A80',
        'HSD_DevComRequest', 'HSD_DevComARAMCallback',
    }
    async_compaction_coverage = async_compaction_functions <= set(declarations)
    manager = None
    if async_compaction_coverage:
        try:
            manager = CompactionManagerState(layout.get('lbmemory_initial_manager'))
        except ValueError as error:
            raise ReplayProblem('provenance', str(error)) from error
    devcom_counter = parse_u32(layout.get('devcom_initial_request_counter', 4),
                              'independent DevCom request counter')
    if retail and async_compaction_coverage and 'devcom_initial_request_counter' not in layout:
        raise ReplayProblem('provenance', 'independent DevCom request counter is missing')
    devcom_ids = {}
    compact_devcom_id = None
    compact_devcom_pending = False
    compact_generation = None
    compact_callback_generation = None
    compact_callback_address = None
    compact_callback_arg = None
    compact_state_snapshot = None
    dvd_completion_tokens = {}
    compaction_callback_tokens = {}
    compaction_alarm_tokens = {}

    current_call = header

    def equal(actual, expected, what, call):
        if actual != expected:
            raise ReplayProblem('validation', f'{what} differs: observed {actual!r}, derived {expected!r}', call=call)

    def run(op, call, **fields):
        modeled_functions.add(call.get('function'))
        try:
            output = model.run({'op': op, **fields}, call)
        except OSError as error:
            raise ReplayProblem('resource', f'allocation replay artifact write failed: {error}', call=call) from error
        for retired in output.get('retired_payloads', []):
            derived_payloads.pop(retired['payload'], None)
            derived_handles.pop(retired['handle'], None)
        return output

    def metadata(output, row, call):
        observed = row.get('observed', {})
        for key in ('pool_words', 'object_heap_words', 'game_heap_words', 'handle_words'):
            if key in observed:
                if key not in output:
                    raise ReplayProblem('model', f'missing derived {key}', call=call)
                expected = output[key]
                if key == 'handle_words' and len(observed[key]) == 3:
                    expected = expected[:3]
                equal(observed[key], expected, key, call)
        if 'heaps' in observed:
            expected = dict(output.get('heaps', {}), __OSArenaLo=arena_lo, __OSArenaHi=arena_hi)
            equal(observed['heaps'], expected, 'SDK heap metadata', call)

    def pointer(row, output, field, label, call):
        equal(row['result'], output[field], 'source allocation result', call)
        if output[field]:
            observed_labels[row['result']] = label
            try:
                identities.append({'call': call['call'], 'sequence': row['sequence'],
                                   'function': call['function'], 'label': label,
                                   'derived': output[field]})
            except OSError as error:
                raise ReplayProblem('resource', f'allocation replay artifact write failed: {error}', call=call) from error
        derived_calls[call['call']] = dict(output, label=label)

    def known(address, call):
        if address not in observed_labels:
            raise ReplayProblem('unsupported', 'pointer lacks a previously derived allocation producer', call=call)
        return observed_labels[address]

    def descendant(call, names):
        matches = []
        def visit(parent):
            for child in children.get(parent, []):
                if child['function'] in names:
                    matches.append(child)
                else:
                    visit(child['call'])
        visit(call['call'])
        if len(matches) != 1 or matches[0]['call'] not in derived_calls:
            raise ReplayProblem('unsupported', 'source wrapper lacks exactly one derived child allocation', call=call)
        return derived_calls[matches[0]['call']]

    def descendants(call, names):
        matches = []

        def visit(parent):
            for child in children.get(parent, []):
                if child['function'] in names:
                    matches.append(child)
                visit(child['call'])

        visit(call['call'])
        return matches

    def prune_closed_children(call_id):
        """Drop completed descendants while retaining this call's result.

        Wrapper validation runs at the parent's return boundary.  Its direct
        child entry and derived result must remain available until then, while
        descendants of a completed call cannot affect any later source
        operation.  This keeps artifact-mode replay proportional to the
        largest live source wrapper instead of the whole trace.
        """
        nested = children.pop(call_id, [])
        for child in nested:
            prune_closed_subtree(child['call'])

    def prune_closed_subtree(call_id):
        nested = children.pop(call_id, [])
        for child in nested:
            prune_closed_subtree(child['call'])
        derived_calls.pop(call_id, None)
        active_enters.pop(call_id, None)

    def wrapper_observation(row, call, field):
        observed = row.get('observed')
        if not isinstance(observed, dict) or not isinstance(observed.get(field), dict):
            raise ReplayProblem('stream', f'{call["function"]} return lacks {field} identity metadata', call=call)
        return observed[field]

    def require_u32_fields(value, fields, call):
        for field in fields:
            if field not in value:
                raise ReplayProblem('stream', f'{call["function"]} metadata lacks {field}', call=call)
            parse_u32(value[field], f'{call["function"]}.{field}')

    def first_argument(call, context):
        values = call.get('args')
        if not isinstance(values, list) or not values:
            raise ReplayProblem('stream', f'{call["function"]} lacks its {context} argument', call=call)
        return parse_u32(values[0], f'{call["function"]}.{context}')

    def pool_id(args, call):
        if not args or args[0] not in pool_ids:
            raise ReplayProblem('unsupported', 'pool descriptor lacks independent original static identity', call=call)
        return pool_ids[args[0]]

    def source_function_address(name, call):
        declaration = declarations.get(name)
        if not isinstance(declaration, dict) or 'address' not in declaration:
            raise ReplayProblem('provenance', f'{name} lacks an independently profiled source identity', call=call)
        return parse_u32(declaration['address'], f'{name} source identity')

    def compact_state(output, call):
        state = output.get('compact') if isinstance(output, dict) else None
        if not isinstance(state, dict) or not isinstance(state.get('move'), dict):
            raise ReplayProblem('model', 'compaction operation lacks a complete symbolic state', call=call)
        for key in ('heap', 'callback', 'callback_arg', 'callback_handle',
                    'callback_generation', 'cursor'):
            parse_u32(state.get(key), f'compaction state {key}')
        move = state['move']
        for key in ('handle', 'source', 'destination', 'next', 'size', 'offset', 'generation'):
            parse_u32(move.get(key), f'compaction move {key}')
        if state.get('phase') not in {'idle', 'awaiting_callback', 'waiting_ram_alarm',
                                      'waiting_devcom', 'awaiting_completion',
                                      'complete', 'rejected'}:
            raise ReplayProblem('model', 'compaction state has an unknown phase', call=call)
        if move.get('transfer') not in {'none', 'ram_alarm', 'devcom_1b'}:
            raise ReplayProblem('model', 'compaction state has an unknown transfer kind', call=call)
        return state

    def compare_compaction(row, call):
        if manager is None or call['function'] not in {
                'lbMemory_8001529C', 'lbMemory_80015320', 'fn_80015184'}:
            return
        observed = row.get('observed', {}).get('compaction')
        if observed is None and not retail:
            return  # Synthetic fixtures may isolate a different boundary.
        if not isinstance(observed, dict):
            raise ReplayProblem('stream', 'compaction metadata is missing', call=call)
        words = observed.get('manager')
        if not isinstance(words, dict) or set(words) != set(manager.snapshot()):
            raise ReplayProblem('stream', 'compaction manager fields are missing or unknown', call=call)
        for name, value in words.items():
            parse_u32(value, 'observed compaction manager ' + name)
        equal(observed.get('phase'), 'entry' if row['record'] == 'enter' else 'return',
              'compaction observation phase', call)
        equal(observed.get('manager'), manager.snapshot(), 'compaction manager', call)
        args = call['args']
        if call['function'] == 'lbMemory_8001529C':
            wrapper = enters.get(call.get('parent'))
            if not wrapper or wrapper['function'] != 'lbHeap_80015D6C':
                raise ReplayProblem('unsupported', 'compaction handle lacks its wrapper', call=call)
            handle = run('game_owner', call, index=wrapper['args'][0])
            words = handle['handle_words']
            equal(observed.get('handle'), dict(pointer=handle['handle'],
                  x0_next=words[0], x4_lo=words[1], x8_hi=words[2], xC_prev=words[3]),
                  'compaction heap handle', call)
            equal(observed.get('callback'), args[1], 'compaction callback metadata', call)
            equal(observed.get('callback_arg'), args[2], 'compaction callback argument metadata', call)
        elif call['function'] == 'lbMemory_80015320':
            equal(observed.get('callback_arg'), args[2], 'move callback argument metadata', call)
            equal(observed.get('cancel'), bool(args[3]), 'move cancellation metadata', call)
            if not args[1]:
                equal(observed.get('handle'), None, 'final callback null handle', call)
            else:
                label = derived_handles.get(args[1])
                if label is None:
                    raise ReplayProblem('unsupported', 'callback handle lacks derived producer', call=call)
                handle = run('game_handle_read', call, label=label)
                words = handle['handle_words']
                equal(observed.get('handle'), dict(pointer=handle['handle'],
                      x0_next=words[0], x4_lo=words[1], x8_hi=words[2]),
                      'compaction allocation handle', call)
        else:
            equal(observed.get('alarm'), layout['lbmemory_allocator'] + 0x6A0,
                  'source RAM alarm identity', call)
            equal(args[0], layout['lbmemory_allocator'] + 0x6A0,
                  'source RAM alarm argument', call)
            equal(observed.get('context'), args[1], 'source alarm context metadata', call)

    def relocate_payload(state, call):
        if state['phase'] not in {'waiting_ram_alarm', 'waiting_devcom'}:
            return
        move = state['move']
        label = derived_handles.get(move['handle'])
        if label is None or derived_payloads.get(move['source']) != label:
            raise ReplayProblem('model', 'compaction move lacks its derived payload producer', call=call)
        del derived_payloads[move['source']]
        if move['destination'] in derived_payloads:
            raise ReplayProblem('model', 'compaction destination aliases a live payload', call=call)
        derived_payloads[move['destination']] = label

    try:
        run('configure', header, heap_count=context['heap_max_num'], descriptor_base=context['arena_lo'],
            arena_start=context['arena_start'], arena_end=context['arena_end'],
            main_lo=context['main_begin'], main_hi=context['main_end'], initial_hsd_heap=hsd_heap,
            pools=' '.join(f'{index} {address}' for address, index in pool_ids.items()),
            descriptors=' '.join(str(word) for desc in descriptors[:-1] for word in desc))
        for row in rows:
            record = row['record']
            if record == 'error':
                if stream_error_reason is None:
                    stream_error_reason = row.get('error') or 'allocation trace contains an error record'
                continue
            if record == 'repeated_stop':
                original_sequence = row.get('original_sequence')
                if (type(original_sequence) is not int or original_sequence < 0
                        or original_sequence >= row['sequence']):
                    raise ReplayProblem('stream', 'repeated stop references an invalid original sequence')
                original = rows[original_sequence]
                phase = original.get('record')
                if (original['sequence'] >= row['sequence'] or phase not in ('enter', 'return')
                    or row.get('phase') != phase or row.get('function') != original.get('function')
                    or row.get('machine') != original.get('machine') or row.get('observed') != original.get('observed')):
                    raise ReplayProblem('stream', 'repeated stop differs from original boundary')
                count = repeated_counts.get(original['sequence'], 0) + 1
                repeated_counts[original['sequence']] = count
                if count > 32:
                    raise ReplayProblem('stream', 'repeated stop exceeded progress bound')
                continue
            if record not in ('enter', 'return'):
                continue
            call = enters[row['call']]
            # Track the replay prefix in both in-memory and artifact modes.
            # The source ``pending_calls`` field below remains the complete
            # trace's ownership summary; this set describes work actually
            # entered by this replay before a failure or successful return.
            if record == 'enter':
                pending_ids.add(call['call'])
            if bounded_state:
                if record == 'enter':
                    active_enters[call['call']] = call
                    parent = call.get('parent')
                    if parent is not None:
                        children.setdefault(parent, []).append(call)
            current_call = call
            function = call['function']
            if not isinstance(call.get('args'), list):
                raise ReplayProblem('stream', 'source entry requires an argument list', call=call)
            if record == 'return':
                parse_u32(row.get('result'), 'source return result')
            validate_metadata_shape(row, call, required=retail)
            args = [parse_u32(value, 'source argument') for value in call['args']]
            if function in declarations and len(args) != declarations[function]['argc']:
                raise ReplayProblem('stream', 'source argument count differs from pinned function profile', call=call)
            compare_compaction(row, call)
            ancestors = call_chain(call, active_enters if bounded_state else enters)
            names = [parent['function'] for parent in ancestors]
            label = f"call_{call['call']}"
            if record == 'enter':
                if function not in supported:
                    raise ReplayProblem('unsupported', f'no source binding for observed function {function}', call=call)
                if function in async_compaction_functions and not async_compaction_coverage:
                    raise ReplayProblem('unsupported',
                                        'asynchronous compaction callback coverage is not declared by the source profile',
                                        call=call)
                if function == 'lbHeap_80015900':
                    run('game_begin', call)
                elif function == 'HSD_CreateMainHeap':
                    output = run('hsd_replace_begin', call)
                    equal(args, output['args'], 'replacement bounds', call)
                elif function in ('lbHeap_80015BD0', 'lbHeap_80015CA8', 'lbHeap_80015D6C'):
                    derived_calls[call['call']] = run('game_owner', call, index=args[0])
                elif function == 'Fighter_FirstInitialize_80067A84':
                    if not scene_active:
                        raise ReplayProblem('unsupported',
                                            'fighter initialization is outside an active VS scene owner',
                                            call=call)
                    if fighter_initialize_call is not None:
                        raise ReplayProblem('unsupported',
                                            'fighter pool initialization reenters an existing source generation',
                                            call=call)
                    if fighter_pool_address is None:
                        raise ReplayProblem('unsupported',
                                            'fighter initialization lacks the independently identified fighter_alloc_data pool',
                                            call=call)
                    if fighter_initialized and not scene_exit_completed:
                        raise ReplayProblem('unsupported',
                                            'fighter pool initialization reenters before the prior VS owner exited',
                                            call=call)
                    # Fighter_FirstInitialize is called once for each VS
                    # setup.  The source resets the fighter pool here; the
                    # previous generation is retired by the completed VS
                    # exit/main-heap teardown, so no guessed Fighter_Free
                    # callback is required in this diagnostic model.
                    fighter_initialized = False
                    fighter_pool_init_call = None
                    fighter_initialize_call = call['call']
                elif function == 'ftDemo_ObjAllocInit':
                    if demo_initialize_call is not None:
                        raise ReplayProblem('unsupported',
                                            'demo fighter pool initialization reenters an existing source generation',
                                            call=call)
                    # ftDemo_ObjAllocInit calls Fighter_800679B0 directly,
                    # outside the VS owner.  Its fighter_alloc_data reset is
                    # a separate, explicitly named demo generation.
                    demo_initialized = False
                    demo_pool_init_call = None
                    demo_initialize_call = call['call']
                elif function == 'Fighter_Create':
                    if (not fighter_initialized or fighter_pool_generation is None
                            or fighter_initialized_scene_generation != scene_generation):
                        raise ReplayProblem('unsupported',
                                            'Fighter_Create has no completed fighter pool generation',
                                            call=call)
                    if not scene_active:
                        raise ReplayProblem('unsupported',
                                            'Fighter_Create is outside an active VS scene owner', call=call)
                elif function == 'ftDemo_CreateFighter':
                    if demo_initialize_call is not None or not demo_initialized or demo_pool_generation is None:
                        raise ReplayProblem('unsupported',
                                            'ftDemo_CreateFighter has no completed demo fighter pool generation',
                                            call=call)
                elif function == 'gm_Scene_Vs_OnEnter':
                    if scene_active:
                        raise ReplayProblem('unsupported',
                                            'VS scene ownership entered before the prior generation exited',
                                            call=call)
                    scene_generation += 1
                    scene_active = True
                    scene_enter_call = call['call']
                    scene_exit_call = None
                    scene_enter_completed = False
                    ownership_events.append({
                        'kind': 'vs_enter', 'call': call['call'], 'sequence': call['sequence'],
                        'generation': scene_generation, 'fighter_pool_generation': fighter_pool_generation,
                    })
                elif function == 'gm_Scene_Vs_OnExit':
                    if not scene_active or scene_enter_call is None or scene_exit_call is not None:
                        raise ReplayProblem('unsupported',
                                            'VS scene ownership exited without one active owner', call=call)
                    scene_exit_call = call['call']
                elif function == 'lbMemory_8001529C' and async_compaction_coverage:
                    if not ancestors or ancestors[0]['function'] != 'lbHeap_80015D6C':
                        raise ReplayProblem('unsupported',
                                            'handle compaction lacks original game-heap wrapper context', call=call)
                    wrapper = ancestors[0]
                    owner = wrapper['args'][0]
                    if len(args) != 3:
                        raise ReplayProblem('stream', 'compaction entry requires handle, callback, and callback argument',
                                            call=call)
                    owner_output = derived_calls.get(wrapper['call'])
                    if not owner_output or 'handle' not in owner_output:
                        raise ReplayProblem('unsupported',
                                            'source compaction wrapper lacks a derived owner handle', call=call)
                    equal(args[0], owner_output['handle'], 'source compaction owner', call)
                    equal(args[1], wrapper['args'][1], 'source compaction callback argument', call)
                    equal(args[2], wrapper['args'][2], 'source compaction callback context', call)
                    callback_address = source_function_address('lbDvd_80017A80', call)
                    equal(args[1], callback_address,
                          'source compaction callback identity', call)
                    output = run('game_handle_compact_begin', call, index=owner,
                                 callback=callback_address, callback_arg=args[2])
                    state = compact_state(output, call)
                    if output.get('owner') != args[0]:
                        raise ReplayProblem('validation', 'source compaction handle owner differs', call=call)
                    if output.get('result') not in (0, 1):
                        raise ReplayProblem('model', 'source compaction result is not a boolean start result', call=call)
                    manager.begin(state)
                    compact_state_snapshot = state
                    compact_callback_address = args[1]
                    compact_callback_arg = args[2]
                    compact_generation = None
                    compact_callback_generation = (state['callback_generation']
                                                   if output['result'] else None)
                    compact_devcom_pending = False
                    derived_calls[call['call']] = output
                elif function == 'fn_80015184' and async_compaction_coverage:
                    if compact_generation is None:
                        raise ReplayProblem('unsupported',
                                            'RAM compaction callback has no active derived transfer', call=call)
                    output = run('game_handle_compact_ram_chunk', call, generation=compact_generation)
                    state = compact_state(output, call)
                    manager.alarm(state)
                    compact_state_snapshot = state
                    if output.get('status') == 'invalid_generation':
                        raise ReplayProblem('validation', 'RAM compaction callback generation was rejected', call=call)
                    if state['phase'] == 'waiting_ram_alarm':
                        compact_generation = state['move']['generation']
                    elif state['phase'] in {'awaiting_callback', 'awaiting_completion'}:
                        if not output.get('transfer_complete'):
                            raise ReplayProblem('model',
                                                'RAM compaction callback advanced without completing its transfer',
                                                call=call)
                        compact_generation = None
                        compact_callback_generation = state['callback_generation']
                    else:
                        raise ReplayProblem('unsupported',
                                            'RAM compaction callback did not preserve a pending transfer boundary',
                                            call=call)
                    compaction_alarm_tokens[call['call']] = {
                        'generation': compact_generation,
                    }
                elif function == 'lbMemory_80015320' and async_compaction_coverage:
                    if len(args) != 4 or args[2] != 0 or args[3] not in (0, 1):
                        raise ReplayProblem('stream',
                                            'source compaction callback arguments are malformed', call=call)
                    equal(args[0], compact_devcom_id if compact_devcom_pending else 0,
                          'source compaction callback request identity', call)
                    if compact_devcom_pending:
                        if compact_generation is None:
                            raise ReplayProblem('unsupported',
                                                'DevCom completion has no active derived transfer', call=call)
                        output = run('game_handle_compact_devcom_complete', call,
                                     generation=compact_generation, cancelled=args[3])
                        state = compact_state(output, call)
                        compact_state_snapshot = state
                        compact_devcom_pending = False
                        if args[3]:
                            if output.get('status') != 'cancelled':
                                raise ReplayProblem('model',
                                                    'cancelled DevCom callback was not rejected by the model',
                                                    call=call)
                            raise ReplayProblem('unsupported',
                                                'source compaction callback was cancelled', call=call)
                        if state['phase'] not in {'awaiting_callback', 'awaiting_completion'}:
                            raise ReplayProblem('model',
                                                'DevCom completion did not expose the source callback boundary',
                                                call=call)
                        equal(args[1], state['callback_handle'],
                              'source compaction handle callback identity', call)
                        compact_generation = None
                        compact_callback_generation = state['callback_generation']
                    else:
                        state = compact_state_snapshot
                        if state is None or state['phase'] not in {
                                'awaiting_callback', 'awaiting_completion'}:
                            raise ReplayProblem('unsupported',
                                                'source compaction callback has no pending symbolic transfer',
                                                call=call)
                        equal(args[1], state['callback_handle'],
                              'source compaction handle callback identity', call)
                    if compact_callback_generation is None:
                        raise ReplayProblem('unsupported',
                                            'source compaction callback lacks a derived callback generation', call=call)
                    output = run('game_handle_compact_callback', call,
                                 generation=compact_callback_generation, cancelled=args[3])
                    state = compact_state(output, call)
                    manager.callback(state, source_function_address('lbMemory_80015320', call))
                    relocate_payload(state, call)
                    compact_state_snapshot = state
                    if output.get('status') == 'invalid_generation':
                        raise ReplayProblem('validation',
                                            'source compaction callback generation was rejected', call=call)
                    if args[3]:
                        if output.get('status') != 'cancelled':
                            raise ReplayProblem('model',
                                                'cancelled source compaction callback was not rejected by the model',
                                                call=call)
                        raise ReplayProblem('unsupported',
                                            'source compaction callback was cancelled', call=call)
                    if state['phase'] in {'waiting_ram_alarm', 'waiting_devcom'}:
                        compact_generation = state['move']['generation']
                    elif state['phase'] in {'awaiting_callback', 'awaiting_completion'}:
                        compact_generation = None
                    elif state['phase'] == 'complete':
                        compact_generation = None
                        compact_callback_generation = None
                    else:
                        raise ReplayProblem('unsupported',
                                            'source compaction callback produced a rejected symbolic state',
                                            call=call)
                    if state['phase'] != 'complete':
                        compact_callback_generation = state['callback_generation']
                    compaction_callback_tokens[call['call']] = {
                        'handle': args[1], 'phase': state['phase'],
                    }
                elif function == 'lbDvd_80017A80' and async_compaction_coverage:
                    state = compact_state_snapshot
                    if state is None or state['phase'] != 'complete' or not state['callback_invoked']:
                        raise ReplayProblem('unsupported',
                                            'DVD preload callback arrived before symbolic compaction completion',
                                            call=call)
                    callback_address = source_function_address('lbDvd_80017A80', call)
                    equal(args[0], compact_callback_arg,
                          'DVD preload callback argument', call)
                    equal(callback_address, compact_callback_address,
                          'DVD preload callback identity', call)
                    dvd_completion_tokens[call['call']] = {
                        'callback_arg': args[0], 'callback': callback_address,
                    }
                elif function == 'HSD_DevComRequest' and async_compaction_coverage:
                    priority = args[5] if args[4] & 0x38 == 0x20 else 3
                    if priority not in range(4):
                        raise ReplayProblem('unsupported', 'DevCom priority is outside source queues', call=call)
                    devcom_ids[call['call']] = (devcom_counter + priority) & 0xFFFFFFFF
                    devcom_counter = (devcom_counter + 4) & 0xFFFFFFFF
                    state = compact_state_snapshot
                    callback = args[6]
                    if callback != source_function_address('lbMemory_80015320', call):
                        continue  # Unrelated transport; nested allocations still replay.
                    if state is None or state['phase'] != 'waiting_devcom':
                        raise ReplayProblem('unsupported', 'compaction DevCom request has no pending move', call=call)
                    equal(args, [0, state['move']['source'], state['move']['destination'],
                                 state['move']['size'], 0x1B, 1,
                                 source_function_address('lbMemory_80015320', call),
                                 state['move']['next']], 'DevCom move request', call)
                    compact_devcom_id = devcom_ids[call['call']]
                    compact_devcom_pending = True
                elif function == 'HSD_DevComARAMCallback' and async_compaction_coverage:
                    pass  # Transfer completion is validated at the nested handle callback.
                continue
            output = None
            if function == 'Fighter_FirstInitialize_80067A84':
                if fighter_initialize_call != call['call'] or fighter_initialized:
                    raise ReplayProblem('unsupported',
                                        'fighter initialization return does not close its source generation',
                                        call=call)
                init_calls = descendants(call, {'HSD_ObjAllocInit'})
                fighter_inits = [item for item in init_calls
                                 if first_argument(item, 'pool') == fighter_pool_address]
                if not fighter_inits:
                    raise ReplayProblem('unsupported',
                                        'Fighter_FirstInitialize lacks a nested fighter_alloc_data pool initialization',
                                        call=call)
                if len(fighter_inits) != 1:
                    raise ReplayProblem('unsupported',
                                        'Fighter_FirstInitialize has ambiguous fighter_alloc_data pool initialization',
                                        call=call)
                init = fighter_inits[0]
                if fighter_pool_init_call != init['call']:
                    raise ReplayProblem('unsupported',
                                        'Fighter_FirstInitialize pool generation was not opened by its source pool init',
                                        call=call)
                if init['call'] not in derived_calls or derived_calls[init['call']].get('pool_generation') is None:
                    raise ReplayProblem('unsupported',
                                        'fighter_alloc_data pool initialization has no derived model generation',
                                        call=call)
                fighter_pool_generation = derived_calls[init['call']]['pool_generation']
                fighter_initialized = True
                fighter_initialized_scene_generation = scene_generation
                fighter_initialize_call = None
                fighter_pool_init_call = None
                derived_calls[call['call']] = {
                    'wrapper': 'Fighter_FirstInitialize_80067A84',
                    'pool_generation': fighter_pool_generation,
                }
                ownership_events.append({
                    'kind': 'fighter_pool_init', 'call': call['call'], 'sequence': row['sequence'],
                    'pool': fighter_pool_address, 'generation': fighter_pool_generation,
                })
            elif function == 'ftDemo_ObjAllocInit':
                if demo_initialize_call != call['call']:
                    raise ReplayProblem('unsupported',
                                        'demo fighter pool initialization return does not close its source generation',
                                        call=call)
                init_calls = descendants(call, {'HSD_ObjAllocInit'})
                fighter_inits = [item for item in init_calls
                                 if first_argument(item, 'pool') == fighter_pool_address]
                if not fighter_inits:
                    raise ReplayProblem('unsupported',
                                        'ftDemo_ObjAllocInit lacks a nested fighter_alloc_data pool initialization',
                                        call=call)
                if len(fighter_inits) != 1:
                    raise ReplayProblem('unsupported',
                                        'ftDemo_ObjAllocInit has ambiguous fighter_alloc_data pool initialization',
                                        call=call)
                init = fighter_inits[0]
                if demo_pool_init_call != init['call']:
                    raise ReplayProblem('unsupported',
                                        'ftDemo_ObjAllocInit pool generation was not opened by its source pool init',
                                        call=call)
                derived = derived_calls.get(init['call'])
                if not derived or derived.get('pool_generation') is None:
                    raise ReplayProblem('unsupported',
                                        'demo fighter pool initialization has no derived model generation',
                                        call=call)
                demo_pool_generation = derived['pool_generation']
                demo_initialized = True
                demo_initialize_call = None
                demo_pool_init_call = None
                derived_calls[call['call']] = {
                    'wrapper': 'ftDemo_ObjAllocInit',
                    'pool_generation': demo_pool_generation,
                }
            elif function in ('Fighter_Create', 'ftDemo_CreateFighter'):
                demo_owner = function == 'ftDemo_CreateFighter'
                if demo_owner:
                    if (not demo_initialized or demo_pool_generation is None
                            or demo_initialize_call is not None):
                        raise ReplayProblem('unsupported',
                                            'ftDemo_CreateFighter return has no completed demo ownership generation',
                                            call=call)
                    owner_generation = demo_pool_generation
                    owner_kind = 'demo'
                else:
                    if (not scene_active or fighter_pool_generation is None
                            or fighter_initialized_scene_generation != scene_generation):
                        raise ReplayProblem('unsupported',
                                            'Fighter_Create return has no active source ownership generation',
                                            call=call)
                    owner_generation = fighter_pool_generation
                    owner_kind = 'vs'
                allocation_calls = descendants(call, {'HSD_ObjAlloc'})
                if not allocation_calls:
                    raise ReplayProblem('unsupported',
                                        f'{function} lacks a nested HSD_ObjAlloc source allocation', call=call)
                fighter_allocations = [item for item in allocation_calls
                                       if first_argument(item, 'pool') == fighter_pool_address]
                if not fighter_allocations:
                    raise ReplayProblem('unsupported',
                                        f'{function} nested allocations use no independent fighter_alloc_data pool',
                                        call=call)
                if len(fighter_allocations) != 1:
                    raise ReplayProblem('unsupported',
                                        f'{function} has ambiguous fighter_alloc_data nested allocations', call=call)
                allocation = fighter_allocations[0]
                allocation_derived = derived_calls.get(allocation['call'])
                if not allocation_derived or not allocation_derived.get('address'):
                    raise ReplayProblem('unsupported',
                                        f'{function} fighter allocation lacks a derived source address', call=call)
                if allocation_derived.get('pool_generation') != owner_generation:
                    raise ReplayProblem('unsupported',
                                        f'{function} fighter allocation belongs to an inconsistent pool generation',
                                        call=call)
                if gobj_pool_address is None:
                    raise ReplayProblem('unsupported',
                                        f'{function} lacks the independently identified gobj_alloc_data pool',
                                        call=call)
                gobj_allocations = [item for item in allocation_calls
                                    if first_argument(item, 'pool') == gobj_pool_address]
                if not gobj_allocations:
                    raise ReplayProblem('unsupported',
                                        f'{function} lacks a nested gobj_alloc_data source allocation',
                                        call=call)
                if len(gobj_allocations) != 1:
                    raise ReplayProblem('unsupported',
                                        f'{function} has ambiguous gobj_alloc_data nested allocations', call=call)
                gobj_allocation = gobj_allocations[0]
                gobj_derived = derived_calls.get(gobj_allocation['call'])
                if not gobj_derived or not gobj_derived.get('address'):
                    raise ReplayProblem('unsupported',
                                        f'{function} GObj allocation lacks a derived source address', call=call)
                fighter = wrapper_observation(row, call, 'fighter')
                require_u32_fields(fighter, ('gobj', 'address', 'slot', 'kind'), call)
                gobj = parse_u32(row.get('result'), 'Fighter_Create GObj result')
                equal(fighter['gobj'], gobj, f'{function} GObj identity', call)
                equal(gobj, gobj_derived['address'],
                      f'{function} GObj allocation identity', call)
                equal(fighter['address'], allocation_derived['address'],
                      f'{function} fighter identity', call)
                if gobj == 0 or fighter['address'] == 0:
                    raise ReplayProblem('validation', f'{function} returned a null source identity', call=call)
                all_owners = [*fighter_owners.values(), *demo_owners.values()]
                if gobj in {owner['gobj'] for owner in all_owners} or fighter['address'] in {
                        owner['fighter'] for owner in all_owners}:
                    raise ReplayProblem('unsupported',
                                        f'{function} reuses a fighter identity in the active source generation',
                                        call=call)
                owner = {
                    'gobj': gobj, 'fighter': fighter['address'], 'slot': fighter['slot'],
                    'fighter_kind': fighter['kind'], 'generation': scene_generation if not demo_owner else None,
                    'pool_generation': owner_generation, 'owner_kind': owner_kind,
                    'call': call['call'],
                }
                if not demo_owner:
                    fighter_owners[gobj] = owner
                    ownership_events.append({'kind': 'fighter_create', 'sequence': row['sequence'], **owner})
                else:
                    demo_owners[gobj] = owner
                derived_calls[call['call']] = {'wrapper': function, **owner}
            elif function == 'gm_Scene_Vs_OnEnter':
                if scene_enter_call != call['call'] or not scene_active:
                    raise ReplayProblem('unsupported',
                                        'VS scene-enter return does not close its source owner', call=call)
                if not fighter_initialized or fighter_pool_generation is None:
                    raise ReplayProblem('unsupported',
                                        'VS scene ownership lacks a completed fighter pool generation',
                                        call=call)
                observed = wrapper_observation(row, call, 'globals')
                require_u32_fields(observed, ('seed_ptr', 'ArenaStart', 'ArenaEnd', 'HeapArray', 'NumHeaps'), call)
                require_u32_fields(row.get('observed', {}), ('r2', 'r13'), call)
                derived_calls[call['call']] = {
                    'wrapper': 'gm_Scene_Vs_OnEnter', 'generation': scene_generation,
                    'fighter_pool_generation': fighter_pool_generation,
                    'globals': observed,
                }
                ownership_events.append({
                    'kind': 'vs_enter_complete', 'call': call['call'], 'sequence': row['sequence'],
                    'generation': scene_generation, 'fighter_pool_generation': fighter_pool_generation,
                })
                scene_enter_completed = True
            elif function == 'gm_Scene_Vs_OnExit':
                if scene_exit_call != call['call'] or not scene_active:
                    raise ReplayProblem('unsupported',
                                        'VS scene-exit return does not close its source owner', call=call)
                owner_count = len(fighter_owners)
                ownership_events.append({
                    'kind': 'vs_exit', 'call': call['call'], 'sequence': row['sequence'],
                    'generation': scene_generation, 'fighters': owner_count,
                })
                fighter_owners.clear()
                scene_active = False
                scene_enter_call = None
                scene_exit_call = None
                scene_enter_completed = False
                scene_exit_completed = True
                derived_calls[call['call']] = {
                    'wrapper': 'gm_Scene_Vs_OnExit', 'generation': scene_generation,
                }
            elif function == 'OSSetArenaLo':
                try:
                    expected = next(arena_lows)
                except StopIteration as error:
                    raise ReplayProblem('unsupported', 'additional OS arena transition lacks independently derived context', call=call) from error
                equal(args, [expected], 'OS arena-low transition', call)
                arena_lo = expected
            elif function == 'OSSetArenaHi':
                equal(args, [arena_hi], 'original OS arena-high', call)
            elif function == 'OSAllocFromArenaLo':
                equal(args, [stage['crash_allocation_size'], stage['crash_allocation_alignment']], 'boot crash allocation request', call)
                equal(row['result'], stage['crash_allocation_base'], 'boot crash allocation identity', call)
                arena_lo = stage['after_crash']
            elif function == 'HSD_AllocateFifo':
                equal(args, [stage['fifo_size']], 'original FIFO request', call)
                equal(row['result'], stage['after_xfb'], 'original FIFO identity', call)
            elif function == 'HSD_AllocateXFB':
                equal(args[0], stage['framebuffer_count'], 'original framebuffer count', call)
            elif function == 'OSInitAlloc':
                if os_initialized:
                    raise ReplayProblem('unsupported', 'reinitializing the entire SDK allocator is not modeled', call=call)
                equal(args, [context['arena_lo'], context['arena_hi'], context['heap_max_num']], 'independent OS initialization', call)
                equal(row['result'], context['arena_start'], 'OS descriptor end', call)
                output = run('os_snapshot', call)
                os_initialized = True
            elif function == 'OSCreateHeap':
                if 'HSD_CreateMainHeap' in names:
                    output = run('hsd_replace_create', call)
                else:
                    output = run('bootstrap_heap', call, index=os_created)
                    os_created += 1
                equal(args, output['args'], 'derived OS heap bounds', call)
                equal(row['result'], output['result'], 'derived OS heap identity', call)
            elif function == 'OSDestroyHeap':
                if 'HSD_CreateMainHeap' not in names:
                    raise ReplayProblem('unsupported', 'additional SDK heap lifetime requires source-owned request binding', call=call)
                output = run('hsd_replace_destroy', call)
                equal(args, output['args'], 'destroyed HSD heap', call)
                destroyed_heap = output['args'][0]
                for address, heap_id in list(observed_heaps.items()):
                    if heap_id == destroyed_heap:
                        observed_heaps.pop(address, None)
                        observed_labels.pop(address, None)
                        observed_pools.pop(address, None)
                for pool, backings in list(pool_backings.items()):
                    remaining = [backing for backing in backings if backing[0] != destroyed_heap]
                    if remaining:
                        pool_backings[pool] = remaining
                    else:
                        pool_backings.pop(pool, None)
            elif function == 'OSSetCurrentHeap':
                output = run('os_select_hsd', call)
                equal(args, output['args'], 'selected SDK heap', call)
                equal(row['result'], output['result'], 'previous SDK heap', call)
                hsd_heap = output['args'][0]
            elif function == 'HSD_SetHeap':
                equal(row['result'], hsd_heap, 'previous HSD heap selector', call)
                output = run('hsd_select', call, heap=args[0])
                hsd_heap = args[0]
            elif function == 'HSD_ObjSetHeap':
                output = run('object_heap_set', call)
                equal(args, output['args'], 'original object heap mode', call)
            elif function == 'OSAllocFromHeap':
                if 'HSD_MemAlloc' in names:
                    equal(args[0], hsd_heap, 'HSD allocation heap selector', call)
                    parent = next(p for p in ancestors if p['function'] == 'HSD_MemAlloc')
                    equal(args[1], parent['args'][0], 'HSD allocation request size', call)
                if 'lbHeap_80015BD0' in names:
                    wrapper = next(p for p in ancestors if p['function'] == 'lbHeap_80015BD0')
                    owner = derived_calls[wrapper['call']]
                    equal(owner['type'], 0, 'OS-backed game allocation type', call)
                    equal(args[0], owner['id'], 'OS-backed game allocation heap', call)
                    equal(args[1], wrapper['args'][1], 'game allocation size', call)
                output = run('raw_alloc', call, heap=args[0], requested=args[1], label=label)
                pointer(row, output, 'address', label, call)
                if output.get('status') == 'ok':
                    observed_heaps[row['result']] = args[0]
                derived_calls[call['call']]['heap'] = args[0]
            elif function == 'OSFreeToHeap':
                if 'HSD_Free' in names:
                    equal(args[0], hsd_heap, 'HSD free heap selector', call)
                if 'lbHeap_80015CA8' in names:
                    wrapper = next(p for p in ancestors if p['function'] == 'lbHeap_80015CA8')
                    owner = derived_calls[wrapper['call']]
                    equal(owner['type'], 0, 'OS-backed game release type', call)
                    equal(args[0], owner['id'], 'OS-backed game release heap', call)
                    equal(args[1], wrapper['args'][1], 'game released pointer', call)
                label = known(args[1], call)
                output = run('raw_free', call, heap=args[0], label=label)
                if output.get('status') == 'ok':
                    observed_labels.pop(args[1], None)
                    observed_heaps.pop(args[1], None)
                    observed_pools.pop(args[1], None)
                derived_calls[call['call']] = output
            elif function == 'HSD_MemAlloc':
                child = descendant(call, {'OSAllocFromHeap'})
                equal(row['result'], child['address'], 'HSD returned source allocation', call)
                derived_calls[call['call']] = child
            elif function == 'HSD_Free':
                nested = children.get(call['call'], [])
                if len(nested) != 1 or nested[0]['function'] != 'OSFreeToHeap':
                    raise ReplayProblem('unsupported', 'HSD free lacks its source SDK release', call=call)
                equal(nested[0]['args'][1], args[0], 'HSD released pointer', call)
            elif function == 'HSD_ObjAllocInit':
                if ('Fighter_FirstInitialize_80067A84' in names and
                        fighter_pool_address is None):
                    raise ReplayProblem('unsupported',
                                        'fighter initialization lacks the independently identified fighter_alloc_data pool',
                                        call=call)
                pool = pool_id(args, call)
                pool_heaps[pool] = hsd_heap
                for address, old_pool in list(observed_pools.items()):
                    if old_pool == pool:
                        observed_pools.pop(address, None)
                        observed_labels.pop(address, None)
                        observed_heaps.pop(address, None)
                if args[0] == fighter_pool_address:
                    is_vs_owner = ('Fighter_FirstInitialize_80067A84' in names)
                    is_demo_owner = ('ftDemo_ObjAllocInit' in names)
                    if is_vs_owner == is_demo_owner:
                        raise ReplayProblem(
                            'unsupported',
                            'fighter_alloc_data pool initialization lacks one explicit VS or demo owner',
                            call=call)
                    if is_vs_owner:
                        if fighter_initialize_call is None:
                            raise ReplayProblem(
                                'unsupported',
                                'fighter_alloc_data pool initialization lacks its Fighter_FirstInitialize owner',
                                call=call)
                        if fighter_pool_init_call is not None:
                            # A second pool initialization inside one wrapper
                            # is ambiguous even if the allocator can reset it.
                            raise ReplayProblem(
                                'unsupported',
                                'fighter_alloc_data pool initialization changes VS generation more than once',
                                call=call)
                    else:
                        if demo_initialize_call is None:
                            raise ReplayProblem(
                                'unsupported',
                                'fighter_alloc_data pool initialization lacks its ftDemo_ObjAllocInit owner',
                                call=call)
                        if demo_pool_init_call is not None:
                            raise ReplayProblem(
                                'unsupported',
                                'fighter_alloc_data pool initialization changes demo generation more than once',
                                call=call)
                output = run('pool_reset', call, pool=pool, size=args[1], align=args[2])
                if output.get('status') == 'ok':
                    pool_backings.pop(pool, None)
                    # A successful source pool reset is the independently
                    # observed retirement boundary for demo fighter owners.
                    # Do not clear them merely when the wrapper is entered:
                    # an incomplete/reset-failed wrapper must remain visible
                    # as pending ownership.
                    if args[0] == fighter_pool_address and demo_owners:
                        demo_owners.clear()
                if args[0] == fighter_pool_address:
                    if 'ftDemo_ObjAllocInit' in names:
                        demo_pool_init_call = call['call']
                        demo_pool_generation = (demo_pool_generation or 0) + 1
                        derived_calls[call['call']] = dict(output, pool_generation=demo_pool_generation)
                    else:
                        fighter_pool_init_call = call['call']
                        fighter_pool_generation = (fighter_pool_generation or 0) + 1
                        derived_calls[call['call']] = dict(output, pool_generation=fighter_pool_generation)
            elif function == '_HSD_ObjAllocForgetMemory':
                output = run('pool_registry_forget', call)
                equal(args, output['args'], 'object pool forget bounds', call)
            elif function == 'HSD_ObjAllocAddFree':
                child = descendant(call, {'OSAllocFromHeap'})
                output = run('pool_adopt', call, pool=pool_id(args, call), count=args[1],
                             label=child['label'], heap=child['heap'])
                equal(row['result'], output['result'], 'pool refill count', call)
                if output.get('status') == 'ok':
                    pool = pool_id(args, call)
                    pool_words = output.get('pool_words')
                    if not isinstance(pool_words, list) or len(pool_words) < 9:
                        raise ReplayProblem('model', 'pool refill lacks its derived object size', call=call)
                    pool_backings.setdefault(pool, []).append(
                        (child['heap'], child['address'], pool_words[8] * args[1]))
            elif function == 'HSD_ObjAlloc':
                if (('Fighter_Create' in names or 'ftDemo_CreateFighter' in names)
                        and fighter_pool_address is None):
                    raise ReplayProblem('unsupported',
                                        'fighter wrapper lacks the independently identified fighter_alloc_data pool',
                                        call=call)
                if args[0] == fighter_pool_address:
                    has_demo_owner = any(parent['function'] == 'ftDemo_CreateFighter'
                                         for parent in ancestors)
                    has_vs_owner = any(parent['function'] == 'Fighter_Create'
                                       for parent in ancestors)
                    if has_demo_owner == has_vs_owner:
                        raise ReplayProblem('unsupported',
                                            'fighter_alloc_data allocation lacks one explicit VS or demo owner',
                                            call=call)
                pool = pool_id(args, call)
                output = run('pool_pop', call, pool=pool, label=label)
                pointer(row, output, 'address', label, call)
                if output.get('status') == 'ok':
                    observed_pools[row['result']] = pool
                    backing_heap = output.get('backing_heap')
                    if (type(backing_heap) is not int or isinstance(backing_heap, bool)
                            or not 0 <= backing_heap <= 0xFFFFFFFF):
                        raise ReplayProblem(
                            'model',
                            'pool allocation lacks the model-derived backing heap identity',
                            call=call)
                    backings = pool_backings.get(pool)
                    if backings:
                        matches = [heap_id for heap_id, base, size in backings
                                   if base <= row['result'] < base + size]
                        if len(matches) != 1 or matches[0] != backing_heap:
                            raise ReplayProblem(
                                'model',
                                'pool object lacks a unique derived backing heap',
                                call=call)
                        observed_heaps[row['result']] = backing_heap
                    else:
                        # The native model derives this identity from its
                        # authored pool backing table.  A selected heap is
                        # not a valid substitute, even for a retail trace
                        # whose refill metadata is incomplete.
                        if retail:
                            raise ReplayProblem(
                                'model',
                                'retail pool allocation lacks an authored backing span',
                                call=call)
                        observed_heaps[row['result']] = backing_heap
                if args[0] == fighter_pool_address:
                    demo_owner = any(parent['function'] == 'ftDemo_CreateFighter'
                                     for parent in ancestors)
                    vs_owner = any(parent['function'] == 'Fighter_Create'
                                   for parent in ancestors)
                    if demo_owner == vs_owner:
                        raise ReplayProblem(
                            'unsupported',
                            'fighter_alloc_data allocation lacks one explicit VS or demo owner',
                            call=call)
                    generation = demo_pool_generation if demo_owner else fighter_pool_generation
                    initialized = demo_initialized if demo_owner else fighter_initialized
                    if not initialized or generation is None:
                        raise ReplayProblem(
                            'unsupported',
                            'fighter_alloc_data allocation occurs before its owned pool generation',
                            call=call)
                    derived_calls[call['call']]['pool_generation'] = generation
            elif function == 'HSD_ObjFree':
                label = known(args[1], call)
                output = run('pool_release', call, pool=pool_id(args, call), label=label)
                if output.get('status') == 'ok':
                    observed_labels.pop(args[1], None)
                    observed_heaps.pop(args[1], None)
                    observed_pools.pop(args[1], None)
            elif function in ('ARInit', 'ARAlloc', 'ARFree', 'ARGetSize'):
                table = layout['aram_stack_table']
                if function == 'ARInit':
                    equal(args, [table['address'], table['size'] // 4], 'ARAM stack table', call)
                    output = run('aram_init', call, initial_base=context['aram_base'],
                                 capacity=table['size'] // 4, hardware_size=context['aram_size'])
                    aram_cursor = output['address']
                elif function == 'ARAlloc':
                    output = run('aram_alloc', call, requested=args[0])
                    aram_lengths.append(output['size'])
                    aram_depth += 1
                    aram_cursor = output['address'] + output['size']
                elif function == 'ARFree':
                    output = run('aram_free', call)
                    equal(output['size'], aram_lengths.pop(), 'ARAM released size', call)
                    aram_depth -= 1
                    aram_cursor = output['address']
                else:
                    output = run('aram_size', call)
                equal(row['result'], output['size' if function == 'ARGetSize' else 'address'], 'ARAM return', call)
                equal(row['observed']['aram'], {'__AR_BlockLength': table['address'] + aram_depth * 4,
                    '__AR_FreeBlocks': table['size'] // 4 - aram_depth,
                    '__AR_Size': context['aram_size'], '__AR_StackPointer': aram_cursor,
                    '__AR_init_flag': 1}, 'ARAM metadata', call)
            elif function == 'lbMemory_80014E24' and 'lbMemory_8001564C' in names:
                if aram_cursor is None:
                    raise ReplayProblem('unsupported', 'history lacks original ARAM initialization/allocation events', call=call)
                base = layout['lbmemory_allocator']
                equal(args, [aram_cursor, context['aram_size']], 'initial handle bounds', call)
                output = run('handle_init_from_aram', call, allocator=base, mem_entries=base + 8,
                             heap_handles=base + 0x638, current_handle_slot=base + 0x69c)
                pointer(row, output, 'handle', label, call)
                output['handle_words'] = [0, aram_cursor, context['aram_size'], 0]
            elif function == 'lbHeap_80015F3C':
                output = run('game_init', call)
            elif function == 'lbHeap_800158D0':
                output = run('game_set', call, index=args[0], value=args[1])
            elif function == 'HSD_CreateMainHeap':
                output = run('hsd_replace_end', call)
                equal(row['result'], output['result'], 'replacement HSD heap', call)
            elif function in ('lbMemory_80014E24', 'lbMemory_800154D4'):
                output = run('game_new_handle' if function == 'lbMemory_80014E24' else 'game_new_current', call, label=label)
                equal(args, output['args'], 'new source handle bounds', call)
                pointer(row, output, 'result', label, call)
            elif function in ('lbMemory_80014EEC', 'lbMemory_800155A4'):
                output = run('game_destroy_handle' if function == 'lbMemory_80014EEC' else 'game_destroy_current', call)
                if 'args' in output:
                    equal(args, output['args'], 'destroyed source handle', call)
            elif function in ('lbMemory_80014FC8', 'lbMemory_8001529C'):
                wrapper_name = 'lbHeap_80015BD0' if function == 'lbMemory_80014FC8' else 'lbHeap_80015D6C'
                if not ancestors or ancestors[0]['function'] != wrapper_name:
                    raise ReplayProblem('unsupported', 'handle operation lacks original game-heap wrapper context', call=call)
                owner = ancestors[0]['args'][0]
                if function == 'lbMemory_80014FC8':
                    equal(args[1], ancestors[0]['args'][1], 'game allocation request size', call)
                    output = run('game_handle_alloc', call, index=owner, requested=args[1], label=label)
                    pointer(row, output, 'result', label, call)
                else:
                    if async_compaction_coverage:
                        output = derived_calls.get(call['call'])
                        if not output:
                            raise ReplayProblem('model',
                                                'asynchronous compaction return lacks its modeled entry state',
                                                call=call)
                    else:
                        output = run('game_handle_compact', call, index=owner)
                    equal(row['result'], output['result'], 'compaction result', call)
                    derived_calls[call['call']] = output
                equal(args[0], output['owner'], 'source heap handle owner', call)
            elif function == 'lbMemory_80015320' and async_compaction_coverage:
                token = compaction_callback_tokens.pop(call['call'], None)
                if token is None:
                    raise ReplayProblem('unsupported',
                                        'source compaction callback return lacks its validated entry token',
                                        call=call)
            elif function == 'fn_80015184' and async_compaction_coverage:
                token = compaction_alarm_tokens.pop(call['call'], None)
                if token is None:
                    raise ReplayProblem('unsupported',
                                        'RAM compaction callback return lacks its validated entry token',
                                        call=call)
            elif function == 'HSD_DevComRequest' and async_compaction_coverage:
                equal(row['result'], devcom_ids.pop(call['call']), 'DevCom request identity', call)

            elif function == 'HSD_DevComARAMCallback' and async_compaction_coverage:
                pass
            elif function == 'lbDvd_80017A80' and async_compaction_coverage:
                token = dvd_completion_tokens.pop(call['call'], None)
                if token is None:
                    raise ReplayProblem('unsupported',
                                        'DVD preload callback return lacks its validated entry token', call=call)
                equal(args[0], token['callback_arg'], 'DVD preload callback argument', call)
                equal(token['callback'], source_function_address('lbDvd_80017A80', call),
                      'DVD preload callback identity', call)
            elif function == 'lbMemFreeToHeap':
                if not ancestors or ancestors[0]['function'] != 'lbHeap_80015CA8':
                    raise ReplayProblem('unsupported', 'handle release lacks its original game-heap wrapper', call=call)
                if args[1] not in derived_payloads:
                    raise ReplayProblem('unsupported', 'handle release payload lacks a derived allocation producer', call=call)
                output = run('game_handle_free', call, index=ancestors[0]['args'][0],
                             label=derived_payloads[args[1]])
                equal(args, [output['owner'], output['payload']], 'released handle owner and payload', call)
                equal(args[1], ancestors[0]['args'][1], 'game wrapper released payload', call)
                if output.get('status') == 'ok':
                    derived_payloads.pop(args[1], None)
                derived_calls[call['call']] = output
            elif function == 'lbHeap_80015900':
                output = run('game_end', call)
            elif function in ('lbHeap_80015BD0', 'lbHeap_80015D6C'):
                child = descendant(call, {'lbMemory_80014FC8', 'lbMemory_8001529C', 'OSAllocFromHeap'})
                if function == 'lbHeap_80015BD0':
                    value = child['address'] if 'address' in child else child['payload' if child['owner_type'] == 3 else 'result']
                    equal(row['result'], value, 'game allocation wrapper result', call)
                    derived_calls[call['call']] = child
                else:
                    equal(row['result'], child['result'], 'game compaction wrapper result', call)
                output = run('game_snapshot', call)
            elif function == 'lbHeap_80015CA8':
                descendant(call, {'OSFreeToHeap', 'lbMemFreeToHeap'})
                output = run('game_snapshot', call)
            elif function in ('HSD_OSInit', 'lbMemory_8001564C'):
                pass  # Initialization is executed at each nested captured primitive.
            else:
                raise ReplayProblem('unsupported', f'no source binding for observed function {function}', call=call)
            if output is not None:
                metadata(output, row, call)
                if function == 'lbMemory_80014FC8':
                    derived_payloads[output['payload']] = label
                    derived_handles[output['result']] = label
            completed_count += 1
            if record == 'return':
                # A returned child may still be needed by its active parent
                # when that parent validates a wrapper-level result.  Keep
                # the completed subtree until the parent returns, then drop
                # all descendants in one step.  A top-level call has no
                # consumer after its own return, so release its result too.
                if bounded_state:
                    parent_id = call.get('parent')
                    if parent_id is None or parent_id not in active_enters:
                        prune_closed_children(call['call'])
                        derived_calls.pop(call['call'], None)
                    active_enters.pop(call['call'], None)
                # A return is replay-pending until all result, metadata and
                # ownership checks above have succeeded.  Keeping this after
                # the common tail preserves the failing return in either
                # replay mode without changing successful replay semantics.
                pending_ids.discard(call['call'])
    except ReplayProblem as error:
        current_call = error.call or current_call
        first_problem = {'kind': error.kind, 'reason': error.message,
                         'call': current_call.get('call'), 'sequence': current_call.get('sequence'),
                         'function': current_call.get('function')}
    wasm_matches = None
    wasm_problem = None
    if checked_wasm:
        driver = None
        try:
            driver = ModelDriver(wasm=True, source=ROOT / 'tests/allocation_lifetime_model.cpp',
                                 extra_impls=[ROOT / 'src/source_game_heap_context.cpp'])
            if bounded_state:
                from .allocation_replay_spool import run_checked_file_model
                checked = run_checked_file_model(
                    driver.runner, ROOT, model.actions, model.outputs,
                    Path(artifact_dir) / 'checked-wasm', timeout=1800)
                wasm_matches = checked['matches']
                mismatch = checked.get('mismatch_index')
                check_error = checked.get('error')
                stderr_path = checked.get('stderr')
                if check_error is not None:
                    wasm_problem = {
                        'kind': 'model', 'reason': str(check_error),
                        'stderr_path': str(stderr_path) if stderr_path else None,
                        'call': current_call.get('call'), 'sequence': current_call.get('sequence'),
                        'function': current_call.get('function'),
                    }
                elif not wasm_matches:
                    if mismatch is None:
                        raise ReplayProblem('model', 'checked Wasm mismatch lacks an index')
                    if mismatch == len(model.actions):
                        wasm_problem = {
                            'kind': 'validation',
                            'reason': 'checked Wasm output length differs from native model',
                            'call': current_call.get('call'), 'sequence': current_call.get('sequence'),
                            'function': current_call.get('function'),
                        }
                    else:
                        action = model.actions[mismatch]
                        wasm_problem = dict(action, kind='validation',
                                            reason='checked Wasm differs from native model')
                        wasm_problem.pop('command', None)
            else:
                wasm_outputs = driver.run([json.dumps(action['command']) for action in model.actions])
                wasm_matches = wasm_outputs == model.outputs
                if not wasm_matches:
                    mismatch = next((index for index, (a, b) in enumerate(zip(wasm_outputs, model.outputs)) if a != b),
                                    min(len(wasm_outputs), len(model.outputs)))
                    if mismatch == len(model.actions):
                        wasm_problem = {
                            'kind': 'validation',
                            'reason': 'checked Wasm output length differs from native model',
                            'call': current_call.get('call'), 'sequence': current_call.get('sequence'),
                            'function': current_call.get('function'),
                        }
                    else:
                        action = model.actions[mismatch]
                        wasm_problem = dict(action, kind='validation',
                                            reason='checked Wasm differs from native model')
                        wasm_problem.pop('command', None)
        except ReplayProblem as error:
            wasm_problem = {'kind': error.kind, 'reason': error.message,
                             'call': current_call.get('call'), 'sequence': current_call.get('sequence'),
                             'function': current_call.get('function')}
        except OSError as error:
            wasm_problem = {'kind': 'resource', 'reason': f'checked Wasm artifact setup failed: {error}',
                            'call': current_call.get('call'), 'sequence': current_call.get('sequence'),
                            'function': current_call.get('function')}
        finally:
            if driver is not None:
                driver.close()
    artifact_receipts = None
    artifact_problem = None
    if bounded_state:
        artifact_receipts = {}
        try:
            for name, store in (('actions', action_store), ('outputs', output_store),
                                ('identities', identity_store)):
                artifact_receipts[name] = store.receipt()
        except OSError as error:
            artifact_problem = {
                'kind': 'resource',
                'reason': f'allocation replay artifact receipt failed: {error}',
                'call': current_call.get('call'), 'sequence': current_call.get('sequence'),
                'function': current_call.get('function'),
            }
    cleanup_problem = None
    try:
        model.close()
    except OSError as error:
        cleanup_problem = {
            'kind': 'resource', 'reason': f'allocation replay model cleanup failed: {error}',
            'call': current_call.get('call'), 'sequence': current_call.get('sequence'),
            'function': current_call.get('function'),
        }
    if bounded_state:
        for store in (action_store, output_store, identity_store):
            try:
                store.close()
            except OSError as error:
                cleanup_problem = cleanup_problem or {
                    'kind': 'resource', 'reason': f'allocation replay artifact cleanup failed: {error}',
                    'call': current_call.get('call'), 'sequence': current_call.get('sequence'),
                    'function': current_call.get('function'),
                }
    if first_problem is None and wasm_problem is not None:
        first_problem = wasm_problem
    if first_problem is None and artifact_problem is not None:
        first_problem = artifact_problem
    if first_problem is None and cleanup_problem is not None:
        first_problem = cleanup_problem
    end = next((row for row in reversed(rows) if row['record'] == 'end'), {})
    # ``pending_ids`` is only the replayed prefix in artifact mode: an early
    # model failure leaves later source entries unseen and would otherwise
    # falsely turn a complete captured trace into an incomplete trace.  The
    # end record is validated by load_trace against the complete source call
    # stacks, so its pending_calls field is the source-truth summary.  Keep a
    # separate replay-prefix view for diagnostics.
    source_pending_value = end.get('pending_calls')
    source_pending_error = None
    if source_pending_value is None:
        source_pending = sorted(set(enters) - set(returns))
    elif isinstance(source_pending_value, list):
        source_pending = []
        seen_source_pending = set()
        for item in source_pending_value:
            value = item.get('call') if isinstance(item, dict) else item
            if type(value) is not int or isinstance(value, bool) or value < 0:
                source_pending_error = 'trace end pending_calls contains an invalid call identity'
                break
            if value in seen_source_pending:
                source_pending_error = 'trace end pending_calls contains a duplicate call identity'
                break
            if value not in enters:
                source_pending_error = 'trace end pending_calls references an unknown call identity'
                break
            seen_source_pending.add(value)
            source_pending.append(value)
        source_pending = sorted(source_pending)
    else:
        source_pending = []
        source_pending_error = 'trace end pending_calls is not a list'
    replay_pending = sorted(pending_ids)
    pending = source_pending
    stream_complete = (source_pending_error is None and end.get('status') == 'captured'
                       and not source_pending)
    if first_problem is None and stream_error_reason is not None:
        first_problem = {
            'kind': 'stream', 'reason': stream_error_reason,
            'call': current_call.get('call'), 'sequence': current_call.get('sequence'),
            'function': current_call.get('function'),
        }
    if first_problem is None and source_pending_error is not None:
        first_problem = {
            'kind': 'stream', 'reason': source_pending_error,
            'call': current_call.get('call'), 'sequence': current_call.get('sequence'),
            'function': current_call.get('function'),
        }
    boundary_flag = end.get('boundary_complete')
    scoped_prefix_complete = False
    if first_problem is None:
        if scene_active:
            boundary_call = enters.get(scene_enter_call, current_call)
            if pending:
                first_problem = {
                    'kind': 'incomplete',
                    'reason': 'boundary-complete VS prefix has pending source ownership calls',
                    'call': boundary_call.get('call'),
                    'sequence': boundary_call.get('sequence'),
                    'function': boundary_call.get('function'),
                }
            elif type(boundary_flag) is not bool:
                first_problem = {
                    'kind': 'stream',
                    'reason': 'VS-enter allocation prefix requires an exact boolean boundary_complete flag',
                    'call': boundary_call.get('call'),
                    'sequence': boundary_call.get('sequence'),
                    'function': boundary_call.get('function'),
                }
            elif not boundary_flag:
                first_problem = {
                    'kind': 'incomplete',
                    'reason': 'source ownership wrapper did not reach its matching VS/fighter lifecycle boundary',
                    'call': boundary_call.get('call'),
                    'sequence': boundary_call.get('sequence'),
                    'function': boundary_call.get('function'),
                }
            elif not scene_enter_completed:
                first_problem = {
                    'kind': 'incomplete',
                    'reason': 'boundary-complete VS prefix lacks a returned gm_Scene_Vs_OnEnter',
                    'call': boundary_call.get('call'),
                    'sequence': boundary_call.get('sequence'),
                    'function': boundary_call.get('function'),
                }
            elif not fighter_owners:
                first_problem = {
                    'kind': 'incomplete',
                    'reason': 'boundary-complete VS prefix lacks a validated fighter owner',
                    'call': boundary_call.get('call'),
                    'sequence': boundary_call.get('sequence'),
                    'function': boundary_call.get('function'),
                }
            else:
                # The collector may intentionally stop after the first
                # verified VS-enter return.  This is an allocation identity
                # prefix only: keep the scene active and ownership incomplete
                # so it cannot be mistaken for a full VS/results/CSS session.
                scoped_prefix_complete = stream_complete
        elif fighter_initialize_call is not None or scene_exit_call is not None:
            first_problem = {
                'kind': 'incomplete',
                'reason': 'source ownership wrapper did not reach its matching VS/fighter lifecycle boundary',
                'call': scene_exit_call if scene_exit_call is not None else scene_enter_call,
                'sequence': current_call.get('sequence'),
                'function': current_call.get('function'),
            }
    ownership_complete = not (scene_active or fighter_initialize_call is not None
                              or scene_exit_call is not None or demo_initialize_call is not None
                              or demo_owners)
    # The source collector publishes its own lifecycle scope at the end of the
    # stream.  Preserve an explicit incomplete ownership result even when the
    # replay stops earlier on an unsupported operation and therefore has no
    # wrapper calls left open locally.  Internal state remains the lower bound:
    # a source claim cannot make an active replayed owner complete.
    source_ownership_complete = end.get('ownership_complete')
    if type(source_ownership_complete) is bool:
        ownership_complete = ownership_complete and source_ownership_complete
    # New VS-target captures publish their bounded ownership contract in the
    # end record.  Validate it against the independently observed wrapper
    # events; otherwise a target that stopped early could be mistaken for a
    # complete generation (or a target count could silently be ignored).
    target_fields = {'scope', 'vs_target', 'stop_at', 'target_complete',
                     'vs_entries', 'vs_exits', 'active_owner'}
    target_header_fields = {'scope', 'vs_target', 'stop_at'}
    ownership_target = None
    if first_problem is None and (target_fields & set(end) or target_header_fields & set(header)):
        ownership_target = {
            'header': {field: header.get(field) for field in sorted(target_header_fields)},
            'end': {field: end.get(field) for field in sorted(target_fields)},
        }
        target_error = None
        if end.get('scope') != 'vs_ownership':
            target_error = 'VS ownership target has an unknown scope'
        for field in ('scope', 'vs_target', 'stop_at'):
            if field not in header:
                target_error = target_error or f'VS ownership header lacks {field}'
            elif field not in end:
                target_error = target_error or f'VS ownership end record lacks {field}'
            elif header.get(field) != end.get(field):
                target_error = target_error or f'VS ownership header {field} differs from end record'
        target = end.get('vs_target')
        stop_at = end.get('stop_at')
        target_complete = end.get('target_complete')
        source_entries = end.get('vs_entries')
        source_exits = end.get('vs_exits')
        active_owner = end.get('active_owner')
        source_target_ownership = end.get('ownership_complete')
        header_target = header.get('vs_target')
        if (type(header_target) is not int or isinstance(header_target, bool)
                or header_target not in range(1, 17)):
            target_error = target_error or 'VS ownership header vs_target must be an integer from 1 through 16'
        if (type(target) is not int or isinstance(target, bool) or target not in range(1, 17)):
            target_error = target_error or 'VS ownership target must be an integer from 1 through 16'
        if stop_at not in {'entry', 'exit'}:
            target_error = target_error or 'VS ownership target stop_at must be entry or exit'
        if type(target_complete) is not bool:
            target_error = target_error or 'VS ownership target_complete must be boolean'
        if any(type(value) is not int or isinstance(value, bool) or value < 0
               for value in (source_entries, source_exits)):
            target_error = target_error or 'VS ownership entry and exit counts must be non-negative integers'
        if type(active_owner) is not bool:
            target_error = target_error or 'VS ownership active_owner must be boolean'
        if type(source_target_ownership) is not bool:
            target_error = target_error or 'VS ownership ownership_complete must be boolean'
        observed_entries = sum(event['kind'] == 'vs_enter' for event in ownership_events)
        observed_exits = sum(event['kind'] == 'vs_exit' for event in ownership_events)
        if target_error is None and (source_entries != observed_entries or source_exits != observed_exits):
            target_error = 'VS ownership counts differ from replayed source lifecycle events'
        if target_error is None and active_owner != (observed_entries > observed_exits):
            target_error = 'VS ownership active_owner differs from replayed scene state'
        if target_error is None and target_complete:
            expected_exits = target if stop_at == 'exit' else target - 1
            if source_entries != target or source_exits != expected_exits:
                target_error = 'completed VS ownership target has inconsistent entry/exit counts'
            elif stop_at == 'entry' and not active_owner:
                target_error = 'completed VS entry target must retain its active owner'
            elif stop_at == 'exit' and active_owner:
                target_error = 'completed VS exit target must have no active owner'
            elif source_target_ownership != (stop_at == 'exit'):
                target_error = 'completed VS ownership target has inconsistent ownership_complete'
        elif target_error is None and source_target_ownership:
            target_error = 'incomplete VS ownership target cannot claim ownership_complete'
        if target_error is None and not target_complete and end.get('status') == 'captured':
            target_error = 'incomplete VS ownership target cannot have captured status'
        if target_error is not None:
            first_problem = {
                'kind': 'stream', 'reason': target_error,
                'call': current_call.get('call'), 'sequence': current_call.get('sequence'),
                'function': current_call.get('function'),
            }
    source_prefix_complete = boundary_flag is True and not ownership_complete
    observed_target_count = end.get('vs_entries')
    if not (type(observed_target_count) is int and not isinstance(observed_target_count, bool)):
        observed_target_count = sum(event['kind'] == 'vs_enter' for event in ownership_events)
    repeated_target = observed_target_count > 1
    if scoped_prefix_complete or source_prefix_complete:
        completion_scope = 'repeated_vs_enter_prefix' if repeated_target else 'vs_enter_prefix'
    elif ownership_complete:
        completion_scope = 'repeated_vs_ownership_exit' if repeated_target else 'ownership_exit'
    else:
        completion_scope = None
    if bounded_state:
        action_value = artifact_receipts.get('actions')
        output_value = artifact_receipts.get('outputs')
        identity_value = artifact_receipts.get('identities')
        replay_hashes = {
            'actions_sha256': (artifact_receipts.get('actions') or {}).get('canonical_array_sha256'),
            'outputs_sha256': (artifact_receipts.get('outputs') or {}).get('canonical_array_sha256'),
            'identities_sha256': (artifact_receipts.get('identities') or {}).get('canonical_array_sha256'),
        }
    else:
        action_value = model.actions
        output_value = model.outputs
        identity_value = identities
        replay_hashes = {
            'actions_sha256': canonical_sha256(model.actions),
            'outputs_sha256': canonical_sha256(model.outputs),
            'identities_sha256': canonical_sha256(identities),
        }
    report = {
        'schema': 'melee-web-original-allocation-replay', 'version': 2,
        'status': ('validated_prefix' if stream_complete else 'incomplete_prefix') if first_problem is None else first_problem['kind'],
        'complete': False, 'scope': 'allocation_lifetime_prefix',
        'trace_complete': stream_complete, 'trace_end_status': end.get('status'),
        'calls_seen': len(enters), 'calls_replayed': completed_count,
        'pending_calls': pending, 'replay_pending_calls': replay_pending,
        'all_captured_calls_replayed': first_problem is None and stream_complete,
        'boundary_complete': boundary_flag if type(boundary_flag) is bool else None,
        'completion_scope': completion_scope,
        'ownership_target': ownership_target,
        'model_actions': len(model.actions), 'pointer_aliases': len(identities),
        'derived_identities': identity_value, 'replay_actions': action_value, 'model_outputs': output_value,
        'artifact_receipts': artifact_receipts,
        # Wrapper observations are diagnostic ownership joins.  They are kept
        # separate from allocator identities so a captured fighter pointer can
        # never become a model allocation input.
        'ownership_events': ownership_events,
        'ownership_complete': ownership_complete,
        'checked_wasm_matches_native': wasm_matches,
        'checked_wasm_problem': wasm_problem,
        'modeled_functions': sorted(name for name in modeled_functions if name),
        'replay_hashes': replay_hashes,
        'derived_context': context,
        'boot_context_provenance': {'mode': 'independent_boot', **verified},
        'provenance': {'dol_sha1': profile['dol_sha1'], 'source_revision': profile['source_revision'],
                       'profile_sha256': sha256_file(profile_path), 'trace_sha256': sha256_file(trace)},
        'scenario_gate': 'not_evaluated_no_complete_scenario_receipt',
    }
    if first_problem:
        report['first_unsupported'] = first_problem
    if require_complete:
        raise ReplayProblem('incomplete', 'full scenario allocation acceptance has not been established')
    return report
