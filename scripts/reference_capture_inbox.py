#!/usr/bin/env python3
"""Inspect and advance the reference-session inbox.

This command only moves a finalized raw bundle after the library has verified
its manifest and semantic lifecycle.  It never manufactures a completion row
or rewrites raw capture bytes.  Derived output is written to the separate
``derived`` state and carries the source manifest digest.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from reference_session_bundle import (  # noqa: E402
    ACCEPTED_UNPROCESSED,
    BundleError,
    ReferenceCaptureInbox,
    validate_bundle,
)


def _emit(value, *, pretty: bool = False) -> None:
    print(json.dumps(value, sort_keys=True, indent=2 if pretty else None))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path,
                        default=Path.home() / "Library/Application Support/WebMelee Reference Capture/Captures",
                        help="reference capture inbox (defaults to the application's private inbox)")
    parser.add_argument("--pretty", action="store_true", help="format JSON output")
    subparsers = parser.add_subparsers(dest="command")

    list_parser = subparsers.add_parser("list", help="list inbox states")
    list_parser.add_argument("--state", help="restrict to one inbox state")

    validate_parser = subparsers.add_parser("validate", help="validate one bundle")
    validate_parser.add_argument("session_id")
    validate_parser.add_argument("--state", default=ACCEPTED_UNPROCESSED)
    validate_parser.add_argument("--allow-partial", action="store_true")

    ingest_parser = subparsers.add_parser("ingest", help="move accepted raw data to ingested")
    ingest_parser.add_argument("session_id")
    ingest_parser.add_argument("--manifest-sha256")

    quarantine_parser = subparsers.add_parser("quarantine", help="move a failed partial to failed")
    quarantine_parser.add_argument("session_id", help="session id, or session id.partial for activepartials")
    quarantine_parser.add_argument("--state", default="activepartials")

    derived_parser = subparsers.add_parser("derived", help="store a hash-bound derived JSON result")
    derived_parser.add_argument("session_id")
    derived_parser.add_argument("artifact_name")
    derived_parser.add_argument("payload", help="JSON object, kept separate from raw capture")
    derived_parser.add_argument("--source-state", default=ACCEPTED_UNPROCESSED)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    command = args.command or "list"
    try:
        inbox = ReferenceCaptureInbox(args.root)
        if command == "list":
            _emit({"root": str(inbox.root), "runs": inbox.list_runs(args.state)}, pretty=args.pretty)
            return 0
        if command == "validate":
            report = inbox.validate(
                args.session_id,
                state=args.state,
                require_complete=not args.allow_partial,
            )
            _emit(report.to_dict(), pretty=args.pretty)
            return 0 if report.valid else 2
        if command == "ingest":
            destination = inbox.ingest(
                args.session_id,
                expected_manifest_sha256=args.manifest_sha256,
            )
            _emit({"state": "ingested", "session_id": args.session_id, "path": str(destination)}, pretty=args.pretty)
            return 0
        if command == "quarantine":
            destination = inbox.quarantine(args.session_id, state=args.state)
            _emit({"state": "failed", "session_id": args.session_id, "path": str(destination)}, pretty=args.pretty)
            return 0
        if command == "derived":
            try:
                payload = json.loads(args.payload)
            except json.JSONDecodeError as error:
                raise BundleError(f"payload is not valid JSON: {error}") from error
            destination = inbox.store_derived(
                args.session_id,
                args.artifact_name,
                payload,
                source_state=args.source_state,
            )
            _emit({"state": "derived", "session_id": args.session_id, "path": str(destination)}, pretty=args.pretty)
            return 0
        raise BundleError(f"unknown command: {command}")
    except (BundleError, OSError, ValueError) as error:
        _emit({"error": str(error)}, pretty=args.pretty)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
