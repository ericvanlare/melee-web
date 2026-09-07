#!/usr/bin/env python3
"""Report native CPU parser coverage as JSON lines; no rendering is tested.

Pass explicit DAT paths or one directory (nonrecursive .dat/.usd selection).
Every public root is attempted as an HSD joint; other root types can be rejected.
Use --symbol to select one known model root without attempting other root types.
Textures counts distinct TObj descriptors. Exit 1 means a parser rejection or
input error; exit 2 means the compiler/checking process could not complete.
"""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--symbol", help="check only this exact public model symbol")
    args = parser.parse_args()
    paths = args.paths
    if len(paths) == 1 and paths[0].is_dir():
        paths = sorted(path for path in paths[0].iterdir()
                       if path.is_file() and path.suffix.lower() in (".dat", ".usd"))
    elif any(path.is_dir() for path in paths):
        parser.error("pass either explicit files or one directory")
    if not paths:
        parser.error("the directory contains no DAT/USD files")

    compiler = shutil.which("clang++") or shutil.which("c++")
    if compiler is None:
        parser.error("a C++20 compiler is required")
    binary = ROOT / "build/native/asset_check"
    binary.parent.mkdir(parents=True, exist_ok=True)
    # Recompile once per invocation so dependency/flag changes cannot leave a
    # stale parser binary producing misleading corpus results.
    sources = ["src/dat_archive.cpp", "src/dat_texture.cpp", "src/dat_material.cpp",
               "src/rigid_model.cpp", "tools/asset_check.cpp"]
    try:
        compiled = subprocess.run(
            [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
             "-I", str(ROOT / "src"), *(str(ROOT / source) for source in sources),
             "-o", str(binary)], capture_output=True, timeout=120,
        )
        if compiled.returncode:
            sys.stderr.buffer.write(compiled.stdout + compiled.stderr)
            return 2
        status = 0
        for path in paths:
            command = [str(binary), str(path)] + ([args.symbol] if args.symbol else [])
            result = subprocess.run(command, capture_output=True, timeout=60)
            if result.returncode not in (0, 1):
                sys.stderr.buffer.write(result.stderr)
                return 2
            status = max(status, result.returncode)
            for line in result.stdout.splitlines():
                # Preserve non-UTF8 archive symbol bytes as escaped surrogates;
                # filenames are reported by Python with their original spelling.
                record = json.loads(line.decode("utf-8", errors="surrogateescape"))
                print(json.dumps({"file": str(path), **record}, sort_keys=True), flush=True)
        return status
    except (OSError, subprocess.TimeoutExpired, ValueError) as error:
        print(f"Asset check could not complete: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
