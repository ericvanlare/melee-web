# SPDX-License-Identifier: MIT
"""Verify a configured local service build and write its fresh lineage receipt."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

from run_local import generate_service_lineage  # noqa: E402


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for option in (
        "dolphin-source", "enet-source", "dolphin-build", "matchmaker-build",
        "client-binary", "service-binary", "source-patch", "output",
    ):
        parser.add_argument(f"--{option}", type=Path, required=True)
    args = parser.parse_args(argv)
    lock = json.loads((HERE / "client.lock.json").read_text(encoding="utf-8"))
    receipt = generate_service_lineage(
        lock,
        dolphin_source=args.dolphin_source.expanduser().resolve(strict=True),
        enet_source=args.enet_source.expanduser().resolve(strict=True),
        dolphin_build=args.dolphin_build.expanduser().resolve(strict=True),
        matchmaker_build=args.matchmaker_build.expanduser().resolve(strict=True),
        client_binary=args.client_binary.expanduser().resolve(strict=True),
        service_binary=args.service_binary.expanduser().resolve(strict=True),
        source_patch=args.source_patch.expanduser().resolve(strict=True),
        output=args.output.expanduser().resolve(),
    )
    print(json.dumps({
        "schema": receipt["schema"],
        "service_binary_sha256": receipt["service_binary_sha256"],
        "service_build_cache_sha256": receipt["service_build_cache_sha256"],
        "service_source_inventory_sha256": receipt["service_source_inventory_sha256"],
        "service_worktree_head": receipt["service_worktree_head"],
        "output": str(args.output.expanduser().resolve()),
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
