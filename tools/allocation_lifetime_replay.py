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

from .allocation_history_replay import (
    ROOT, ModelDriver, ReplayProblem, call_chain, canonical_sha256, parse_result,
    sha256_file, parse_u32,
)


class StreamModel:
    def __init__(self):
        self.drivers = []
        self.processes = []
        self.actions = []
        self.outputs = []
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
        if results[0].get('status') != 'ok':
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
                     require_complete: bool) -> dict:
    layout = validate_layout(verified)
    retail = not profile['source_revision'].startswith('synthetic-')
    pool_ids = {address: index for index, (_, address) in
                enumerate(sorted(layout['pool_descriptors'].items()), 1)}
    descriptors = layout['lbheap_descriptors']
    if not descriptors or descriptors[-1] != [6, 0, 0, 0]:
        raise ReplayProblem('provenance', 'original game-heap descriptor sentinel differs')
    model = StreamModel()
    observed_labels: dict[int, str] = {}
    derived_calls: dict[int, dict] = {}
    derived_payloads: dict[int, str] = {}
    completed = []
    identities = []
    ownership_events = []
    fighter_owners = {}
    children: dict[int, list[dict]] = {}
    for call in enters.values():
        if call.get('parent') is not None:
            children.setdefault(call['parent'], []).append(call)
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
    repeated_counts = {}
    modeled_functions = set()
    static_pool_descriptors = {
        name: parse_u32(address, 'independent pool descriptor')
        for name, address in layout['pool_descriptors'].items()
    }
    fighter_pool_address = static_pool_descriptors.get('fighter_alloc_data')
    fighter_initialized = False
    fighter_pool_generation = None
    fighter_initialize_call = None
    scene_active = False
    scene_generation = 0
    scene_enter_call = None
    scene_exit_call = None
    scene_enter_completed = False
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
        'gm_Scene_Vs_OnEnter', 'gm_Scene_Vs_OnExit',
    }
    declarations = {item['name']: item for item in profile.get('functions', [])}

    current_call = header

    def equal(actual, expected, what, call):
        if actual != expected:
            raise ReplayProblem('validation', f'{what} differs: observed {actual!r}, derived {expected!r}', call=call)

    def run(op, call, **fields):
        modeled_functions.add(call.get('function'))
        return model.run({'op': op, **fields}, call)

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
            identities.append({'call': call['call'], 'sequence': row['sequence'],
                               'function': call['function'], 'label': label,
                               'derived': output[field]})
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

    try:
        run('configure', header, heap_count=context['heap_max_num'], descriptor_base=context['arena_lo'],
            arena_start=context['arena_start'], arena_end=context['arena_end'],
            main_lo=context['main_begin'], main_hi=context['main_end'], initial_hsd_heap=hsd_heap,
            pools=' '.join(f'{index} {address}' for address, index in pool_ids.items()),
            descriptors=' '.join(str(word) for desc in descriptors[:-1] for word in desc))
        for row in rows:
            record = row['record']
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
            ancestors = call_chain(call, enters)
            names = [parent['function'] for parent in ancestors]
            label = f"call_{call['call']}"
            if record == 'enter':
                if function not in supported:
                    raise ReplayProblem('unsupported', f'no source binding for observed function {function}', call=call)
                if function == 'lbHeap_80015900':
                    run('game_begin', call)
                elif function == 'HSD_CreateMainHeap':
                    output = run('hsd_replace_begin', call)
                    equal(args, output['args'], 'replacement bounds', call)
                elif function in ('lbHeap_80015BD0', 'lbHeap_80015CA8'):
                    derived_calls[call['call']] = run('game_owner', call, index=args[0])
                elif function == 'Fighter_FirstInitialize_80067A84':
                    # This diagnostic prefix models one source initialization.
                    # A later VS generation may call this again, but accepting
                    # that reset requires a separately captured ownership
                    # contract; do not infer reuse from this prefix.
                    if fighter_initialized or fighter_initialize_call is not None:
                        raise ReplayProblem('unsupported',
                                            'fighter pool initialization repeats an existing source generation',
                                            call=call)
                    if fighter_pool_address is None:
                        raise ReplayProblem('unsupported',
                                            'fighter initialization lacks the independently identified fighter_alloc_data pool',
                                            call=call)
                    fighter_initialize_call = call['call']
                elif function == 'Fighter_Create':
                    if not fighter_initialized or fighter_pool_generation is None:
                        raise ReplayProblem('unsupported',
                                            'Fighter_Create has no completed fighter pool generation',
                                            call=call)
                    if not scene_active:
                        raise ReplayProblem('unsupported',
                                            'Fighter_Create is outside an active VS scene owner', call=call)
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
                if init['call'] not in derived_calls or derived_calls[init['call']].get('pool_generation') is None:
                    raise ReplayProblem('unsupported',
                                        'fighter_alloc_data pool initialization has no derived model generation',
                                        call=call)
                fighter_pool_generation = derived_calls[init['call']]['pool_generation']
                fighter_initialized = True
                fighter_initialize_call = None
                derived_calls[call['call']] = {
                    'wrapper': 'Fighter_FirstInitialize_80067A84',
                    'pool_generation': fighter_pool_generation,
                }
                ownership_events.append({
                    'kind': 'fighter_pool_init', 'call': call['call'], 'sequence': row['sequence'],
                    'pool': fighter_pool_address, 'generation': fighter_pool_generation,
                })
            elif function == 'Fighter_Create':
                if not scene_active or fighter_pool_generation is None:
                    raise ReplayProblem('unsupported',
                                        'Fighter_Create return has no active source ownership generation',
                                        call=call)
                allocation_calls = descendants(call, {'HSD_ObjAlloc'})
                if not allocation_calls:
                    raise ReplayProblem('unsupported',
                                        'Fighter_Create lacks a nested HSD_ObjAlloc source allocation', call=call)
                fighter_allocations = [item for item in allocation_calls
                                       if first_argument(item, 'pool') == fighter_pool_address]
                if not fighter_allocations:
                    raise ReplayProblem('unsupported',
                                        'Fighter_Create nested allocations use no independent fighter_alloc_data pool',
                                        call=call)
                if len(fighter_allocations) != 1:
                    raise ReplayProblem('unsupported',
                                        'Fighter_Create has ambiguous fighter_alloc_data nested allocations', call=call)
                allocation = fighter_allocations[0]
                allocation_derived = derived_calls.get(allocation['call'])
                if not allocation_derived or not allocation_derived.get('address'):
                    raise ReplayProblem('unsupported',
                                        'Fighter_Create fighter allocation lacks a derived source address', call=call)
                if allocation_derived.get('pool_generation') != fighter_pool_generation:
                    raise ReplayProblem('unsupported',
                                        'Fighter_Create fighter allocation belongs to an inconsistent pool generation',
                                        call=call)
                fighter = wrapper_observation(row, call, 'fighter')
                require_u32_fields(fighter, ('gobj', 'address', 'slot', 'kind'), call)
                gobj = parse_u32(row.get('result'), 'Fighter_Create GObj result')
                equal(fighter['gobj'], gobj, 'Fighter_Create GObj identity', call)
                equal(fighter['address'], allocation_derived['address'],
                      'Fighter_Create fighter identity', call)
                if gobj == 0 or fighter['address'] == 0:
                    raise ReplayProblem('validation', 'Fighter_Create returned a null source identity', call=call)
                if gobj in fighter_owners or fighter['address'] in {
                        owner['fighter'] for owner in fighter_owners.values()}:
                    raise ReplayProblem('unsupported',
                                        'Fighter_Create reuses a fighter identity in the active source generation',
                                        call=call)
                owner = {
                    'gobj': gobj, 'fighter': fighter['address'], 'slot': fighter['slot'],
                    'fighter_kind': fighter['kind'], 'generation': scene_generation,
                    'pool_generation': fighter_pool_generation, 'call': call['call'],
                }
                fighter_owners[gobj] = owner
                derived_calls[call['call']] = {'wrapper': 'Fighter_Create', **owner}
                ownership_events.append({'kind': 'fighter_create', 'sequence': row['sequence'], **owner})
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
                output = run('raw_free', call, heap=args[0], label=known(args[1], call))
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
                if args[0] == fighter_pool_address:
                    if fighter_initialize_call is None or 'Fighter_FirstInitialize_80067A84' not in names:
                        raise ReplayProblem('unsupported',
                                            'fighter_alloc_data pool initialization lacks its Fighter_FirstInitialize owner',
                                            call=call)
                    if fighter_pool_generation is not None and fighter_initialize_call is not None:
                        # A second pool initialization inside one wrapper is
                        # ambiguous even if the allocator model can reset it.
                        prior = [item for item in descendants(enters[fighter_initialize_call],
                                                               {'HSD_ObjAllocInit'})
                                 if item['call'] != call['call']
                                 and first_argument(item, 'pool') == fighter_pool_address]
                        if prior:
                            raise ReplayProblem('unsupported',
                                                'fighter_alloc_data pool initialization changes generation more than once',
                                                call=call)
                output = run('pool_reset', call, pool=pool, size=args[1], align=args[2])
                if args[0] == fighter_pool_address:
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
            elif function == 'HSD_ObjAlloc':
                if ('Fighter_Create' in names and fighter_pool_address is None):
                    raise ReplayProblem('unsupported',
                                        'Fighter_Create lacks the independently identified fighter_alloc_data pool',
                                        call=call)
                if args[0] == fighter_pool_address:
                    if fighter_initialize_call is not None or not fighter_initialized:
                        raise ReplayProblem('unsupported',
                                            'fighter_alloc_data allocation occurs before pool initialization completes',
                                            call=call)
                    if not any(parent['function'] == 'Fighter_Create' for parent in ancestors):
                        raise ReplayProblem('unsupported',
                                            'fighter_alloc_data allocation lacks a Fighter_Create wrapper owner',
                                            call=call)
                pool = pool_id(args, call)
                output = run('pool_pop', call, pool=pool, label=label)
                pointer(row, output, 'address', label, call)
                if args[0] == fighter_pool_address:
                    derived_calls[call['call']]['pool_generation'] = fighter_pool_generation
            elif function == 'HSD_ObjFree':
                output = run('pool_release', call, pool=pool_id(args, call), label=known(args[1], call))
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
                    output = run('game_handle_compact', call, index=owner)
                    equal(row['result'], output['result'], 'compaction result', call)
                    derived_calls[call['call']] = output
                equal(args[0], output['owner'], 'source heap handle owner', call)
            elif function == 'lbMemFreeToHeap':
                if not ancestors or ancestors[0]['function'] != 'lbHeap_80015CA8':
                    raise ReplayProblem('unsupported', 'handle release lacks its original game-heap wrapper', call=call)
                if args[1] not in derived_payloads:
                    raise ReplayProblem('unsupported', 'handle release payload lacks a derived allocation producer', call=call)
                output = run('game_handle_free', call, index=ancestors[0]['args'][0],
                             label=derived_payloads[args[1]])
                equal(args, [output['owner'], output['payload']], 'released handle owner and payload', call)
                equal(args[1], ancestors[0]['args'][1], 'game wrapper released payload', call)
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
            completed.append(call['call'])
    except ReplayProblem as error:
        current_call = error.call or current_call
        first_problem = {'kind': error.kind, 'reason': error.message,
                         'call': current_call.get('call'), 'sequence': current_call.get('sequence'),
                         'function': current_call.get('function')}
    finally:
        model.close()
    wasm_matches = None
    wasm_problem = None
    if checked_wasm:
        driver = None
        try:
            driver = ModelDriver(wasm=True, source=ROOT / 'tests/allocation_lifetime_model.cpp',
                                 extra_impls=[ROOT / 'src/source_game_heap_context.cpp'])
            wasm_outputs = driver.run([json.dumps(action['command']) for action in model.actions])
            wasm_matches = wasm_outputs == model.outputs
            if not wasm_matches:
                mismatch = next((index for index, (a, b) in enumerate(zip(wasm_outputs, model.outputs)) if a != b),
                                min(len(wasm_outputs), len(model.outputs)))
                action = model.actions[min(mismatch, len(model.actions) - 1)]
                wasm_problem = dict(action, kind='validation', reason='checked Wasm differs from native model')
                wasm_problem.pop('command', None)
        except ReplayProblem as error:
            wasm_problem = {'kind': error.kind, 'reason': error.message,
                             'call': current_call.get('call'), 'sequence': current_call.get('sequence'),
                             'function': current_call.get('function')}
        finally:
            if driver is not None:
                driver.close()
    if first_problem is None and wasm_problem is not None:
        first_problem = wasm_problem
    end = next((row for row in reversed(rows) if row['record'] == 'end'), {})
    pending = sorted(set(enters) - set(returns))
    stream_complete = (end.get('status') == 'captured' and not pending
                       and not any(row['record'] == 'error' for row in rows))
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
    ownership_complete = not (scene_active or fighter_initialize_call is not None or scene_exit_call is not None)
    # The source collector publishes its own lifecycle scope at the end of the
    # stream.  Preserve an explicit incomplete ownership result even when the
    # replay stops earlier on an unsupported operation and therefore has no
    # wrapper calls left open locally.  Internal state remains the lower bound:
    # a source claim cannot make an active replayed owner complete.
    source_ownership_complete = end.get('ownership_complete')
    if type(source_ownership_complete) is bool:
        ownership_complete = ownership_complete and source_ownership_complete
    source_prefix_complete = boundary_flag is True and not ownership_complete
    report = {
        'schema': 'melee-web-original-allocation-replay', 'version': 2,
        'status': ('validated_prefix' if stream_complete else 'incomplete_prefix') if first_problem is None else first_problem['kind'],
        'complete': False, 'scope': 'allocation_lifetime_prefix',
        'trace_complete': stream_complete, 'trace_end_status': end.get('status'),
        'calls_seen': len(enters), 'calls_replayed': len(completed), 'pending_calls': pending,
        'all_captured_calls_replayed': first_problem is None and stream_complete,
        'boundary_complete': boundary_flag if type(boundary_flag) is bool else None,
        'completion_scope': ('vs_enter_prefix' if (scoped_prefix_complete or source_prefix_complete) else
                             'ownership_exit' if ownership_complete else None),
        'model_actions': len(model.actions), 'pointer_aliases': len(identities),
        'derived_identities': identities, 'replay_actions': model.actions, 'model_outputs': model.outputs,
        # Wrapper observations are diagnostic ownership joins.  They are kept
        # separate from allocator identities so a captured fighter pointer can
        # never become a model allocation input.
        'ownership_events': ownership_events,
        'ownership_complete': ownership_complete,
        'checked_wasm_matches_native': wasm_matches,
        'checked_wasm_problem': wasm_problem,
        'modeled_functions': sorted(name for name in modeled_functions if name),
        'replay_hashes': {'actions_sha256': canonical_sha256(model.actions),
                          'outputs_sha256': canonical_sha256(model.outputs),
                          'identities_sha256': canonical_sha256(identities)},
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
