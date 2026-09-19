#!/usr/bin/env python3
"""Seed selected object files into ccache without building a complete target."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import resource
import subprocess
import sys
import time

from ci_verify import GROUPS, ninja_timings


ROOT = Path(__file__).resolve().parents[1]
SHARD_COUNT = 4


def _ninja_path(root):
    return root / ".venv/bin/ninja"


def _environment(root):
    sdk = root / ".deps/emsdk"
    environment = dict(
        os.environ,
        EMSDK=str(sdk),
        EM_CONFIG=str(sdk / ".emscripten"),
        EM_CACHE=str(sdk / "upstream/emscripten/cache"),
        EMSDK_PYTHON=sys.executable,
        EM_COMPILER_WRAPPER="ccache",
    )
    environment["PATH"] = str(root / ".venv/bin") + os.pathsep + environment.get("PATH", "")
    return environment


def target_union():
    return list(dict.fromkeys(target for targets in GROUPS.values() for target in targets))


def _relative_object(output):
    if not isinstance(output, str) or not output.endswith(".o"):
        return None
    path = PurePosixPath(output)
    if path.is_absolute() or not path.parts or ".." in path.parts:
        return None
    return path.as_posix()


def compdb_objects(rows):
    """Return unique relative object outputs from compdb-targets rows."""
    return sorted({output for row in rows if (output := _relative_object(row.get("output")))})


def object_inventory(outputs):
    outputs = sorted(set(outputs))
    digest = hashlib.sha256("\n".join(outputs).encode("utf-8")).hexdigest()
    return {"count": len(outputs), "sha256": digest}


def shard_for(output):
    return int.from_bytes(hashlib.sha256(output.encode("utf-8")).digest(), "big") % SHARD_COUNT


def select_shard(outputs, shard):
    if shard not in range(SHARD_COUNT):
        raise ValueError(f"shard must be between 0 and {SHARD_COUNT - 1}")
    return [output for output in sorted(set(outputs)) if shard_for(output) == shard]


def _compdb(root, environment, ninja):
    command = [str(ninja), "-C", str(root / "build/browser"), "-t", "compdb-targets", "-x", *target_union()]
    result = subprocess.run(command, cwd=root, env=environment, capture_output=True, text=True, check=True)
    try:
        rows = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise ValueError(f"Ninja compdb-targets returned invalid JSON: {exc}") from exc
    if not isinstance(rows, list) or any(not isinstance(row, dict) for row in rows):
        raise ValueError("Ninja compdb-targets output must be a list of objects")
    return rows


def run_seed(shard, jobs=2):
    if shard not in range(SHARD_COUNT):
        raise ValueError(f"shard must be between 0 and {SHARD_COUNT - 1}")
    if jobs < 1:
        raise ValueError("jobs must be positive")

    root = ROOT
    build_dir = root / "build/browser"
    report_dir = root / "work/ci-seed"
    report_dir.mkdir(parents=True, exist_ok=True)
    ninja = _ninja_path(root)
    environment = _environment(root)
    report = {
        "schema": 1,
        "shard": shard,
        "status": "running",
        "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "machine": {"platform": platform.platform(), "cpus": os.cpu_count(), "jobs": jobs},
        "targets": target_union(),
        "phases": [],
    }
    started = time.monotonic()

    def phase(name, operation):
        begin = time.monotonic()
        try:
            return operation()
        finally:
            report["phases"].append({"name": name, "seconds": time.monotonic() - begin})

    try:
        phase("configure", lambda: subprocess.run(
            [sys.executable, str(root / "scripts/build.py"), "--configure-only"],
            cwd=root, env=environment, check=True,
        ))
        rows = phase("compdb", lambda: _compdb(root, environment, ninja))
        all_objects = compdb_objects(rows)
        selected = select_shard(all_objects, shard)
        if not selected:
            raise ValueError(f"seed shard {shard} selected no object outputs")
        report["all_objects"] = object_inventory(all_objects)
        report["selected_objects"] = object_inventory(selected)
        phase("build", lambda: subprocess.run(
            [str(ninja), "-C", str(build_dir), "-j", str(jobs), *selected],
            cwd=root, env=environment, check=True,
        ))
        report["status"] = "passed"
    except BaseException:
        report["status"] = "failed"
        raise
    finally:
        usage = resource.getrusage(resource.RUSAGE_CHILDREN)
        report.update(
            seconds=time.monotonic() - started,
            child_cpu_seconds=usage.ru_utime + usage.ru_stime,
            child_maxrss_bytes=usage.ru_maxrss * (1 if sys.platform == "darwin" else 1024),
            ninja=ninja_timings(build_dir / ".ninja_log"),
        )
        (report_dir / f"shard-{shard}.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({key: value for key, value in report.items() if key != "ninja"}, indent=2))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shard", type=int, choices=range(SHARD_COUNT), required=True)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    run_seed(args.shard, args.jobs)


if __name__ == "__main__":
    main()
