#!/usr/bin/env python3
"""Derive an input-only retail controller plan from one finalized Slippi file."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from normalize_slippi import load, publish
from retail_input_plan import plan_from_timeline


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('replay', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.output.resolve() == args.replay.resolve():
            raise ValueError('Output cannot overwrite the Slippi input')
        timeline, digest = load(args.replay)
        plan = plan_from_timeline(timeline, digest)
        publish(args.output, plan)
    except (OSError, ValueError) as error:
        parser.exit(2, f'Input plan export failed: {error}\n')
    print(f"Wrote {len(plan['frames'])} input-only ticks; no expected state or gold admission")


if __name__ == '__main__':
    main()
