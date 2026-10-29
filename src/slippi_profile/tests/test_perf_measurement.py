# SPDX-License-Identifier: GPL-3.0-or-later
"""Source-backed native control for the private HSD measurement owner."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[3]
PROFILE = ROOT / "src/slippi_profile"
COMPOSED = ROOT / "build/gameplay-source-slippi-profile/src"
BASE = ROOT / "build/gameplay-source/src"
FEATURE = "MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE"

BASE_SOURCE_PINS = {
    "sysdolphin/baselib/perf.c": "bc606b1e1edd536ee80059942a9e7b8296af2ef8cc35908d0114e38e5c058479",
    "sysdolphin/baselib/perf.h": "3292eb80497f76fdc0e7065ded0fe2646becb4294233dac053cee6e3c59875e6",
    "sysdolphin/baselib/hsd_392A.c": "53668f9e531b334b72093fd255ceeabe63ce6d5afcb37fb00e70b1dbe4cd6b56",
}
PROFILE_COMPOSED_SOURCE_PINS = {
    "sysdolphin/baselib/pobj.c": "9d8f99b06d96fd2ff719ed2bd98116db05a7bffc183289ca09888928cc776079",
    "sysdolphin/baselib/shadow.c": "63e69afcc3c470b4c38d7880a1fa666c95ada9d3a5fd8541f06976669ce2fab4",
    "melee/ft/ftparts.c": "9db8fb8c2b6eb53309015b78e41fcbdef7e0aa4216596a300f7b6e64c90eeed7",
}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_function(source, signature):
    if source.count(signature) != 1:
        raise AssertionError(f"expected one source function {signature!r}")
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1] + "\n"
    raise AssertionError(f"unterminated source function {signature!r}")


def source_counter_branch(source):
    marker = "current->nb_mtx_load++;"
    if source.count(marker) != 1:
        raise AssertionError("expected the one direct lbshadow counter in configured source")
    end = source.index(marker) + len(marker)
    start = source.rfind(f"#if defined({FEATURE})", 0, end)
    if start < 0:
        raise AssertionError("direct lbshadow counter lacks its private source branch")
    lines = source[start:].splitlines(keepends=True)
    depth = 0
    consumed = []
    for line in lines:
        directive = line.lstrip()
        if directive.startswith("#if"):
            depth += 1
        elif directive.startswith("#endif"):
            depth -= 1
        consumed.append(line)
        if depth == 0:
            break
    branch = "".join(consumed)
    if marker not in branch or "HSD_PerfCurrentStat.nb_mtx_load++;" not in branch:
        raise AssertionError("direct source counter no longer contains both private and ordinary paths")
    return branch


PLATFORM_STUB = r'''#ifndef PERF_FIXTURE_PLATFORM_H
#define PERF_FIXTURE_PLATFORM_H
#include <stddef.h>
#include <stdint.h>
typedef float f32;
typedef double f64;
typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;
#define ATTRIBUTE_NORETURN __attribute__((noreturn))
#endif
'''

OS_STUB = r'''#ifndef PERF_FIXTURE_OS_H
#define PERF_FIXTURE_OS_H
#include <Runtime/platform.h>
typedef struct OSContext { uintptr_t opaque[8]; } OSContext;
typedef s64 OSTime;
OSTime OSGetTime(void);
u64 OSSecondsToTicks(u32 seconds);
#endif
'''


class PerfMeasurementTests(unittest.TestCase):
    def test_original_source_and_private_measurement_lanes(self):
        melee = ROOT / ".deps/melee"
        if not melee.is_dir() or not COMPOSED.is_dir() or not BASE.is_dir():
            self.skipTest("configured Melee source checkout is unavailable")
        compiler = shlex.split(os.environ.get("CC", "cc"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("native C compiler is unavailable")

        for relative, expected in BASE_SOURCE_PINS.items():
            path = BASE / relative
            self.assertTrue(path.is_file(), f"missing pinned source {path}")
            self.assertEqual(sha256(path), expected, f"source pin changed: {relative}")
        for relative, expected in PROFILE_COMPOSED_SOURCE_PINS.items():
            path = COMPOSED / relative
            self.assertTrue(path.is_file(), f"missing pinned composed source {path}")
            self.assertEqual(sha256(path), expected, f"composed source pin changed: {relative}")

        perf_c = COMPOSED / "sysdolphin/baselib/perf.c"
        hsd_392a = COMPOSED / "sysdolphin/baselib/hsd_392A.c"
        lbshadow = COMPOSED / "melee/lb/lbshadow.c"
        self.assertTrue(perf_c.is_file() and hsd_392a.is_file() and lbshadow.is_file())
        self.assertIn("slippi_hsd_perf_measurement_current()", perf_c.read_text())
        self.assertIn("slippi_hsd_perf_measurement_last()", hsd_392a.read_text())
        self.assertIn("slippi_hsd_perf_measurement_current()", lbshadow.read_text())
        self.assertIn("HSD_PerfCountMtxLoad()", (COMPOSED / "sysdolphin/baselib/pobj.c").read_text())
        self.assertIn("HSD_PerfCountMtxLoad()", (COMPOSED / "sysdolphin/baselib/shadow.c").read_text())
        self.assertIn("HSD_PerfCountMtxLoad()", (COMPOSED / "melee/ft/ftparts.c").read_text())

        # These are checked bootstrap/restore records over the gameplay heap;
        # the persistent owner has no reset hook in restore or disarm paths.
        bootstrap = (ROOT / "src/gameplay_bootstrap.c").read_text()
        self.assertLess(bootstrap.index("slippi_hsd_perf_measurement_world_begin"),
                        bootstrap.index("vs_startup_callback(error, error_size)"))
        self.assertEqual(bootstrap.count("slippi_hsd_perf_measurement_world_end"), 3)
        restore_adapter = (PROFILE / "tests/cpu_restore_adapter.inc").read_text()
        self.assertNotIn("slippi_hsd_perf_measurement_", restore_adapter)

        reader = source_function(hsd_392a.read_text(), "void fn_80392934(void)")
        direct_counter = source_counter_branch(lbshadow.read_text())
        template = (PROFILE / "tests/perf_measurement_control.c.in").read_text()
        fixture = template.replace("@HSD_392A_SOURCE_FUNCTION@", reader).replace(
            "@LBSHADOW_COUNTER_SOURCE@", direct_counter)
        self.assertNotIn("@HSD_392A_SOURCE_FUNCTION@", fixture)
        self.assertNotIn("@LBSHADOW_COUNTER_SOURCE@", fixture)

        external_evidence = os.environ.get("SLIPPI_PERF_MEASUREMENT_EVIDENCE")
        if external_evidence:
            evidence_root = Path(external_evidence).resolve()
            evidence_root.mkdir(parents=True, exist_ok=True)
            output = Path(tempfile.mkdtemp(prefix=f"control-{time.time_ns()}-", dir=evidence_root))
        else:
            work = ROOT / "work"
            work.mkdir(exist_ok=True)
            output = Path(tempfile.mkdtemp(prefix="perf-measurement-", dir=work))
        (output / "fixture.c").write_text(fixture)
        (output / "platform.h").write_text(PLATFORM_STUB)
        (output / "os.h").write_text(OS_STUB)
        (output / "source-pins.json").write_text(json.dumps({
            "base": {relative: sha256(BASE / relative) for relative in BASE_SOURCE_PINS},
            "profile_composed_unchanged": {
                relative: sha256(COMPOSED / relative)
                for relative in PROFILE_COMPOSED_SOURCE_PINS
            },
            "profile_composed": {
                "perf.c": sha256(perf_c), "perf.h": sha256(COMPOSED / "sysdolphin/baselib/perf.h"),
                "hsd_392A.c": sha256(hsd_392a), "lbshadow.c": sha256(lbshadow),
            },
            "control_template": sha256(PROFILE / "tests/perf_measurement_control.c.in"),
        }, indent=2) + "\n")

        def command(name, args, binary=False):
            wrapped = [os.environ.get("PYTHON", "python3"),
                       str(ROOT / "scripts/agent_workspace.py"), "run", "--", *args]
            (output / f"{name}.command.json").write_text(json.dumps(wrapped, indent=2) + "\n")
            try:
                result = subprocess.run(wrapped, cwd=ROOT, capture_output=True,
                                        timeout=30, check=False)
            except Exception as error:
                (output / f"{name}.error.txt").write_text(str(error) + "\n")
                raise
            (output / f"{name}.stdout.bin" if binary else output / f"{name}.stdout.txt").write_bytes(result.stdout)
            (output / f"{name}.stderr.txt").write_bytes(result.stderr)
            (output / f"{name}.exit.txt").write_text(f"{result.returncode}\n")
            self.assertEqual(result.returncode, 0,
                             f"{name} failed with {result.returncode}; retained output: {output}")
            return result.stdout

        try:
            stubs = output / "stubs"
            (stubs / "Runtime").mkdir(parents=True)
            (stubs / "dolphin").mkdir()
            shutil.copyfile(output / "platform.h", stubs / "Runtime/platform.h")
            shutil.copyfile(output / "os.h", stubs / "dolphin/os.h")
            include_args = ["-I", str(stubs), "-I", str(output),
                            "-I", str(COMPOSED), "-I", str(ROOT / "src")]
            common = [*compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O0",
                      "-ffp-contract=off", *include_args, str(perf_c), str(output / "fixture.c")]
            for lane, private in (("ordinary", False), ("private", True)):
                executable = output / lane
                sources = list(common)
                if private:
                    sources.insert(len(compiler), f"-D{FEATURE}=1")
                    sources.append(str(PROFILE / "perf_measurement.c"))
                sources.extend(["-o", str(executable)])
                command(f"compile-{lane}", sources)
                output_bytes = command(f"run-{lane}", [str(executable)], binary=True)
                (output / f"{lane}.output.sha256").write_text(
                    hashlib.sha256(output_bytes).hexdigest() + "\n")
                if lane == "ordinary":
                    ordinary_output = output_bytes
                else:
                    self.assertEqual(output_bytes, ordinary_output,
                                     f"private metrics differ from original source; retained output: {output}")
                    command("run-private-canonical-drift", [str(executable), "drift"])
            if external_evidence:
                (output / "result.json").write_text(json.dumps({
                    "result": "passed",
                    "scope": "actual perf.c, perf.h inline, lbshadow counter branch and hsd_392A reader; deterministic OS clock leaves",
                    "private_vs_ordinary_output_sha256": hashlib.sha256(ordinary_output).hexdigest(),
                    "overlay_runtime": "not exercised; extracted Last reader only",
                    "browser_or_gameplay_claim": False,
                }, indent=2) + "\n")
            else:
                shutil.rmtree(output)
        except Exception:
            # Failed source, compile and runtime evidence stays available.
            raise


if __name__ == "__main__":
    unittest.main()
