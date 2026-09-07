#!/usr/bin/env python3
"""Configure and build the browser integration probe using project-local tools."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

from bootstrap import read_lock, verify_sources

ROOT = Path(__file__).resolve().parents[1]


def build(jobs, root=ROOT):
    lock = read_lock(root)
    verify_sources(root, lock)
    if (root / ".venv").is_symlink():
        raise ValueError(".venv must be a local directory, not a symlink")
    bins = root / ".venv" / ("Scripts" if os.name == "nt" else "bin")
    cmake = bins / ("cmake.exe" if os.name == "nt" else "cmake")
    ninja = bins / ("ninja.exe" if os.name == "nt" else "ninja")
    sdk = root / ".deps/emsdk"
    emscripten = sdk / "upstream/emscripten"
    emcmake = emscripten / ("emcmake.bat" if os.name == "nt" else "emcmake")
    if not all(path.is_file() for path in (cmake, ninja, emcmake, sdk / ".emscripten")):
        raise ValueError("Build tools are missing. Run python3 scripts/bootstrap.py first.")
    version = (emscripten / "emscripten-version.txt").read_text().strip().strip('"')
    if version != lock["emscripten"]:
        raise ValueError(f"Expected Emscripten {lock['emscripten']}, got {version}. Run bootstrap.py.")
    env = dict(os.environ)
    env["PATH"] = str(bins) + os.pathsep + env.get("PATH", "")
    # A separately activated SDK must not redirect this build to global tools/cache.
    env["EMSDK"] = str(sdk)
    env["EM_CONFIG"] = str(sdk / ".emscripten")
    env["EM_CACHE"] = str(emscripten / "cache")
    env["EMSDK_PYTHON"] = sys.executable
    build_dir = root / "build/browser"
    if (root / "build").is_symlink() or build_dir.is_symlink():
        raise ValueError("Build output must be a local directory, not a symlink")
    subprocess.run([str(emcmake), str(cmake), "-S", str(root), "-B", str(build_dir),
                    "-G", "Ninja", "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                    f"-DCMAKE_MAKE_PROGRAM={ninja}"], cwd=root, env=env, check=True)
    subprocess.run([str(cmake), "--build", str(build_dir), "--target", "gx_probe", "-j", str(jobs)],
                   cwd=root, env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 6))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        build(args.jobs)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"build: {error}") from error


if __name__ == "__main__":
    main()
