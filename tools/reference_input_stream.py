"""Validate the bounded SI input stream; never convert CPU observations to input."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import struct
import zlib

HEADER = struct.Struct('<4sHHII')
RECORD = struct.Struct('<QQIH10BII')
MAGIC = b'MWRI'
MAX_BYTES = 64 * 1024 * 1024
VERSION = 1

class InputStreamError(ValueError):
    pass

def _regular(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise InputStreamError('Input recording is missing or is not an ordinary file')
    return path

def validate_stream(path):
    path = _regular(path)
    size = path.stat().st_size
    if not 56 <= size <= MAX_BYTES or (size-HEADER.size) % RECORD.size:
        raise InputStreamError('Input recording has an invalid size')
    count = (size-HEADER.size)//RECORD.size
    digest = hashlib.sha256()
    ports, operations = {}, {'pad_status': 0, 'adapter_connection': 0}
    previous = 0
    first = None
    with path.open('rb') as stream:
        header = stream.read(HEADER.size); digest.update(header)
        if HEADER.unpack(header) != (MAGIC, VERSION, HEADER.size, RECORD.size, 0):
            raise InputStreamError('Unsupported input recording header')
        for index in range(count):
            raw = stream.read(RECORD.size); digest.update(raw)
            if len(raw) != RECORD.size:
                raise InputStreamError('Input recording is truncated')
            seq, tick, channel, button, *fields, kind, crc = RECORD.unpack(raw)
            if seq != index or tick < previous or zlib.crc32(raw[:-4]) != crc:
                raise InputStreamError(f'Input sequence, clock or checksum is invalid at event {index}')
            previous = tick
            if index == count-1:
                if kind != 2 or channel != 0xffffffff:
                    raise InputStreamError('Input recording lacks a successful completion footer')
            else:
                if kind != 1 or channel not in (*range(4), *range(256,260)) or fields[-1] not in (0,1):
                    raise InputStreamError(f'Invalid controller input event {index}')
                if first is None: first = tick
                port = str(channel & 255)
                ports[port] = ports.get(port,0)+1
                operations['adapter_connection' if channel >=256 else 'pad_status'] += 1
        if stream.read(1): raise InputStreamError('Input recording grew during validation')
    if count < 2: raise InputStreamError('Input recording has no controller samples')
    return {'version':1,'sha256':digest.hexdigest(),'bytes':size,'events':count-1,
            'first_emulated_tick':first,'last_emulated_tick':previous,
            'ports':ports,'operations':operations,'complete':True}

def validate_status(path, *, mode=None, events=None, require_complete=True):
    path = _regular(path)
    if path.stat().st_size > 16384: raise InputStreamError('Input status exceeds its bound')
    try: value = json.loads(path.read_text())
    except (UnicodeDecodeError, ValueError) as error: raise InputStreamError('Input status is malformed') from error
    if (not isinstance(value,dict) or set(value) != {'version','mode','events','complete','invalid','error'} or
            type(value['version']) is not int or value['version'] != 1 or
            value['mode'] not in ('record','replay') or
            type(value['events']) is not int or not 0 <= value['events'] <= MAX_BYTES//RECORD.size or
            type(value['complete']) is not bool or type(value['invalid']) is not bool or
            value['error'] is not None and not isinstance(value['error'],str)):
        raise InputStreamError('Input status has an invalid schema')
    if mode is not None and value['mode'] != mode: raise InputStreamError('Input recording mode disagrees with the request')
    if value['invalid'] or value['error']: raise InputStreamError('Dolphin input stream failed: '+str(value['error']))
    if require_complete and not value['complete']: raise InputStreamError('Dolphin input stream did not complete')
    if events is not None and value['events'] != events: raise InputStreamError('Input event count disagrees with the recorded stream')
    return value
