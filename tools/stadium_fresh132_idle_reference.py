"""Offline Fresh132 idle3500 adapter. No capture, browser admission or C3 claim.

Reuse the original wire/idle validators and the sealed legacy extractor's pairing
algorithm. The legacy run2 extractor and its fixed identities remain unchanged.

Echoed launch producer fields are not same-run provenance verification. Exact
binary/manifest/profile/GCI identity and same-run report/intentions joins remain
mandatory external immutable packet/preflight gates before capture acceptance.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
from pathlib import Path

from stadium_go_prefix import (
    validate_stadium_idle_3500_reference, _go2_start_context_record,
    _go_marker_seed_observation, _validate_stadium_idle_state_record,
    _slice, RNG_POINTER_TAG, RNG_VALUE_TAG, StadiumGoPrefixError,
)
from stadium_idle_browser_schedule import _build_schedule
from reference_observer_stream import iter_records
from reference_input_stream import validate_stream, validate_status
from whole_session_replay import _consumed_ports

FILES = {'observer', 'observer_status', 'inputs', 'input_status', 'menu_recipe',
         'recipe_input', 'intentions', 'launch', 'ready_expected'}
READY_SHA = '5011f00a1ac910adbbe8d8f6b9793bc6e2c88129d8c24b33a21d3e2211dae9d1'
RECIPE_SHA = '85d64433715ba61db429b0c15de70f7c48b6689c877e061c5111f58bd7c2957f'
CANONICAL_SHA = '5f4dd95fb2e00cd35aa9fa42de10758acc4998624c964a1a077d683ebebe95f8'
PRODUCER_KEYS = {'build', 'profile_gci_sha256', 'profile_sha256',
                 'menu_recipe_sha256', 'input_plan_sha256'}

class FreshIdleError(ValueError):
    pass

def require(ok, message):
    if not ok:
        raise FreshIdleError(message)

def read_bound(item):
    require(isinstance(item, dict) and set(item) == {'path','bytes','sha256'}, 'file identity shape')
    path = Path(item['path'])
    require(not path.is_symlink() and path.is_file(), 'identity requires regular non-symlink file')
    require(type(item['bytes']) is int and 0 <= item['bytes'] <= 64*1024*1024, 'file byte bound')
    require(path.stat().st_size == item['bytes'], 'file byte identity differs')
    raw = path.read_bytes()
    require(hashlib.sha256(raw).hexdigest() == item['sha256'], 'file SHA identity differs')
    return raw

def validate_binding(binding):
    require(set(binding) == {'schema','version','files','producer'}, 'binding key inventory')
    require(binding['schema'] == 'melee-web-fresh132-idle-reference-binding' and
            type(binding['version']) is int and binding['version'] == 1, 'binding schema/version')
    require(isinstance(binding['files'], dict) and set(binding['files']) == FILES, 'binding file inventory')
    raw = {name:read_bound(item) for name,item in binding['files'].items()}
    for name,limit in {'inputs':16777216,'intentions':1048576,'launch':1048576,'ready_expected':1048576,'observer_status':1048576,'input_status':1048576,'menu_recipe':1048576,'recipe_input':1048576}.items():
        require(len(raw[name]) <= limit,'existing '+name+' byte cap differs')
    for name,digest in [('ready_expected',READY_SHA),('recipe_input',RECIPE_SHA),('menu_recipe',CANONICAL_SHA)]:
        require(binding['files'][name]['sha256'] == digest, 'Fresh132 retained '+name+' identity differs')
    require(json.loads(raw['recipe_input']) == json.loads(raw['menu_recipe']), 'original/canonical recipe content differs')
    launch = json.loads(raw['launch'])
    require(isinstance(binding['producer'],dict) and set(binding['producer']) == PRODUCER_KEYS and
            binding['producer'] == {key:launch.get(key) for key in PRODUCER_KEYS}, 'launch producer binding differs')
    producer=binding['producer']; build=producer['build']
    require(isinstance(build,dict) and build.get('target')=='dolphin-nogui' and build.get('build_cpu')=='JITARM64' and
            build.get('dolphin_commit')=='c77bbaa0f372c3f72281602a8b087206706542cb' and
            all(isinstance(build.get(key),str) and re.fullmatch(r'[0-9a-f]{64}',build[key]) for key in ('sha256','binary_sha256')) and
            isinstance(producer['profile_sha256'],dict) and set(producer['profile_sha256'])=={'Config/Dolphin.ini','Config/GCPadNew.ini'} and
            all(isinstance(v,str) and re.fullmatch(r'[0-9a-f]{64}',v) for v in list(producer['profile_sha256'].values())+[producer['profile_gci_sha256']]),
            'producer build/profile/GCI identity malformed')
    require(launch.get('scope') == 'stadium_go_prefix' and launch.get('input_mode','record') == 'record' and 'input_replay' not in launch and
            launch.get('input_plan_sha256') is None and launch.get('stadium_constructor_owner') is True and
            launch.get('menu_recipe_sha256') == RECIPE_SHA and
            launch.get('original_idle_post_go_ticks') == 3500 and
            launch.get('stadium_go_ordinal2',False) is False, 'launch is not explicit fresh idle3500 recording')
    require(launch.get('caps') == dict(observer_bytes=67108864,input_bytes=16777216,
            observer_records=77602,pre_C_records=16384,intent_bytes=1048576,intent_records=4096,log_bytes=8388608) and
            launch.get('wall_seconds') == 180.0, 'existing idle caps/deadline differ')
    return raw

def extract_records(rows, route, idle):
    """Pair already strictly validated records; synthetic tests label their scope separately."""
    def event(row):
        return row.get('event'),row.get('payload',{}).get('boundary')
    setup = [r for r in rows if event(r) == ('boundary','setup')]
    go = [r for r in rows if r.get('payload',{}).get('phase') == 'go_after']
    require(len(setup) == len(go) == 1, 'unique Setup/GO required')
    end = route['draw_return_sequence']; c = route['go_source_tick_C']
    require(route['comparison_end_tick'] == c+3500 and
            type(idle['natural_tail_ticks']) is int and 0 <= idle['natural_tail_ticks'] <= 4,
            'idle target/natural tail differs')
    selected = rows[setup[0]['seq']+1:end+1]
    ticks = [r for r in selected if event(r) == ('boundary','source_tick')]
    draws = [r for r in selected if event(r) == ('boundary','draw_return')]
    post = {kind:sum(event(r) == ('boundary',kind) and r['seq'] > go[0]['seq'] for r in selected)
            for kind in ('source_tick','draw_enter','draw_return')}
    require(ticks and ticks[-1]['source_tick'] == c+3500+idle['natural_tail_ticks'], 'terminal tick/tail differs')
    schedule = _build_schedule(rows,_consumed_ports,setup_sequence=setup[0]['seq'],
        go_sequence=go[0]['seq'],terminal_sequence=end,c_tick=c,f_tick=route['first_full_post_go_source_tick_F'],
        expected_ticks=len(ticks),expected_draws=len(draws),expected_post_go_counts=post)
    require(len(idle['literal_input_bundles']) == len(ticks)-c, 'idle/full bundle count differs')
    for ours, original, provenance in zip(schedule['runtime_payload']['bundles'][c:],
            idle['literal_input_bundles'],schedule['bundle_provenance'][c:]):
        require(ours['ports'] == original['ports'] and
                ours['bundle'] == c+original['ordinal'] and
                provenance['consume_sequence'] == original['consume_sequence'], 'idle/full literal bundle join differs')
    states=[]; digest=hashlib.sha256()
    for row in ticks[c:]:
        ordinal=row['source_tick']-c
        state=_validate_stadium_idle_state_record(row,'IDLE',row['source_tick'])
        digest.update(json.dumps(state,sort_keys=True).encode())
        p=row['payload']; pointer=_slice(p,RNG_POINTER_TAG,'idle seed pointer',4); seed=_slice(p,RNG_VALUE_TAG,'idle seed',4)
        require(pointer['address'] == 0x804d5f94 and int.from_bytes(pointer['raw'],'big') == seed['address'], 'idle seed owner differs')
        states.append({'ordinal':ordinal,'scope':'comparison' if ordinal<=3500 else 'natural_tail',
                       'typed_fields':state,'seed_hex':seed['raw'].hex()})
    require(digest.hexdigest() == idle['state_observations_sha256'] and
            len(states) == 3501+idle['natural_tail_ticks'], 'typed state digest/count differs')
    return {'same_run_provenance_verified':False,
        'runtime_payload':schedule['runtime_payload'], 'host_only':{
        'route':route,'counts':schedule['counts'],'bundle_provenance':schedule['bundle_provenance'],
        'action_provenance':schedule['action_provenance'],'typed_states':states,
        'go_seed_observation':_go_marker_seed_observation(go[0]),
        'comparison_post_go_ticks':3500,'natural_tail_ticks':idle['natural_tail_ticks']}}

def validate_fresh_context(rows,route,expected):
    entry=[r for r in rows if r.get('payload',{}).get('boundary')=='entry']
    setup=[r for r in rows if r.get('payload',{}).get('boundary')=='setup']
    require(len(entry)==len(setup)==1,'unique Entry/Setup required')
    a=_go2_start_context_record(entry[0],'vs_entry'); b=_go2_start_context_record(setup[0],'vs_setup')
    typed=a['typed_start']
    require(a['seed_hex']==expected['random_seed_hex'] and
            typed['rules']==expected['start']['rules'] and typed['players'][:4]==expected['start']['players'],
            'actual Entry differs from retained Fresh132 declared Start/seed')
    ctor=route['constructor_observations']['state_projection']; seq=ctor['source_sequences']
    require(a['sequence']<min(seq)<=max(seq)<b['sequence'] and
            a['source_tick']==b['source_tick'] and a['draw_ordinal']==b['draw_ordinal'] and
            a['start_sha256']==b['start_sha256'] and a['typed_start']==b['typed_start'] and
            ctor['seed_before_hex']==a['seed_hex'],'Entry/constructor/Setup handoff differs')
    return {'entry':a,'setup':b,'constructor':ctor}

def extract(binding):
    raw=validate_binding(binding); files=binding['files']; path=lambda name:Path(files[name]['path'])
    # Complete idle parsing precedes typed/profile projection: GO+2 is never a positive idle source.
    idle=validate_stadium_idle_3500_reference(path('observer'),status_path=path('observer_status'),
        extract_inputs=True,include_constructor_state=True)
    input_summary=validate_stream(path('inputs'))
    validate_status(path('input_status'),mode='record',events=input_summary['events'],require_complete=True)
    require(input_summary.get('complete') is True and input_summary['events']>0,'incomplete fresh MWRI')
    rows=list(iter_records(path('observer'),max_bytes=67108864,max_records=77602))
    route=idle['route']; result=extract_records(rows,route,idle)
    context=validate_fresh_context(rows,route,json.loads(raw['ready_expected'])['expected'])
    result.update(schema='melee-web-fresh132-idle-reference-extract',version=1,
        decision='PASS_OFFLINE_ORIGINAL_IDLE_REFERENCE_COMPONENT_ONLY',source_identity=binding,
        claims=dict(browser_execution=False,lifetime_B=False,per_owner_rng_equivalence=False,
                    fresh132_menu_prefix_equivalence=False,actual_capture_acceptance=False,C3=False))
    result['host_only']['start_context']=context
    require(raw == {name:read_bound(item) for name,item in files.items()},'input changed during extraction')
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binding',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(); require(not args.output.exists(),'output already exists')
    result=extract(json.loads(args.binding.read_text()))
    encoded=(json.dumps(result,sort_keys=True,indent=2)+'\n').encode()
    require(len(encoded)<=32*1024*1024,'bounded extraction output exceeded')
    with args.output.open('xb') as stream:stream.write(encoded)

if __name__=='__main__':main()
