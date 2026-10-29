#!/usr/bin/env python3
"""Extract observed GObj/GObjProc registration relationships from a v4 trace.

This is a projection over the existing validated allocation-history stream. It
does not simulate list contents, infer unobserved calls, or compare PPC
addresses/generation numbers with another runtime. The generation counter is
local to this extracted original trace and monotonic across the two tracked
pools.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import importlib
import json
from pathlib import Path
import sys
from typing import Any, Mapping, Sequence


PROFILE_SCHEMA = "melee-web-original-allocation-profile"
TRACE_SCHEMA = "melee-web-original-allocation-history"
REGISTRATION_PROFILE = "gobjproc-registration-v1"
OUTPUT_SCHEMA = "melee-web-original-gobjproc-registration-extraction"
OUTPUT_VERSION = 1
EXPECTED_DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"
EXPECTED_SOURCE_REVISION = "b43912cc78606f96c9569f5d6229bc9d7e265ea5"
EXPECTED_SYMBOLS_SHA256 = "214477d5b27989a9675c4b881f4db2dad5eeaada39dbac17c57c70d5ba84d579"
EXPECTED_PROFILE_SHA256 = "d4dcc28edcf534586b3953d3235e8880173f6dbcd9ecb384e97991137646756e"
EXPECTED_PROFILE_SHA256_V5 = "b1f4b68f72ba9e0af754c4100845860321cac6b537f3803d6fe41e3d68626e09"
SOURCE_HELPER_SHA256 = {
    "allocation_history_replay.py": "eb7cc59c5361668d50850da0e0d716c1e44ae15758f9be4517462a2b38779085",
    "allocation_trace_store.py": "1f9f860006396f4477e063dcb0b620c8dfa66da360b28ed398cf6e59d587136a",
    "retail_allocation_profile.py": "eeb9b7fc22d40c5c7689f6cfc5c8e7a6c8e2ee8047c5ddce603d73bdc2a6a8f3",
    "generate_reference_allocation_profile.py": "3b41793466612696c407ee390f7d2fd23710e8d32cfc8d78c67f825877cd06a3",
}

FUNCTION_ARITIES = {
    "HSD_ObjAlloc": 1,
    "HSD_ObjFree": 2,
    "HSD_ObjAllocInit": 3,
    "_HSD_ObjAllocForgetMemory": 2,
    "HSD_GObj_SetupProc": 3,
    "HSD_GObjProc_8038FAA8": 1,
    "HSD_GObjProc_8038FC18": 1,
    "HSD_GObjProc_8038FCE4": 1,
    "HSD_GObjProc_8038FE24": 1,
}
POOL_NAMES = {1: "gobj", 2: "gobjproc"}
POOL_SYMBOLS = {1: "gobj_alloc_data", 2: "gobjproc_alloc_data"}
# Exact GALE01 symbols.txt identities (objects, size 0x2c), authenticated by load_and_validate.
POOL_IDENTITIES = {1: (0x804CE38C, 0x2C), 2: (0x804CE3B8, 0x2C)}
CALLBACK_ROLES = {
    "grStadium_801D1390": "stadium",
    "efLib_particles_proc_main": "particle",
    "Fighter_8006A360": "fighter",
}
MEM1_START = 0x80000000
MEM1_END = 0x81800000


class RegistrationProblem(ValueError):
    """Input or relationship evidence is incomplete or inconsistent."""


def _u32(value: Any, context: str) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, str)):
        raise RegistrationProblem(f"{context} must be a source u32")
    try:
        result = int(value, 0) if isinstance(value, str) else value
    except ValueError as error:
        raise RegistrationProblem(f"{context} is not a source u32") from error
    if result < 0 or result > 0xFFFFFFFF:
        raise RegistrationProblem(f"{context} is outside source u32")
    return result


def _hex(value: int) -> str:
    return f"0x{value:08x}"


def _function_table(profile: Mapping[str, Any]) -> dict[str, dict[str, Any]]:
    if (profile.get("schema") != PROFILE_SCHEMA or type(profile.get("version")) is not int
            or profile.get("version") not in (4, 5)
            or profile.get("registration_profile") != REGISTRATION_PROFILE):
        raise RegistrationProblem("only the pinned v4/v5 GObjProc registration profile is supported")
    if profile.get("dol_sha1") != EXPECTED_DOL_SHA1:
        raise RegistrationProblem("profile DOL differs from the owned GALE01r2 identity")
    if profile.get("source_revision") != EXPECTED_SOURCE_REVISION:
        raise RegistrationProblem("profile source revision differs from the pinned retail source")
    if profile.get("symbols_sha256") != EXPECTED_SYMBOLS_SHA256:
        raise RegistrationProblem("profile symbol map differs from the pinned GALE01 map")
    rows = profile.get("functions")
    if not isinstance(rows, list):
        raise RegistrationProblem("profile functions must be a list")
    result: dict[str, dict[str, Any]] = {}
    for row in rows:
        if not isinstance(row, dict) or not isinstance(row.get("name"), str):
            raise RegistrationProblem("profile contains a malformed function identity")
        name = row["name"]
        if name in result:
            raise RegistrationProblem(f"profile duplicates function identity {name}")
        result[name] = row
    for name, arity in FUNCTION_ARITIES.items():
        row = result.get(name)
        if row is None:
            raise RegistrationProblem(f"v4 profile omits required function {name}")
        if row.get("argc") != arity:
            raise RegistrationProblem(f"profile has wrong argument count for {name}")
        if not isinstance(row.get("body_sha256"), str) or len(row["body_sha256"]) != 64:
            raise RegistrationProblem(f"profile omits body identity for {name}")
    return result


def _pools(profile: Mapping[str, Any], symbols: Mapping[str, Mapping[str, Any]]) -> dict[int, int]:
    globals_value = profile.get("globals")
    if not isinstance(globals_value, dict):
        raise RegistrationProblem("profile globals must be an object")
    result: dict[int, int] = {}
    for pool_kind, name in POOL_SYMBOLS.items():
        profile_global = globals_value.get(name)
        source_symbol = symbols.get(name)
        if not isinstance(source_symbol, Mapping):
            raise RegistrationProblem(f"source symbol map omits {name}")
        address = _u32(source_symbol.get("address"), f"symbols.{name}.address")
        size = _u32(source_symbol.get("size"), f"symbols.{name}.size")
        if (address, size) != POOL_IDENTITIES[pool_kind]:
            raise RegistrationProblem(f"source pool identity differs from pinned symbol {name}")
        # These pools are not part of the generated allocator-state global set.
        # Retain contradiction checks for profiles that explicitly carry them.
        if name in globals_value and (not isinstance(profile_global, dict) or
                _u32(profile_global.get("address"), f"globals.{name}.address") != address or
                _u32(profile_global.get("size"), f"globals.{name}.size") != size):
            raise RegistrationProblem(f"profile pool identity differs from source symbol {name}")
        if address % 4 or not MEM1_START <= address < MEM1_END:
            raise RegistrationProblem(f"pool symbol {name} is outside aligned MEM1")
        result[pool_kind] = address
    if result[1] == result[2]:
        raise RegistrationProblem("tracked allocator pools share an address")
    return result


def _address_to_symbol(symbols: Mapping[str, Mapping[str, Any]]) -> dict[int, str]:
    reverse: dict[int, str] = {}
    for name in CALLBACK_ROLES:
        row = symbols.get(name)
        if not isinstance(row, Mapping):
            raise RegistrationProblem(f"source symbol map omits selected callback {name}")
        address = _u32(row.get("address"), f"symbols.{name}.address")
        if address in reverse:
            raise RegistrationProblem("selected callbacks have an ambiguous address")
        reverse[address] = name
    return reverse


def _call_chain(call: Mapping[str, Any], enters: Mapping[int, Mapping[str, Any]]) -> list[Mapping[str, Any]]:
    chain: list[Mapping[str, Any]] = []
    parent = call.get("parent")
    seen: set[int] = set()
    while parent is not None:
        parent_id = _u32(parent, "call.parent")
        if parent_id in seen or parent_id not in enters:
            raise RegistrationProblem("trace call has an invalid source parent chain")
        seen.add(parent_id)
        parent_call = enters[parent_id]
        chain.append(parent_call)
        parent = parent_call.get("parent")
    return chain


def reduce_registration_trace(
    header: Mapping[str, Any],
    rows: Sequence[Mapping[str, Any]],
    enters: Mapping[int, Mapping[str, Any]],
    returns: Mapping[int, Mapping[str, Any]],
    profile: Mapping[str, Any],
    symbols: Mapping[str, Mapping[str, Any]],
) -> dict[str, Any]:
    """Project validated trace calls into source-local allocations and edges.

    ``rows``, ``enters`` and ``returns`` are the objects returned by
    ``allocation_history_replay.load_trace``. A disk-backed TraceRows view is
    streamed once and closed by the caller.
    """
    function_rows = _function_table(profile)
    pool_addresses = _pools(profile, symbols)
    callbacks = _address_to_symbol(symbols)
    if (header.get("schema") != TRACE_SCHEMA or type(header.get("version")) is not int
            or header.get("version") != 1):
        raise RegistrationProblem("unsupported original allocation trace schema/version")
    expected_profile = EXPECTED_PROFILE_SHA256_V5 if profile["version"] == 5 else EXPECTED_PROFILE_SHA256
    if header.get("profile_sha256") != expected_profile:
        raise RegistrationProblem("trace header differs from the pinned observer profile identity")
    if header.get("start") != "original_dol_entry":
        raise RegistrationProblem("registration extraction requires the original DOL entry boundary")
    if header.get("scene_endpoint_profile") is not None:
        raise RegistrationProblem("scene-endpoint-only traces cannot support registration extraction")
    if header.get("writes_game_state") is not False:
        raise RegistrationProblem("trace must attest that the observer did not write guest state")

    if not len(rows):
        raise RegistrationProblem("registration trace is empty")
    terminal = rows[-1]
    if terminal.get("record") != "end" or terminal.get("status") != "captured":
        raise RegistrationProblem("registration extraction requires one captured terminal row")

    # load_trace has already checked footer uniqueness/position. The main pass
    # is a single streaming walk over its sequence view.
    local_generation = 0
    active_by_pool_ptr: dict[tuple[int, int], dict[str, Any]] = {}
    active_by_ptr: dict[int, dict[str, Any]] = {}
    pool_epoch = {1: 0, 2: 0}
    reset_residuals: list[dict[str, Any]] = []
    residual_by_pool_ptr: defaultdict[tuple[int, int], list[dict[str, Any]]] = defaultdict(list)
    registry_forget_count = 0
    setup_calls: dict[int, dict[str, Any]] = {}
    setup_allocations: defaultdict[int, list[int]] = defaultdict(list)
    pending_requests: defaultdict[tuple[int, int, str], list[dict[str, Any]]] = defaultdict(list)
    request_stack_by_thread: defaultdict[Any, list[dict[str, Any]]] = defaultdict(list)
    events: list[dict[str, Any]] = []
    counters: defaultdict[str, int] = defaultdict(int)
    source_call_count = 0
    terminal_sequence = _u32(terminal.get("sequence"), "end.sequence")

    def required_active(pool_kind: int, address: int, context: str) -> dict[str, Any]:
        obj = active_by_pool_ptr.get((pool_kind, address))
        if obj is None:
            raise RegistrationProblem(f"{context} refers to no live observed {POOL_NAMES[pool_kind]} allocation")
        return obj

    def event_base(call: Mapping[str, Any], ret: Mapping[str, Any]) -> dict[str, Any]:
        return {
            "call": str(_u32(call.get("call"), "call.call")),
            "enter_sequence": _u32(call.get("sequence"), "call.sequence"),
            "return_sequence": _u32(ret.get("sequence"), "return.sequence"),
            "function": call.get("function"),
        }

    for row in rows:
        record = row.get("record")
        if record == "enter":
            source_call_count += 1
            function = row.get("function")
            if function == "HSD_GObj_SetupProc":
                args = row.get("args")
                if not isinstance(args, list) or len(args) != 3:
                    raise RegistrationProblem("SetupProc entry must contain exactly three source arguments")
                gobj_address = _u32(args[0], "SetupProc.args[0]")
                callback_address = _u32(args[1], "SetupProc.args[1]")
                priority = _u32(args[2], "SetupProc.args[2]")
                if priority > 0xFF:
                    raise RegistrationProblem("SetupProc priority is outside source u8")
                gobj = required_active(1, gobj_address, "SetupProc")
                setup_calls[_u32(row.get("call"), "SetupProc.call")] = {
                    "gobj_address": gobj_address,
                    "gobj_generation": gobj["generation"],
                    "callback_address": callback_address,
                    "priority": priority,
                }
            elif function == "HSD_GObjProc_8038FE24":
                args = row.get("args")
                if not isinstance(args, list) or len(args) != 1:
                    raise RegistrationProblem("FE24 entry must contain exactly one source argument")
                proc_address = _u32(args[0], "FE24.args[0]")
                proc = required_active(2, proc_address, "FE24")
                tracker = {
                    "call": _u32(row.get("call"), "FE24.call"),
                    "thread": row.get("thread"),
                    "proc_address": proc_address,
                    "proc_generation": proc["generation"],
                    "gobj_generation": proc.get("gobj_generation"),
                    "callback_symbol": proc.get("callback_symbol"),
                    "owner_role": proc.get("owner_role"),
                    "nested_owner_unlink": False,
                    "nested_scheduler_unlink": False,
                    "nested_free": False,
                }
                request_stack_by_thread[row.get("thread")].append(tracker)
            continue

        if record != "return":
            continue
        call_id = _u32(row.get("call"), "return.call")
        call = enters.get(call_id)
        if call is None:
            raise RegistrationProblem(f"return {call_id} has no source entry")
        function = call.get("function")
        if row.get("function") != function:
            raise RegistrationProblem(f"return {call_id} function differs from its source entry")
        args = call.get("args")
        if not isinstance(args, list):
            raise RegistrationProblem(f"call {call_id} arguments are malformed")
        source_args = [_u32(value, f"call {call_id}.args") for value in args]
        result = _u32(row.get("result", 0), f"return {call_id}.result")
        base = event_base(call, row)

        if function == "_HSD_ObjAllocForgetMemory":
            if len(source_args) != 2:
                raise RegistrationProblem("ObjAllocForgetMemory entry must contain low/high bounds")
            registry_forget_count += 1
            events.append({
                **base, "event": "allocator_registry_forget_return",
                "low_address": _hex(source_args[0]), "high_address": _hex(source_args[1]),
                "registry_forget_ordinal": registry_forget_count,
                "pool_objects_freed": False,
            })
            counters["allocator_registry_forget"] += 1
            continue

        if function == "HSD_ObjAllocInit":
            if len(source_args) != 3:
                raise RegistrationProblem("ObjAllocInit entry must contain data/size/alignment arguments")
            pool_kind = next((kind for kind, address in pool_addresses.items()
                              if address == source_args[0]), None)
            if pool_kind is None:
                continue
            if source_args[1] == 0 or source_args[2] == 0:
                raise RegistrationProblem("tracked ObjAllocInit size and alignment must be nonzero")
            prior_epoch = pool_epoch[pool_kind]
            residualized = []
            for key, obj in list(active_by_pool_ptr.items()):
                if key[0] != pool_kind:
                    continue
                del active_by_pool_ptr[key]
                if active_by_ptr.get(key[1]) is obj:
                    del active_by_ptr[key[1]]
                obj["active_in_pool"] = False
                obj["residual_reason"] = "pool_reinitialized_without_observed_free"
                obj["residual_sequence"] = base["return_sequence"]
                residual_by_pool_ptr[key].append(obj)
                reset_residuals.append(obj)
                residualized.append(obj)
            pool_epoch[pool_kind] += 1
            events.append({
                **base, "event": "pool_reinitialize_return",
                "pool_kind": pool_kind, "pool": POOL_NAMES[pool_kind],
                "pool_epoch_before": prior_epoch, "pool_epoch_after": pool_epoch[pool_kind],
                "pool_address": _hex(source_args[0]), "object_size": source_args[1],
                "alignment": source_args[2], "reset_residual_count": len(residualized),
                "reset_residual_generations": [obj["generation"] for obj in residualized],
                "objfree_observed": False,
            })
            counters[f"pool_reinitialize_{POOL_NAMES[pool_kind]}"] += 1
            continue

        if function == "HSD_ObjAlloc":
            if len(source_args) != 1:
                raise RegistrationProblem("ObjAlloc entry must contain one pool pointer")
            pool_kind = next((kind for kind, address in pool_addresses.items()
                              if address == source_args[0]), None)
            if pool_kind is None or result == 0:
                continue
            if result % 4 or not MEM1_START <= result < MEM1_END:
                raise RegistrationProblem("tracked ObjAlloc returned a pointer outside aligned MEM1")
            key = (pool_kind, result)
            if key in active_by_pool_ptr or result in active_by_ptr:
                raise RegistrationProblem("tracked pool reallocated a still-live source address")
            local_generation += 1
            if local_generation > 0xFFFFFFFFFFFFFFFF:
                raise RegistrationProblem("local allocation generation overflow")
            obj: dict[str, Any] = {
                "generation": str(local_generation),
                "pool_kind": pool_kind,
                "pool": POOL_NAMES[pool_kind],
                "pool_epoch": pool_epoch[pool_kind],
                "address": result,
                "live": True,
                "active_in_pool": True,
                "owner_linked": False,
                "scheduler_linked": False,
            }
            if pool_kind == 2:
                ancestors = _call_chain(call, enters)
                setup_ancestors = [ancestor for ancestor in ancestors
                                   if ancestor.get("function") == "HSD_GObj_SetupProc"]
                if len(setup_ancestors) > 1:
                    raise RegistrationProblem("ObjAlloc is nested under ambiguous SetupProc calls")
                if setup_ancestors:
                    setup_call_id = _u32(setup_ancestors[0].get("call"), "SetupProc.call")
                    setup = setup_calls.get(setup_call_id)
                    if setup is None:
                        raise RegistrationProblem("nested proc allocation precedes SetupProc arguments")
                    callback_symbol = callbacks.get(setup["callback_address"])
                    obj.update({
                        "gobj_address": setup["gobj_address"],
                        "gobj_generation": setup["gobj_generation"],
                        "callback_address": setup["callback_address"],
                        "callback_symbol": callback_symbol,
                        "owner_role": CALLBACK_ROLES.get(callback_symbol),
                        "priority": setup["priority"],
                        "setup_call": str(setup_call_id),
                        "nested_allocation_call": str(_u32(call.get("call"), "ObjAlloc.call")),
                        "setup_returned": False,
                    })
                    setup_allocations[setup_call_id].append(result)
            active_by_pool_ptr[key] = obj
            active_by_ptr[result] = obj
            counters[f"alloc_{POOL_NAMES[pool_kind]}"] += 1
            events.append({
                **base, "event": "allocation_return", "pool_kind": pool_kind,
                "pool": POOL_NAMES[pool_kind], "allocation_generation": obj["generation"],
                "pool_epoch": obj["pool_epoch"],
                "source_address": _hex(result),
                "setup_call": obj.get("setup_call"),
            })
            continue

        if function == "HSD_ObjFree":
            if len(source_args) != 2:
                raise RegistrationProblem("ObjFree entry must contain pool and object pointers")
            pool_kind = next((kind for kind, address in pool_addresses.items()
                              if address == source_args[0]), None)
            object_address = source_args[1]
            if pool_kind is None:
                if object_address in active_by_ptr:
                    raise RegistrationProblem("tracked object is freed through a different allocator pool")
                continue
            key = (pool_kind, object_address)
            obj = active_by_pool_ptr.get(key)
            reset_candidates = residual_by_pool_ptr.get(key, [])
            if obj is None and not reset_candidates:
                raise RegistrationProblem(f"ObjFree refers to no observed active or reset-residual {POOL_NAMES[pool_kind]} allocation")
            if obj is not None:
                obj["live"] = False
                obj["active_in_pool"] = False
                del active_by_pool_ptr[key]
                del active_by_ptr[object_address]
            counters[f"free_{POOL_NAMES[pool_kind]}"] += 1
            free_event = {
                **base, "event": "free_return", "pool_kind": pool_kind,
                "pool": POOL_NAMES[pool_kind],
                "allocation_generation": obj["generation"] if obj is not None else None,
                "allocation_identity_status": (
                    "active_current_epoch_with_prior_residual_alias" if obj is not None and reset_candidates
                    else "active_current_epoch" if obj is not None
                    else "unmatched_reset_residual_pointer"),
                "prior_residual_generations_same_address": [item["generation"] for item in reset_candidates],
                "source_address": _hex(object_address),
                "owner_linked_at_free": bool(obj and obj.get("owner_linked")),
                "scheduler_linked_at_free": bool(obj and obj.get("scheduler_linked")),
            }
            events.append(free_event)
            if pool_kind == 2:
                pending_key = ((pool_kind, object_address, obj["generation"])
                               if obj is not None else None)
                pending = pending_requests.get(pending_key, []) if pending_key is not None else []
                if len(pending) > 1:
                    raise RegistrationProblem("multiple FE24 requests ambiguously precede one proc free")
                if pending:
                    pending[0]["later_free_sequence"] = free_event["return_sequence"]
                    pending[0]["request_outcome"] = "later_actual_free_observed"
                    pending[0]["later_free_allocation_identity_status"] = free_event["allocation_identity_status"]
                    pending_requests[pending_key].clear()
                for tracker in request_stack_by_thread.get(call.get("thread"), []):
                    if tracker["proc_address"] == object_address:
                        if obj is not None and tracker["proc_generation"] != obj["generation"]:
                            continue
                        if obj is None and tracker["proc_generation"] not in {
                                item["generation"] for item in reset_candidates}:
                            continue
                        tracker["nested_free"] = True
                        tracker["nested_free_identity_status"] = free_event["allocation_identity_status"]
            continue

        if function == "HSD_GObj_SetupProc":
            if len(source_args) != 3:
                raise RegistrationProblem("SetupProc entry must contain exactly three source arguments")
            setup = setup_calls.get(call_id)
            if setup is None:
                raise RegistrationProblem("SetupProc return has no validated entry")
            allocated = setup_allocations.get(call_id, [])
            if len(allocated) != 1:
                raise RegistrationProblem("SetupProc must contain exactly one observed gobjproc allocation")
            proc = required_active(2, allocated[0], "SetupProc return")
            if proc.get("setup_call") != str(call_id) or result != allocated[0]:
                raise RegistrationProblem("SetupProc returned a pointer different from its nested proc allocation")
            callback_address = setup["callback_address"]
            callback_symbol = next((name for address, name in callbacks.items()
                                    if address == callback_address), None)
            owner_role = CALLBACK_ROLES.get(callback_symbol) if callback_symbol else None
            proc.update({
                "setup_returned": True,
                "callback_symbol": callback_symbol,
                "owner_role": owner_role,
            })
            counters["setup_proc"] += 1
            events.append({
                **base, "event": "setup_registration", "pool_kind": 2,
                "proc_generation": proc["generation"],
                "gobj_generation": setup["gobj_generation"],
                "source_proc_address": _hex(result),
                "source_gobj_address": _hex(setup["gobj_address"]),
                "callback_address": _hex(callback_address),
                "callback_symbol": callback_symbol,
                "owner_role": owner_role,
                "priority": setup["priority"],
                "setup_call": proc["setup_call"],
                "nested_allocation_call": proc["nested_allocation_call"],
            })
            continue

        if function == "HSD_GObjProc_8038FE24":
            if len(source_args) != 1:
                raise RegistrationProblem("FE24 entry must contain exactly one proc pointer")
            proc_address = source_args[0]
            stack = request_stack_by_thread.get(call.get("thread"), [])
            if not stack or stack[-1]["call"] != call_id:
                raise RegistrationProblem("FE24 return does not close the current request")
            tracker = stack.pop()
            if tracker["proc_address"] != proc_address:
                raise RegistrationProblem("FE24 return pointer differs from its observed entry")
            proc = active_by_pool_ptr.get((2, proc_address))
            nested_free = tracker["nested_free"]
            edge = {
                **base, "event": "proc_remove_request", "pool_kind": 2,
                "proc_generation": tracker["proc_generation"],
                "gobj_generation": tracker.get("gobj_generation"),
                "source_proc_address": _hex(proc_address),
                "callback_symbol": tracker.get("callback_symbol"),
                "owner_role": tracker.get("owner_role"),
                "owner_linked_after_return": bool(proc and proc["owner_linked"]),
                "scheduler_linked_after_return": bool(proc and proc["scheduler_linked"]),
                "nested_owner_unlink": tracker["nested_owner_unlink"],
                "nested_scheduler_unlink": tracker["nested_scheduler_unlink"],
                "nested_free": nested_free,
                "nested_free_identity_status": tracker.get("nested_free_identity_status"),
                "request_outcome": ("actual_free_nested_before_request_return" if nested_free
                                    else "no_actual_free_observed_before_request_return"),
            }
            events.append(edge)
            counters["proc_remove_request"] += 1
            if not nested_free:
                pending_requests[(2, proc_address, tracker["proc_generation"])].append(edge)
            for parent_tracker in stack:
                if parent_tracker["proc_address"] == proc_address:
                    if tracker["nested_owner_unlink"]:
                        parent_tracker["nested_owner_unlink"] = True
                    if tracker["nested_scheduler_unlink"]:
                        parent_tracker["nested_scheduler_unlink"] = True
                    if nested_free:
                        parent_tracker["nested_free"] = True
            continue

        if function in ("HSD_GObjProc_8038FAA8", "HSD_GObjProc_8038FC18",
                        "HSD_GObjProc_8038FCE4"):
            if len(source_args) != 1:
                raise RegistrationProblem(f"{function} entry must contain exactly one proc pointer")
            proc_address = source_args[0]
            proc = required_active(2, proc_address, function)
            if not proc.get("setup_call") and not proc.get("setup_returned"):
                raise RegistrationProblem(f"{function} uses a proc without a completed SetupProc")
            if function == "HSD_GObjProc_8038FAA8":
                proc["owner_linked"] = True
                proc["scheduler_linked"] = True
                event_name = "proc_link"
                counters["proc_link"] += 1
            elif function == "HSD_GObjProc_8038FC18":
                proc["scheduler_linked"] = False
                event_name = "proc_scheduler_unlink"
                counters["proc_scheduler_unlink"] += 1
            elif function == "HSD_GObjProc_8038FCE4":
                proc["owner_linked"] = False
                proc["scheduler_linked"] = False
                event_name = "proc_owner_unlink"
                counters["proc_owner_unlink"] += 1
            edge = {
                **base, "event": event_name, "pool_kind": 2,
                "proc_generation": proc["generation"],
                "gobj_generation": proc.get("gobj_generation"),
                "source_proc_address": _hex(proc_address),
                "callback_symbol": proc.get("callback_symbol"),
                "owner_role": proc.get("owner_role"),
                "owner_linked_after_return": bool(proc["owner_linked"]),
                "scheduler_linked_after_return": bool(proc["scheduler_linked"]),
            }
            events.append(edge)
            for tracker in request_stack_by_thread.get(call.get("thread"), []):
                if tracker["proc_address"] == proc_address:
                    if function == "HSD_GObjProc_8038FCE4":
                        tracker["nested_owner_unlink"] = True
                        tracker["nested_scheduler_unlink"] = True
                    elif function == "HSD_GObjProc_8038FC18":
                        tracker["nested_scheduler_unlink"] = True
            continue

    for thread, stack in request_stack_by_thread.items():
        if stack:
            raise RegistrationProblem("captured trace ends inside an FE24 removal request")
    if source_call_count != terminal.get("calls"):
        raise RegistrationProblem("captured trace call count differs from validated entries")
    if terminal_sequence != len(rows) - 1:
        raise RegistrationProblem("captured footer is not the last validated trace row")
    if any(obj["live"] is not True for obj in active_by_ptr.values()):
        raise RegistrationProblem("internal tracked allocation liveness state is inconsistent")
    live_by_pool = {POOL_NAMES[kind]: 0 for kind in POOL_NAMES}
    for obj in active_by_ptr.values():
        live_by_pool[obj["pool"]] += 1
    return {
        "schema": OUTPUT_SCHEMA,
        "version": OUTPUT_VERSION,
        "scope": "observed-gobjproc-registration-edges-only",
        "trace": {
            "schema": TRACE_SCHEMA,
            "version": 1,
            "profile_sha256": header["profile_sha256"],
            "capture_scope": header.get("scope"),
            "start": header.get("start"),
            "terminal_sequence": terminal_sequence,
            "terminal_status": terminal.get("status"),
        },
        "source": {
            "dol_sha1": profile["dol_sha1"],
            "source_revision": profile["source_revision"],
            "symbols_sha256": profile["symbols_sha256"],
            "pools": {POOL_NAMES[kind]: {
                "pool_kind": kind,
                "source_symbol": POOL_SYMBOLS[kind],
                "source_address": _hex(pool_addresses[kind]),
            } for kind in POOL_NAMES},
        },
        "generation_policy": "one-monotonic-counter-across-tracked-pools; source-local-only",
        "claims": [
            "allocation/free events are observations from tracked HSD_ObjAlloc/HSD_ObjFree calls",
            "HSD_ObjAllocInit returns create a new tracked pool epoch; prior allocations remain reset residuals and are not reported as frees",
            "_HSD_ObjAllocForgetMemory is only an observed allocator-registry reset and does not imply pool objects were freed",
            "SetupProc links are joined through nested observed allocation and returned pointer",
            "FE24 is a request; only HSD_ObjFree is reported as an actual free",
            "source addresses and local generations are not cross-runtime comparison fields",
            "unselected callback symbols and allocator pools are outside this extraction scope",
            "this output does not prove all allocator writers or whole-game lifecycle parity",
        ],
        "summary": {
            "source_calls": source_call_count,
            "next_local_generation": local_generation + 1,
            "allocations": {POOL_NAMES[kind]: counters[f"alloc_{POOL_NAMES[kind]}"]
                            for kind in POOL_NAMES},
            "frees": {POOL_NAMES[kind]: counters[f"free_{POOL_NAMES[kind]}"]
                      for kind in POOL_NAMES},
            "setup_proc_returns": counters["setup_proc"],
            "links": counters["proc_link"],
            "scheduler_unlinks": counters["proc_scheduler_unlink"],
            "owner_unlinks": counters["proc_owner_unlink"],
            "remove_requests": counters["proc_remove_request"],
            "live_at_capture_end": live_by_pool,
            "pool_epochs": {POOL_NAMES[kind]: pool_epoch[kind] for kind in POOL_NAMES},
            "pool_reinitializations": {
                POOL_NAMES[kind]: counters[f"pool_reinitialize_{POOL_NAMES[kind]}"]
                for kind in POOL_NAMES
            },
            "allocator_registry_forgets": counters["allocator_registry_forget"],
            "reset_residual_count": len(reset_residuals),
            "reset_residuals": [{
                "pool_kind": obj["pool_kind"], "pool": obj["pool"],
                "pool_epoch": obj["pool_epoch"], "allocation_generation": obj["generation"],
                "source_address": _hex(obj["address"]),
                "reset_sequence": obj["residual_sequence"],
            } for obj in reset_residuals],
            "free_calls_unmatched_to_active_generation": sum(
                1 for event in events if event.get("event") == "free_return" and
                event.get("allocation_generation") is None),
            "free_calls_with_prior_residual_alias": sum(
                1 for event in events if event.get("event") == "free_return" and
                event.get("allocation_identity_status") == "active_current_epoch_with_prior_residual_alias"),
            "requests_without_observed_later_free": sum(
                len(items) for items in pending_requests.values()),
            "lifecycle_complete": False,
        },
        "events": events,
    }


def load_and_validate(profile_path: Path, dol_path: Path, symbols_path: Path,
                      source_tools: Path) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    """Load only the pinned source helpers and authenticate profile provenance."""
    tools_path = source_tools.resolve()
    if not tools_path.is_dir():
        raise RegistrationProblem("--source-tools must name the checked S/tools directory")
    for name, expected_sha in SOURCE_HELPER_SHA256.items():
        helper = tools_path / name
        if not helper.is_file() or hashlib.sha256(helper.read_bytes()).hexdigest() != expected_sha:
            raise RegistrationProblem(f"source helper identity differs from the reviewed reader: {name}")
    sys.path.insert(0, str(tools_path))
    try:
        replay = importlib.import_module("allocation_history_replay")
        retail = importlib.import_module("retail_allocation_profile")
        profile_generator = importlib.import_module("generate_reference_allocation_profile")
    except ImportError as error:
        raise RegistrationProblem(f"cannot load pinned allocation-history helpers: {error}") from error
    profile = replay.load_profile(profile_path)
    profile_generator.validate_profile(profile)
    if profile.get("version") not in (4, 5) or profile.get("registration_profile") != REGISTRATION_PROFILE:
        raise RegistrationProblem("the supplied profile is not an explicit v4/v5 registration profile")
    if profile.get("profile_sha256") is not None:
        raise RegistrationProblem("profile file must not self-assert its own digest")
    expected_profile = EXPECTED_PROFILE_SHA256_V5 if profile["version"] == 5 else EXPECTED_PROFILE_SHA256
    if hashlib.sha256(profile_path.read_bytes()).hexdigest() != expected_profile:
        raise RegistrationProblem("registration profile differs from the original observer build identity")
    symbols = retail.read_symbols(symbols_path)
    replay.validate_provenance(
        {"profile_sha256": hashlib.sha256(profile_path.read_bytes()).hexdigest()},
        profile, profile_path, dol_path, symbols_path)
    # Verify each selected function address and source ABI against the original
    # symbol table and exact DOL body, independently of the generated profile.
    dol = retail.Dol(dol_path)
    function_table = {row["name"]: row for row in profile["functions"]}
    for name, arity in FUNCTION_ARITIES.items():
        expected = symbols.get(name)
        actual = function_table.get(name)
        if not isinstance(expected, Mapping) or not isinstance(actual, Mapping):
            raise RegistrationProblem(f"selected source function {name} is absent from profile or symbols")
        if (actual.get("address") != expected.get("address") or
                actual.get("size") != expected.get("size") or actual.get("argc") != arity):
            raise RegistrationProblem(f"profile function identity differs from source for {name}")
        body = dol.read(_u32(expected.get("address"), f"symbols.{name}.address"),
                        _u32(expected.get("size"), f"symbols.{name}.size"))
        if hashlib.sha256(body).hexdigest() != actual.get("body_sha256"):
            raise RegistrationProblem(f"profile body hash differs from owned DOL for {name}")
    return replay, profile, symbols


def extract(trace_path: Path, profile_path: Path, dol_path: Path, symbols_path: Path,
            source_tools: Path, temp_directory: Path | None = None) -> dict[str, Any]:
    replay, profile, symbols = load_and_validate(profile_path, dol_path, symbols_path, source_tools)
    loaded = replay.load_trace(trace_path, disk_backed=True, temp_directory=temp_directory)
    header, rows, enters, returns = loaded
    try:
        replay.validate_provenance(header, profile, profile_path, dol_path, symbols_path)
        return reduce_registration_trace(header, rows, enters, returns, profile, symbols)
    finally:
        close = getattr(rows, "close", None)
        if callable(close):
            close()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--dol", type=Path, required=True)
    parser.add_argument("--symbols", type=Path, required=True)
    parser.add_argument("--source-tools", type=Path, required=True,
                        help="checked Stadium checkout tools directory")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--temp-directory", type=Path,
                        help="owned external directory for the existing disk-backed trace reader")
    args = parser.parse_args(argv)
    try:
        result = extract(args.trace, args.profile, args.dol, args.symbols,
                         args.source_tools, args.temp_directory)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    except Exception as error:
        print(f"registration extraction refused: {error}", file=sys.stderr)
        return 2
    print(json.dumps({"status": "parsed", "events": len(result["events"]),
                      "summary": result["summary"], "output": str(args.output)},
                     sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
