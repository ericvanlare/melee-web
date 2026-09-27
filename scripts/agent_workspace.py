#!/usr/bin/env python3
"""Report space, protect active checkout mutations, or explicitly reclaim disposable storage."""
import argparse
import json
from pathlib import Path
import subprocess

from workspace_resources import operation, report_space, retire_builds

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    commands.add_parser("status")
    commands.add_parser("dedup-toolchain", help="opt in to verified APFS toolchain sharing")
    retire = commands.add_parser("retire-builds")
    retire.add_argument("--apply", action="store_true", help="remove journaled .o/.a files after open-file checks")
    run = commands.add_parser("run")
    run.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    try:
        if args.action == "status":
            print(json.dumps(report_space(ROOT), indent=2))
        elif args.action == "dedup-toolchain":
            from share_toolchain import share_installed_toolchain
            print(json.dumps(share_installed_toolchain(ROOT), indent=2))
        elif args.action == "retire-builds":
            print(json.dumps(retire_builds(ROOT, apply=args.apply), indent=2))
        else:
            command = args.command
            if command[:1] == ["--"]:
                command = command[1:]
            if not command:
                parser.error("run requires a command after --")
            with operation(ROOT, " ".join(command[:2])):
                return subprocess.call(command, cwd=ROOT)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"workspace: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
