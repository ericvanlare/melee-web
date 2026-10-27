"""Strict decoder for the private live compact RNG stream; no parity admission.

The bounded v5 JSON/reduction codec stays unchanged. This is a distinct v3 live
wire contract: fixed records, actual GO windows, and natural-completion footer.
Exact primary input/draw schedule and owner lifecycle remain separate evidence.
"""
import hashlib
import json
import struct

ROW = struct.Struct('>BBBBIIIQIIIIIIIHBBBBBBI')
WINDOW = struct.Struct('>IIQIIIIQIIQQ')
FOOTER_BYTES = 176
MAX_BYTES = 16 * 1024 * 1024
MAX_HEADER_BYTES = 4096
MAX_LIFETIME_EVENTS = 131072
MAX_EVENTS = (MAX_BYTES - 8 - MAX_HEADER_BYTES - FOOTER_BYTES) // ROW.size
DOL = 'dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646'
PCS = ['0x80380524','0x8038057c','0x803805b8']
SYMBOLS = [None,'unknown','grStadium_801D1390','efLib_particles_proc_main','Fighter_8006A360']
CALLBACKS = {0x801d1390:2,0x8005c9a4:3,0x8006a360:4}

class LiveLedgerError(ValueError):
    pass

def require(ok, message):
    if not ok:
        raise LiveLedgerError(message)

def mem1(p):
    return 0x80000000 <= p <= 0x817ffffc and p % 4 == 0

def _unique(pairs):
    result = {}
    for key,value in pairs:
        require(key not in result,'duplicate compact header key')
        result[key] = value
    return result

def decode_live_compact(blob, *, capture_id, sequence_id, dol_sha256, primary_status):
    require(isinstance(blob,bytes) and 8+FOOTER_BYTES <= len(blob) <= MAX_BYTES,
            'compact byte bound/truncation')
    magic,version,header_size = struct.unpack_from('>4sHH',blob)
    require(magic == b'MWRL' and version == 3 and 0 < header_size <= MAX_HEADER_BYTES,
            'compact live header/version')
    end_header = 8+header_size
    footer_offset = len(blob)-FOOTER_BYTES
    require(end_header <= footer_offset and (footer_offset-end_header)%ROW.size == 0,
            'compact row geometry')
    try:
        header = json.loads(blob[8:end_header],object_pairs_hook=_unique)
    except (ValueError,UnicodeError) as error:
        raise LiveLedgerError('compact header JSON: '+str(error)) from error
    require(isinstance(header,dict),'compact header is not an object')
    require(set(header) == {'schema','version','row_bytes','dol_sha256','capture_id',
        'sequence_id','matches','first_offset','last_offset','site_pcs',
        'source_schedule_required_separately','lifecycle_parity'},'compact header roles')
    require(header['schema'] == 'melee-web-live-compact-rng-ledger' and
        type(header['version']) is int and header['version'] == 3 and
        header['row_bytes'] == ROW.size == 64 and
        header['dol_sha256'] == dol_sha256 == DOL and
        header['capture_id'] == capture_id and header['sequence_id'] == sequence_id and
        isinstance(capture_id,str) and 0<len(capture_id)<=128 and
        isinstance(sequence_id,str) and 0<len(sequence_id)<=128 and
        header['matches'] in ([0],[1],[0,1]) and
        all(type(m) is int for m in header['matches']) and
        type(header['first_offset']) is int and header['first_offset'] == 1 and
        type(header['last_offset']) is int and header['last_offset'] == 3500 and
        header['site_pcs'] == PCS and header['source_schedule_required_separately'] is True and
        header['lifecycle_parity'] is False,'compact producer/window/identity differs')
    magic,count,window_count,complete = struct.unpack_from('>4sIII',blob,footer_offset)
    require(magic == b'END!' and 0<count<=MAX_EVENTS and
        count == (footer_offset-end_header)//ROW.size and window_count == len(header['matches']) and
        complete == 1 and hashlib.sha256(blob[:-32]).digest() == blob[-32:],
        'compact footer/count/hash/incomplete')
    require(isinstance(primary_status,dict) and primary_status.get('state') == 'completed' and
        primary_status.get('completed') is True and primary_status.get('invalid') is False and
        primary_status.get('error') is None,'primary stream not naturally completed')
    windows=[]
    for index in range(2):
        raw=WINDOW.unpack_from(blob,footer_offset+16+index*WINDOW.size)
        if index>=window_count:
            require(not any(raw),'unused compact window is nonzero')
            continue
        match,go,go_seq,target,last_source,n,close_tick,close_seq,seed_pointer,seed,containing_seq,target_seq = raw
        tail=last_source-target
        require(match == header['matches'][index] and go<=0xffffffff-3505 and target == go+3500 and
            0<=tail<=4 and close_tick == last_source+1 and 0<n<=MAX_LIFETIME_EVENTS and
            go_seq<=containing_seq<target_seq<close_seq<=9007199254740991 and mem1(seed_pointer) and
            type(primary_status.get('last_seq')) is int and close_seq<=primary_status['last_seq'],
            'compact GO/close window differs')
        if windows:
            require(windows[-1]['close_sequence']<go_seq,'compact window order differs')
        windows.append(dict(match=match,go_tick=go,go_sequence=go_seq,first_tick=go,last_tick=close_tick,
            target_source_tick=target,last_source_tick=last_source,natural_tail_ticks=tail,
            containing_source_sequence=containing_seq,target_source_sequence=target_seq,
            count=n,close_tick=close_tick,close_sequence=close_seq,seed_pointer=seed_pointer,seed=seed))
    require(sum(w['count'] for w in windows)==count,'compact window event totals differ')
    records=[]; cursor=0;unresolved=0
    for w in windows:
        previous=(w['first_tick'],w['go_sequence'],0)
        seeds={w['seed_pointer']:w['seed']}
        for _ in range(w['count']):
            values=ROW.unpack_from(blob,end_header+cursor*ROW.size)
            (site,status,symbol,slot,tick,match,ordinal,sequence,draw,lr,seed_pointer,seed,
             gobj,proc,callback,classifier,plink,slink,entity,r0,r1,r2,kind)=values
            require(site in (1,2,3) and status in (0,1) and symbol<len(SYMBOLS) and
                not(r0 or r1 or r2) and mem1(lr) and mem1(seed_pointer) and
                ordinal == cursor and match == w['match'] and w['first_tick']<=tick<=w['last_tick'] and
                previous[0]<=tick and previous[1]<=sequence<=w['close_sequence'] and
                previous[2]<=draw and sequence<=9007199254740991,'compact event encoding/order differs')
            if not status:
                require(not(symbol or gobj or proc or callback or classifier or plink or slink) and
                    slot==entity==255 and kind==0xffffffff,'compact outside owner differs')
            else:
                require(all(mem1(p) for p in (gobj,proc,callback)) and
                    symbol==CALLBACKS.get(callback,1),'compact callback/pointers differ')
                if symbol==4:
                    require((classifier,plink,slink)==(4,8,1) and slot<4 and entity<2,
                            'compact fighter owner differs')
                else:
                    require(slot==entity==255 and kind==0xffffffff,'compact nonfighter claims a port')
            require(seed_pointer == w['seed_pointer'],'compact seed owner differs from actual GO pointer')
            require(seed == (214013*seeds[seed_pointer]+2531011)&0xffffffff,
                    'compact actual seed discontinuity')
            seeds[seed_pointer]=seed
            unresolved+=symbol==1
            ptr=lambda value:f'0x{value:08x}'
            records.append(dict(pc=PCS[site-1],label=('rand_return','randf_return','randi_return')[site-1],
                expected_word='0x4e800020',primitive='HSD_Randf' if site==2 else 'HSD_Rand',
                source_tick=tick,match=match,rng_ordinal=ordinal,observer_next_sequence=sequence,
                coordinate_scope='partial_containing_tick_guard' if sequence<=w['containing_source_sequence'] else
                    'selected_post_go_ticks_and_interleaved_draws' if sequence<=w['target_source_sequence'] else
                    'natural_finish_draw_and_tail',
                draw_ordinal=draw,lr=ptr(lr),seed_owner_pointer=ptr(seed_pointer),seed_after=f'{seed:08x}',
                owner=dict(status='scheduled_proc' if status else 'outside_proc',callback_symbol=SYMBOLS[symbol],
                    gobj=ptr(gobj),proc=ptr(proc),callback=ptr(callback),classifier=classifier,p_link=plink,s_link=slink,
                    source_slot=None if slot==255 else slot,entity=None if entity==255 else entity,
                    fighter_kind=None if kind==0xffffffff else kind)))
            previous=(tick,sequence,draw);cursor+=1
    return dict(schema='melee-web-live-compact-rng-validation-v1',valid=True,
        output_sha256=hashlib.sha256(blob).hexdigest(),output_bytes=len(blob),record_count=count,
        header=header,windows=windows,records=records,unresolved_owner_events=unresolved,
        owner_comparison_ready=unresolved==0,source_schedule_verified=False,
        all_rng_writers_observed=False,lifecycle_parity=False,C3=False)
