"""Semantic PAD snapshot v1 within retail capture/recipe v2. No C padding."""
import math
import struct

CONFIG = [('i', 'repeat_start'), ('i', 'repeat_interval'), ('b', 'adc_type'),
          ('b', 'adc_th'), ('f', 'adc_angle'), ('B', 'clamp_stickType'),
          ('B', 'clamp_stickShift'), ('b', 'clamp_stickMax'), ('b', 'clamp_stickMin'),
          ('B', 'clamp_analogLRShift'), ('B', 'clamp_analogLRMax'), ('B', 'clamp_analogLRMin'),
          ('B', 'clamp_analogABShift'), ('B', 'clamp_analogABMax'), ('B', 'clamp_analogABMin'),
          ('b', 'scale_stick'), ('B', 'scale_analogLR'), ('B', 'scale_analogAB'),
          ('B', 'cross_dir'), ('B', 'reset_switch_status'), ('B', 'reset_switch')]
HISTORY = [('I', name) for name in ('button', 'last_button', 'trigger', 'repeat', 'release')]
HISTORY += [('i', 'repeat_count')]
HISTORY += [('b', name) for name in ('stickX', 'stickY', 'subStickX', 'subStickY')]
HISTORY += [('B', name) for name in ('analogL', 'analogR', 'analogA', 'analogB')]
HISTORY += [('f', name) for name in ('nml_stickX', 'nml_stickY', 'nml_subStickX',
             'nml_subStickY', 'nml_analogL', 'nml_analogR', 'nml_analogA', 'nml_analogB')]
HISTORY += [('B', 'cross_dir'), ('b', 'err')]
PAD_STATE_BYTES = 30 + 3 * 4 * 66


def decode_pad_state(value):
    if not isinstance(value, str) or len(value) != PAD_STATE_BYTES * 2:
        raise ValueError('PAD snapshot requires exactly 822 semantic bytes')
    raw = bytes.fromhex(value)
    if len(raw) != PAD_STATE_BYTES: raise ValueError('Invalid PAD snapshot hex')
    cursor = 0
    def record(fields):
        nonlocal cursor
        result = {}
        for kind, name in fields:
            size = struct.calcsize('>' + kind)
            item = struct.unpack_from('>' + kind, raw, cursor)[0]
            if kind == 'f':
                if not math.isfinite(item): raise ValueError('Nonfinite PAD '+name)
                item = raw[cursor:cursor + size].hex()
            result[name + ('_bits' if kind == 'f' else '')] = item
            cursor += size
        return result
    config = record(CONFIG)
    c = config
    if not (c['repeat_start'] > 0 and c['repeat_interval'] > 0 and 0 <= c['adc_type'] <= 3
            and c['adc_th'] >= 0 and c['clamp_stickType'] <= 1
            and c['clamp_stickShift'] <= 1 and 0 <= c['clamp_stickMin'] < c['clamp_stickMax']
            and c['clamp_analogLRShift'] <= 1 and c['clamp_analogLRMin'] < c['clamp_analogLRMax']
            and c['clamp_analogABShift'] <= 1 and c['clamp_analogABMin'] < c['clamp_analogABMax']
            and c['scale_stick'] > 0 and c['scale_analogLR'] and c['scale_analogAB']
            and c['cross_dir'] <= 3 and c['reset_switch_status'] <= 1 and c['reset_switch'] <= 1):
        raise ValueError('Invalid PAD processing configuration')
    return {'configuration': config, **{bank: [record(HISTORY) for _ in range(4)]
                                       for bank in ('master', 'copy', 'game')}}
