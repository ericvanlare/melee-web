#!/usr/bin/env python3
"""Freeze and record bounded browser hitch evidence."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from hitch_capture import (  # noqa: E402
    HitchCaptureError,
    MAX_JSON_BYTES,
    begin_attempt,
    create_plan,
    finish_attempt,
    status,
)


def _emit(value: object) -> None:
    print(json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")))


def _attachments(values: list[str]) -> list[str | dict]:
    """Accept repeated paths and the runner's optional JSON list sidecar."""

    if len(values) == 1:
        candidate = Path(values[0])
        if candidate.is_file():
            try:
                # A single 256 MiB trace is also a valid attachment argument.
                # Inspect only a bounded candidate before deciding whether it
                # is the optional JSON sidecar list.
                if candidate.stat().st_size > MAX_JSON_BYTES:
                    value = None
                else:
                    with candidate.open("rb") as stream:
                        raw = stream.read(MAX_JSON_BYTES + 1)
                    value = (json.loads(raw.decode("utf-8"))
                             if len(raw) <= MAX_JSON_BYTES else None)
            except (OSError, UnicodeDecodeError, json.JSONDecodeError):
                value = None
            if isinstance(value, list):
                if not all(isinstance(item, (str, dict)) for item in value):
                    raise HitchCaptureError("Attachment list must contain paths or objects")
                return value
    return values


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    plan = commands.add_parser("plan", help="freeze a finite matrix and evidence identities")
    plan.add_argument("--spec", required=True, type=Path)
    plan.add_argument("--output", required=True, type=Path)

    start = commands.add_parser("start", help="reserve one slot before browser work")
    start.add_argument("--plan", required=True, type=Path)
    start.add_argument("--slot", required=True)

    finish = commands.add_parser("finish", help="consume one slot with result evidence")
    finish.add_argument("--plan", required=True, type=Path)
    finish.add_argument("--slot", required=True)
    finish.add_argument("--status", choices=("completed", "aborted", "crashed", "interrupted", "timeout"),
                        default="completed")
    finish.add_argument("--report", type=Path)
    finish.add_argument("--attachment", action="append", default=[])
    finish.add_argument("--attachments", nargs="*", default=[])
    finish.add_argument("--reason")

    report = commands.add_parser("status", help="verify and summarize the frozen plan")
    report.add_argument("--plan", required=True, type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = _parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "plan":
            plan = create_plan(args.spec, args.output)
            _emit({"ok": True, "command": "plan", "plan": str(args.output.resolve()),
                   "plan_id": plan["plan_id"], "planned": len(plan["slots"])})
        elif args.command == "start":
            result = begin_attempt(args.plan, args.slot)
            _emit({"ok": True, "command": "start", **result})
        elif args.command == "finish":
            attachments = list(args.attachment) + _attachments(list(args.attachments))
            result = finish_attempt(args.plan, args.slot, status=args.status,
                                    report_path=args.report, attachments=attachments,
                                    reason=args.reason)
            _emit({"ok": True, "command": "finish", **result})
        else:
            _emit({"ok": True, "command": "status", **status(args.plan)})
        return 0
    except (HitchCaptureError, OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        parser.exit(2, f"hitch evidence failed: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
