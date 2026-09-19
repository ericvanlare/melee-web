#!/usr/bin/env python3
"""Run an isolated CI partition without reusing build products from another run."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import resource
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]

# The union is exactly build.py's existing all + fighter target inventory.
# Each Linux partition configures a clean graph; only compiler cache entries
# cross runs. Unit shards keep module fixtures together.
GROUPS = {
    "unit-0": (),
    "unit-1": (),
    "runtime": ("gameplay_menu_browser",),
    "graphics": ("gx_probe",),
    "gameplay": ("gameplay_checks",),
    "fighter": ("fighter_runtime_probe", "gameplay_pad_state_trace"),
    "effects": ("gameplay_effect_banks_trace", "gameplay_bonus_data_trace", "gameplay_stage_numeric_trace"),
    "menus": ("native_menu_scene_trace", "dat_menu_support_trace"),
}
UNIT_GROUPS = ("unit-0", "unit-1")

# Full discovery runs after configuration, so SDK/source/SDL/fmt-dependent
# compiler tests execute there. Only tests needing linked artifacts repeat in
# their owning partition. Proprietary-asset cases keep their normal skips.
LINKED_TESTS = {
    "fighter": ("test_pad_state.PadSnapshotTests.test_native_codec_and_source_input_edges",),
    "gameplay": (
        "test_hsd_native.NativeJointRuntimeTests",
        "test_gameplay_common_context",
    ),
    "effects": (
        "test_gameplay_effects", "test_gameplay_bonus_data", "test_gameplay_stage_numeric",
    ),
    "menus": (
        "test_gameplay_native_menus.NativeMenuSourceTests.test_original_sis_layout_and_style_stack",
        "test_dat_menu_support",
    ),
}
REQUIRED_TESTS = {
    "fighter": ("test_pad_state.PadSnapshotTests.test_native_codec_and_source_input_edges",),
    "gameplay": (
        "test_hsd_native.NativeJointRuntimeTests.test_original_allocation_callback_rejection_and_restart",
        "test_hsd_native.NativeJointRuntimeTests.test_replacement_heap_is_never_used_for_teardown",
        "test_gameplay_common_context.CommonContextTests.test_original_material_owners_restore_all_common_globals",
    ),
    "effects": (
        "test_gameplay_effects.EffectContextTests.test_authored_bank_bounds_lifetimes_and_restart",
    ),
    "menus": (
        "test_gameplay_native_menus.NativeMenuSourceTests.test_original_sis_layout_and_style_stack",
        "test_dat_menu_support.DatMenuSupportTests.test_support_roots_and_optional_local_assets",
    ),
}

def check_inventory():
    if not set(UNIT_GROUPS).issubset(GROUPS):
        raise ValueError("CI partitions must include both unit-0 and unit-1")
    from build import BUILD_TARGETS
    expected = set(BUILD_TARGETS["all"]) | set(BUILD_TARGETS["fighter"])
    actual = [target for targets in GROUPS.values() for target in targets]
    if len(actual) != len(set(actual)) or set(actual) != expected:
        raise ValueError("CI partitions must cover every all/fighter target exactly once")


def _test_cases(suite):
    """Yield test cases in unittest discovery order without running them."""
    for test in suite:
        if isinstance(test, unittest.TestSuite):
            yield from _test_cases(test)
        else:
            yield test


def _test_module(test):
    return type(test).__module__


def unit_shard(module):
    """Assign a test module to a stable unit shard."""
    digest = hashlib.sha256(module.encode("utf-8")).digest()
    return int.from_bytes(digest, "big") % len(UNIT_GROUPS)


def _shard_index(group):
    try:
        index = UNIT_GROUPS.index(group)
    except ValueError as exc:
        raise ValueError(f"unknown unit shard: {group}") from exc
    return index


def _filter_suite(suite, shard):
    selected = unittest.TestSuite()
    for test in suite:
        if isinstance(test, unittest.TestSuite):
            child = _filter_suite(test, shard)
            if child.countTestCases():
                selected.addTest(child)
        elif unit_shard(_test_module(test)) == shard:
            selected.addTest(test)
    return selected


def _inventory(suite, *, include_ids=False):
    ids = sorted(test.id() for test in _test_cases(suite))
    encoded = "\n".join(ids).encode("utf-8")
    inventory = {"count": len(ids), "sha256": hashlib.sha256(encoded).hexdigest()}
    if include_ids:
        inventory["ids"] = ids
    return inventory


def ninja_timings(path):
    if not path.is_file():
        return []
    rows = []
    for line in path.read_text().splitlines():
        if line.startswith("#"):
            continue
        start, end, _, output, _ = line.split("\t")
        rows.append({"output": output, "seconds": (int(end) - int(start)) / 1000})
    return sorted(rows, key=lambda row: row["seconds"], reverse=True)


class RecordingResult(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.outcomes = []

    def addSuccess(self, test):
        super().addSuccess(test)
        self.outcomes.append({"test": test.id(), "status": "passed"})

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self.outcomes.append({"test": test.id(), "status": "skipped", "reason": reason})

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self.outcomes.append({"test": test.id(), "status": "failed"})

    def addError(self, test, err):
        super().addError(test, err)
        self.outcomes.append({"test": test.id(), "status": "error"})

    def addSubTest(self, test, subtest, err):
        super().addSubTest(test, subtest, err)
        if err is not None:
            status = "failed" if issubclass(err[0], test.failureException) else "error"
            self.outcomes.append({"test": subtest.id(), "status": status})


def run_tests(group, report):
    sys.path.insert(0, str(ROOT / "tests"))
    loader = unittest.TestLoader()
    if group in UNIT_GROUPS:
        discovered = loader.discover(str(ROOT / "tests"))
        suite = _filter_suite(discovered, _shard_index(group))
        report["discovered"] = _inventory(discovered)
        report["selected"] = _inventory(suite, include_ids=True)
    else:
        suite = loader.loadTestsFromNames(LINKED_TESTS[group])
    result = unittest.TextTestRunner(verbosity=2, resultclass=RecordingResult).run(suite)
    report["tests"] = result.outcomes
    report["tests_run"] = result.testsRun
    if not result.wasSuccessful():
        raise RuntimeError("CI test partition failed")
    passed = {row["test"] for row in result.outcomes if row["status"] == "passed"}
    missing = set(REQUIRED_TESTS.get(group, ())) - passed
    if missing:
        raise RuntimeError(f"Required linked tests did not pass: {sorted(missing)}")


def run_group(group, jobs):
    check_inventory()
    directory = ROOT / "work/ci"
    directory.mkdir(parents=True, exist_ok=True)
    report = {"schema": 1, "group": group, "status": "running", "phases": [],
              "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "machine": {"platform": platform.platform(), "cpus": os.cpu_count(), "jobs": jobs},
              "targets": list(GROUPS[group])}
    started = time.monotonic()

    def phase(name, operation):
        begin = time.monotonic()
        try:
            operation()
        finally:
            report["phases"].append({"name": name, "seconds": time.monotonic() - begin})

    def command(*args):
        sdk = ROOT / ".deps/emsdk"
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EM_CACHE=str(sdk / "upstream/emscripten/cache"), EMSDK_PYTHON=sys.executable)
        env["PATH"] = str(ROOT / ".venv/bin") + os.pathsep + env.get("PATH", "")
        subprocess.run(list(map(str, args)), cwd=ROOT, env=env, check=True)

    try:
        phase("configure", lambda: command(sys.executable, ROOT / "scripts/build.py",
                                           "--configure-only"))
        if GROUPS[group]:
            phase("build", lambda: command(ROOT / ".venv/bin/cmake", "--build", ROOT / "build/browser",
                                            "--target", *GROUPS[group], "-j", jobs))
        if group in UNIT_GROUPS or group in LINKED_TESTS:
            phase("tests", lambda: run_tests(group, report))
        if group == "gameplay":
            phase("gameplay-check", lambda: command(sys.executable, ROOT / "scripts/check_gameplay.py"))
        if group == "unit-0":
            phase("source-census", lambda: command(sys.executable, ROOT / "scripts/gameplay_census.py", "--jobs", jobs))
        report["status"] = "passed"
    except BaseException:
        report["status"] = "failed"
        raise
    finally:
        usage = resource.getrusage(resource.RUSAGE_CHILDREN)
        report.update(seconds=time.monotonic() - started,
                      child_cpu_seconds=usage.ru_utime + usage.ru_stime,
                      child_maxrss_bytes=usage.ru_maxrss * (1 if sys.platform == "darwin" else 1024),
                      ninja=ninja_timings(ROOT / "build/browser/.ninja_log"))
        (directory / f"{group}.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({key: value for key, value in report.items() if key not in {"tests", "ninja"}}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", choices=GROUPS, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    run_group(args.group, args.jobs)


if __name__ == "__main__":
    main()
