#!/usr/bin/env python3
"""Bind recorded platform input-queue snapshots to a conditional MWRC v6 replay.

Require an independently verified original semantic replay before using its
clock stream. Queue-count returns are platform input fixtures, not a prediction
of CPU interrupt timing. Game state and source draw observations are excluded.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def bind(recipe, clocks, recipe_sha256, clocks_sha256):
    digest = lambda data: hashlib.sha256(data).hexdigest()
    if digest(recipe) != recipe_sha256 or digest(clocks) != clocks_sha256:
        raise ValueError('queue binding source SHA-256 mismatch')
    if len(recipe) < 20:
        raise ValueError('input recipe is truncated')
    magic, version, seed, frames = struct.unpack_from('>4sIII', recipe)
    if magic != b'MWRC' or version != 4 or not 1 <= frames <= 36000 or len(recipe) != 20 + 312 + 822 + 44 * frames:
        raise ValueError('queue binding requires a complete bounded MWRC v4 recipe')
    events = []
    cursor = 0
    origin = None
    last = -1
    for line in clocks.splitlines():
        row = json.loads(line)
        p = row['payload']
        if p.get('clock_pc') != 0x803769d4:
            continue
        available = p.get('r3')
        if available == 0:
            continue
        tick, time = row['source_tick'], p['core_ticks']
        if (type(tick) is not int or tick != cursor or type(time) is not int or
                time <= last or not 0 <= time < 2**64 or type(available) is not int or
                not 1 <= available <= 5 or cursor + available > frames):
            raise ValueError('invalid or discontinuous original input-queue snapshot')
        queue = p.get('queue_bytes')
        if not isinstance(queue, list) or len(queue) < 4 or queue[0] != 5 or queue[3] != available:
            raise ValueError('original queue read disagrees with protected queue state')
        if origin is None:
            origin = time
        events.append(struct.pack('>QB', time - origin, available))
        cursor += available
        last = time
    if cursor != frames:
        raise ValueError('original input-queue history is incomplete')
    queue_bytes = struct.pack('>II', len(events), 0) + b''.join(events)
    output = struct.pack('>4sIII', magic, 6, seed, frames) + recipe[16:20] + queue_bytes + recipe[20:]
    evidence = {
        'schema': 'melee-web-retail-queue-binding', 'version': 1,
        'scope': 'gameplay replay conditioned on recorded nonempty input-queue snapshots',
        'input_recipe_sha256': recipe_sha256, 'clock_stream_sha256': clocks_sha256,
        'output_recipe_sha256': digest(output), 'frames': frames,
        'queue_snapshots': len(events), 'queue_context_bytes': len(queue_bytes),
        'observation_pc': '0x803769d4', 'clock': 'relative emulated CPU block-clock ticks',
        'input_bytes_preserved': True, 'expected_game_state_supplied': False,
        'expected_draw_indexes_supplied': False, 'live_scheduling_equivalence': 'not_claimed',
        'original_cpu_scheduling_equivalence': 'not_claimed', 'gold_admitted': False,
    }
    return output, evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ('recipe', 'clocks', 'output'):
        parser.add_argument('--' + flag, type=Path, required=True)
    for flag in ('recipe-sha256', 'clocks-sha256'):
        parser.add_argument('--' + flag, required=True)
    args = parser.parse_args()
    output, evidence = bind(args.recipe.read_bytes(), args.clocks.read_bytes(),
                            args.recipe_sha256, args.clocks_sha256)
    sidecar = args.output.with_suffix(args.output.suffix + '.json')
    if args.output.exists() or sidecar.exists():
        raise ValueError('refusing to overwrite queue binding outputs')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('xb') as stream:
        stream.write(output)
    with sidecar.open('x') as stream:
        stream.write(json.dumps(evidence, indent=2) + '\n')
    print(json.dumps(evidence, indent=2))


if __name__ == '__main__':
    main()
