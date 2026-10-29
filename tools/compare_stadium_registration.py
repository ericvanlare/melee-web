#!/usr/bin/env python3
"""Compare selected observed registration edges, never cross-runtime addresses.

The fresh strict paired RNG comparison selects owner correspondences. Actual
allocation generations and VS/world boundaries then join registration events.
Nothing here turns a pool reset, retirement, or FE24 request into ObjFree.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

ROLES = {'grStadium_801D1390': 1, 'efLib_particles_proc_main': 2, 'Fighter_8006A360': 3}
OPS = {'setup_registration': 'setup', 'proc_link': 'link',
       'proc_scheduler_unlink': 'scheduler_unlink', 'proc_owner_unlink': 'owner_unlink',
       'proc_remove_request': 'remove_request'}
KINDS = {6: ('setup', 'enter'), 7: ('setup', 'return'),
         12: ('link', 'enter'), 8: ('link', 'return'),
         13: ('scheduler_unlink', 'enter'), 9: ('scheduler_unlink', 'return'),
         14: ('owner_unlink', 'enter'), 10: ('owner_unlink', 'return'),
         11: ('remove_request', 'enter'), 15: ('remove_request', 'return')}


class Refusal(ValueError):
    pass


def need(condition, message):
    if not condition:
        raise Refusal(message)


def ptr(value):
    need(isinstance(value, str), 'pointer token must be serialized locally')
    try:
        number = int(value, 16)
    except ValueError as error:
        raise Refusal('invalid local pointer token') from error
    need(number >= 0, 'negative local pointer token')
    return number


def pin(path):
    path = Path(path)
    need(path.is_file() and not path.is_symlink(), f'not a regular snapshot: {path}')
    before = path.stat()
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            digest.update(block)
    after = path.stat()
    need((before.st_ino, before.st_size, before.st_mtime_ns) ==
         (after.st_ino, after.st_size, after.st_mtime_ns), 'snapshot changed during hashing')
    return {'path': str(path.resolve()), 'bytes': before.st_size, 'sha256': digest.hexdigest()}


def read_pin(value):
    actual = pin(value['path'])
    need(actual == value, f'input identity differs: {value["path"]}')
    return Path(value['path'])


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    need(spec is not None and spec.loader is not None, 'module unavailable')
    result = importlib.util.module_from_spec(spec)
    sys.modules[name] = result
    spec.loader.exec_module(result)
    return result


def scene_bounds(enters, returns):
    """Use actual source callback calls, not RNG or seed equality, for intervals."""
    starts = sorted((v for v in enters.values() if v.get('function') == 'gm_Scene_Vs_OnEnter'),
                    key=lambda v: v['sequence'])
    exits = sorted((v for v in enters.values() if v.get('function') == 'gm_Scene_Vs_OnExit'),
                   key=lambda v: v['sequence'])
    need(len(starts) == len(exits) == 2, 'requires exactly two actual VS source lifetimes')
    result = []
    for i, (start, end) in enumerate(zip(starts, exits)):
        sr, er = returns.get(int(start['call'])), returns.get(int(end['call']))
        need(sr is not None and er is not None, 'VS source callback return missing')
        need(start['sequence'] < sr['sequence'] < end['sequence'] < er['sequence'],
             'VS callback source order differs')
        if i:
            need(result[-1]['end'] < start['sequence'], 'VS lifetimes overlap')
        result.append({'begin': start['sequence'], 'entry_return': sr['sequence'],
                       'exit_enter': end['sequence'], 'end': er['sequence']})
    return result


def fresh_owner_edges(paired, matches, rows, probes):
    """Reuse the strict action/event comparator; ordinals never select by seed."""
    reports = paired.compare_browser(matches, rows, probes)
    actions = paired.completed_actions(rows, matches)
    need(len(actions) == len(probes), 'native action/probe envelope count differs')
    edges, inverse = {}, {}
    for global_action, ((begin, _), probe) in enumerate(zip(actions, probes)):
        mi, ai = begin['match_index'], begin['local_action_index']
        records = matches[mi]['selected'].get(ai)
        if records is None:
            continue
        for ordinal, (original, native) in enumerate(zip(records, probe['draws'])):
            oe = paired.semantics._original_event(original, paired.owners)
            ne = paired.semantics._native_draw(native, global_action, ordinal, paired.owners)
            semantic, op = oe['owner'], oe['local_pointers']
            np = ne['local_pointers']
            need(semantic['status'] != 'outside_proc', 'selected RNG event lacks a scheduled owner')
            need(semantic == ne['owner'] and semantic['callback_symbol'] in ROLES,
                 'strict selected callback semantic identity differs')
            key = (mi, ptr(op['gobj']), ptr(op['proc']))
            value = (str(begin['world_generation']), ptr(np['gobj']), ptr(np['proc']))
            edge = {'match_index': mi, 'original': key[1:], 'native': value[1:],
                    'world': value[0], 'semantic': semantic,
                    'original_callback': ptr(op['callback']), 'native_callback': ptr(np['callback'])}
            need(key not in edges or edges[key] == edge, 'original owner maps to multiple native identities')
            reverse = (mi, *value)
            need(reverse not in inverse or inverse[reverse] == key, 'native owner maps to multiple original identities')
            edges[key] = edge
            inverse[reverse] = key
    need(edges and {e['match_index'] for e in edges.values()} == {0, 1}, 'owner mapping lacks two lifetimes')
    return list(edges.values()), reports


def original_stream(extraction, bounds, edges):
    need(extraction.get('schema') == 'melee-web-original-gobjproc-registration-extraction'
         and extraction.get('version') == 1, 'original extraction schema differs')
    events = extraction['events']
    allocations = {e['allocation_generation']: e for e in events if e['event'] == 'allocation_return'}
    need(len(allocations) == sum(e['event'] == 'allocation_return' for e in events), 'duplicate original generation')
    selected, owners, tokens, selected_setups = [], {}, {}, set()
    for index, edge in enumerate(edges):
        mi = edge['match_index']; b = bounds[mi]
        setups = [e for e in events if e['event'] == 'setup_registration'
                  and ptr(e['source_gobj_address']) == edge['original'][0]
                  and ptr(e['source_proc_address']) == edge['original'][1]
                  and b['begin'] < e['enter_sequence'] < e['return_sequence'] < b['end']]
        need(len(setups) == 1, f'match {mi}: selected local owner does not join exactly one actual SetupProc')
        s = setups[0]; selected_setups.add(s['return_sequence']); symbol = edge['semantic']['callback_symbol']
        need(s['callback_symbol'] == symbol and ptr(s['callback_address']) == edge['original_callback'] and
             s['priority'] == edge['semantic']['s_link'],
             'original callback/priority differs from strict RNG owner')
        oid = (mi, index); owners[oid] = edge
        for gen, pool, address in [(s['gobj_generation'], 1, edge['original'][0]),
                                   (s['proc_generation'], 2, edge['original'][1])]:
            a = allocations.get(gen)
            need(a is not None and a['pool_kind'] == pool and ptr(a['source_address']) == address
                 and b['begin'] < a['return_sequence'] < s['return_sequence'],
                 'original selected owner lacks actual in-lifetime allocation birth')
            token = ('gobj' if pool == 1 else 'proc', oid)
            key = (mi, gen)
            need(key not in tokens, 'selected owners alias an allocation generation')
            tokens[key] = token
        # Both allocation identities must still be live at their SetupProc return.
        for e in events:
            if e['event'] == 'free_return' and e.get('allocation_generation') in (s['gobj_generation'], s['proc_generation']):
                need(e['enter_sequence'] > s['return_sequence'], 'selected original allocation freed before registration')
            if e['event'] == 'pool_reinitialize_return' and s['enter_sequence'] < e['return_sequence'] < b['end']:
                need(s['proc_generation'] not in e['reset_residual_generations'] and
                     s['gobj_generation'] not in e['reset_residual_generations'],
                     'selected original generation reset inside matched lifetime')
    for mi, b in enumerate(bounds):
        for s in events:
            if s['event'] == 'setup_registration' and s.get('callback_symbol') in ROLES and b['begin'] < s['return_sequence'] < b['end']:
                need(s['return_sequence'] in selected_setups,
                     f'match {mi}: selected original callback registration has no strict RNG owner correspondence')
        for e in events:
            kind = e['event']
            if kind == 'allocation_return':
                token = tokens.get((mi, e['allocation_generation']))
                if token:
                    selected.append((e['return_sequence'], ('allocate', 'return', token)))
            elif kind == 'free_return':
                token = tokens.get((mi, e.get('allocation_generation')))
                if token:
                    need(e['allocation_identity_status'] == 'active_current_epoch', 'ambiguous original free after pool reset')
                    need(e['return_sequence'] < b['end'], 'original selected free occurs outside VS lifetime')
                    selected.extend([(e['enter_sequence'], ('free', 'enter', token)),
                                     (e['return_sequence'], ('free', 'return', token))])
            elif kind in OPS:
                token = tokens.get((mi, e.get('proc_generation')))
                if token:
                    need(b['begin'] < e['enter_sequence'] < e['return_sequence'] < b['end'],
                         'original selected registration edge crosses VS boundary')
                    selected.extend([(e['enter_sequence'], (OPS[kind], 'enter', token)),
                                     (e['return_sequence'], (OPS[kind], 'return', token))])
    selected.sort()
    return selected, owners


def native_stream(rows, status, edges):
    need(status.get('schema') == 'melee-web-source-registration-status-v1' and
         status.get('armed') is True and status.get('sealed') is True and
         status.get('invalid') is False and status.get('overflow') is False and
         status.get('active_world') == '0' and status.get('pending_setup') is False and
         status.get('count') == len(rows) and status.get('row_capacity') == 65536 and
         status.get('slot_capacity') == 8192 and 0 < status.get('storage_bytes', 0) <= 16*1024*1024,
         'native registration stream is not complete and healthy')
    active, stack, world_ranges = {}, [], {}
    last_generation = 0
    for i, r in enumerate(rows):
        need(r.get('schema') == 'melee-web-source-registration-row-v1' and r.get('sequence') == i,
             'native row framing/sequence differs')
        k, w = r['kind'], r['world_generation']
        need(type(k) is int and 1 <= k <= 15, 'native row kind differs')
        if k == 1:
            need(not active and not stack and w not in world_ranges and w != '0', 'native world begin differs')
            active[w] = {}; world_ranges[w] = [i, None]
        elif k == 2:
            need(w in active and not stack, 'native world end has pending source calls')
            need(r['residual_live'] == len(active[w]), 'native retirement residual count differs')
            del active[w]; world_ranges[w][1] = i
        else:
            need(w in active, 'native row outside actual world')
            if k == 3 and ptr(r['object']):
                gen = r['object_generation']
                need(int(gen) > last_generation and gen not in active[w], 'native allocation generation reused or reordered')
                last_generation = int(gen)
                active[w][gen] = r
            if k in (6, 7, 8, 9, 10, 11, 12, 13, 14):
                need(r['gobj_generation'] in active[w] and
                     ptr(active[w][r['gobj_generation']]['object']) == ptr(r['gobj']),
                     'native registration lacks live allocated GObj')
                if k != 6:
                    need(r['proc_generation'] in active[w] and
                         ptr(active[w][r['proc_generation']]['object']) == ptr(r['proc']),
                         'native registration lacks live allocated Proc')
            if k in (4, 5):
                a = active[w].get(r['object_generation'])
                need(a is not None and a['pool_kind'] == r['pool_kind'] and ptr(a['object']) == ptr(r['object']),
                     'native free lacks actual allocation identity')
                if k == 5:
                    del active[w][r['object_generation']]
            if k in (4, 6, 11, 12, 13, 14):
                need(r['call_index'] == i and r['call_depth'] == len(stack), 'native entry nesting differs')
                stack.append(i)
            elif k in (5, 7, 8, 9, 10, 15):
                need(stack and stack.pop() == r['call_index'] and r['call_depth'] == len(stack), 'native return nesting differs')
                need(rows[r['call_index']]['kind'] == {5:4, 7:6, 8:12, 9:13, 10:14, 15:11}[k],
                     'native return source operation differs')
    need(not active and not stack and status['worlds'] == len(world_ranges), 'native world lifetime incomplete')
    tokens = {}; selected_setups = set()
    for index, edge in enumerate(edges):
        w = edge['world']; mi = edge['match_index']
        need(w in world_ranges and world_ranges[w][1] is not None, 'native matched world missing checked retirement')
        matches = [r for r in rows if r['kind'] == 7 and r['world_generation'] == w and
                   (ptr(r['gobj']), ptr(r['proc'])) == tuple(edge['native'])]
        need(len(matches) == 1, 'native selected local owner does not join exactly one actual SetupProc')
        s = matches[0]; selected_setups.add(s['sequence']); semantic = edge['semantic']; oid = (mi, index)
        need(ptr(s['callback']) == edge['native_callback'] and s['owner_role'] == ROLES[semantic['callback_symbol']] and
             (s['classifier'], s['p_link'], s['s_link']) ==
             (semantic['classifier'], semantic['p_link'], semantic['s_link']), 'native registration semantic role differs')
        enter = rows[s['call_index']]
        need(ptr(enter['gobj']) == ptr(s['gobj']) and ptr(enter['callback']) == ptr(s['callback']) and
             enter['gobj_generation'] == s['gobj_generation'] and enter['s_link'] == s['s_link'],
             'native SetupProc callback/owner/priority changed')
        for gen, pool, address in [(s['gobj_generation'], 1, edge['native'][0]),
                                   (s['proc_generation'], 2, edge['native'][1])]:
            a = [r for r in rows if r['kind'] == 3 and r['world_generation'] == w and
                 r['object_generation'] == gen and r['pool_kind'] == pool and ptr(r['object']) == address]
            need(len(a) == 1 and a[0]['sequence'] < s['sequence'], 'native selected owner lacks actual allocation birth')
            token = ('gobj' if pool == 1 else 'proc', oid)
            need((w, gen) not in tokens, 'native selected owner aliases generation')
            tokens[w, gen] = token
    matched_worlds = {edge['world'] for edge in edges}
    for r in rows:
        if r['kind'] == 7 and r['world_generation'] in matched_worlds:
            need(r['sequence'] in selected_setups,
                 'selected native callback registration has no strict RNG owner correspondence')
    selected = []
    for r in rows:
        k, w = r['kind'], r['world_generation']
        if k in (3, 4, 5):
            token = tokens.get((w, r['object_generation']))
            if token:
                selected.append((r['sequence'], ('allocate' if k == 3 else 'free', 'enter' if k == 4 else 'return', token)))
        elif k in KINDS:
            token = tokens.get((w, r['proc_generation']))
            if k == 6:
                # Setup entry has no proc yet; correlate with its actual return.
                returns = [x for x in rows if x['kind'] == 7 and x['call_index'] == r['sequence']]
                need(len(returns) == 1, 'native SetupProc entry lacks unique actual return')
                token = tokens.get((w, returns[0]['proc_generation']))
            if token:
                # Immutable local callback and owner must agree on every edge.
                s = next(x for x in rows if x['kind'] == 7 and x['world_generation'] == w and
                         tokens.get((w, x['proc_generation'])) == token)
                need(ptr(r['callback']) == ptr(s['callback']) and ptr(r['gobj']) == ptr(s['gobj']) and
                     r['owner_role'] == s['owner_role'] and r['s_link'] == s['s_link'],
                     'native callback or owner changes between registration edges')
                selected.append((r['sequence'], (*KINDS[k], token)))
    return selected, world_ranges


def compare(extraction, bounds, rows, status, edges):
    need({e['match_index'] for e in edges} == {0, 1} and
         len({e['world'] for e in edges}) == 2, 'comparison requires two distinct matched worlds')
    # Stable owner IDs come from original registration order, not pointer values.
    def order(edge):
        b = bounds[edge['match_index']]
        values = [e['return_sequence'] for e in extraction['events'] if e['event'] == 'setup_registration'
                  and ptr(e['source_proc_address']) == edge['original'][1] and
                  ptr(e['source_gobj_address']) == edge['original'][0] and b['begin'] < e['return_sequence'] < b['end']]
        need(len(values) == 1, 'original owner occurrence is ambiguous')
        return edge['match_index'], values[0]
    edges = sorted(edges, key=order)
    original, _ = original_stream(extraction, bounds, edges)
    native, worlds = native_stream(rows, status, edges)
    for i in range(max(len(original), len(native))):
        left = original[i] if i < len(original) else None
        right = native[i] if i < len(native) else None
        need(left is not None and right is not None and left[1] == right[1],
             f'first selected registration divergence at edge {i}: original={left!r}; native={right!r}')
    return {'status': 'PASS_SELECTED_OBSERVED_REGISTRATION_CORRESPONDENCE',
            'selected_owners': len(edges), 'selected_edges': len(original),
            'owner_correspondence': edges, 'native_worlds': worlds,
            'original_pool_reset_residuals': extraction['summary'].get('reset_residual_count'),
            'native_unselected_proc_hook_calls': status.get('unselected_proc_hook_calls'),
            'unpaired': ['original neighbor fields unavailable', 'unselected callback registrations',
                         'unmatched source prefix', 'retirement residuals are not ObjFree'],
            'allocation_lifetime_parity': False, 'all_rng_writers_observed': False, 'C3': False}



PROOF_SHA = 'c3d4b475ad4dc5515efffc72dafa795affc786dab01fb8d8dfb9bfd3a6aa5318'
REDUCTION_SHA = 'e547e02adab6acda445412e9931b3d61d3267d28d9c206662407516a73b97b78'



def validate_successors(actual, expected):
    need(actual == expected, 'actual Results/source closure differs')
    need(len(actual)==8, 'two complete scene successor intervals required')
    for mi in range(2):
        group = actual[mi*4:mi*4+4]
        need([x['match_index'] for x in group]==[mi]*4 and
             [x['boundary'] for x in group]==['vs_exit','vs_exit_return','results_enter','return_css'] and
             all(group[i]['sequence'] < group[i+1]['sequence'] for i in range(3)),
             'scene successor order differs')
        need(group[2]['actual_pc']=='0x80177368' and group[2]['actual_lr']=='0x801a40e8',
             'Results entry does not bind mandatory dispatcher path')

def admit_retirement_proof(path, paths, projection):
    """Admit this reviewed source proof; never accept caller-authored booleans.

    The fixed proof is reference-specific. Dynamic B reset rows remain absent.
    Re-read actual primary successors, DOL instructions and every source leaf.
    """
    need(pin(path)['sha256'] == PROOF_SHA, 'unreviewed retirement proof')
    proof = json.loads(path.read_bytes())
    reduction_path = read_pin(proof['raw_reduction'])
    need(proof['raw_reduction']['sha256'] == REDUCTION_SHA, 'retirement reduction differs')
    reduction = json.loads(reduction_path.read_bytes())
    read_pin(proof['first_comparison_result'])
    for source in reduction['source_files']:
        read_pin(source)
    dol_pin = proof['original_DOL_call_path']['dol']
    need(pin(paths['dol']) == dol_pin, 'retirement DOL differs')
    need(pin(paths['symbols']) == proof['original_DOL_call_path']['symbol_map'], 'retirement symbols differ')
    from retail_allocation_profile import Dol
    dol = Dol(paths['dol'])
    for body in proof['original_DOL_call_path']['function_bodies']:
        raw = dol.read(ptr(body['address']), body['bytes'])
        need(hashlib.sha256(raw).hexdigest() == body['body_sha256'], 'retirement DOL body differs')
        for call in body['required_bl_sites']:
            need(int.from_bytes(dol.read(ptr(call['pc']),4),'big') == ptr(call['word']), 'retirement call differs')
    for word in proof['scene_on_enter_instruction_window']:
        need(int.from_bytes(dol.read(ptr(word['pc']),4),'big') == ptr(word['word']), 'Results dispatcher differs')
    need(paths['primary_reader'].resolve() == paths['source_paired_comparator'].parent.parent /
         'reference-capture/dolphin/reference_observer_stream.py', 'primary reader source location differs')
    reader = module(paths['primary_reader'], 'registration_retirement_primary_reader')
    primary_pin = projection['source_files']['native-primary.mwro']
    primary = read_pin(primary_pin)
    actual = []
    for row in reader.iter_records(primary):
        payload = row.get('payload', {})
        if row['event'] == 'boundary' and payload.get('boundary') in ('vs_exit','vs_exit_return','results_enter','return_css'):
            actual.append(dict(match_index=payload['match_index'],boundary=payload['boundary'],
                               sequence=row['seq'],actual_pc=hex(payload['pc']),actual_lr=hex(payload['lr']),
                               source_tick=row['source_tick'],draw_ordinal=row['draw_ordinal']))
    validate_successors(actual, proof['observed_successors'])
    return dict(proof=pin(path), reduction=pin(reduction_path), sources=reduction['source_files'],
                primary=primary_pin, successors=actual,
                B_dynamic_reset_rows_observed=False,
                B_retirement='SOURCE_PROVED_ACTIVE_ROOT_REPLACEMENT_NO_LATER_THAN_RESULTS_ENTRY',
                every_stale_pointer_cleared=False)


def compare_scheduled_lifetimes(extraction, bounds, rows, status, edges, closure):
    """Separate active-lifetime equivalence from literal storage-event equality."""
    need(len(bounds) == 2 and {e['match_index'] for e in edges} == {0,1} and
         len({e['world'] for e in edges}) == 2, 'two distinct owner lifetimes required')
    def setup_for(edge):
        b = bounds[edge['match_index']]
        matches = [x for x in extraction['events'] if x['event']=='setup_registration' and
                   ptr(x['source_gobj_address']) == edge['original'][0] and
                   ptr(x['source_proc_address']) == edge['original'][1] and
                   b['begin'] < x['return_sequence'] < b['end']]
        need(len(matches)==1, 'selected Setup occurrence differs')
        return matches[0]
    edges = sorted(edges,key=lambda e:(e['match_index'],setup_for(e)['return_sequence']))
    original, _ = original_stream(extraction,bounds,edges)
    native, worlds = native_stream(rows,status,edges)
    creation = {'allocate','setup','link'}
    oc = [value for _,value in original if value[0] in creation]
    nc = [value for _,value in native if value[0] in creation]
    need(oc == nc, 'selected creation/Setup/link order differs')
    need(all(value[0] in creation for _,value in original),
         'original selected owner has an in-match removal/free; not this retirement contract')
    retirement = []
    for index, edge in enumerate(edges):
        mi = edge['match_index']; setup = setup_for(edge); b = bounds[mi]
        oid = (mi,index)
        for event in extraction['events']:
            if event['event'] in OPS and event.get('proc_generation') == setup['proc_generation']:
                need(event.get('gobj_generation') == setup['gobj_generation'] and
                     ptr(event['source_proc_address']) == edge['original'][1] and
                     event['callback_symbol'] == edge['semantic']['callback_symbol'],
                     'original immutable registration identity changed')
                if event['event']=='proc_link':
                    need(event['owner_linked_after_return'] is True and
                         event['scheduler_linked_after_return'] is True, 'original link did not complete')
        for pool,gen in [(1,setup['gobj_generation']),(2,setup['proc_generation'])]:
            resets = [x for x in extraction['events'] if x['event']=='pool_reinitialize_return' and
                      gen in x['reset_residual_generations']]
            if mi == 0:
                need(len(resets)==1 and resets[0]['pool_kind']==pool and resets[0]['enter_sequence'] > b['end'] and
                     resets[0]['return_sequence'] < bounds[1]['begin'],
                     'A generation lacks observed post-exit pool retirement')
            else:
                need(not resets, 'B dynamic reset shape differs from source-proved-only contract')
        local = [(seq,value) for seq,value in native if value[2][1] == oid]
        cleanup = [(seq,value) for seq,value in local if value[0] not in creation]
        need(cleanup, 'native selected owner lacks local cleanup')
        for token in [('gobj',oid),('proc',oid)]:
            frees = [phase for _,(op,phase,t) in cleanup if op=='free' and t==token]
            need(frees == ['enter','return'], 'native selected generation lacks exact local ObjFree')
        proc_ops = [(op,phase) for _,(op,phase,t) in cleanup if t==('proc',oid) and op!='free']
        need(proc_ops == [('remove_request','enter'),('owner_unlink','enter'),
                         ('scheduler_unlink','enter'),('scheduler_unlink','return'),
                         ('owner_unlink','return'),('remove_request','return')],
             'native selected removal/unlink nesting differs')
        need(max(seq for seq,value in local if value[0] in creation) < min(seq for seq,_ in cleanup)
             and max(seq for seq,_ in cleanup) < worlds[edge['world']][1],
             'native selected generation resurrected or cleanup crosses checked world end')
        retirement.append(dict(match_index=mi,original_gobj_generation=setup['gobj_generation'],
                               original_proc_generation=setup['proc_generation'],native_world=edge['world'],
                               original_retirement='OBSERVED_POOL_RESET_RESIDUAL' if mi==0 else
                               'SOURCE_PROVED_ACTIVE_ROOT_REPLACEMENT_AT_RESULTS_BOUND',
                               dynamic_original_reset_observed=mi==0,native_cleanup_edges=len(cleanup)))
    try:
        literal = compare(extraction,bounds,rows,status,edges)
    except Refusal as error:
        literal = dict(status='FAIL_LITERAL_SELECTED_REGISTRATION_EDGE_ORDER',first_divergence=str(error))
    return dict(status='PASS_SELECTED_SCHEDULED_LIFETIME_CORRESPONDENCE',
                raw_literal_registration_comparison=literal,selected_owners=len(edges),
                creation_edges=len(oc),retirements=retirement,owner_correspondence=edges,
                native_local_cleanup_edges=len(native)-len(nc),source_closure=closure,
                unpaired=['B dynamic pool reset/free rows unavailable','literal cleanup event order',
                          'original neighbor fields unavailable','unselected registrations',
                          'every stale global pointer cleared','arbitrary allocator/writer coverage'],
                allocation_lifetime_parity=False,all_rng_writers_observed=False,C3=False)

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--scheduled-lifetimes', action='store_true')
    args = parser.parse_args(argv)
    manifest = json.loads(args.manifest.read_bytes())
    need(manifest.get('schema') == 'melee-web-registration-comparison-inputs-v1', 'manifest schema differs')
    paths = {k: read_pin(v) for k, v in manifest['inputs'].items()}
    required = {'extractor', 'source_paired_comparator', 'semantic_comparator', 'owner_semantics',
                'compact_decoder', 'trace', 'profile', 'dol', 'symbols', 'projection', 'browser_report',
                'browser_rows', 'browser_rng', 'registration_rows', 'registration_status'}
    if args.scheduled_lifetimes:
        required |= {'retirement_proof','primary_reader'}
    need(set(paths) == required, 'manifest roles differ')
    source_tools = paths['source_paired_comparator'].parent
    sys.path.insert(0, str(source_tools.parent))
    paired = module(paths['source_paired_comparator'], 'registration_paired_comparator')
    for role, imported in [('semantic_comparator', paired.semantics), ('owner_semantics', paired.owners),
                           ('compact_decoder', paired.compact)]:
        need(Path(imported.__file__).resolve() == paths[role].resolve(), f'imported dependency differs: {role}')
    extractor = module(paths['extractor'], 'registration_original_extractor')
    replay, profile, symbols = extractor.load_and_validate(paths['profile'], paths['dol'], paths['symbols'], source_tools)
    loaded = replay.load_trace(paths['trace'], disk_backed=True, temp_directory=args.out.parent)
    header, trace_rows, enters, returns = loaded
    try:
        replay.validate_provenance(header, profile, paths['profile'], paths['dol'], paths['symbols'])
        extraction = extractor.reduce_registration_trace(header, trace_rows, enters, returns, profile, symbols)
        bounds = scene_bounds(enters, returns)
    finally:
        close = getattr(trace_rows, 'close', None)
        if close:
            close()
    matches, strict_inputs = paired.load_reference(paths['projection'], manifest['inputs']['projection']['sha256'])
    edges, rng_reports = fresh_owner_edges(paired, matches, paired.read_rows(paths['browser_rows'], 'browser rows'),
                                           paired.read_rows(paths['browser_rng'], 'browser RNG'))
    # Fresh reference provenance must name the same original trace/profile.
    projection = paired.read_json(paths['projection'])
    source_pins = projection['source_files']
    for role, name in [('trace', 'native-allocations.jsonl'), ('profile', 'allocation-profile.json')]:
        need(name in source_pins and source_pins[name]['sha256'] == manifest['inputs'][role]['sha256'],
             f'fresh projection does not bind same original {role}')
    browser_report = paired.read_json(paths['browser_report'])
    need(browser_report.get('paired_v11_full_pair', {}).get('component_pass') is True and
         browser_report['paired_v11_full_pair'].get('comparison', {}).get('pass') is True and
         browser_report.get('stadium_registration', {}).get('complete') is True,
         'browser capture does not attest completed strict pair and registration framing')
    need(browser_report.get('inputs', {}).get('recipe', {}).get('sha256') == projection.get('recipe_sha256'),
         'browser recipe identity differs from fresh original projection')
    for role, name in [('browser_rows', 'paired-v11-full-rows.jsonl'), ('browser_rng', 'paired-v11-full-rng.jsonl'),
                       ('registration_rows', 'stadium-registration-rows.jsonl'),
                       ('registration_status', 'stadium-registration-status.json')]:
        need(paths[role].parent == paths['browser_report'].parent and paths[role].name == name,
             f'browser capture artifact location differs: {role}')
    rows = paired.read_rows(paths['registration_rows'], 'registration rows')
    if args.scheduled_lifetimes:
        closure = admit_retirement_proof(paths['retirement_proof'], paths, projection)
        result = compare_scheduled_lifetimes(extraction, bounds, rows, paired.read_json(paths['registration_status']), edges, closure)
    else:
        result = compare(extraction, bounds, rows, paired.read_json(paths['registration_status']), edges)
    result.update(schema='melee-web-selected-registration-scheduled-lifetime-comparison-v1' if args.scheduled_lifetimes else 'melee-web-selected-registration-comparison-v1', inputs=manifest['inputs'],
                  original_scene_bounds=bounds, strict_rng_reports=rng_reports, strict_reference_inputs=strict_inputs)
    if args.scheduled_lifetimes:
        for identity in [closure['reduction'], closure['primary'], *closure['sources']]:
            read_pin(identity)
    for role, identity in manifest['inputs'].items():
        need(pin(paths[role]) == identity, f'input changed during comparison: {role}')
    need(not args.out.exists(), 'output already exists')
    args.out.write_text(json.dumps(result, indent=2)+'\n')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (Refusal, ValueError, KeyError, TypeError) as error:
        print(f'registration comparison refused: {error}', file=sys.stderr)
        raise SystemExit(1)
