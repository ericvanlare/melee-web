"""Validate read-only camera-traversal observations against their source ticks.

This measures changes to declared state during drawing, not pixel equivalence.
It cannot prove that unobserved rendering state is irrelevant to future ticks.
"""
import hashlib
from pathlib import Path

from retail_replay_validation import (
    CaptureError, STATE_KEYS, _require_keys, _int, _u32, _pad_state,
    _validate_fighter, _first_difference, read_jsonl,
)


def validate_draw_rows(capture, rows):
    if capture.header['version'] != 2:
        raise CaptureError('draw audit requires a v2 reference capture')
    if not isinstance(rows, list) or len(rows) != len(capture.frames):
        raise CaptureError('draw audit requires exactly one traversal per captured tick')
    changes = []
    for index, (frame, row) in enumerate(zip(capture.frames, rows)):
        context = f'draw[{index}]'
        if not isinstance(row, dict):
            raise CaptureError(context + ': expected object')
        _require_keys(row, {'record', 'index', 'before', 'after'}, context)
        if row['record'] != 'draw' or _int(row['index'], context + '.index') != index:
            raise CaptureError(context + ': noncontiguous traversal')
        for phase in ('before', 'after'):
            sample = row[phase]
            if not isinstance(sample, dict):
                raise CaptureError(context + '.' + phase + ': expected object')
            _require_keys(sample, STATE_KEYS | {'pad_state_hex'}, context + '.' + phase)
            for key in ('rng', 'scene_frame', 'match_frame'):
                _u32(sample[key], context + '.' + phase + '.' + key)
            _pad_state(sample['pad_state_hex'], context + '.' + phase + '.pad_state_hex')
            if not isinstance(sample['fighters'], list) or len(sample['fighters']) != 2:
                raise CaptureError(context + ': expected two fighters')
            for slot, fighter in enumerate(sample['fighters']):
                _validate_fighter(fighter, slot, context + '.' + phase + '.fighter')
            if sample['scene_frame'] != index + 1:
                raise CaptureError(context + ': unexpected source scene counter')
        expected = {key: frame[key] for key in STATE_KEYS | {'pad_state_hex'}}
        # The source increments its scene counter between scheduler return and
        # drawing. All other declared fields must still identify that same tick.
        expected['scene_frame'] += 1
        difference = _first_difference(expected, row['before'])
        if difference:
            raise CaptureError(context + ': draw entry differs from scheduler: ' + difference[0])
        before, after = dict(row['before']), dict(row['after'])
        for sample in (before, after):
            sample['pad_state'] = _pad_state(sample.pop('pad_state_hex'), context)
        difference = _first_difference(before, after)
        if difference:
            field, expected_value, actual_value = difference
            changes.append({'index': index, 'field': field,
                            'before': expected_value, 'after': actual_value})
    return {
        'status': 'declared_state_changed' if changes else 'declared_state_unchanged',
        'draws_compared': len(rows), 'draws_with_changes': len(changes),
        'first_change': changes[0] if changes else None,
        'scope': 'one HSD_GObj_80390FC0 traversal per source tick; declared fighter fields, '
                 'PAD configuration/history, RNG and clocks only',
        'gold_admitted': False, 'pixels': 'not_compared', 'performance': 'not_evaluated',
    }


def load_draw_audit(capture, path):
    path = Path(path)
    if path.stat().st_size > 512 * 1024 * 1024:
        raise CaptureError('draw audit exceeds byte limit')
    result = validate_draw_rows(capture, read_jsonl(path))
    result['capture_sha256'] = capture.sha256
    result['audit_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result
