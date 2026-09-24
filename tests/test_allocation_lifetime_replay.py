"""Synthetic, independently rooted tests for the allocation lifetime replay."""
from __future__ import annotations

import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

# Keep the focused test importable when unittest invokes it by absolute path or
# from outside the repository root, as CI does with ``python -I``.
ROOT = Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0, str(ROOT))

from tools.compaction_manager_state import FIELDS
from tools.allocation_history_replay import ReplayProblem
from tools.allocation_history_replay import ModelDriver
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
        "HSD_ObjAllocInit": 3,
        "HSD_ObjAlloc": 1,
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
        "lbHeap_80015D6C": 3,
        "lbMemory_80014FC8": 2,
        "lbMemory_8001529C": 3,
        "lbHeap_80015CA8": 2,
        "lbMemFreeToHeap": 2,
        "fn_80015184": 2,
        "lbMemory_80015320": 4,
        "lbDvd_80017A80": 1,
        "HSD_DevComARAMCallback": 1,
        "HSD_DevComRequest": 8,
        "Fighter_FirstInitialize_80067A84": 0,
        "Fighter_Create": 1,
        "gm_Scene_Vs_OnEnter": 1,
        "gm_Scene_Vs_OnExit": 1,
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
                ({"name": name, "argc": argc, "address": {
                    "lbDvd_80017A80": 0x80017A80,
                    "lbMemory_80015320": 0x80015320,
                }[name]} if name in {"lbDvd_80017A80", "lbMemory_80015320"}
                 else {"name": name, "argc": argc})
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
                "lbmemory_initial_manager": dict.fromkeys(FIELDS, 0),
                "devcom_initial_request_counter": 4,
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

    def _stream(self, *, with_fighters=False, with_exit=True):
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
        if with_fighters:
            vs_enter = enter("gm_Scene_Vs_OnEnter", [0x7100])
            fighter_init = enter("Fighter_FirstInitialize_80067A84", [], vs_enter)
            fighter_pool = enter("HSD_ObjAllocInit", [0x9000, 0x100, 4], fighter_init)
            ret(fighter_pool, 0)
            ret(fighter_init, 0)

            create = enter("Fighter_Create", [0x7200], vs_enter)
            fighter_alloc = enter("HSD_ObjAlloc", [0x9000], create)
            ret(fighter_alloc, 0x3000)
            ret(create, 0x7300, {"fighter": {
                "gobj": 0x7300, "address": 0x3000, "slot": 0, "kind": 0x12,
            }})
            ret(vs_enter, 0, {"globals": {
                "seed_ptr": 0x804D5F90, "ArenaStart": 0x1000,
                "ArenaEnd": 0x20000, "HeapArray": 0x80400000, "NumHeaps": 2,
            }, "r2": 0x804DE00, "r13": 0x804D5F00})
            if with_exit:
                vs_exit = enter("gm_Scene_Vs_OnExit", [0x7400])
                ret(vs_exit, 0)
        rows.append(self._row("end", sequence, status="captured", calls=call_id))
        return rows, enters, returns

    def _replacement_stream(self, *, include_game_io=False, include_compaction=False):
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
        if include_compaction:
            first = enter("lbHeap_80015BD0", [1, 64], game)
            first_handle = enter("lbMemory_80014FC8", [0x5638, 64], first)
            ret(first_handle, 0x5008, {"handle_words": [0, 0x300000, 64]})
            ret(first, 0x300000)
            second = enter("lbHeap_80015BD0", [1, 64], game)
            second_handle = enter("lbMemory_80014FC8", [0x5638, 64], second)
            ret(second_handle, 0x5014, {"handle_words": [0, 0x300040, 64]})
            ret(second, 0x300040)
            release = enter("lbHeap_80015CA8", [1, 0x300000], game)
            release_handle = enter("lbMemFreeToHeap", [0x5638, 0x300000], release)
            ret(release_handle)
            ret(release)
            compact = enter("lbHeap_80015D6C", [1, 0x80017A80, 4], game)
            compact_call = enter("lbMemory_8001529C", [0x5638, 0x80017A80, 4], compact)
            move = enter("lbMemory_80015320", [0, 0x5014, 0, 0], compact_call)
            transfer = enter("HSD_DevComRequest",
                             [0, 0x300040, 0x300000, 64, 0x1B, 1, 0x80015320, 0], move)
            ret(transfer, 7)
            ret(move, 0)
            ret(compact_call, 1)
            ret(compact, 1)
            callback = enter("lbMemory_80015320", [7, 0, 0, 0])
            preload = enter("lbDvd_80017A80", [4], callback)
            nested_compact = enter("lbHeap_80015D6C", [1, 0x80017A80, 4], preload)
            nested_call = enter("lbMemory_8001529C", [0x5638, 0x80017A80, 4], nested_compact)
            ret(nested_call, 0)
            ret(nested_compact, 0)
            ret(preload, 0)
            ret(callback, 0)
            moved_release = enter("lbHeap_80015CA8", [1, 0x300000], game)
            moved_free = enter("lbMemFreeToHeap", [0x5638, 0x300000], moved_release)
            ret(moved_free)
            ret(moved_release)
        elif include_game_io:
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

    def _run_fighter(self, rows, enters, returns, *, verified=None):
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
                elif op == "pool_reset":
                    output = {"op": op, "status": "ok", "size": command["size"],
                              "align_mask": command["align"] - 1}
                elif op == "pool_pop":
                    output = {"op": op, "status": "ok", "address": 0x3000,
                              "used": 1, "free_count": 0, "peak": 1}
                else:
                    output = {"op": op, "status": "ok"}
                self.outputs.append(output)
                return output

            def close(self):
                pass

        profile_path, profile_value = self.profile(current_heap=0xFFFFFFFF)
        trace_path = self.root / "fighter-trace.jsonl"
        trace_path.write_text("\n".join(json.dumps(row, sort_keys=True) for row in rows) + "\n")
        with mock.patch("tools.allocation_lifetime_replay.StreamModel", FakeStreamModel):
            return replay_lifetimes(
                trace_path, profile_path, profile_value, rows[0], rows,
                enters, returns, self.CONTEXT, verified or self.verified(pools={
                    "fighter_alloc_data": 0x9000,
                }), checked_wasm=False, require_complete=False,
            )

    @staticmethod
    def _async_model_commands():
        """Build one real source-handle compaction with a DevCom boundary."""
        return [
            {"op": "configure", "heap_count": 2, "descriptor_base": 0x1000,
             "arena_start": 0x1020, "arena_end": 0x20000,
             "main_lo": 0x2020, "main_hi": 0x20000,
             "initial_hsd_heap": 1, "pools": "", "descriptors": "2 1 6 4096"},
            {"op": "os_snapshot"},
            {"op": "bootstrap_heap", "index": 0},
            {"op": "bootstrap_heap", "index": 1},
            {"op": "os_select_hsd"},
            {"op": "aram_init", "initial_base": 0x300000,
             "capacity": 16, "hardware_size": 0x400000},
            {"op": "handle_init_from_aram", "allocator": 0x5000,
             "mem_entries": 0x5008, "heap_handles": 0x5638,
             "current_handle_slot": 0x569C},
            {"op": "game_init"},
            {"op": "game_begin"},
            {"op": "hsd_replace_begin"},
            {"op": "pool_registry_forget"},
            {"op": "hsd_replace_destroy"},
            {"op": "hsd_replace_create"},
            {"op": "os_select_hsd"},
            {"op": "object_heap_set"},
            {"op": "hsd_replace_end"},
            {"op": "game_destroy_current"},
            {"op": "game_new_current", "label": "current"},
            {"op": "game_handle_alloc", "index": 1, "requested": 64, "label": "first"},
            {"op": "game_handle_alloc", "index": 1, "requested": 64, "label": "second"},
            {"op": "game_handle_free", "index": 1, "label": "first"},
            {"op": "game_handle_compact_begin", "index": 1,
             "callback": 0x80017A80, "callback_arg": 4},
            {"op": "game_handle_compact_callback", "generation": 1, "cancelled": 0},
            {"op": "game_handle_compact_devcom_complete", "generation": 2,
             "cancelled": 0},
            {"op": "game_handle_compact_callback", "generation": 3, "cancelled": 0},
        ]

    def _run_async_model(self, *, wasm=False, commands=None, tail=()):
        driver = None
        try:
            driver = ModelDriver(
                wasm=wasm,
                source=ROOT / "tests/allocation_lifetime_model.cpp",
                extra_impls=[ROOT / "src/source_game_heap_context.cpp"],
            )
            command_list = list(self._async_model_commands() if commands is None else commands)
            command_list += list(tail)
            return driver.run([json.dumps(command, sort_keys=True) for command in command_list])
        finally:
            if driver is not None:
                driver.close()

    def test_async_compaction_model_matches_checked_wasm(self):
        native = self._run_async_model()
        statuses = [item["status"] for item in native[-4:]]
        self.assertEqual(statuses, ["compact_started", "compact_started",
                                    "compact_in_progress", "compact_complete"])
        self.assertEqual(native[-1]["compact"]["phase"], "complete")
        try:
            wasm = self._run_async_model(wasm=True)
        except ReplayProblem as error:
            self.skipTest(f"checked Wasm toolchain unavailable: {error}")
        self.assertEqual(wasm, native)

    def test_async_compaction_rejects_stale_generation_and_cancel(self):
        prefix = self._async_model_commands()[:-3]
        stale = self._run_async_model(commands=prefix, tail=[
            {"op": "game_handle_compact_callback", "generation": 0xFFFF, "cancelled": 0},
        ])
        self.assertEqual(stale[-1]["status"], "invalid_generation")
        cancelled = self._run_async_model(commands=prefix, tail=[
            {"op": "game_handle_compact_callback", "generation": 1, "cancelled": 1},
        ])
        self.assertEqual(cancelled[-1]["status"], "cancelled")

    def test_moved_payload_label_frees_current_address_and_is_retired(self):
        commands = self._async_model_commands()
        moved = self._run_async_model(commands=commands, tail=[
            {"op": "game_handle_free", "index": 1, "label": "second"},
        ])
        self.assertEqual(moved[-1]['status'], 'ok')
        self.assertEqual(moved[-1]['payload'], 0x300000)
        self.assertEqual(moved[-1]['retired_payloads'],
                         [{'handle': 0x5014, 'payload': 0x300000}])
        with self.assertRaises(ReplayProblem):
            self._run_async_model(commands=commands, tail=[
                {"op": "game_handle_free", "index": 1, "label": "first"},
            ])

    def test_async_compaction_end_rejects_unfinished_callback(self):
        with self.assertRaises(ReplayProblem):
            self._run_async_model(commands=self._async_model_commands()[:-3],
                                  tail=[{"op": "game_end"}])

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

    def test_source_compaction_replays_callback_and_devcom_order(self):
        rows, enters, returns = self._replacement_stream(include_compaction=True)
        report = self._run(rows, enters, returns, profile=1,
                           verified=self.verified(), checked_wasm=True)
        self.assertEqual(report["status"], "validated_prefix")
        self.assertTrue(report["checked_wasm_matches_native"])
        commands = [action["command"] for action in report["replay_actions"]]
        compact_ops = [command["op"] for command in commands
                       if command["op"].startswith("game_handle_compact")]
        self.assertEqual(compact_ops[0:2], ["game_handle_compact_begin",
                                            "game_handle_compact_callback"])
        devcom = compact_ops.index("game_handle_compact_devcom_complete")
        final = compact_ops.index("game_handle_compact_callback", devcom + 1)
        self.assertLess(devcom, final)

    def test_compaction_manager_is_a_comparison_target(self):
        rows, enters, returns = self._replacement_stream(include_compaction=True)
        entry = next(row for row in rows if row.get('record') == 'enter' and
                     row.get('function') == 'lbMemory_8001529C')
        entry['observed'] = {'compaction': {
            'phase': 'entry', 'manager': dict(dict.fromkeys(FIELDS, 0), remaining=0, chunk=0),
            'handle': dict(pointer=0x5638, x0_next=0, x4_lo=0x300000,
                           x8_hi=0x400000, xC_prev=0x5014),
            'callback': 0x80017A80, 'callback_arg': 4,
        }}
        accepted = self._run(rows, enters, returns, profile=1, verified=self.verified())
        self.assertEqual(accepted['status'], 'validated_prefix')
        entry['observed']['compaction']['manager']['offset'] = 32
        rejected = self._run(rows, enters, returns, profile=1, verified=self.verified())
        self.assertEqual(rejected['status'], 'validation')
        self.assertIn('compaction manager differs', rejected['first_unsupported']['reason'])

    def test_source_compaction_rejects_cancelled_callback_in_replay(self):
        rows, enters, returns = self._replacement_stream(include_compaction=True)
        callback = next(row for row in rows
                        if row.get("record") == "enter" and
                        row.get("function") == "lbMemory_80015320" and
                        row.get("args") == [7, 0, 0, 0])
        callback["args"][3] = 1
        report = self._run(rows, enters, returns, profile=1, verified=self.verified())
        self.assertEqual(report["status"], "unsupported")
        self.assertEqual(report["first_unsupported"]["function"], "lbMemory_80015320")
        self.assertIn("cancelled", report["first_unsupported"]["reason"])

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

    def test_fighter_and_vs_wrappers_join_derived_pool_identity(self):
        rows, enters, returns = self._stream(with_fighters=True)
        report = self._run_fighter(rows, enters, returns)
        self.assertEqual(report["status"], "validated_prefix")
        self.assertTrue(report["ownership_complete"])
        self.assertEqual(
            [event["kind"] for event in report["ownership_events"]],
            ["vs_enter", "fighter_pool_init", "fighter_create", "vs_enter_complete", "vs_exit"],
        )
        fighter = next(event for event in report["ownership_events"]
                        if event["kind"] == "fighter_create")
        self.assertEqual(fighter["gobj"], 0x7300)
        self.assertEqual(fighter["fighter"], 0x3000)
        self.assertIn(0x3000, [item["derived"] for item in report["derived_identities"]])
        # The wrapper observation is comparison-only; no captured fighter
        # address is present in any command sent to the model.
        self.assertNotIn("3000", json.dumps(report["replay_actions"], sort_keys=True).lower())

    def test_vs_enter_boundary_can_complete_an_allocation_prefix_only(self):
        rows, enters, returns = self._stream(with_fighters=True, with_exit=False)
        rows[-1]["boundary_complete"] = True

        report = self._run_fighter(rows, enters, returns)

        self.assertEqual(report["status"], "validated_prefix")
        self.assertFalse(report["complete"])
        self.assertEqual(report["completion_scope"], "vs_enter_prefix")
        self.assertTrue(report["boundary_complete"])
        self.assertFalse(report["ownership_complete"])
        self.assertIsNone(next((event for event in report["ownership_events"]
                                if event["kind"] == "vs_exit"), None))

    def test_vs_enter_boundary_requires_return_and_fighter_owner(self):
        rows, enters, returns = self._stream(with_fighters=True, with_exit=False)
        rows[-1]["boundary_complete"] = True
        vs_enter = next(call for call in enters.values()
                        if call["function"] == "gm_Scene_Vs_OnEnter")
        rows = [row for row in rows
                if not (row.get("record") == "return" and row.get("call") == vs_enter["call"])]
        returns.pop(vs_enter["call"])

        report = self._run_fighter(rows, enters, returns)

        self.assertEqual(report["status"], "incomplete")
        self.assertIn("pending source ownership calls", report["first_unsupported"]["reason"])

        rows, enters, returns = self._stream(with_fighters=True, with_exit=False)
        rows[-1]["boundary_complete"] = True
        create = next(call for call in enters.values()
                      if call["function"] == "Fighter_Create")
        nested = next(call for call in enters.values()
                      if call.get("parent") == create["call"])
        removed = {create["call"], nested["call"]}
        rows = [row for row in rows if row.get("call") not in removed]
        for call in removed:
            enters.pop(call)
            returns.pop(call)

        report = self._run_fighter(rows, enters, returns)

        self.assertEqual(report["status"], "incomplete")
        self.assertIn("validated fighter owner", report["first_unsupported"]["reason"])

    def test_vs_enter_boundary_flag_must_be_a_boolean(self):
        rows, enters, returns = self._stream(with_fighters=True, with_exit=False)
        rows[-1]["boundary_complete"] = "true"

        report = self._run_fighter(rows, enters, returns)

        self.assertEqual(report["status"], "stream")
        self.assertIn("exact boolean boundary_complete", report["first_unsupported"]["reason"])

    def test_source_prefix_scope_survives_replay_stop_after_capture_boundary(self):
        rows, enters, returns = self._stream()
        end = rows[-1]
        end["boundary_complete"] = True
        end["ownership_complete"] = False
        call_id = max(enters) + 1
        sequence = end["sequence"]
        entry = self._row("enter", sequence, call=call_id,
                          function="unsupported_after_prefix", args=[], parent=None,
                          thread=0)
        returned = self._row("return", sequence + 1, call=call_id,
                             function=entry["function"], result=0, observed={}, thread=0)
        rows[-1:-1] = [entry, returned]
        end["sequence"] += 2
        end["calls"] = call_id + 1
        enters[call_id] = entry
        returns[call_id] = returned

        report = self._run(rows, enters, returns, profile=0xFFFFFFFF,
                           verified=self.verified())

        self.assertEqual(report["status"], "unsupported")
        self.assertEqual(report["first_unsupported"]["function"], "unsupported_after_prefix")
        self.assertTrue(report["boundary_complete"])
        self.assertFalse(report["ownership_complete"])
        self.assertEqual(report["completion_scope"], "vs_enter_prefix")

    def test_fighter_wrapper_rejects_missing_or_wrong_static_pool(self):
        rows, enters, returns = self._stream(with_fighters=True)
        report = self._run_fighter(rows, enters, returns, verified=self.verified())
        self.assertEqual(report["status"], "unsupported")
        self.assertIn("fighter_alloc_data", report["first_unsupported"]["reason"])

        rows, enters, returns = self._stream(with_fighters=True)
        fighter_alloc = next(call for call in enters.values()
                             if call["function"] == "HSD_ObjAlloc")
        fighter_alloc["args"][0] = 0x9100
        verified = self.verified(pools={"fighter_alloc_data": 0x9000, "other_pool": 0x9100})
        report = self._run_fighter(rows, enters, returns, verified=verified)
        self.assertEqual(report["status"], "unsupported")
        self.assertIn("no independent fighter_alloc_data pool", report["first_unsupported"]["reason"])

    def test_fighter_wrapper_rejects_ambiguous_or_inconsistent_generation(self):
        rows, enters, returns = self._stream(with_fighters=True)
        first = next(call for call in enters.values()
                     if call["function"] == "Fighter_FirstInitialize_80067A84")
        # Add a second same-pool initialization inside the same wrapper.  The
        # model must reject this generation change before accepting a fighter.
        sequence = max(row["sequence"] for row in rows) + 1
        call_id = max(enters) + 1
        duplicate = {"record": "enter", "sequence": sequence, "call": call_id,
                     "function": "HSD_ObjAllocInit", "args": [0x9000, 0x100, 4],
                     "parent": first["call"], "thread": 0}
        duplicate_return = {"record": "return", "sequence": sequence + 1,
                            "call": call_id, "function": duplicate["function"],
                            "result": 0, "observed": {}, "thread": 0}
        first_return_index = next(index for index, row in enumerate(rows)
                                  if row.get("record") == "return" and row.get("call") == first["call"])
        rows[first_return_index:first_return_index] = [duplicate, duplicate_return]
        enters[call_id] = duplicate
        returns[call_id] = duplicate_return
        report = self._run_fighter(rows, enters, returns)
        self.assertEqual(report["status"], "unsupported")
        self.assertIn("more than once", report["first_unsupported"]["reason"])

        rows, enters, returns = self._stream(with_fighters=True)
        fighter_init = next(call for call in enters.values()
                            if call["function"] == "Fighter_FirstInitialize_80067A84")
        sequence = max(row["sequence"] for row in rows) + 1
        call_id = max(enters) + 1
        repeated = {"record": "enter", "sequence": sequence, "call": call_id,
                    "function": fighter_init["function"], "args": [], "parent": None,
                    "thread": 0}
        repeated_return = {"record": "return", "sequence": sequence + 1,
                           "call": call_id, "function": repeated["function"],
                           "result": 0, "observed": {}, "thread": 0}
        end_index = next(index for index, row in enumerate(rows) if row["record"] == "end")
        rows[end_index:end_index] = [repeated, repeated_return]
        enters[call_id] = repeated
        returns[call_id] = repeated_return
        report = self._run_fighter(rows, enters, returns)
        self.assertEqual(report["status"], "unsupported")
        self.assertIn("existing source generation", report["first_unsupported"]["reason"])

    def test_fighter_wrapper_rejects_observed_join_mismatch(self):
        rows, enters, returns = self._stream(with_fighters=True)
        create_return = next(row for row in rows
                             if row.get("record") == "return"
                             and row.get("function") == "Fighter_Create")
        create_return["observed"]["fighter"]["address"] = 0x4000
        report = self._run_fighter(rows, enters, returns)
        self.assertEqual(report["status"], "validation")
        self.assertIn("fighter identity", report["first_unsupported"]["reason"])


if __name__ == "__main__":
    unittest.main()
