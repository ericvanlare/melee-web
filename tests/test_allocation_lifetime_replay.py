"""Synthetic, independently rooted tests for the allocation lifetime replay."""
from __future__ import annotations

import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools.allocation_history_replay import ReplayProblem
from tools.allocation_lifetime_replay import (
    replay_lifetimes, validate_layout, validate_metadata_shape,
)


class AllocationLifetimeReplayTests(unittest.TestCase):
    """Exercise the public replay boundary with hand-derived source identities."""

    CONTEXT = {
        "arena_lo": 0x1000,
        "arena_hi": 0x20000,
        "heap_max_num": 2,
        "audio_heap_size": 0x1000,
        "arena_start": 0x1020,
        "arena_end": 0x20000,
        "audio_begin": 0x1020,
        "audio_end": 0x2020,
        "main_begin": 0x2020,
        "main_end": 0x20000,
        "aram_base": 0x300000,
        "aram_size": 0x400000,
    }

    # These are the pinned declaration arities, not values inferred from the
    # synthetic rows.  Keeping them here catches a trace that silently omits
    # an argument before it reaches the model.
    ARITIES = {
        "HSD_OSInit": 0,
        "OSInitAlloc": 3,
        "OSCreateHeap": 2,
        "OSSetCurrentHeap": 1,
        "HSD_MemAlloc": 1,
        "OSAllocFromHeap": 2,
        "ARInit": 2,
        "lbMemory_8001564C": 0,
        "lbMemory_80014E24": 2,
        "lbHeap_80015F3C": 0,
        "lbHeap_80015900": 0,
        "HSD_CreateMainHeap": 2,
        "_HSD_ObjAllocForgetMemory": 2,
        "OSDestroyHeap": 1,
        "HSD_ObjSetHeap": 2,
        "HSD_ObjAllocInit": 3,
        "lbHeap_80015BD0": 2,
        "lbMemory_80014FC8": 2,
        "lbHeap_80015CA8": 2,
        "lbMemFreeToHeap": 2,
    }

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="allocation lifetime replay ")
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def profile(self, *, current_heap=0xFFFFFFFF):
        value = {
            "schema": "melee-web-original-allocation-profile",
            "version": 1,
            "dol_sha1": "0" * 40,
            "source_revision": "synthetic-lifetime-source",
            "initial_dol_words": {
                "__OSArenaLo": self.CONTEXT["arena_start"],
                "current_heap": current_heap,
            },
            "functions": [
                {"name": name, "argc": argc}
                for name, argc in self.ARITIES.items()
            ],
        }
        path = self.root / "profile.json"
        path.write_text(json.dumps(value, sort_keys=True))
        return path, value

    def verified(self, *, pools=None):
        return {
            "static_layout": {
                "pool_descriptors": pools or {},
                # The first descriptor is an independently declared game heap
                # root used only by the replacement fixture.  The sentinel is
                # intentionally present and is checked by the replay itself.
                "lbheap_descriptors": [[2, 1, 6, 0x1000], [6, 0, 0, 0]],
                "aram_stack_table": {"address": 0x4000, "size": 0x40},
                "lbmemory_allocator": 0x5000,
            },
            "boot": {"arena_hi": self.CONTEXT["arena_hi"]},
            "stages": {
                "linker_arena_lo": self.CONTEXT["arena_lo"],
                "os_arena_lo": self.CONTEXT["arena_lo"],
                "after_xfb": self.CONTEXT["arena_lo"],
                "os_init_alloc_lo": self.CONTEXT["arena_start"],
                "crash_allocation_size": 0x20,
                "crash_allocation_alignment": 0x20,
                "crash_allocation_base": self.CONTEXT["arena_start"],
                "after_crash": self.CONTEXT["arena_start"] + 0x20,
                "framebuffer_count": 2,
                "fifo_size": 0x1000,
            },
        }

    @staticmethod
    def _row(record, sequence, **fields):
        return {"record": record, "sequence": sequence, **fields}

    def _stream(self):
        rows = [self._row("header", 0, schema="synthetic-lifetime", version=1)]
        enters = {}
        returns = {}
        sequence = 1
        call_id = 0

        def enter(function, args, parent=None):
            nonlocal sequence, call_id
            call = call_id
            enters[call] = {
                "record": "enter", "sequence": sequence, "call": call,
                "function": function, "args": list(args), "parent": parent,
                "thread": 0,
            }
            rows.append(enters[call])
            sequence += 1
            call_id += 1
            return call

        def ret(call, result=0, observed=None):
            nonlocal sequence
            row = self._row("return", sequence, call=call,
                            function=enters[call]["function"], result=result,
                            observed=observed or {}, thread=0)
            rows.append(row)
            returns[call] = row
            sequence += 1

        hsd = enter("HSD_OSInit", [])
        alloc = enter("OSInitAlloc", [0x1000, 0x20000, 2], hsd)
        ret(alloc, self.CONTEXT["arena_start"])
        audio = enter("OSCreateHeap", [0x1020, 0x2020], hsd)
        ret(audio, 0)
        main = enter("OSCreateHeap", [0x2020, 0x20000], hsd)
        ret(main, 1)
        select = enter("OSSetCurrentHeap", [1], hsd)
        ret(select, 0xFFFFFFFF)
        ret(hsd)

        mem = enter("HSD_MemAlloc", [64])
        raw = enter("OSAllocFromHeap", [1, 64], mem)
        # Manual source-address calculation: a 64-byte request occupies a
        # 96-byte cell (32-byte header plus the 64-byte payload), so its
        # payload starts at 0x2020 + 0x20 = 0x2040.
        ret(raw, 0x2040)
        ret(mem, 0x2040)
        rows.append(self._row("end", sequence, status="captured", calls=call_id))
        return rows, enters, returns

    def _replacement_stream(self, *, include_game_io=False):
        rows = [self._row("header", 0, schema="synthetic-lifetime", version=1)]
        enters = {}
        returns = {}
        sequence = 1
        call_id = 0

        def enter(function, args, parent=None):
            nonlocal sequence, call_id
            call = call_id
            enters[call] = {
                "record": "enter", "sequence": sequence, "call": call,
                "function": function, "args": list(args), "parent": parent,
                "thread": 0,
            }
            rows.append(enters[call])
            sequence += 1
            call_id += 1
            return call

        def ret(call, result=0, observed=None):
            nonlocal sequence
            rows.append(self._row("return", sequence, call=call,
                                  function=enters[call]["function"], result=result,
                                  observed=observed or {}, thread=0))
            returns[call] = rows[-1]
            sequence += 1

        hsd = enter("HSD_OSInit", [])
        alloc = enter("OSInitAlloc", [0x1000, 0x20000, 2], hsd)
        ret(alloc, 0x1020)
        audio = enter("OSCreateHeap", [0x1020, 0x2020], hsd)
        ret(audio, 0)
        main = enter("OSCreateHeap", [0x2020, 0x20000], hsd)
        ret(main, 1)
        select = enter("OSSetCurrentHeap", [1], hsd)
        ret(select, 0xFFFFFFFF)
        ret(hsd)

        ar = enter("ARInit", [0x4000, 16])
        ret(ar, 0x300000, {"aram": {
            "__AR_BlockLength": 0x4000,
            "__AR_FreeBlocks": 16,
            "__AR_Size": 0x400000,
            "__AR_StackPointer": 0x300000,
            "__AR_init_flag": 1,
        }})
        memory = enter("lbMemory_8001564C", [])
        handle = enter("lbMemory_80014E24", [0x300000, 0x400000], memory)
        # allocator + 0x638 is the first source heap-handle descriptor.
        ret(handle, 0x5638, {"handle_words": [0, 0x300000, 0x400000, 0]})
        ret(memory)
        init = enter("lbHeap_80015F3C", [])
        ret(init)

        game = enter("lbHeap_80015900", [])
        replace = enter("HSD_CreateMainHeap", [0x2020, 0x20000], game)
        forget = enter("_HSD_ObjAllocForgetMemory", [0x2020, 0x20000], replace)
        ret(forget)
        destroy = enter("OSDestroyHeap", [1], replace)
        ret(destroy)
        create = enter("OSCreateHeap", [0x2020, 0x20000], replace)
        ret(create, 1)
        select_new = enter("OSSetCurrentHeap", [1], replace)
        ret(select_new, 1)
        object_heap = enter("HSD_ObjSetHeap", [0x1DFE0, 0], replace)
        ret(object_heap)
        ret(replace, 1)
        # The source game-heap state machine completes the replacement
        # request, then destroys and recreates the current ARAM handle before
        # the outer frame can finish.  The pushed descriptor is deliberately
        # the same 0x5638 identity, computed from the explicit allocator root.
        destroy_current = enter("lbMemory_800155A4", [], game)
        ret(destroy_current)
        new_current = enter("lbMemory_800154D4", [0x300000, 0x400000], game)
        ret(new_current, 0x5638,
            {"handle_words": [0, 0x300000, 0x400000, 0]})
        if include_game_io:
            allocate = enter("lbHeap_80015BD0", [1, 64], game)
            allocate_handle = enter("lbMemory_80014FC8", [0x5638, 64], allocate)
            ret(allocate_handle, 0x5008,
                {"handle_words": [0, 0x300000, 64]})
            ret(allocate, 0x300000)
            release = enter("lbHeap_80015CA8", [1, 0x300000], game)
            release_handle = enter("lbMemFreeToHeap", [0x5638, 0x300000], release)
            ret(release_handle)
            ret(release)
        ret(game)
        rows.append(self._row("end", sequence, status="captured", calls=call_id))
        return rows, enters, returns

    def _run(self, rows, enters, returns, *, profile, verified,
             checked_wasm=False):
        profile_path, profile_value = self.profile(current_heap=profile)
        trace_path = self.root / "trace.jsonl"
        trace_path.write_text("\n".join(json.dumps(row, sort_keys=True) for row in rows) + "\n")
        return replay_lifetimes(
            trace_path, profile_path, profile_value, rows[0], rows,
            enters, returns, self.CONTEXT, verified,
            checked_wasm=checked_wasm, require_complete=False,
        )

    def test_layout_rejects_duplicate_pool_identities(self):
        verified = self.verified(pools={"first": 0x9000, "duplicate": 0x9000})
        with self.assertRaisesRegex(ReplayProblem, "duplicated"):
            validate_layout(verified)

    def test_layout_rejects_malformed_or_out_of_order_game_descriptors(self):
        cases = (
            [[3, 1, 2, 0x1000], [6, 0, 0, 0]],
            [[2, 1, 6, 0x1000, 0], [6, 0, 0, 0]],
            [[2, 1, 6, 0x1000], [2, 2, 2, 0x100], [6, 0, 0, 0]],
        )
        for descriptors in cases:
            with self.subTest(descriptors=descriptors), self.assertRaisesRegex(
                    ReplayProblem, "descriptor"):
                validate_layout({**self.verified(), "static_layout": {
                    **self.verified()["static_layout"],
                    "lbheap_descriptors": descriptors,
                }})

    def test_retail_metadata_requires_expected_fields_and_lengths(self):
        call = {"call": 17, "sequence": 3, "function": "HSD_ObjAllocInit"}
        missing = {"record": "return", "result": 0, "observed": {}}
        wrong_length = {"record": "return", "result": 0,
                        "observed": {"pool_words": [0] * 10}}
        with self.assertRaisesRegex(ReplayProblem, "11-word pool_words"):
            validate_metadata_shape(missing, call, required=True)
        with self.assertRaisesRegex(ReplayProblem, "11-word pool_words"):
            validate_metadata_shape(wrong_length, call, required=True)

    def test_native_source_mismatch_survives_checked_wasm_failure(self):
        rows, enters, returns = self._stream()
        raw_return = next(row for row in rows
                          if row.get("record") == "return" and row.get("call") == 6)
        raw_return["result"] = 0xDEADBEEF

        class FakeStreamModel:
            def __init__(self):
                self.actions = []
                self.outputs = []

            def run(self, command, call):
                self.actions.append({"call": call.get("call"),
                                     "sequence": call.get("sequence"),
                                     "function": call.get("function"),
                                     "command": command})
                op = command["op"]
                if op == "bootstrap_heap":
                    output = {"op": op, "status": "ok",
                              "result": command["index"],
                              "args": ([0x1020, 0x2020] if command["index"] == 0
                                       else [0x2020, 0x20000])}
                elif op == "os_select_hsd":
                    output = {"op": op, "status": "ok", "result": 0xFFFFFFFF,
                              "args": [1]}
                elif op == "raw_alloc":
                    output = {"op": op, "status": "ok", "address": 0x2040}
                else:
                    output = {"op": op, "status": "ok"}
                self.outputs.append(output)
                return output

            def close(self):
                pass

        class FakeWasmDriver:
            def __init__(self, *args, **kwargs):
                pass

            def run(self, commands):
                return [{"op": "checked-wasm", "status": "ok"}
                        for _ in commands]

            def close(self):
                pass

        with mock.patch("tools.allocation_lifetime_replay.StreamModel", FakeStreamModel), \
             mock.patch("tools.allocation_lifetime_replay.ModelDriver", FakeWasmDriver):
            report = self._run(rows, enters, returns, profile=0xFFFFFFFF,
                               verified=self.verified(), checked_wasm=True)
        self.assertEqual(report["status"], "validation")
        self.assertEqual(report["first_unsupported"]["function"], "OSAllocFromHeap")
        self.assertIsNotNone(report["checked_wasm_problem"])
        self.assertEqual(report["checked_wasm_problem"]["kind"], "validation")
        self.assertNotEqual(report["checked_wasm_problem"], report["first_unsupported"])

    def test_native_replay_uses_manual_pointer_identity_and_pool_reset(self):
        rows, enters, returns = self._stream()
        pool_enter = {
            "record": "enter", "sequence": rows[-1]["sequence"], "call": 99,
            "function": "HSD_ObjAllocInit", "args": [0x9000, 8, 4],
            "parent": None, "thread": 0,
        }
        pool_return = self._row("return", pool_enter["sequence"] + 1,
                                call=99, function="HSD_ObjAllocInit", result=0,
                                observed={}, thread=0)
        rows[-1]["sequence"] += 2
        rows[-1]["calls"] = 100
        rows[-1:-1] = [pool_enter, pool_return]
        enters[99] = pool_enter
        returns[99] = pool_return
        report = self._run(rows, enters, returns, profile=0xFFFFFFFF,
                           verified=self.verified(pools={"pool0": 0x9000}))
        self.assertEqual(report["status"], "validated_prefix")
        self.assertTrue(report["all_captured_calls_replayed"])
        self.assertIn(0x2040, [item["derived"] for item in report["derived_identities"]])
        self.assertTrue(any(action["command"]["op"] == "pool_reset"
                            for action in report["replay_actions"]))

    def test_changed_observed_pointer_fails_without_becoming_model_input(self):
        rows, enters, returns = self._stream()
        raw_return = next(row for row in rows
                          if row.get("record") == "return" and row.get("call") == 6)
        raw_return["result"] = 0xDEADBEEF
        report = self._run(rows, enters, returns, profile=0xFFFFFFFF,
                           verified=self.verified())
        self.assertEqual(report["status"], "validation")
        self.assertEqual(report["first_unsupported"]["function"], "OSAllocFromHeap")
        commands = [action["command"] for action in report["replay_actions"]]
        raw = next(command for command in commands if command["op"] == "raw_alloc")
        self.assertNotIn("address", raw)
        self.assertNotIn("result", raw)
        self.assertNotIn("DEADBEEF", json.dumps(report, sort_keys=True).upper())

    def test_changed_pool_descriptor_is_rejected_before_pool_reset(self):
        rows, enters, returns = self._stream()
        pool_enter = {
            "record": "enter", "sequence": rows[-1]["sequence"], "call": 99,
            "function": "HSD_ObjAllocInit", "args": [0x9004, 8, 4],
            "parent": None, "thread": 0,
        }
        pool_return = self._row("return", pool_enter["sequence"] + 1,
                                call=99, function="HSD_ObjAllocInit", result=0,
                                observed={}, thread=0)
        rows[-1]["sequence"] += 2
        rows[-1:-1] = [pool_enter, pool_return]
        enters[99] = pool_enter
        returns[99] = pool_return
        report = self._run(rows, enters, returns, profile=0xFFFFFFFF,
                           verified=self.verified(pools={"pool0": 0x9000}))
        self.assertEqual(report["status"], "unsupported")
        self.assertEqual(report["first_unsupported"]["function"], "HSD_ObjAllocInit")
        self.assertNotIn("pool_reset", [action["command"]["op"]
                                         for action in report["replay_actions"]])

    def test_replacement_lifecycle_rejects_changed_derived_bounds(self):
        rows, enters, returns = self._replacement_stream()
        report = self._run(rows, enters, returns, profile=1,
                           verified=self.verified())
        self.assertEqual(report["status"], "validated_prefix")
        self.assertTrue(any(action["command"]["op"] == "hsd_replace_begin"
                            for action in report["replay_actions"]))

        altered = copy.deepcopy(rows)
        altered_enters = copy.deepcopy(enters)
        replace_enter = next(row for row in altered_enters.values()
                             if row.get("function") == "HSD_CreateMainHeap")
        replace_enter["args"][0] += 0x20
        altered_report = self._run(altered, altered_enters, returns, profile=1,
                                   verified=self.verified())
        self.assertEqual(altered_report["status"], "validation")
        self.assertEqual(altered_report["first_unsupported"]["function"],
                         "HSD_CreateMainHeap")
        self.assertIn("replacement bounds", altered_report["first_unsupported"]["reason"])

    def test_game_handle_allocation_and_free_use_derived_payload_identity(self):
        rows, enters, returns = self._replacement_stream(include_game_io=True)
        report = self._run(rows, enters, returns, profile=1,
                           verified=self.verified())
        self.assertEqual(report["status"], "validated_prefix")
        commands = [action["command"] for action in report["replay_actions"]]
        self.assertTrue(any(command["op"] == "game_handle_alloc"
                            and command["index"] == 1
                            and command["requested"] == 64
                            for command in commands))
        self.assertTrue(any(command["op"] == "game_handle_free"
                            and command["index"] == 1
                            and command["label"] == "call_19"
                            for command in commands))
        self.assertIn(0x5008, [item["derived"]
                               for item in report["derived_identities"]])

        altered = copy.deepcopy(enters)
        release = next(call for call in altered.values()
                       if call["function"] == "lbMemFreeToHeap")
        release["args"][1] = 0xDEADBEEF
        altered_report = self._run(rows, altered, returns, profile=1,
                                   verified=self.verified())
        self.assertEqual(altered_report["status"], "unsupported")
        self.assertEqual(altered_report["first_unsupported"]["function"],
                         "lbMemFreeToHeap")
        self.assertIn("derived allocation producer",
                      altered_report["first_unsupported"]["reason"])
        self.assertNotIn("game_handle_free",
                         [action["command"]["op"]
                          for action in altered_report["replay_actions"]])

    def test_malformed_repeated_stop_is_explicit_stream_failure(self):
        rows, enters, returns = self._stream()
        original = rows[1]
        repeated = {
            "record": "repeated_stop", "sequence": 2,
            "original_sequence": original["sequence"], "phase": "return",
            "function": original["function"], "machine": "different",
            "observed": {},
        }
        for row in rows:
            if row["sequence"] >= repeated["sequence"]:
                row["sequence"] += 1
        rows.insert(2, repeated)
        report = self._run(rows, enters, returns, profile=0xFFFFFFFF,
                           verified=self.verified())
        self.assertEqual(report["status"], "stream")
        self.assertIn("repeated stop differs", report["first_unsupported"]["reason"])


if __name__ == "__main__":
    unittest.main()
