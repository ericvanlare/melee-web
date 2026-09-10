#!/usr/bin/env python3
"""Materialize the reviewed Aurora pipeline seed used by browser builds."""

import argparse
import base64
import gzip
import hashlib
from pathlib import Path
import tempfile


EXPECTED_SHA256 = "8247da6150196f23942dd6ca86d14808904d7736e11b8e51c345296d274a549a"


def materialize(source: Path, output: Path) -> None:
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


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    materialize(args.source, args.output)


if __name__ == "__main__":
    main()
