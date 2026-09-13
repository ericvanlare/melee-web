#!/usr/bin/env python3
"""CPU match extension of the existing validated replay coverage report."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from replay_coverage import analyze_cpu_capture, DEFAULT_SOURCE_ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('capture', 'input-plan', 'observation', 'completion', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cpu', choices=('Interpreter64', 'JITARM64'), default='Interpreter64')
    parser.add_argument('--source-root', type=Path, default=DEFAULT_SOURCE_ROOT)
    args = parser.parse_args()
    if args.output.resolve() in {p.resolve() for p in
            (args.capture, args.input_plan, args.observation, args.completion)}:
        parser.error('output must not replace input evidence')
    try:
        report = analyze_cpu_capture(args.capture, input_plan=args.input_plan,
            observation=args.observation, completion=args.completion,
            cpu=args.cpu, source_root=args.source_root)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    print(json.dumps({'output': str(args.output), 'ticks': report['frames'],
                      'players': report['cpu_match']['player_count']}))


if __name__ == '__main__':
    main()
