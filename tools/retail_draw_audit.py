"""Validate read-only camera-traversal observations against their source ticks.

This measures changes to declared state during drawing, not pixel equivalence.
It accepts legacy one-traversal-per-tick rows and explicit source-index rows
that preserve sparse retail camera cadence. It cannot prove that unobserved
rendering state is irrelevant to future ticks.
"""
import hashlib
from pathlib import Path

from retail_replay_validation import (
    CaptureError, MULTIPLAYER_VERSION, STATE_KEYS, _require_keys, _int, _u32, _pad_state,
    _validate_fighter, _first_difference, read_jsonl,
)


def validate_draw_rows(capture, rows):
    if capture.header['version'] not in (2, MULTIPLAYER_VERSION):
        raise CaptureError('draw audit requires a v2 or v3 reference capture')
    active_player_count = getattr(capture, 'active_player_count', 2)
    if not isinstance(rows, list) or not rows:
        raise CaptureError('draw audit requires at least one traversal')

    # Version-one audit rows predate sparse retail drawing and use their
    # ordinal ``index`` as the source tick.  The explicit form adds a separate
    # source index so a traversal can be bound to the last completed scheduler
    # tick even when the source performs several ticks between camera calls.
    explicit = 'source_index' in rows[0] if isinstance(rows[0], dict) else False
    for row_index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise CaptureError(f'draw[{row_index}]: expected object')
        if ('source_index' in row) != explicit:
            raise CaptureError('draw audit cannot mix legacy and explicit row schemas')
    if not explicit and len(rows) != len(capture.frames):
        raise CaptureError('draw audit requires exactly one traversal per captured tick')

    changes = []
    source_indices = []
    previous_source_index = -1
    for index, row in enumerate(rows):
        context = f'draw[{index}]'
        expected_keys = {'record', 'index', 'before', 'after'}
        if explicit:
            expected_keys.add('source_index')
        _require_keys(row, expected_keys, context)
        if row['record'] != 'draw' or _int(row['index'], context + '.index') != index:
            raise CaptureError(context + ': noncontiguous traversal')

        source_index = index
        if explicit:
            source_index = _int(row['source_index'], context + '.source_index',
                                minimum=0, maximum=len(capture.frames) - 1)
            if source_index <= previous_source_index:
                raise CaptureError(context + ': source_index must be strictly increasing')
        previous_source_index = source_index
        source_indices.append(source_index)
        frame = capture.frames[source_index]
        expected_scene_frame = (frame['scene_frame'] + 1) & 0xFFFFFFFF
        for phase in ('before', 'after'):
            sample = row[phase]
            if not isinstance(sample, dict):
                raise CaptureError(context + '.' + phase + ': expected object')
            _require_keys(sample, STATE_KEYS | {'pad_state_hex'}, context + '.' + phase)
            for key in ('rng', 'scene_frame', 'match_frame'):
                _u32(sample[key], context + '.' + phase + '.' + key)
            _pad_state(sample['pad_state_hex'], context + '.' + phase + '.pad_state_hex')
            if (not isinstance(sample['fighters'], list) or
                    len(sample['fighters']) != active_player_count):
                raise CaptureError(context +
                                   f': expected {active_player_count} fighters')
            for slot, fighter in enumerate(sample['fighters']):
                _validate_fighter(fighter, slot, context + '.' + phase + '.fighter')
            if sample['scene_frame'] != expected_scene_frame:
                raise CaptureError(context + ': unexpected source scene counter')
        expected = {key: frame[key] for key in STATE_KEYS | {'pad_state_hex'}}
        # The source increments its scene counter between scheduler return and
        # drawing. All other declared fields must still identify that same tick.
        expected['scene_frame'] = expected_scene_frame
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
                            'source_index': source_index,
                            'before': expected_value, 'after': actual_value})

    if explicit and source_indices[-1] != len(capture.frames) - 1:
        raise CaptureError('draw audit must include a traversal for the final source tick')
    observed_source_indices = set(source_indices)
    source_ticks_not_drawn = [index for index in range(len(capture.frames))
                              if index not in observed_source_indices]
    return {
        'status': 'declared_state_changed' if changes else 'declared_state_unchanged',
        'draws_compared': len(rows), 'draws_with_changes': len(changes),
        'traversals_observed': len(rows),
        'source_ticks_total': len(capture.frames),
        'source_ticks_compared': len(source_indices),
        'source_ticks_not_drawn': source_ticks_not_drawn,
        'source_ticks_not_drawn_count': len(source_ticks_not_drawn),
        'source_indices': source_indices,
        'source_index_cadence': source_indices,
        'first_change': changes[0] if changes else None,
        'scope': 'observed HSD_GObj_80390FC0 camera traversals bound to completed source '
                 'scheduler ticks; declared fighter fields, PAD configuration/history, '
                 'RNG and clocks only; source-tick gaps are reported, with no pixel or '
                 'cadence equivalence or performance claim; source-index cadence is '
                 'evidence only',
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
