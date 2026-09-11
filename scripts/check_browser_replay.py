#!/usr/bin/env python3
"""Validate a paired retail trace plus visible Release cold/warm replay reports."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from browser_replay_validation import check_evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('reference-a', 'reference-b', 'recipe', 'port', 'state', 'cold', 'warm',
                 'profile', 'browser-errors', 'build-directory', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    for name in ('completion-a', 'completion-b'):
        parser.add_argument('--' + name, type=Path)
    parser.add_argument("--cpu", choices=("Interpreter64", "JITARM64"), default="Interpreter64")
    args = vars(parser.parse_args())
    output = args.pop('output')
    if output.resolve() in {p.resolve() for p in args.values() if isinstance(p, Path)}:
        parser.error('Output cannot overwrite input evidence')
    try:
        result = check_evidence(**args)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(2, f'Replay evidence failed: {error}\n')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    print(f"{result['status']}: {result['frames']} frames; content admission remains separate")


if __name__ == '__main__':
    main()
