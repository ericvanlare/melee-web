#!/usr/bin/env python3
"""Bind validated passive startup timing to a new MWRC v5 input recipe.

The caller must separately compare the observer's semantic timeline to the
original. This tool derives context only from startup clocks, then rejects a
model that disagrees with any observed input-queue snapshot. It neither derives
nor accepts expected draw indexes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def validate_queue_prediction(rows, count, next_pad, first_poll, pad_period, vi_period):
    """Check every observed input-queue snapshot, without reading draw outputs.

    The startup-only model is conditional: intervening original work can delay a
    queue check. Refuse to package it when the capture disproves that assumption.
    """
    expected = [(0, 1), (1, 1)]
    alarm = next_pad + pad_period
    poll = first_poll
    cursor = 2
    while cursor < count:
        queued = 0
        while alarm <= poll:
            queued += 1
            alarm += pad_period
        if not queued:
            queued = 1
            alarm += pad_period
        queued = min(queued, count - cursor)
        expected.append((cursor, queued))
        cursor += queued
        poll += vi_period
    observed = []
    for row in rows:
        payload = row['payload']
        if payload.get('clock_pc') == 0x803769d4 and payload.get('r3', 0) != 0:
            tick, queued = row['source_tick'], payload['r3']
            if type(tick) is not int or type(queued) is not int or not 1 <= queued <= 2:
                raise ValueError('unsupported observed input-queue snapshot')
            observed.append((tick, queued))
    for index, (wanted, actual) in enumerate(zip(expected, observed)):
        if wanted != actual:
            raise ValueError(f'startup clock model disagrees with input queue at snapshot {index}: '
                             f'predicted {wanted}, observed {actual}; clock binding rejected')
    if len(observed) != len(expected):
        raise ValueError('complete input-queue history is required for clock binding')
    return len(expected)


def bind(recipe, clocks, recipe_sha256, clocks_sha256):
    if hashlib.sha256(recipe).hexdigest() != recipe_sha256:
        raise ValueError('recipe SHA-256 mismatch')
    if hashlib.sha256(clocks).hexdigest() != clocks_sha256:
        raise ValueError('clock SHA-256 mismatch')
    if len(recipe) < 20:
        raise ValueError('recipe is truncated')
    magic, version, seed, count = struct.unpack_from('>4sIII', recipe)
    if magic != b'MWRC' or version != 4 or not 2 <= count <= 36000 or len(recipe) != 20+312+822+44*count:
        raise ValueError('clock binding requires a complete bounded MWRC v4 recipe')
    rows = [json.loads(line) for line in clocks.splitlines()]
    startup = [row for row in rows if row['source_tick'] <= 3]
    first = next(row for row in startup if row['source_tick'] == 0 and row['payload']['clock_pc'] == 0x803769d4)
    vi = next(row for row in startup if row['source_tick'] == 2 and row['payload']['clock_pc'] == 0x803769d4)
    p = first['payload']; w = p['cadence_words']
    pad_period = 12*((w[26]<<32)|w[27])
    deadline = 12*(((w[22]<<32)|w[23])-p['time_adjust']-p['time_base'])
    first_poll = vi['payload']['core_ticks']-p['core_ticks']
    vi_period = p['vi_period']
    if (pad_period != 8100000 or vi_period != 8108100 or
            p['queue_bytes'][3] != 1 or p['video_words'][117] != 2 or
            sorted([p['video_words'][23], p['video_words'][47]]) != [2,7] or
            vi['payload']['r3'] != 0 or not 0 < deadline < first_poll <= vi_period):
        raise ValueError('unsupported original startup clock/buffer context')
    snapshots = validate_queue_prediction(rows, count, deadline, first_poll, pad_period, vi_period)
    # Two original XFBs, one free and one displayed, permit two initial source
    # traversals before the first VI-gated queue check (copy waiting follows draw).
    context = struct.pack('>QQQQII', pad_period, vi_period, deadline, first_poll, 2, 0)
    payload = struct.pack('>4sIII', magic, 5, seed, count)+recipe[16:20]+context+recipe[20:]
    evidence = {'schema':'melee-web-retail-clock-binding','version':1,
        'scope':'startup clock context with full input-queue prediction check; semantic equivalence requires independent comparison',
        'input_queue_snapshots_checked':snapshots,
        'input_recipe_sha256':recipe_sha256,'clock_stream_sha256':clocks_sha256,
        'output_recipe_sha256':hashlib.sha256(payload).hexdigest(),
        'frames':count,'pad_period_cpu_ticks':pad_period,'vi_period_cpu_ticks':vi_period,
        'next_pad_relative_cpu_ticks':deadline,'first_vi_poll_relative_cpu_ticks':first_poll,
        'startup_draws':2,'source_ticks_used':[0,1,2,3],
        'clock_precision':'block-clock timestamps; TB-to-CPU quantization up to 11 cycles'}
    return payload, evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ('recipe','clocks','output'):
        parser.add_argument('--'+flag, required=True, type=Path)
    for flag in ('recipe-sha256','clocks-sha256'):
        parser.add_argument('--'+flag, required=True)
    args = parser.parse_args()
    payload, evidence = bind(args.recipe.read_bytes(), args.clocks.read_bytes(), args.recipe_sha256, args.clocks_sha256)
    sidecar = args.output.with_suffix(args.output.suffix+'.json')
    if args.output.exists() or sidecar.exists():
        raise ValueError('refusing to overwrite clock binding outputs')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('xb') as stream: stream.write(payload)
    with sidecar.open('x') as stream: stream.write(json.dumps(evidence,indent=2)+'\n')
    print(json.dumps(evidence,indent=2))


if __name__ == '__main__': main()
