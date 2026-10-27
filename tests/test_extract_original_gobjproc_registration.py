from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import unittest

from extract_original_gobjproc_registration import (
    CALLBACK_ROLES,
    FUNCTION_ARITIES,
    PROFILE_SCHEMA,
    REGISTRATION_PROFILE,
    TRACE_SCHEMA,
    EXPECTED_PROFILE_SHA256,
    RegistrationProblem,
    reduce_registration_trace,
)


GOBJ_POOL = 0x804CE38C
PROC_POOL = 0x804CE3B8
GOBJ = 0x804D1000
PROC = 0x804D2000
STADIUM_CB = 0x801D1390


def profile_and_symbols():
    names = sorted(FUNCTION_ARITIES)
    functions = [
        {"name": name, "argc": FUNCTION_ARITIES[name], "body_sha256": "a" * 64,
         "address": 0x80001000 + index * 0x100, "size": 4}
        for index, name in enumerate(names)
    ]
    profile = {
        "schema": PROFILE_SCHEMA,
        "version": 4,
        "registration_profile": REGISTRATION_PROFILE,
        "dol_sha1": "08e0bf20134dfcb260699671004527b2d6bb1a45",
        "source_revision": "b43912cc78606f96c9569f5d6229bc9d7e265ea5",
        "symbols_sha256": "214477d5b27989a9675c4b881f4db2dad5eeaada39dbac17c57c70d5ba84d579",
        "functions": functions,
        "globals": {
            "gobj_alloc_data": {"address": GOBJ_POOL, "size": 0x2C},
            "gobjproc_alloc_data": {"address": PROC_POOL, "size": 0x2C},
        },
    }
    symbols = {
        "gobj_alloc_data": {"address": GOBJ_POOL, "size": 0x2C},
        "gobjproc_alloc_data": {"address": PROC_POOL, "size": 0x2C},
        "grStadium_801D1390": {"address": STADIUM_CB},
        "efLib_particles_proc_main": {"address": 0x8005C9A4},
        "Fighter_8006A360": {"address": 0x8006A360},
    }
    return profile, symbols


class TraceBuilder:
    def __init__(self):
        self.rows = [{"record": "header", "sequence": 0,
                      "schema": TRACE_SCHEMA, "version": 1,
                      "profile_sha256": EXPECTED_PROFILE_SHA256,
                      "scope": "vs_ownership", "start": "original_dol_entry",
                      "writes_game_state": False}]
        self.enters = {}
        self.returns = {}
        self.next_sequence = 1
        self.next_call = 0

    def enter(self, function, args, parent=None, thread=0):
        call_id = self.next_call
        self.next_call += 1
        row = {"record": "enter", "sequence": self.next_sequence, "call": call_id,
               "function": function, "args": args, "parent": parent,
               "thread": thread, "sp": 0x80010000, "lr": 0x80020000}
        self.next_sequence += 1
        self.rows.append(row)
        self.enters[call_id] = row
        return call_id

    def ret(self, call_id, result=0, observed=None, thread=0):
        entry = self.enters[call_id]
        row = {"record": "return", "sequence": self.next_sequence, "call": call_id,
               "function": entry["function"], "thread": thread, "result": result,
               "observed": observed or {}}
        self.next_sequence += 1
        self.rows.append(row)
        self.returns[call_id] = row
        return row

    def finish(self, status="captured"):
        self.rows.append({"record": "end", "sequence": self.next_sequence,
                          "status": status, "calls": self.next_call,
                          "pending_calls": [] if status == "captured" else list(self.enters)})
        self.next_sequence += 1
        return self.rows, self.enters, self.returns


def add_alloc(trace, pool, address, parent=None):
    call = trace.enter("HSD_ObjAlloc", [pool], parent)
    trace.ret(call, address)
    return call


def add_free(trace, pool, address, parent=None):
    call = trace.enter("HSD_ObjFree", [pool, address], parent)
    trace.ret(call)
    return call


def add_pool_init(trace, pool, size, alignment=4):
    call = trace.enter("HSD_ObjAllocInit", [pool, size, alignment])
    trace.ret(call)
    return call


def add_registry_forget(trace, low=0x80000000, high=0x81800000):
    call = trace.enter("_HSD_ObjAllocForgetMemory", [low, high])
    trace.ret(call)
    return call


def add_setup(trace, gobj_address=GOBJ, proc_address=PROC):
    setup = trace.enter("HSD_GObj_SetupProc", [gobj_address, STADIUM_CB, 9])
    alloc = add_alloc(trace, PROC_POOL, proc_address, setup)
    link = trace.enter("HSD_GObjProc_8038FAA8", [proc_address], setup)
    trace.ret(link)
    trace.ret(setup, proc_address)
    return setup, alloc


class GObjProcRegistrationExtractorTests(unittest.TestCase):
    def setUp(self):
        self.profile, self.symbols = profile_and_symbols()
        self.header = {"schema": TRACE_SCHEMA, "version": 1,
                       "profile_sha256": EXPECTED_PROFILE_SHA256,
                       "scope": "vs_ownership", "start": "original_dol_entry",
                       "writes_game_state": False}

    def extract(self, trace):
        rows, enters, returns = trace.finish()
        return reduce_registration_trace(self.header, rows, enters, returns,
                                         self.profile, self.symbols)

    def test_setup_link_and_nested_remove_are_distinct_observed_edges(self):
        trace = TraceBuilder()
        add_alloc(trace, GOBJ_POOL, GOBJ)
        setup_call, nested_alloc = add_setup(trace)
        request = trace.enter("HSD_GObjProc_8038FE24", [PROC])
        owner_unlink = trace.enter("HSD_GObjProc_8038FCE4", [PROC], request)
        sched_unlink = trace.enter("HSD_GObjProc_8038FC18", [PROC], owner_unlink)
        trace.ret(sched_unlink)
        trace.ret(owner_unlink)
        add_free(trace, PROC_POOL, PROC, request)
        trace.ret(request)
        add_free(trace, GOBJ_POOL, GOBJ)

        result = self.extract(trace)
        events = result["events"]
        allocations = [item for item in events if item["event"] == "allocation_return"]
        setup = next(item for item in events if item["event"] == "setup_registration")
        link = next(item for item in events if item["event"] == "proc_link")
        request = next(item for item in events if item["event"] == "proc_remove_request")
        self.assertEqual([item["allocation_generation"] for item in allocations], ["1", "2"])
        self.assertEqual([item["pool_kind"] for item in allocations], [1, 2])
        self.assertEqual(setup["gobj_generation"], "1")
        self.assertEqual(setup["proc_generation"], "2")
        self.assertEqual(setup["callback_symbol"], "grStadium_801D1390")
        self.assertEqual(setup["owner_role"], "stadium")
        self.assertEqual(setup["priority"], 9)
        self.assertEqual(setup["call"], str(setup_call))
        self.assertEqual(setup["setup_call"], str(setup_call))
        self.assertEqual(setup["nested_allocation_call"], str(nested_alloc))
        self.assertEqual(setup["source_proc_address"], f"0x{PROC:08x}")
        self.assertEqual(link["callback_symbol"], "grStadium_801D1390")
        self.assertEqual(link["owner_role"], "stadium")
        self.assertLess(link["return_sequence"], setup["return_sequence"])
        self.assertEqual(request["request_outcome"], "actual_free_nested_before_request_return")
        self.assertTrue(request["nested_owner_unlink"])
        self.assertTrue(request["nested_scheduler_unlink"])
        self.assertTrue(request["nested_free"])
        self.assertEqual(result["summary"]["frees"], {"gobj": 1, "gobjproc": 1})
        self.assertFalse(result["summary"]["lifecycle_complete"])

    def test_address_reuse_gets_a_new_generation_across_pool_history(self):
        trace = TraceBuilder()
        add_alloc(trace, GOBJ_POOL, GOBJ)
        add_free(trace, GOBJ_POOL, GOBJ)
        add_alloc(trace, PROC_POOL, PROC)
        add_free(trace, PROC_POOL, PROC)
        add_alloc(trace, GOBJ_POOL, GOBJ)
        result = self.extract(trace)
        generations = [item["allocation_generation"] for item in result["events"]
                       if item["event"] == "allocation_return"]
        self.assertEqual(generations, ["1", "2", "3"])
        self.assertEqual(result["summary"]["live_at_capture_end"]["gobj"], 1)

    def test_pool_reinitialization_preserves_residuals_and_monotonic_generation(self):
        trace = TraceBuilder()
        add_alloc(trace, GOBJ_POOL, GOBJ)
        add_setup(trace)
        deferred = trace.enter("HSD_GObjProc_8038FE24", [PROC])
        trace.ret(deferred)
        add_registry_forget(trace)

        # ForgetMemory only drops the registry. The active source allocations
        # remain tracked until each exact pool Init return is observed.
        add_pool_init(trace, GOBJ_POOL, 0x24)
        add_pool_init(trace, PROC_POOL, 0x18)
        add_alloc(trace, GOBJ_POOL, GOBJ)
        add_setup(trace)
        add_free(trace, PROC_POOL, PROC)
        add_free(trace, GOBJ_POOL, GOBJ)

        result = self.extract(trace)
        events = result["events"]
        resets = [item for item in events if item["event"] == "pool_reinitialize_return"]
        forget = next(item for item in events if item["event"] == "allocator_registry_forget_return")
        allocations = [item for item in events if item["event"] == "allocation_return"]
        frees = [item for item in events if item["event"] == "free_return"]
        request = next(item for item in events if item["event"] == "proc_remove_request")

        self.assertEqual(forget["pool_objects_freed"], False)
        self.assertEqual(len(resets), 2)
        self.assertEqual([item["reset_residual_generations"] for item in resets], [["1"], ["2"]])
        self.assertTrue(all(item["objfree_observed"] is False for item in resets))
        self.assertEqual([item["allocation_generation"] for item in allocations], ["1", "2", "3", "4"])
        self.assertEqual([item["pool_epoch"] for item in allocations], [0, 0, 1, 1])
        self.assertEqual([item["allocation_generation"] for item in frees], ["4", "3"])
        self.assertEqual([item["allocation_identity_status"] for item in frees], [
            "active_current_epoch_with_prior_residual_alias",
            "active_current_epoch_with_prior_residual_alias",
        ])
        self.assertEqual(result["summary"]["reset_residual_count"], 2)
        self.assertEqual(result["summary"]["pool_epochs"], {"gobj": 1, "gobjproc": 1})
        self.assertEqual(result["summary"]["frees"], {"gobj": 1, "gobjproc": 1})
        self.assertEqual(request["proc_generation"], "2")
        self.assertNotIn("later_free_sequence", request)
        self.assertEqual(request["request_outcome"], "no_actual_free_observed_before_request_return")
        self.assertEqual(result["summary"]["requests_without_observed_later_free"], 1)

    def test_deferred_request_is_not_reported_as_free_and_later_free_is_linked(self):
        trace = TraceBuilder()
        add_alloc(trace, GOBJ_POOL, GOBJ)
        add_setup(trace)
        request_call = trace.enter("HSD_GObjProc_8038FE24", [PROC])
        trace.ret(request_call)
        req_event = None
        # The later actual unlink/free is source evidence distinct from FE24.
        unlink = trace.enter("HSD_GObjProc_8038FCE4", [PROC])
        trace.ret(unlink)
        add_free(trace, PROC_POOL, PROC)
        result = self.extract(trace)
        req_event = next(item for item in result["events"] if item["event"] == "proc_remove_request")
        self.assertEqual(req_event["request_outcome"], "later_actual_free_observed")
        self.assertIn("later_free_sequence", req_event)
        self.assertEqual(result["summary"]["requests_without_observed_later_free"], 0)

    def test_unclosed_setup_proc_and_uncompleted_trace_refuse(self):
        trace = TraceBuilder()
        add_alloc(trace, GOBJ_POOL, GOBJ)
        setup = trace.enter("HSD_GObj_SetupProc", [GOBJ, STADIUM_CB, 1])
        trace.ret(setup, PROC)
        with self.assertRaisesRegex(RegistrationProblem, "exactly one observed gobjproc allocation"):
            self.extract(trace)

        trace2 = TraceBuilder()
        add_alloc(trace2, GOBJ_POOL, GOBJ)
        rows, enters, returns = trace2.finish("incomplete")
        with self.assertRaisesRegex(RegistrationProblem, "captured terminal"):
            reduce_registration_trace(self.header, rows, enters, returns,
                                     self.profile, self.symbols)

    def test_legacy_profile_and_unknown_or_duplicate_free_refuse(self):
        legacy = dict(self.profile)
        legacy["version"] = 3
        with self.assertRaisesRegex(RegistrationProblem, "only the pinned v4"):
            reduce_registration_trace(self.header, [], {}, {}, legacy, self.symbols)

        trace = TraceBuilder()
        add_alloc(trace, GOBJ_POOL, GOBJ)
        add_free(trace, GOBJ_POOL, GOBJ)
        add_free(trace, GOBJ_POOL, GOBJ)
        with self.assertRaisesRegex(RegistrationProblem, "no observed active or reset-residual gobj allocation"):
            self.extract(trace)

    def test_trace_endpoint_scope_and_guest_state_write_guard(self):
        trace = TraceBuilder()
        rows, enters, returns = trace.finish()
        endpoint_header = dict(self.header, scene_endpoint_profile="paired_idle_scene_endpoints_v1")
        with self.assertRaisesRegex(RegistrationProblem, "scene-endpoint-only"):
            reduce_registration_trace(endpoint_header, rows, enters, returns,
                                     self.profile, self.symbols)
        write_header = dict(self.header, writes_game_state=True)
        with self.assertRaisesRegex(RegistrationProblem, "did not write guest state"):
            reduce_registration_trace(write_header, rows, enters, returns,
                                     self.profile, self.symbols)

        wrong_profile_header = dict(self.header, profile_sha256="c" * 64)
        with self.assertRaisesRegex(RegistrationProblem, "pinned observer profile"):
            reduce_registration_trace(wrong_profile_header, rows, enters, returns,
                                     self.profile, self.symbols)

        wrong_start_header = dict(self.header, start="after_setup")
        with self.assertRaisesRegex(RegistrationProblem, "original DOL entry"):
            reduce_registration_trace(wrong_start_header, rows, enters, returns,
                                     self.profile, self.symbols)


if __name__ == "__main__":
    unittest.main()
