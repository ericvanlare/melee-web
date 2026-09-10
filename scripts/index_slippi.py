#!/usr/bin/env python3
"""Index local Slippi files without retaining player names or paths."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from slippi_format import SCHEMA, SCHEMA_VERSION, SPEC_URL, SlippiFormatError, read_header


def discover(paths):
    files = []
    for supplied in paths:
        path = supplied.expanduser().resolve(strict=True)
        if path.is_dir():
            files.extend(candidate for candidate in path.rglob("*")
                         if candidate.is_file() and candidate.suffix.lower() == ".slp")
        elif path.is_file() and path.suffix.lower() == ".slp":
            files.append(path)
        else:
            raise ValueError(f"not a Slippi file or directory: {supplied}")
    return sorted(set(files))


def inspect(path):
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        header = read_header(stream)
        stream.seek(0)
        while block := stream.read(1024 * 1024):
            digest.update(block)
            size += len(block)
    return {"sha256": digest.hexdigest(), "byte_size": size, **header.record()}


def build_manifest(paths, jobs):
    files = discover(paths)
    if not files:
        raise ValueError("no .slp files found")
    records = []
    errors = []
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [(path, pool.submit(inspect, path)) for path in files]
        for path, future in futures:
            try:
                records.append(future.result())
            except (OSError, SlippiFormatError, ValueError) as error:
                errors.append({
                    "file_id": hashlib.sha256(os.fsencode(path.name)).hexdigest(),
                    "error": type(error).__name__,
                    "reason": str(error),
                })
    unique = {record["sha256"]: record for record in records}
    return {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "slippi_spec": SPEC_URL,
        "files_discovered": len(files),
        "unique_files": len(unique),
        "duplicates": len(records) - len(unique),
        "records": [unique[key] for key in sorted(unique)],
        "errors": errors,
    }


def publish(path, manifest):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".slippi-index-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w") as stream:
            json.dump(manifest, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    args = parser.parse_args()
    if args.jobs < 1 or args.jobs > 64:
        parser.error("--jobs must be between 1 and 64")
    try:
        manifest = build_manifest(args.paths, args.jobs)
        publish(args.output.expanduser().resolve(), manifest)
    except (OSError, ValueError) as error:
        parser.exit(2, f"Slippi index failed: {error}\n")
    print(json.dumps({key: manifest[key] for key in
                      ("files_discovered", "unique_files", "duplicates")},
                     sort_keys=True))
    return 1 if manifest["errors"] else 0


if __name__ == "__main__":
    sys.exit(main())
