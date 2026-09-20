#!/usr/bin/env python3
"""Materialize the reviewed Aurora pipeline seed used by browser builds."""

import argparse
import base64
import gzip
import hashlib
from pathlib import Path
import tempfile


EXPECTED_SHA256 = "8df6a998cef19b88a3eff0ee61f666e42791f5f73841818961aa3b224cad2c4b"


def materialize(source: Path, output: Path, identity_header: Path | None = None) -> None:
    encoded = "".join(source.read_text(encoding="ascii").split())
    payload = gzip.decompress(base64.b64decode(encoded, validate=True))
    digest = hashlib.sha256(payload).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError(f"pipeline seed digest mismatch: {digest}")
    if not payload.startswith(b"SQLite format 3\0"):
        raise ValueError("pipeline seed is not a SQLite database")

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as temporary:
        temporary.write(payload)
        temporary_path = Path(temporary.name)
    temporary_path.replace(output)
    if identity_header is not None:
        identity_header.parent.mkdir(parents=True, exist_ok=True)
        identity_header.write_text(
            '#pragma once\n'
            f'#define MELEE_WEB_PIPELINE_SEED_SHA256 "{digest}"\n',
            encoding="ascii",
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--identity-header", type=Path)
    args = parser.parse_args()
    materialize(args.source, args.output, args.identity_header)


if __name__ == "__main__":
    main()
