"""Bounded passive source request timing; no drive model or timing acceptance."""
PROFILE = 'first_selected_stage_request_v1'
SCHEMA = 'melee-web-stadium-selected-request-source-timing-v1'
PCS = {'request_enter':0x8038f6d4,'request_return':0x8038f950,
       'callback_enter':0x801d4220,'callback_return':0x801d42b4,
       'poll_enter':0x801d42b8,'poll_pending_read':0x801d4318,'poll_return':0x801d4350}
FIELDS = {'schema','profile','kind','whole_session','match_index','clock_ticks',
          'selected_form','request_id','file_entry','map2_gobj','map2_ground','buffer','logical_size',
          'transfer_size','pending_flag','result','pc','sp','lr','source_tick',
          'draw_ordinal','drive_model_observed'}

def require(ok, message):
    if not ok: raise ValueError(message)

def validate_selected_request_timing(records, status):
    """Strict MWRO iterator input; retain at most8192 selected rows, no raw RAM."""
    rows=[];announcements=[];ended=False;last_seq=-1
    for row in records:
        require(type(row.get('seq')) is int and row['seq']>last_seq,'Timing stream sequence')
        last_seq=row['seq'];p=row.get('payload',{})
        require(row.get('event')!='error','Timing primary error')
        if row.get('event') in ('handshake','start'):
            announcements.append(row['event'])
            require(p.get('stadium_selected_request_timing')==PROFILE,'Timing announcement')
        if row.get('event')=='end': ended=True
        if p.get('schema')!=SCHEMA:continue
        require(row['event']=='progress' and announcements==['handshake','start'] and not ended,
                'Timing row lifetime/order')
        require(set(p)==FIELDS and p['profile']==PROFILE and p['whole_session'] is True and
                p['match_index']==1 and p['drive_model_observed'] is False,'Timing row schema')
        require(type(p['pending_flag']) is bool and p['kind'] in PCS and p['pc']==PCS[p['kind']],
                'Timing phase/PC')
        for name in FIELDS-{'schema','profile','kind','whole_session','pending_flag','drive_model_observed'}:
            require(type(p[name]) is int and 0<=p[name]<=((1<<64)-1 if name=='clock_ticks' else 0xffffffff),
                    'Timing integer '+name)
        require(row['source_tick']==p['source_tick'] and row['draw_ordinal']==p['draw_ordinal'],
                'Timing envelope coordinate')
        require(len(rows)<8192,'Timing fixed row cap')
        rows.append(dict(p,sequence=row['seq']))
    require(ended and announcements==['handshake','start'] and rows,'Timing incomplete primary')
    require(status.get('state')=='completed' and status.get('completed') is True and
            status.get('invalid') is False and status.get('error') is None and
            status.get('stadium_selected_request_timing')==PROFILE and
            status.get('stadium_selected_request_timing_complete') is True,'Timing final status')
    first=rows[0];require(first['kind']=='request_enter' and first['request_id']==0 and
                         first['pending_flag'] is True,'Timing request entry')
    require(first['logical_size']>0 and first['logical_size']<=0xffffffe0 and
            first['transfer_size']==(first['logical_size']+31)&~31,'Timing rounded extent')
    for name in ['map2_gobj','map2_ground','buffer']:
        require(0x80000000<=first[name]<=0x817ffffc and first[name]%4==0,'Timing owner address')
    require(first['selected_form'] in (3,4,6,9),'Timing selected nondefault form')
    identity=['selected_form','file_entry','map2_gobj','map2_ground','buffer','logical_size','transfer_size']
    returned=False;callback=None;callback_done=False;poll=None;pending_read=None;done=False;request_id=None;last_clock=-1
    for i,p in enumerate(rows):
        require(not done and all(p[n]==first[n] for n in identity),'Timing changed/trailing owner')
        require(p['clock_ticks']>=last_clock,'Timing emulated clock reversed');last_clock=p['clock_ticks']
        if i==0:continue
        kind=p['kind']
        if kind=='request_return':
            require(not returned and p['pending_flag'] and p['result']==p['request_id'] and
                    p['sp']==first['sp'] and p['lr']==first['lr'],'Timing request return')
            returned=True;request_id=p['request_id'];continue
        require(returned and p['request_id']==request_id,'Timing request ID/order')
        if kind=='callback_enter':
            require(callback is None and p['pending_flag'],'Timing callback entry');callback=p
        elif kind=='callback_return':
            require(callback is not None and not callback_done and not p['pending_flag'] and
                    p['sp']==callback['sp'] and p['lr']==callback['lr'],'Timing callback return');callback_done=True
        elif kind=='poll_enter':
            require(poll is None,'Timing poll reentrancy');poll=p;pending_read=None
        elif kind=='poll_pending_read':
            require(poll is not None and pending_read is None and poll['sp']>=0x10 and
                    p['sp']==poll['sp']-0x10 and p['lr']==0x801d42fc and
                    p['result']<=0xff,'Timing poll pending read')
            pending_read=bool(p['result']&0x40)
        elif kind=='poll_return':
            require(poll is not None and pending_read is not None and p['sp']==poll['sp'] and p['lr']==poll['lr'] and
                    p['result'] in (0,1) and bool(p['result'])== (not pending_read) and
                    (not p['result'] or callback_done),'Timing poll return')
            poll=None;done=bool(p['result'])
        else:raise ValueError('Timing repeated request entry')
    require(done and callback_done and poll is None,'Timing missing callback/successful poll')
    ret=next(p for p in rows if p['kind']=='request_return');cb=next(p for p in rows if p['kind']=='callback_enter')
    return {'schema':'melee-web-stadium-selected-request-source-timing-summary-v1','profile':PROFILE,
            'request_id':request_id,'identity':{n:first[n] for n in identity},'rows':rows,
            'request_call_ticks':ret['clock_ticks']-first['clock_ticks'],
            'request_to_callback_ticks':cb['clock_ticks']-first['clock_ticks'],
            'callback_to_successful_poll_ticks':rows[-1]['clock_ticks']-cb['clock_ticks'],
            'drive_model_observed':False,'dvd_submit_observed':False,
            'scheduled_final_deadline_observed':False,'C4_complete':False}
