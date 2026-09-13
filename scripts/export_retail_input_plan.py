#!/usr/bin/env python3
"""Derive an input-only retail controller plan from one finalized Slippi file."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from normalize_slippi import load, publish
from retail_input_plan import EXPORT_POLICIES, POLICY, plan_from_timeline, prefix_timeline


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('replay', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--policy', choices=EXPORT_POLICIES, default=POLICY)
    parser.add_argument('--frames', type=int,
                        help='export only the initial positive source-frame prefix')
    args = parser.parse_args()
    try:
        if args.output.resolve() == args.replay.resolve():
            raise ValueError('Output cannot overwrite the Slippi input')
        timeline, digest = load(args.replay)
        # Parse and hash the complete source before selecting a workload
        # prefix. The plan keeps that full .slp digest as provenance.
        if args.frames is not None:
            timeline = prefix_timeline(timeline, args.frames)
        plan = plan_from_timeline(timeline, digest, policy=args.policy)
        publish(args.output, plan)
    except (OSError, ValueError) as error:
        parser.exit(2, f'Input plan export failed: {error}\n')
    print(f"Wrote {len(plan['frames'])} input-only ticks; no expected state or gold admission")


if __name__ == '__main__':
    main()
