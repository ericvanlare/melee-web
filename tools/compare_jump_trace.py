#!/usr/bin/env python3
"""Compare the documented stationary-jump fields without hiding duplicate conflicts."""
import argparse
import json
from pathlib import Path


def compare(reference, port, first_frame):
    groups = {}
    for row in reference:
        if 'game_frame' not in row or not row.get('fighters'):
            continue
        fighter = next((f for f in row['fighters'] if f['slot'] == 0), None)
        if fighter is not None:
            groups.setdefault(row['game_frame'], []).append(fighter)
    expected = [row for row in port if 1 <= row['tick'] <= 102]
    if len(expected) != 102 or {row['tick'] for row in expected} != set(range(1, 103)):
        raise ValueError('Port capture must contain exactly one row for each of 102 input frames')
    differences = []
    for row in expected:
        frame = first_frame + row['tick'] - 1
        samples = groups.get(frame, [])
        if not samples:
            raise ValueError(f'Missing original game frame {frame}')
        signatures = {(s['motion'], s['frame']['bits'], s['position'][1]['bits'],
                       s['velocity'][1]['bits']) for s in samples}
        if len(signatures) != 1:
            raise ValueError(f'Conflicting original states within game frame {frame}')
        sample = samples[0]
        if (row['motion'] != sample['motion'] or
            (row['motion'] != 14 and row['frame'] != sample['frame']['value']) or
            row['vy_bits'] != sample['velocity'][1]['bits']):
            differences.append({'input_frame': row['tick'], 'original_frame': frame,
                                'port': row, 'original': sample})
    return {'frames': 102, 'matched': 102 - len(differences), 'differences': differences,
            'fields': ['motion', 'non-idle animation frame', 'vertical velocity bits'],
            'scope': 'Stationary jump only; stage position, RNG, costume, rules and other gameplay are not compared'}


def read(path):
    return [json.loads(line) for line in path.read_text().splitlines() if line.startswith('{')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', required=True, type=Path)
    parser.add_argument('--port', required=True, type=Path)
    parser.add_argument('--first-frame', required=True, type=int)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = compare(read(args.reference), read(args.port), args.first_frame)
    encoded = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end='')
    return bool(result['differences'])


if __name__ == '__main__':
    raise SystemExit(main())
