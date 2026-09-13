#!/usr/bin/env python3
"""Check one named development workload's exact retail StartMeleeData setup."""

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from retail_setup_validation import validate_setup


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", required=True, type=Path,
                        help="immutable development execution-plan-v3 JSON")
    parser.add_argument("--name", required=True,
                        help="exact development entry name")
    parser.add_argument("--capture", required=True, type=Path,
                        help="complete pinned retail capture JSONL")
    parser.add_argument("--cpu", choices=("Interpreter64", "JITARM64"), default="Interpreter64")
    parser.add_argument("--repo-root", type=Path,
                        help="root for relative source and input-plan paths")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        result = validate_setup(args.plan, args.name, args.capture,
                                cpu=args.cpu, repo_root=args.repo_root)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(2, f"Retail setup validation failed: {error}\n")
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        output = args.output.resolve()
        immutable = {args.plan.resolve(), args.capture.resolve()}
        try:
            execution = json.loads(args.plan.read_text(encoding="utf-8"))
            root = args.repo_root.resolve() if args.repo_root is not None else None
            for entry in execution.get("selected_before_reference_execution", []):
                if isinstance(entry, dict) and entry.get("name") == args.name:
                    for key in ("source", "plan"):
                        path = Path(entry[key])
                        immutable.add((root / path if root is not None and not path.is_absolute() else path).resolve())
                    break
        except (OSError, KeyError, TypeError, json.JSONDecodeError):
            # validate_setup already reports malformed plans; the primary plan
            # and capture paths remain protected even when lookup is impossible.
            pass
        if output in immutable:
            parser.error("output cannot overwrite the execution plan or capture")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
