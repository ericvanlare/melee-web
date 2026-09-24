"""Prefix replay and source-address driver checks for allocation history."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.allocation_history_replay import (
    ModelDriver, ReplayProblem, load_profile, load_trace, parse_u32, replay, validate_boot_observation,
)


REPLAY = ROOT / "tools/allocation_history_replay.py"
MODEL = ROOT / "tests/allocation_history_model.cpp"
IMPL = ROOT / "src/source_address_context.cpp"
HANDLE_IMPL = ROOT / "src/source_handle_context.cpp"
ARAM_IMPL = ROOT / "src/source_aram_context.cpp"


class AllocationHistoryReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cxx = shutil.which("c++")
        if not cls.cxx:
            raise unittest.SkipTest("native C++ compiler unavailable")
        cls.temp = tempfile.TemporaryDirectory(prefix="allocation history replay ")
        cls.root = Path(cls.temp.name)
        cls.driver = cls.root / "allocation-model"
        result = subprocess.run(
            [cls.cxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-Isrc",
             str(IMPL), str(HANDLE_IMPL), str(ARAM_IMPL), str(MODEL), "-o", str(cls.driver)],
            cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_profile_versions_accept_legacy_callback_and_demo_coverage_only(self):
        path = self.root / "profile-version.json"
        for version in (1, 2, 3, 4, 3.0, True, None, "2"):
            with self.subTest(version=version):
                path.write_text(json.dumps({
                    "schema": "melee-web-original-allocation-profile",
                    "version": version, "functions": [],
                }))
                if type(version) is int and version in (1, 2, 3):
                    self.assertEqual(load_profile(path)["version"], version)
                else:
                    with self.assertRaises(ReplayProblem):
                        load_profile(path)

    def profile(self):
        profile = {
            "schema": "melee-web-original-allocation-profile",
            "version": 1,
            "dol_sha1": "0" * 40,
            "source_revision": "synthetic-source-context-test",
            "symbols_sha256": "0" * 64,
            "initial_dol_words": {},
            "functions": [{"name": name} for name in (
                "HSD_OSInit", "OSInitAlloc", "OSCreateHeap", "OSSetCurrentHeap",
                "HSD_ObjSetHeap", "HSD_ObjAllocInit", "HSD_ObjAllocAddFree",
                "HSD_ObjAlloc", "HSD_MemAlloc", "OSAllocFromHeap")],
        }
        path = self.root / "profile.json"
        path.write_text(json.dumps(profile, sort_keys=True))
        return path

    @staticmethod
    def _row(record, sequence, **fields):
        return {"record": record, "sequence": sequence, **fields}

    def trace(self, *, bad_heap=False, unsupported=False):
        # Arena roots: descriptors occupy 0x30 bytes, then round up to 0x1040;
        # audio is [0x1040,0x2040) and main is [0x2040,0x100000).
        # All addresses below are observations;
        # the replay derives these same identities from the explicit context.
        rows = [self._row("header", 0, schema="melee-web-original-allocation-history",
                          version=1, profile_sha256=hashlib.sha256(
                              self.profile().read_bytes()).hexdigest(), scope="boot_only")]
        sequence = 1
        call = 0

        def enter(function, args, parent=None):
            nonlocal sequence, call
            value = {"record": "enter", "sequence": sequence, "call": call,
                     "function": function, "args": args, "parent": parent,
                     "thread": 0, "sp": 0x80001000, "lr": 0x80002000}
            rows.append(value)
            sequence += 1
            result = call
            call += 1
            return result

        def ret(call_id, function, result, observed=None):
            nonlocal sequence
            rows.append({"record": "return", "sequence": sequence, "call": call_id,
                         "function": function, "thread": 0, "result": result,
                         "observed": observed or {}})
            sequence += 1

        hsd = enter("HSD_OSInit", [])
        init = enter("OSInitAlloc", [0x1000, 0x100000, 4], hsd)
        ret(init, "OSInitAlloc", 0x1040)
        first = enter("OSCreateHeap", [0x1040, 0x2040], hsd)
        ret(first, "OSCreateHeap", 0)
        second_args = [0x2040, 0x100000]
        second = enter("OSCreateHeap", ([0x2050, 0x100000] if bad_heap else second_args), hsd)
        ret(second, "OSCreateHeap", 1)
        select = enter("OSSetCurrentHeap", [1], hsd)
        ret(select, "OSSetCurrentHeap", 0xFFFFFFFF)
        objheap = enter("HSD_ObjSetHeap", [0xFDFC0, 0], hsd)
        ret(objheap, "HSD_ObjSetHeap", 0)
        ret(hsd, "HSD_OSInit", 0)
        pool = enter("HSD_ObjAllocInit", [0x9000, 8, 4])
        ret(pool, "HSD_ObjAllocInit", 3,
            {"pool_words": [0, 0, 0, 0, 0, 0, 0, 0, 8, 3, 0]})
        obj = enter("HSD_ObjAlloc", [0x9000])
        refill = enter("HSD_ObjAllocAddFree", [0x9000, 1], obj)
        mem = enter("HSD_MemAlloc", [8], refill)
        raw = enter("OSAllocFromHeap", [1, 8], mem)
        ret(raw, "OSAllocFromHeap", 0x2060)
        ret(mem, "HSD_MemAlloc", 0x2060)
        ret(refill, "HSD_ObjAllocAddFree", 1,
            {"pool_words": [0, 0, 0, 1, 0, 0xFFFFFFFF, 0, 0xFFFFFFFF, 8, 3, 0]})
        ret(obj, "HSD_ObjAlloc", 0x2060,
            {"pool_words": [0, 0, 1, 0, 1, 0xFFFFFFFF, 0, 0xFFFFFFFF, 8, 3, 0]})
        ret(hsd, "HSD_OSInit", 0)  # intentionally malformed duplicate is removed below
        rows.pop()
        sequence -= 1
        pending_calls = []
        if unsupported:
            bad = enter("HSD_CreateMainHeap", [0x2030, 0x100000])
            pending_calls.append(bad)
        end = {"record": "end", "sequence": sequence,
               "status": "incomplete" if unsupported else "captured", "calls": call}
        if pending_calls:
            end["pending_calls"] = pending_calls
        rows.append(end)
        return rows

    def run_replay(self, trace, profile, *extra):
        path = self.root / "trace.jsonl"
        path.write_text("\n".join(json.dumps(row, sort_keys=True) for row in trace) + "\n")
        command = ["python3", str(REPLAY), "--trace", str(path), "--profile", str(profile),
                   "--arena-lo", "0x1000", "--arena-hi", "0x100000",
                   "--heap-max-num", "4", "--audio-heap-size", "0x1000", *extra]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        self.assertTrue(result.stdout, result.stderr)
        return result.returncode, json.loads(result.stdout)

    def test_native_model_rejects_refill_on_different_selected_heap(self):
        commands = "\n".join([
            '{"op":"heap_create","heap":1,"begin":4096,"end":131072}',
            '{"op":"heap_create","heap":2,"begin":262144,"end":393216}',
            '{"op":"pool_init","pool":42,"heap":1,"size":21,"align":4,"dedicated":0,"number_limit":0,"heap_limit":0}',
            '{"op":"pool_alloc","pool":42,"heap":1}',
            '{"op":"pool_alloc","pool":42,"heap":2}',
        ]) + "\n"
        result = subprocess.run([str(self.driver)], cwd=ROOT, input=commands,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(rows[-2]["address"], 4128)
        self.assertEqual(rows[-1]["status"], "unsupported_configuration")

    def test_native_and_checked_wasm_model_drivers_agree(self):
        if not (ROOT / ".deps/emsdk/upstream/emscripten/em++.py").is_file():
            self.skipTest("checked Wasm toolchain unavailable")
        commands = [
            '{"op":"heap_create","heap":1,"begin":4096,"end":131072}',
            '{"op":"heap_create","heap":2,"begin":262144,"end":393216}',
            '{"op":"pool_init","pool":42,"heap":1,"size":21,"align":4,"dedicated":0,"number_limit":0,"heap_limit":0}',
            '{"op":"pool_alloc","pool":42,"heap":1}',
            '{"op":"pool_alloc","pool":42,"heap":2}',
        ]
        native = ModelDriver().run(commands)
        wasm = ModelDriver(wasm=True).run(commands)
        self.assertEqual(native, wasm)

    def test_native_model_replays_lbmemory_handle_identity_and_payload(self):
        # Deliberately relocated synthetic addresses: retail DOL roots must
        # only enter through independently-derived boot context, never through
        # a portable model fixture copied from a capture.
        allocator = 0x11000000
        mem_entries = allocator + 0x8
        heap_handles = allocator + 0x638
        current_slot = allocator + 0x69C
        arena_lo = 0x00200000
        arena_hi = 0x00300000
        commands = "\n".join([
            f'{{"op":"handle_init","allocator":{allocator},"mem_entries":{mem_entries},"heap_handles":{heap_handles},"current_handle_slot":{current_slot},"arena_lo":{arena_lo},"arena_hi":{arena_hi}}}',
            '{"op":"handle_destroy_current"}',
            f'{{"op":"handle_current_new","label":"h1","lo":{arena_lo},"hi":{arena_hi}}}',
            '{"op":"handle_alloc","owner":"h1","label":"m1","payload":"p1","requested":1056}',
        ]) + "\n"
        result = subprocess.run([str(self.driver)], cwd=ROOT, input=commands,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(rows[2]["handle"], heap_handles)
        self.assertEqual(rows[3]["handle"], mem_entries)
        self.assertEqual(rows[3]["payload"], arena_lo)
        self.assertEqual(rows[3]["size"], 1056)

    def test_trace_rejects_noncontiguous_or_misnested_streams(self):
        path = self.root / "invalid-trace.jsonl"
        path.write_text("\n".join([
            json.dumps({"record": "header", "sequence": 0,
                        "schema": "melee-web-original-allocation-history", "version": 1}),
            json.dumps({"record": "enter", "sequence": 2, "call": 1,
                        "function": "HSD_OSInit", "thread": 1, "parent": None}),
        ]) + "\n")
        with self.assertRaises(ReplayProblem):
            load_trace(path)
        path.write_text("\n".join([
            json.dumps({"record": "header", "sequence": 0,
                        "schema": "melee-web-original-allocation-history", "version": 1}),
            json.dumps({"record": "enter", "sequence": 1, "call": 1,
                        "function": "HSD_OSInit", "thread": 1, "parent": None}),
            json.dumps({"record": "enter", "sequence": 2, "call": 2,
                        "function": "OSInitAlloc", "thread": 1, "parent": 1}),
            json.dumps({"record": "return", "sequence": 3, "call": 1,
                        "function": "HSD_OSInit", "thread": 1, "result": 0}),
        ]) + "\n")
        with self.assertRaises(ReplayProblem):
            load_trace(path)
        path.write_text("\n".join([
            json.dumps({"record": "header", "sequence": 0,
                        "schema": "melee-web-original-allocation-history", "version": 1}),
            json.dumps({"record": "enter", "sequence": 1, "call": 1,
                        "function": "HSD_OSInit", "thread": 1, "parent": None}),
            json.dumps({"record": "enter", "sequence": 2, "call": 2,
                        "function": "OSInitAlloc", "thread": 1, "parent": None}),
        ]) + "\n")
        with self.assertRaises(ReplayProblem):
            load_trace(path)
        path.write_text("\n".join([
            json.dumps({"record": "header", "sequence": 0,
                        "schema": "melee-web-original-allocation-history", "version": 1}),
            json.dumps({"record": "enter", "sequence": 1, "call": 1,
                        "function": "HSD_OSInit", "thread": 1, "parent": None}),
            json.dumps({"record": "end", "sequence": 2, "status": "captured", "calls": 1}),
        ]) + "\n")
        with self.assertRaises(ReplayProblem):
            load_trace(path)
        with self.assertRaises(ReplayProblem):
            parse_u32(1.5, "synthetic")

    def test_replays_independent_boot_prefix_and_reports_reset_boundary(self):
        profile = self.profile()
        _, report = self.run_replay(self.trace(unsupported=True), profile)
        self.assertEqual(report["status"], "incomplete_prefix")
        self.assertFalse(report["complete"])
        self.assertEqual(report["first_unsupported"]["function"], "HSD_CreateMainHeap")
        self.assertGreaterEqual(report["pointer_aliases"], 2)

    def test_observed_heap_bounds_cannot_define_roots(self):
        profile = self.profile()
        code, report = self.run_replay(self.trace(bad_heap=True), profile)
        self.assertEqual(code, 2)
        self.assertEqual(report["status"], "validation")
        self.assertIn("OSCreateHeap bounds", report["error"])

    def test_retail_api_rejects_unverified_numeric_context(self):
        path = self.profile()
        value = json.loads(path.read_text())
        value["source_revision"] = "retail-source-revision"
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(ReplayProblem, "independently derived boot context"):
            replay(self.root / "unused.jsonl", path, None,
                   {"arena_lo": 0x1000, "arena_hi": 0x100000}, None, None)

    def test_boot_observations_only_validate_independent_roots(self):
        boot = {"arena_lo": 0, "arena_hi": 0x11000000,
                "bi2": 0x10FFE000, "memory_size": 0x1800000}
        header = {"start": "original_dol_entry", "writes_game_state": False,
                  "initial_pc": 0x10203040, "observed_boot_context": dict(boot)}
        validate_boot_observation(header, boot, 0x10203040)
        for key in boot:
            altered = {**header, "observed_boot_context": {**boot, key: boot[key] + 32}}
            with self.subTest(key=key), self.assertRaises(ReplayProblem):
                validate_boot_observation(altered, boot, 0x10203040)
        for key, value in (("start", "savestate"), ("writes_game_state", True),
                           ("initial_pc", 0x10203044)):
            with self.subTest(key=key), self.assertRaises(ReplayProblem):
                validate_boot_observation({**header, key: value}, boot, 0x10203040)

    def test_repeated_debugger_stop_is_validated_and_not_replayed(self):
        profile = self.profile()
        rows = self.trace(unsupported=True)
        original = rows[1]
        repeated = {"record": "repeated_stop", "sequence": 2,
                    "original_sequence": original["sequence"], "function": "HSD_OSInit",
                    "phase": "enter", "machine": original.get("machine"), "observed": original.get("observed")}
        for row in rows:
            if row["sequence"] >= repeated["sequence"]:
                row["sequence"] += 1
        rows.insert(2, repeated)
        _, report = self.run_replay(rows, profile)
        self.assertEqual(report["status"], "incomplete_prefix")
        self.assertEqual(report["first_unsupported"]["function"], "HSD_CreateMainHeap")


if __name__ == "__main__":
    unittest.main()
