"""Strict, scoped port diagnostics against an independently repeated reference.

A match is not gold admission: drawing and hardware/audio output are excluded.
Version 2 includes complete semantic PAD configuration/history; version 1 does
not. This tool cannot waive the remaining boundaries.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
from retail_replay_validation import (
    CaptureError, _require_keys, _int, _u32, _hex, _validate_fighter,
    _validate_inputs, _first_difference, _reject_duplicate_keys, _DuplicateKey,
    MAX_FRAMES, PAD_STATE_BYTES, _pad_state,
)
from retail_replay_recipe import _pair, RecipeError

STATE = {'rng', 'match_frame', 'fighters'}
HEADER = {'record', 'schema', 'version', 'frames_requested', 'phase', 'rendering', 'comparison'}


def _state(row, context, version=1):
    if version == 2: _pad_state(row["pad_state_hex"], context+".pad_state_hex")
    _u32(row['rng'], context+'.rng')
    _u32(row['match_frame'], context+'.match_frame')
    if not isinstance(row['fighters'], list) or len(row['fighters']) != 2:
        raise CaptureError(context+': exactly two fighters required')
    for slot, fighter in enumerate(row['fighters']):
        _validate_fighter(fighter, slot, context+f'.fighters[{slot}]')


def validate_port(rows):
    if not isinstance(rows, list) or not rows or any(not isinstance(x, dict) for x in rows):
        raise CaptureError('port: expected JSON object records')
    head=rows[0]
    _require_keys(head, HEADER, 'port.header')
    version=_int(head["version"], "port.version", minimum=1, maximum=2)
    state_keys=STATE|({"pad_state_hex"} if version == 2 else set())
    fixed={'record':'header', 'schema':'melee-web-port-replay-candidate',
           'phase':'after_source_tick_before_audio_transport', 'rendering':'excluded', 'comparison':'not_run'}
    for key, value in fixed.items():
        if type(head[key]) is not type(value) or head[key] != value:
            raise CaptureError('port.header: unsupported '+key)
    count=_int(head['frames_requested'], 'port.frames_requested', minimum=1, maximum=MAX_FRAMES)
    if len(rows)!=count+4:
        raise CaptureError('port: incomplete or extra records; completion requires teardown')
    _require_keys(rows[1], {'record','rng','start_melee_hex'}|({'pad_state_hex'} if version == 2 else set()), 'port.match_enter')
    if rows[1]['record']!='match_enter': raise CaptureError('port: missing match entry')
    _u32(rows[1]['rng'], 'port.entry.rng')
    _hex(rows[1]['start_melee_hex'], 0x138, 'port.entry.setup')
    if version == 2: _pad_state(rows[1]['pad_state_hex'], 'port.entry.pad_state_hex')
    _require_keys(rows[2], state_keys|{'record'}, 'port.initial')
    if rows[2]['record']!='match_enter_complete': raise CaptureError('port: missing initial state')
    _state(rows[2], 'port.initial', version)
    previous=0
    for index, row in enumerate(rows[3:-1]):
        _require_keys(row, state_keys|{'record','index','supplied_inputs'}, f'port.frame[{index}]')
        if row['record']!='frame' or _int(row['index'], 'port.index')!=index:
            raise CaptureError('port: noncontiguous frame sequence')
        _validate_inputs([row['supplied_inputs']], 'port.supplied_inputs')
        _state(row, f'port.frame[{index}]', version)
        if row['match_frame']<previous: raise CaptureError('port: match clock moved backwards')
        previous=row['match_frame']
    _require_keys(rows[-1], {'record','status','frames'}, 'port.end')
    if (rows[-1]['record']!='end' or rows[-1]['status']!='captured'
            or _int(rows[-1]['frames'], 'port.end.frames')!=count):
        raise CaptureError('port: missing successful teardown/end record')
    return rows


def compare_rows(reference, rows):
    validate_port(rows)
    if reference.header["version"] != rows[0]["version"]:
        raise CaptureError("port and reference schema versions must agree")
    version=rows[0]["version"]
    groups=("rng","match_frame","fighters")+(("pad_state_hex",) if version == 2 else ())
    report={'status':'declared_state_match', 'gold_admitted':False, 'performance':'not_evaluated',
            'scope':'declared entry data, supplied inputs, fighter fields, RNG and match clock'+
                    (', PAD configuration and all three history banks' if version == 2 else '')+' only; '+
                    ('rendering, hardware and audio output are excluded' if version == 2 else
                     'rendering, global PAD history, hardware and audio output are excluded'),
            'frames_compared':0, 'first_divergence':None, 'checks':{}}
    first={}
    def check(group, expected, actual, record, frame=None):
        if group == 'pad_state_hex':
            expected={'pad_state':_pad_state(expected['pad_state_hex'], 'reference')}
            actual={'pad_state':_pad_state(actual['pad_state_hex'], 'port')}
        difference=_first_difference(expected, actual)
        if difference and group not in first:
            field, before, after=difference
            item={'record':record, 'frame':frame, 'field':field, 'expected':before, 'actual':after}
            first[group]=item
            if report['first_divergence'] is None: report['first_divergence']=item
    check('frame_count', len(reference.frames), len(rows)-4, 'header')
    check('entry', {k:reference.match_enter[k] for k in ('rng','start_melee_hex')+(("pad_state_hex",) if version == 2 else ())},
          {k:rows[1][k] for k in ('rng','start_melee_hex')+(("pad_state_hex",) if version == 2 else ())}, 'match_enter')
    for group in groups:
        check(group, {group:reference.initial[group]}, {group:rows[2][group]}, 'match_enter_complete')
    for index,(expected,actual) in enumerate(zip(reference.frames, rows[3:-1])):
        check('inputs', expected['consumed_inputs'][0], actual['supplied_inputs'], 'frame', index)
        for group in groups:
            check(group, {group:expected[group]}, {group:actual[group]}, 'frame', index)
        report['frames_compared']+=1
    for group in ('frame_count','entry','inputs')+groups:
        report['checks'][group]='diverged' if group in first else 'pass'
    report['first_divergence_by_group']=first
    if first: report['status']='diverged'
    return report


def compare_paths(first, second, port):
    report={'status':'invalid_capture','gold_admitted':False,'performance':'not_evaluated'}
    try:
        _,_,a_hash,b_hash,reference,_,_= _pair(first,second)
        path=Path(port)
        if path.stat().st_size>128*1024*1024: raise CaptureError('port capture exceeds byte limit')
        raw=path.read_bytes()
        if len(raw)>128*1024*1024: raise CaptureError('port capture exceeds byte limit')
        rows=[json.loads(line, object_pairs_hook=_reject_duplicate_keys)
              for line in raw.decode('utf-8').splitlines()]
        report=compare_rows(reference,rows)
        report['capture_hashes']={'reference_a':a_hash,'reference_b':b_hash,
                                  'port':hashlib.sha256(raw).hexdigest()}
        report['reference_repeatability']='pass'
    except (CaptureError,RecipeError,_DuplicateKey,OSError,UnicodeError,json.JSONDecodeError) as error:
        report['error']=str(error)
    return report
