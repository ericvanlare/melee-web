"""Strict semantic CPU/camera/HUD sidecars for the existing match replay.

These observations never become replay inputs. A headless comparison explicitly
omits draw phases; a source-drawn comparison requires the same traversal cadence
and compares every recorded field. Neither result establishes pixel or timing
agreement. The associated core replay and original completion gates still apply.
"""
from dataclasses import dataclass
import hashlib
from pathlib import Path
import re
import struct

from retail_cpu_observation import SCHEMA, VERSION
from retail_replay_validation import CaptureError, _first_difference, read_jsonl


def require(value, message):
    if not value:
        raise CaptureError(message)


def keys(value, expected, context):
    require(isinstance(value, dict) and set(value) == set(expected),
            context + ': missing or unexpected fields')


def integer(value, low, high, context):
    require(type(value) is int and low <= value <= high, context + ': invalid integer')


def bits(value, context):
    require(type(value) is str and re.fullmatch('[0-9a-f]{8}', value) is not None,
            context + ': invalid float bits')
    require((int(value, 16) & 0x7f800000) != 0x7f800000, context + ': nonfinite float')


def vector(value, context):
    require(isinstance(value, list) and len(value) == 3, context + ': invalid vector')
    for item in value:
        bits(item, context)


def cpu(value, slot, count, context):
    keys(value, ('kind', 'level', 'state', 'default_state', 'secondary_state',
                 'target_slot', 'buttons', 'sticks', 'triggers', 'command_duration',
                 'command_cursor', 'command_bytes', 'defend_queue', 'attack_queue'), context)
    for name in ('kind', 'state', 'default_state', 'secondary_state'):
        integer(value[name], -(1 << 31), (1 << 31) - 1, context + '.' + name)
    integer(value['level'], 0, 9, context + '.level')
    integer(value['target_slot'], -1, count - 1, context + '.target_slot')
    integer(value['buttons'], 0, 0xffffffff, context + '.buttons')
    for name, length, low, high in (('sticks', 4, -128, 127), ('triggers', 2, 0, 255)):
        require(isinstance(value[name], list) and len(value[name]) == length, context + '.' + name)
        for item in value[name]:
            integer(item, low, high, context + '.' + name)
    integer(value['command_duration'], 0, 0xffffffff, context + '.command_duration')
    integer(value['command_cursor'], -1, 255, context + '.command_cursor')
    raw = value['command_bytes']
    require(type(raw) is str and len(raw) <= 512 and len(raw) % 2 == 0 and
            re.fullmatch('[0-9a-f]*', raw) is not None, context + ': invalid command bytes')
    for name in ('defend_queue', 'attack_queue'):
        require(isinstance(value[name], list) and len(value[name]) <= 8, context + '.' + name)
        for item in value[name]:
            integer(item, -(1 << 31), (1 << 31) - 1, context + '.' + name)


def snapshot(value, types, context):
    keys(value, ('match', 'camera', 'players'), context)
    match = value['match']
    keys(match, ('frame', 'seconds', 'subframe', 'outcome', 'end_state'), context + '.match')
    for name in ('frame', 'seconds'):
        integer(match[name], 0, 0xffffffff, context + '.match.' + name)
    integer(match['subframe'], 0, 0xffff, context + '.match.subframe')
    for name in ('outcome', 'end_state'):
        integer(match[name], 0, 255, context + '.match.' + name)
    camera = value['camera']
    keys(camera, ('position_bits', 'interest_bits', 'projection', 'fov_bits', 'near_bits', 'far_bits'), context + '.camera')
    vector(camera['position_bits'], context + '.camera.position')
    vector(camera['interest_bits'], context + '.camera.interest')
    integer(camera['projection'], 1, 3, context + '.camera.projection')
    for name in ('fov_bits', 'near_bits', 'far_bits'):
        bits(camera[name], context + '.camera.' + name)
    players = value['players']
    require(isinstance(players, list) and len(players) == len(types), context + ': player count changed')
    for slot, (kind, player) in enumerate(zip(types, players)):
        where = context + f'.players[{slot}]'
        keys(player, ('slot', 'cpu', 'subject', 'magnifier', 'hud'), where)
        integer(player['slot'], slot, slot, where + '.slot')
        if kind == 1:
            cpu(player['cpu'], slot, len(types), where + '.cpu')
        else:
            require(player['cpu'] is None, where + ': human has CPU observations')
        subject = player['subject']
        if subject is not None:
            keys(subject, ('state', 'timer', 'on_ledge', 'force_inactive', 'was_framed',
                           'position_bits', 'bone_bits'), where + '.subject')
            integer(subject['state'], -(1 << 31), (1 << 31) - 1, where + '.subject.state')
            integer(subject['timer'], -32768, 32767, where + '.subject.timer')
            for name in ('on_ledge', 'force_inactive', 'was_framed'):
                integer(subject[name], 0, 1, where + '.subject.' + name)
            for name in ('position_bits', 'bone_bits'):
                vector(subject[name], where + '.subject.' + name)
        magnifier = player['magnifier']
        keys(magnifier, ('offscreen', 'ignore_offscreen', 'edge'), where + '.magnifier')
        for name in ('offscreen', 'ignore_offscreen'):
            integer(magnifier[name], 0, 1, where + '.magnifier.' + name)
        integer(magnifier['edge'], 0, 63, where + '.magnifier.edge')
        hud = player['hud']
        keys(hud, ('present', 'damage', 'old_damage', 'last_attack_damage', 'shake_frames',
                   'explode', 'randomize_velocity', 'force_shake', 'hide_digits', 'animation_status'), where + '.hud')
        require(type(hud['present']) is bool, where + '.hud.present')
        for name in ('damage', 'old_damage'):
            integer(hud[name], -32768, 32767, where + '.hud.' + name)
        for name in ('last_attack_damage', 'shake_frames'):
            integer(hud[name], 0, 255, where + '.hud.' + name)
        for name in ('explode', 'randomize_velocity', 'force_shake', 'hide_digits'):
            integer(hud[name], 0, 1, where + '.hud.' + name)
        integer(hud['animation_status'], 0, 3, where + '.hud.animation_status')


@dataclass(frozen=True)
class Observation:
    path: Path
    sha256: str
    header: dict
    initial: dict
    frames: tuple
    draws: tuple
    end: dict


def load_observation(path, capture=None, *, require_teardown=True):
    path = Path(path)
    require(path.stat().st_size <= 512 * 1024 * 1024, 'CPU observation exceeds byte bound')
    rows = read_jsonl(path)
    require(len(rows) >= 4, 'CPU observation is incomplete')
    header = rows[0]
    keys(header, ('record', 'schema', 'version', 'frames_requested', 'source_drawing', 'setup_hex'), 'CPU header')
    require(header['record'] == 'header' and header['schema'] == SCHEMA and
            type(header['version']) is int and header['version'] == VERSION, 'CPU header identity')
    integer(header['frames_requested'], 1, 36000, 'CPU frame count')
    require(type(header['source_drawing']) is bool, 'CPU source drawing declaration')
    raw = header['setup_hex']
    require(type(raw) is str and re.fullmatch('[0-9a-f]{624}', raw) is not None, 'CPU complete setup')
    setup = bytes.fromhex(raw)
    types = [setup[0x61 + slot * 0x24] for slot in range(6)]
    active = [slot for slot, kind in enumerate(types) if kind in (0, 1)]
    require(2 <= len(active) <= 4 and active == list(range(len(active))), 'CPU active player topology')
    require(all(kind == 3 for kind in types[len(active):]), 'CPU inactive player tail must be NA')
    types = types[:len(active)]
    initial = rows[1]
    keys(initial, ('record', 'match', 'camera', 'players'), 'CPU initial')
    require(initial['record'] == 'initial', 'CPU initial phase')
    snapshot({k: v for k, v in initial.items() if k != 'record'}, types, 'CPU initial')
    frames, draws = [], []
    previous_source_draw = -1
    for ordinal, row in enumerate(rows[2:-1]):
        context = f'CPU row[{ordinal + 2}]'
        require(isinstance(row, dict), context + ': not an object')
        record = row.get('record')
        require(record in ('frame', 'draw'), context + ': unexpected phase')
        extra = ('source_index',) if record == 'draw' else ()
        keys(row, ('record', 'index', 'match', 'camera', 'players') + extra, context)
        bucket = frames if record == 'frame' else draws
        integer(row['index'], len(bucket), len(bucket), context + '.index')
        if record == 'draw':
            require(header['source_drawing'], 'Draw in headless CPU observation')
            integer(row['source_index'], previous_source_draw + 1, len(frames) - 1, context + '.source_index')
            require(row['source_index'] == len(frames) - 1, context + ': drawing an old source tick')
            previous_source_draw = row['source_index']
        snapshot({k: row[k] for k in ('match', 'camera', 'players')}, types, context)
        bucket.append(row)
    end = rows[-1]
    keys(end, ('record', 'frames', 'draws', 'remaining_fighter_slots', 'result', 'status'), 'CPU end')
    require(end['record'] == 'end' and end['status'] == 'captured', 'CPU successful end required')
    integer(end['frames'], len(frames), len(frames), 'CPU end.frames')
    integer(end['draws'], len(draws), len(draws), 'CPU end.draws')
    require(len(frames) == header['frames_requested'], 'CPU timeline is incomplete')
    terminal = end['result']
    if terminal is not None:
        keys(terminal, ('outcome', 'winners'), 'CPU result')
        integer(terminal['outcome'], 1, 2, 'CPU result.outcome')
        winners = terminal['winners']
        require(isinstance(winners, list) and 1 <= len(winners) <= len(active),
                'CPU result winner count invalid')
        for slot in winners:
            integer(slot, 0, len(active) - 1, 'CPU result winner')
        require(len(set(winners)) == len(winners), 'CPU result has duplicate winners')
    remaining = end['remaining_fighter_slots']
    require(isinstance(remaining, list) and remaining == sorted(set(remaining)), 'CPU teardown slots malformed')
    for slot in remaining:
        integer(slot, 0, 3, 'CPU teardown slot')
    if require_teardown:
        require(not remaining, 'CPU fighter teardown did not complete')
    if header['source_drawing']:
        require(draws and draws[-1]['source_index'] == len(frames) - 1, 'CPU final source draw missing')
    if capture is not None:
        require(header['setup_hex'] == capture.match_enter['start_melee_hex'], 'CPU setup differs from core capture')
        require(len(frames) == len(capture.frames), 'CPU frame count differs from core capture')
        require(initial['match']['frame'] == capture.initial['match_frame'], 'CPU initial clock differs from core capture')
        for index, row in enumerate(frames):
            require(row['match']['frame'] == capture.frames[index]['match_frame'], f'CPU clock differs at tick {index}')
    return Observation(path, hashlib.sha256(path.read_bytes()).hexdigest(), header, initial,
                       tuple(frames), tuple(draws), end)


def _draw_alignment(expected, actual):
    """Align draw rows by source tick without comparing different ticks.

    ``index`` is the ordinal among recorded draws and therefore changes when a
    source draw is missing.  ``source_index`` is the source simulation tick and
    is the only key that can safely pair the snapshots.
    """
    expected_by_source = {row['source_index']: (ordinal, row)
                          for ordinal, row in enumerate(expected)}
    actual_by_source = {row['source_index']: (ordinal, row)
                        for ordinal, row in enumerate(actual)}
    matched = []
    missing = []
    extra = []
    for source_index in sorted(expected_by_source.keys() | actual_by_source.keys()):
        left = expected_by_source.get(source_index)
        right = actual_by_source.get(source_index)
        if left is None:
            extra.append({'ordinal': right[0], 'source_index': source_index})
        elif right is None:
            missing.append({'ordinal': left[0], 'source_index': source_index})
        else:
            matched.append((source_index, left, right))
    return matched, missing, extra


def _draw_alignment_report(expected, actual, *, scope='source_draws_by_source_index'):
    matched, missing, extra = _draw_alignment(expected, actual)
    return {
        'scope': scope,
        'status': 'aligned' if not missing and not extra else 'misaligned',
        'expected_count': len(expected),
        'actual_count': len(actual),
        'matched_count': len(matched),
        'missing_from_actual_count': len(missing),
        'missing_from_actual': missing,
        'extra_in_actual_count': len(extra),
        'extra_in_actual': extra,
    }


def _draw_alignment_difference(source_index, expected, actual):
    expected_identity = None if expected is None else {
        'ordinal': expected[0], 'source_index': source_index,
    }
    actual_identity = None if actual is None else {
        'ordinal': actual[0], 'source_index': source_index,
    }
    kind = 'missing_from_actual' if expected is not None else 'extra_in_actual'
    return {
        'phase': 'draw',
        'tick': source_index,
        'field': 'draw_alignment.' + kind,
        'expected': expected_identity,
        'actual': actual_identity,
        'scope': 'source_draws_by_source_index',
    }


def _compare(a, b, *, drawing):
    for name in ('setup_hex', 'frames_requested'):
        difference = _first_difference(a.header[name], b.header[name], 'header.' + name)
        if difference:
            return {'phase': 'header', 'tick': None, 'field': difference[0],
                    'expected': difference[1], 'actual': difference[2]}
    difference = _first_difference(a.initial, b.initial)
    if difference:
        return {'phase': 'initial', 'tick': None, 'field': difference[0],
                'expected': difference[1], 'actual': difference[2]}

    # Source simulation and drawing are interleaved.  Frames are keyed by their
    # source index (the frame index), while draw rows are keyed by the explicit
    # source_index.  At each tick compare the frame first, then the draw.  This
    # preserves the first causal divergence when a later count is also wrong.
    expected_draws = {row['source_index']: (ordinal, row)
                      for ordinal, row in enumerate(a.draws)} if drawing else {}
    actual_draws = {row['source_index']: (ordinal, row)
                    for ordinal, row in enumerate(b.draws)} if drawing else {}
    max_tick = max(len(a.frames), len(b.frames),
                   (max(expected_draws, default=-1) + 1),
                   (max(actual_draws, default=-1) + 1))
    for tick in range(max_tick):
        expected_frame = a.frames[tick] if tick < len(a.frames) else None
        actual_frame = b.frames[tick] if tick < len(b.frames) else None
        if expected_frame is None or actual_frame is None:
            return {
                'phase': 'frame', 'tick': tick, 'field': 'frame_alignment',
                'expected': 'present' if expected_frame is not None else None,
                'actual': 'present' if actual_frame is not None else None,
                'scope': 'source_frames_by_index',
            }
        difference = _first_difference(expected_frame, actual_frame)
        if difference:
            return {'phase': 'frame', 'tick': tick, 'field': difference[0],
                    'expected': difference[1], 'actual': difference[2]}

        if drawing and (tick in expected_draws or tick in actual_draws):
            expected_draw = expected_draws.get(tick)
            actual_draw = actual_draws.get(tick)
            if expected_draw is None or actual_draw is None:
                return _draw_alignment_difference(tick, expected_draw, actual_draw)
            difference = _first_difference(expected_draw[1], actual_draw[1])
            if difference:
                return {'phase': 'draw', 'tick': tick, 'field': difference[0],
                        'expected': difference[1], 'actual': difference[2]}
    # Source result publication follows the last draw and must agree too.
    # Headless execution has no draw rows, but that does not excuse a different
    # winner, outcome, frame count or teardown. Source-drawn counts and source
    # indices were checked while walking the timeline above.
    terminal_keys = ('frames', 'result', 'remaining_fighter_slots', 'status')
    difference = _first_difference({key: a.end[key] for key in terminal_keys},
                                   {key: b.end[key] for key in terminal_keys}, 'end')
    if difference:
        return {'phase': 'end', 'tick': len(a.frames), 'field': difference[0],
                'expected': difference[1], 'actual': difference[2]}
    return None


def diagnose_prefix(reference, path):
    """Retain the earliest available mismatch in a failed port observation.

    This never accepts a capture or changes its requested bound. A matching
    partial prefix remains an incomplete failure with its original error.
    """
    path = Path(path)
    require(path.stat().st_size <= 512 * 1024 * 1024, 'CPU observation exceeds byte bound')
    rows = read_jsonl(path)
    require(rows and isinstance(rows[0], dict), 'CPU prefix has no header')
    expected_header = dict(reference.header)
    expected_header['source_drawing'] = rows[0].get('source_drawing')
    require(type(expected_header['source_drawing']) is bool, 'CPU prefix drawing declaration invalid')
    difference = _first_difference(expected_header, rows[0])
    if difference:
        return {'phase': 'header', 'tick': None, 'field': difference[0],
                'expected': difference[1], 'actual': difference[2]}
    types = [player['cpu'] is not None for player in reference.initial['players']]
    frames = draws = 0
    for ordinal, row in enumerate(rows[1:], 1):
        record = row.get('record')
        if record == 'initial' and ordinal == 1:
            expected, tick = reference.initial, None
        elif record == 'frame':
            integer(row.get('index'), frames, frames, 'CPU prefix frame index')
            require(frames < len(reference.frames), 'CPU prefix exceeds original timeline')
            expected, tick = reference.frames[frames], frames
            frames += 1
        elif record == 'draw':
            require(expected_header['source_drawing'], 'CPU prefix draw in headless capture')
            integer(row.get('index'), draws, draws, 'CPU prefix draw index')
            require(draws < len(reference.draws), 'CPU prefix exceeds original draw count')
            expected, tick = reference.draws[draws], row.get('source_index')
            draws += 1
        elif record == 'end':
            expected, tick = dict(reference.end), frames
            if not expected_header['source_drawing']:
                expected['draws'] = 0
        else:
            raise CaptureError('CPU prefix has an invalid phase')
        if record != 'end':
            snapshot({key: row[key] for key in ('match', 'camera', 'players')},
                     types, 'CPU prefix snapshot')
        difference = _first_difference(expected, row)
        if difference:
            return {'phase': record, 'tick': tick, 'field': difference[0],
                    'expected': difference[1], 'actual': difference[2]}
    return None


def domain_divergences(a, b):
    """Supplement the strict first failure with independent causal leads.

    An early camera difference must not conceal a later CPU decision failure.
    These summaries never determine acceptance or omit a compared field.
    """
    domains = ('match', 'camera', 'cpu', 'subject', 'magnifier', 'hud')
    found = {domain: None for domain in domains}
    if a.header['source_drawing'] and b.header['source_drawing']:
        matched, _, _ = _draw_alignment(a.draws, b.draws)
        draw_alignment = _draw_alignment_report(a.draws, b.draws)
        draw_by_tick = {source_index: (left[1], right[1])
                        for source_index, left, right in matched}
    else:
        draw_alignment = _draw_alignment_report(
            a.draws, b.draws, scope='draws_not_compared_headless')
        draw_alignment['status'] = 'not_compared'
        draw_by_tick = {}
    found['draw_alignment'] = draw_alignment

    # As in _compare, source frames precede the draw at the same tick.  Only
    # matching source-index draw pairs enter domain comparisons; missing/extra
    # rows are represented solely by draw_alignment.
    max_tick = max(len(a.frames), len(b.frames),
                   (max(draw_by_tick, default=-1) + 1))
    phases = [('initial', None, a.initial, b.initial)]
    for phase, tick, left, right in phases:
        for domain in domains:
            if found[domain] is not None:
                continue
            if domain in ('match', 'camera'):
                difference = _first_difference(left[domain], right[domain], domain)
            else:
                difference = None
                for slot, (expected, actual) in enumerate(zip(left['players'], right['players'])):
                    difference = _first_difference(expected[domain], actual[domain],
                                                   f'players[{slot}].{domain}')
                    if difference:
                        break
            if difference:
                found[domain] = {'phase': phase, 'tick': tick, 'field': difference[0],
                                 'expected': difference[1], 'actual': difference[2]}
    for tick in range(max_tick):
        if tick < len(a.frames) and tick < len(b.frames):
            phases = [('frame', tick, a.frames[tick], b.frames[tick])]
        else:
            phases = []
        if tick in draw_by_tick:
            left, right = draw_by_tick[tick]
            phases.append(('draw', tick, left, right))
        for phase, phase_tick, left, right in phases:
            for domain in domains:
                if found[domain] is not None:
                    continue
                if domain in ('match', 'camera'):
                    difference = _first_difference(left[domain], right[domain], domain)
                else:
                    difference = None
                    for slot, (expected, actual) in enumerate(zip(left['players'], right['players'])):
                        difference = _first_difference(expected[domain], actual[domain],
                                                       f'players[{slot}].{domain}')
                        if difference:
                            break
                if difference:
                    found[domain] = {'phase': phase, 'tick': phase_tick, 'field': difference[0],
                                     'expected': difference[1], 'actual': difference[2]}
    return found


def load_preparation_observations(path, types):
    """Read source draws outside the paired simulation timeline, including zero."""
    path = Path(path)
    require(path.stat().st_size <= 128 * 1024 * 1024, 'CPU preparation observation exceeds byte bound')
    rows = read_jsonl(path) if path.stat().st_size else []
    require(len(rows) <= 14400, 'CPU preparation draw bound exceeded')
    for index, row in enumerate(rows):
        keys(row, ('schema', 'version', 'phase', 'index', 'match', 'camera', 'players'),
             'CPU preparation')
        require(row['schema'] == 'melee-web-cpu-preparation-observation' and
                type(row['version']) is int and row['version'] == 1 and
                row['phase'] == 'after_source_preparation_draw', 'CPU preparation phase')
        integer(row['index'], index, index, 'CPU preparation index')
        snapshot({key: row[key] for key in ('match', 'camera', 'players')}, types,
                 'CPU preparation snapshot')
    return rows


def compare_observations(reference_a, reference_b, port=None):
    a, b = load_observation(reference_a), load_observation(reference_b)
    require(a.path.resolve() != b.path.resolve(), 'CPU pairing requires separate original runs')
    require(a.header['source_drawing'] and b.header['source_drawing'], 'Original CPU references require drawing')
    first = _compare(a, b, drawing=True)
    result = {'schema': 'melee-web-cpu-observation-comparison', 'version': 1,
              'reference_repeatability': 'fail' if first else 'pass',
              'status': 'reference_divergence' if first else 'repeatable',
              'first_divergence': first, 'frames': len(a.frames), 'reference_draws': len(a.draws),
              'hashes': {'reference_a': a.sha256, 'reference_b': b.sha256},
              'performance': 'not_evaluated', 'pixels': 'not_compared', 'gold_admitted': False}
    if port is not None and not first:
        try:
            p = load_observation(port)
        except (OSError, ValueError) as error:
            result['status'] = 'invalid_or_incomplete_port'
            result['error'] = str(error)
            try:
                result['hashes']['port'] = hashlib.sha256(Path(port).read_bytes()).hexdigest()
                result['first_divergence'] = diagnose_prefix(a, port)
            except (OSError, ValueError, KeyError, TypeError) as prefix_error:
                result['prefix_error'] = str(prefix_error)
            return result
        result['hashes']['port'] = p.sha256
        result['port_draws'] = len(p.draws)
        result['source_drawing'] = p.header['source_drawing']
        if not first:
            first = _compare(a, p, drawing=p.header['source_drawing'])
            result['status'] = 'declared_state_divergence' if first else 'declared_state_match'
            result['first_divergence'] = first
            result['first_divergences_by_domain'] = domain_divergences(a, p)
    return result
