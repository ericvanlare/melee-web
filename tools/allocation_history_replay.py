#!/usr/bin/env python3
"""Replay the independently-derived, address-only allocation prefix.

The retail JSONL is an observation stream.  This tool never seeds the model
with observed pointers: it derives the OS arena/heap roots from a declared
cold-boot context, drives the merged C++ source-address model, and compares
returned source identities with the observation.  Unsupported lifecycle modes
end the report at their first entry rather than receiving guessed state.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
MODEL_SOURCE = ROOT / "tests/allocation_history_model.cpp"
MODEL_IMPL = ROOT / "src/source_address_context.cpp"
HANDLE_IMPL = ROOT / "src/source_handle_context.cpp"
ARAM_IMPL = ROOT / "src/source_aram_context.cpp"


class ReplayProblem(Exception):
    def __init__(self, kind: str, message: str, *, call: dict[str, Any] | None = None):
        super().__init__(message)
        self.kind = kind
        self.message = message
        self.call = call


def parse_u32(value: Any, context: str) -> int:
    if isinstance(value, bool) or (not isinstance(value, (int, str))):
        raise ReplayProblem("input", f"{context} must be an integer")
    try:
        result = int(value, 0) if isinstance(value, str) else int(value)
    except (TypeError, ValueError) as error:
        raise ReplayProblem("input", f"{context} is not an integer") from error
    if result < 0 or result > 0xFFFFFFFF:
        raise ReplayProblem("input", f"{context} is outside source u32")
    return result


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha1_file(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_sha256(value: Any) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def load_context(path: Path | None, overrides: dict[str, int | None],
                 verified: dict[str, Any] | None = None) -> dict[str, int]:
    if verified is not None and any(value is not None for value in overrides.values()):
        raise ReplayProblem("provenance", "independently-derived retail roots cannot be overridden numerically")
    value: dict[str, Any] = {}
    if verified is not None:
        if not isinstance(verified.get("root"), dict):
            raise ReplayProblem("provenance", "verified boot context lacks a root object")
        value = dict(verified["root"])
    if path is not None:
        try:
            declared = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError) as error:
            raise ReplayProblem("input", f"cannot read cold-boot context {path}: {error}") from error
        if not isinstance(declared, dict):
            raise ReplayProblem("input", "cold-boot context must be a JSON object")
        if isinstance(declared.get("root"), dict):
            declared = {**declared, **declared["root"]}
        if verified is None:
            value = declared
        else:
            for key in ("arena_lo", "arena_hi", "heap_max_num", "audio_heap_size"):
                if key in declared and key in value and parse_u32(declared[key], f"context.{key}") != parse_u32(value[key], f"derived.{key}"):
                    raise ReplayProblem("provenance", f"declared cold-boot context differs from independently-derived {key}")
            # Optional metadata (for example a derived ARAM arena) remains
            # source-owned; a numeric declaration cannot override it.
            for key in ("lbmemory_arena_lo", "lbmemory_arena_hi"):
                if key in declared and key in value and parse_u32(declared[key], f"context.{key}") != parse_u32(value[key], f"derived.{key}"):
                    raise ReplayProblem("provenance", f"declared cold-boot context differs from independently-derived {key}")
    aliases = {
        "arena_lo": ("arena_lo", "os_init_arena_lo", "initial_arena_lo"),
        "arena_hi": ("arena_hi", "os_init_arena_hi", "initial_arena_hi"),
        "heap_max_num": ("heap_max_num", "iparam_heap_max_num"),
        "audio_heap_size": ("audio_heap_size", "iparam_audio_heap_size"),
        # lbMemory's ARAM bounds are a separate source context.  They are
        # supplied by the declared hardware boot configuration; allocator
        # observations cannot establish them.
        "lbmemory_arena_lo": ("lbmemory_arena_lo", "lbmemory_aram_lo", "aram_lo"),
        "lbmemory_arena_hi": ("lbmemory_arena_hi", "lbmemory_aram_hi", "aram_hi"),
        "aram_base": ("aram_base", "source_aram_base"),
        "aram_size": ("aram_size", "source_aram_size", "hardware_aram_size"),
    }
    result: dict[str, int] = {}
    for key, names in aliases.items():
        supplied = overrides.get(key)
        if supplied is not None:
            result[key] = supplied
            continue
        for name in names:
            if name in value:
                result[key] = parse_u32(value[name], f"context.{name}")
                break
    required = ("arena_lo", "arena_hi", "heap_max_num", "audio_heap_size")
    missing = [key for key in required if key not in result]
    if missing:
        raise ReplayProblem(
            "input",
            "cold-boot context must explicitly declare " + ", ".join(missing) +
            "; observed OSCreateHeap arguments cannot define replay roots",
        )
    if not result["arena_lo"] < result["arena_hi"]:
        raise ReplayProblem("input", "cold-boot arena bounds are not ordered")
    if not result["heap_max_num"] or result["heap_max_num"] > 32:
        raise ReplayProblem("input", "cold-boot heap_max_num is outside the bounded source range")
    if result["audio_heap_size"] % 32:
        raise ReplayProblem("input", "audio_heap_size must be 32-byte aligned")
    descriptor_end = (result["arena_lo"] + result["heap_max_num"] * 12 + 31) & ~31
    arena_end = result["arena_hi"] & ~31
    if descriptor_end >= arena_end:
        raise ReplayProblem("input", "cold-boot arena cannot hold heap descriptors")
    result.update({
        "arena_start": descriptor_end,
        "arena_end": arena_end,
        "audio_begin": descriptor_end,
        "audio_end": descriptor_end + result["audio_heap_size"],
        "main_begin": descriptor_end + result["audio_heap_size"],
        "main_end": arena_end,
    })
    if result["audio_end"] > arena_end:
        raise ReplayProblem("input", "audio heap exceeds independently-derived arena end")
    return result


def load_trace(path: Path) -> tuple[dict[str, Any], list[dict[str, Any]], dict[int, dict[str, Any]], dict[int, dict[str, Any]]]:
    header: dict[str, Any] | None = None
    rows: list[dict[str, Any]] = []
    enters: dict[int, dict[str, Any]] = {}
    returns: dict[int, dict[str, Any]] = {}
    try:
        stream = path.open()
    except OSError as error:
        raise ReplayProblem("input", f"cannot read allocation trace {path}: {error}") from error
    with stream:
        for lineno, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError as error:
                raise ReplayProblem("stream", f"trace line {lineno} is invalid JSON: {error}") from error
            if not isinstance(row, dict):
                raise ReplayProblem("stream", f"trace line {lineno} is not an object")
            record = row.get("record")
            if record == "header":
                if header is not None or rows:
                    raise ReplayProblem("stream", f"trace line {lineno} has a misplaced header")
                header = row
            elif record == "enter":
                if header is None:
                    raise ReplayProblem("stream", f"trace line {lineno} precedes the header")
                call = parse_u32(row.get("call"), f"trace line {lineno}.call")
                if call in enters:
                    raise ReplayProblem("stream", f"duplicate entry for call {call}")
                enters[call] = row
            elif record == "return":
                call = parse_u32(row.get("call"), f"trace line {lineno}.call")
                if call in returns:
                    raise ReplayProblem("stream", f"duplicate return for call {call}")
                returns[call] = row
            elif record in ("end", "error", "repeated_stop"):
                pass
            else:
                raise ReplayProblem("stream", f"trace line {lineno} has unsupported record {record!r}")
            rows.append(row)
    if header is None:
        raise ReplayProblem("stream", "allocation trace has no header")
    if header.get("schema") != "melee-web-original-allocation-history" or header.get("version") != 1:
        raise ReplayProblem("stream", "unsupported allocation trace schema or version")
    sequences = [row.get("sequence") for row in rows]
    if any(isinstance(value, bool) or not isinstance(value, int) or value < 0 for value in sequences):
        raise ReplayProblem("stream", "allocation trace has a missing or invalid sequence")
    if sequences != list(range(len(rows))):
        raise ReplayProblem("stream", "allocation trace sequence is not contiguous from zero")
    if rows[0].get("record") != "header":
        raise ReplayProblem("stream", "allocation trace header is not the first record")
    end_rows = [row for row in rows if row.get("record") == "end"]
    if len(end_rows) > 1 or (end_rows and rows[-1] is not end_rows[0]):
        raise ReplayProblem("stream", "allocation trace end record is duplicated or not final")
    for row in rows:
        if row.get("record") == "error" and not isinstance(row.get("error"), str):
            raise ReplayProblem("stream", "allocation trace error record lacks a textual reason")
        if row.get("record") == "end":
            if row.get("status") not in ("captured", "incomplete", "error"):
                raise ReplayProblem("stream", "allocation trace end status is invalid")
            if not isinstance(row.get("calls"), int) or row.get("calls") < 0:
                raise ReplayProblem("stream", "allocation trace end call count is invalid")
            if "pending_calls" in row and not isinstance(row.get("pending_calls"), list):
                raise ReplayProblem("stream", "allocation trace end pending_calls is invalid")
            if row.get("status") == "captured" and row.get("calls") != len(enters):
                raise ReplayProblem("stream", "captured trace end call count differs from entries")
    # Validate the complete per-thread call nesting before replay can consume
    # any allocator result. A return that merely has a known call id is not
    # sufficient: the source collector's stack boundary must also agree.
    stacks: dict[Any, list[int]] = {}
    for row in rows:
        record = row.get("record")
        if record == "enter":
            call = parse_u32(row.get("call"), "trace enter.call")
            thread = row.get("thread")
            stack = stacks.setdefault(thread, [])
            parent = row.get("parent")
            if parent is not None:
                parent_id = parse_u32(parent, "trace enter.parent")
                if parent_id not in enters or enters[parent_id].get("sequence", -1) >= row.get("sequence", 0):
                    raise ReplayProblem("stream", "trace enter parent is not an earlier entry")
                parent_entry = enters[parent_id]
                if parent_entry.get("thread") != thread:
                    raise ReplayProblem("stream", "trace enter parent is on a different source thread")
                if not stack or stack[-1] != parent_id:
                    raise ReplayProblem("stream", "trace enter parent is not the current source call")
            elif stack:
                raise ReplayProblem("stream", "trace nested enter is missing its current-call parent")
            stack.append(call)
        elif record == "return":
            call = parse_u32(row.get("call"), "trace return.call")
            entry = enters.get(call)
            if entry is None:
                raise ReplayProblem("stream", f"return {call} has no entry")
            if row.get("thread") != entry.get("thread"):
                raise ReplayProblem("stream", f"return {call} thread differs from entry")
            if row.get("function") != entry.get("function"):
                raise ReplayProblem("stream", f"return {call} function differs from entry")
            stack = stacks.setdefault(row.get("thread"), [])
            if not stack or stack[-1] != call:
                raise ReplayProblem("stream", f"return {call} is not the top source call")
            stack.pop()
    end_status = end_rows[0].get("status") if end_rows else None
    unclosed = sorted(call for stack in stacks.values() for call in stack)
    if end_rows:
        end_pending = end_rows[0].get("pending_calls")
        if end_pending is not None:
            declared_pending = []
            for value in end_pending:
                if isinstance(value, dict):
                    pending_id = parse_u32(value.get("call"), "trace end.pending_calls.call")
                    entry = enters.get(pending_id)
                    if entry is None:
                        raise ReplayProblem("stream", "trace end.pending_calls references an unknown call")
                    for field in ("function", "thread", "sp", "lr", "args", "parent"):
                        if field in value and value.get(field) != entry.get(field):
                            raise ReplayProblem("stream", f"trace end.pending_calls disagrees with entry field {field}")
                else:
                    # Synthetic fixtures may use the compact call-id form;
                    # retail collector output carries the full entry object.
                    pending_id = parse_u32(value, "trace end.pending_calls")
                declared_pending.append(pending_id)
            if len(set(declared_pending)) != len(declared_pending):
                raise ReplayProblem("stream", "trace end pending_calls contains a duplicate call")
            declared_pending.sort()
            if declared_pending != unclosed:
                raise ReplayProblem("stream", "trace end pending_calls differs from the source call stacks")
        if end_status == "captured":
            if unclosed:
                raise ReplayProblem("stream", "captured trace end has unfinished source calls")
            if any(row.get("record") == "error" for row in rows):
                raise ReplayProblem("stream", "captured trace end cannot contain an error record")
    return header, rows, enters, returns


def load_profile(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise ReplayProblem("input", f"cannot read allocation profile {path}: {error}") from error
    if not isinstance(value, dict) or value.get("schema") != "melee-web-original-allocation-profile":
        raise ReplayProblem("input", "unsupported allocation profile schema")
    if (type(value.get("version")) is not int or value["version"] not in (1, 2)
            or not isinstance(value.get("functions"), list)):
        raise ReplayProblem("input", "unsupported allocation profile version")
    return value


def validate_provenance(header: dict[str, Any], profile: dict[str, Any], profile_path: Path,
                        dol: Path | None, symbols: Path | None) -> None:
    expected_profile = header.get("profile_sha256")
    if not isinstance(expected_profile, str) or len(expected_profile) != 64:
        raise ReplayProblem("provenance", "trace profile_sha256 is required for replay")
    if expected_profile != sha256_file(profile_path):
        raise ReplayProblem("provenance", "trace profile_sha256 does not match supplied profile")
    source_revision = profile.get("source_revision")
    dol_identity = profile.get("dol_sha1")
    if not isinstance(source_revision, str) or not source_revision:
        raise ReplayProblem("provenance", "allocation profile source_revision is required")
    if not isinstance(dol_identity, str) or len(dol_identity) != 40:
        raise ReplayProblem("provenance", "allocation profile dol_sha1 is required")
    synthetic = source_revision.startswith("synthetic-")
    if not synthetic and dol is None:
        raise ReplayProblem("provenance", "retail replay requires the explicitly supplied original DOL path")
    if not synthetic and symbols is None:
        raise ReplayProblem("provenance", "retail replay requires the explicitly supplied source symbol path")
    if dol is not None:
        actual = sha1_file(dol)
        if actual != dol_identity:
            raise ReplayProblem("provenance", f"DOL SHA-1 differs from profile: {actual} != {dol_identity}")
    if symbols is not None:
        expected = profile.get("symbols_sha256")
        if not isinstance(expected, str) or len(expected) != 64:
            raise ReplayProblem("provenance", "allocation profile symbols_sha256 is required")
        actual = sha256_file(symbols)
        if actual != expected:
            raise ReplayProblem("provenance", f"symbol SHA-256 differs from profile: {actual} != {expected}")


def parse_result(line: str) -> dict[str, Any]:
    value = json.loads(line)
    if not isinstance(value, dict):
        raise ReplayProblem("model", "allocator driver emitted a non-object result")
    return value


class ModelDriver:
    def __init__(self, *, compiler: str | None = None, wasm: bool = False,
                 node: str | None = None, emxx: str | None = None,
                 source: Path = MODEL_SOURCE, extra_impls: Iterable[Path] = ()):
        self.temp = tempfile.TemporaryDirectory(prefix="allocation-history-model-")
        self.path = Path(self.temp.name) / ("model.js" if wasm else "model")
        self.wasm = wasm
        if wasm:
            emxx_path = Path(emxx) if emxx else ROOT / ".deps/emsdk/upstream/emscripten/em++.py"
            node_path = node or self._discover_node()
            if not emxx_path.is_file() or not node_path:
                raise ReplayProblem("input", "checked Wasm driver requested but Emscripten/Node is unavailable")
            command = [sys.executable, str(emxx_path)]
        else:
            compiler = compiler or os.environ.get("CXX", "c++")
            if not shutil.which(compiler):
                raise ReplayProblem("input", f"C++ compiler is unavailable: {compiler}")
            command = [compiler]
            node_path = None
        command += ["-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-Isrc",
                    str(MODEL_IMPL), str(HANDLE_IMPL), str(ARAM_IMPL),
                    *(str(path) for path in extra_impls), str(source)]
        if wasm:
            command += ["-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2",
                        "-sSAFE_HEAP=1", "-o", str(self.path)]
        else:
            command += ["-o", str(self.path)]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            raise ReplayProblem("model", f"allocator model build failed:\n{result.stdout}\n{result.stderr}")
        self.runner = [node_path, str(self.path)] if wasm else [str(self.path)]

    def close(self) -> None:
        self.temp.cleanup()

    def __del__(self):
        try:
            self.temp.cleanup()
        except Exception:
            pass

    @staticmethod
    def _discover_node() -> str | None:
        config = ROOT / ".deps/emsdk/.emscripten"
        if config.is_file():
            try:
                tree = ast.parse(config.read_text())
                setting = next(ast.literal_eval(statement.value)
                               for statement in tree.body
                               if isinstance(statement, ast.Assign)
                               and any(isinstance(target, ast.Name) and target.id == "NODE_JS"
                                       for target in statement.targets))
                candidate = Path(str(setting).replace("$CFGDIR", str(config.parent))).resolve()
                if candidate.is_file():
                    return str(candidate)
            except (StopIteration, SyntaxError, ValueError, OSError):
                pass
        return shutil.which("node")

    def run(self, commands: list[str]) -> list[dict[str, Any]]:
        if not commands:
            return []
        result = subprocess.run(self.runner, cwd=ROOT, input="\n".join(commands) + "\n",
                                capture_output=True, text=True)
        if result.returncode:
            raise ReplayProblem("model", f"allocator model failed:\n{result.stdout}\n{result.stderr}")
        try:
            return [parse_result(line) for line in result.stdout.splitlines() if line.strip()]
        except json.JSONDecodeError as error:
            raise ReplayProblem("model", f"allocator model emitted invalid JSON: {error}") from error


@dataclass
class Action:
    command: str
    call: dict[str, Any]
    row: dict[str, Any]
    kind: str
    expected: int | None = None
    pool: int | None = None
    nested_raw: list[dict[str, Any]] | None = None
    custom: dict[str, Any] | None = None


CONTEXT_ONLY = {
    "OSSetArenaLo", "OSSetArenaHi", "OSAllocFromArenaLo", "OSAllocFromArenaHi",
    "HSD_OSInit", "HSD_AllocateXFB", "HSD_AllocateFifo", "HSD_MemAlloc", "HSD_Free",
    "HSD_SetHeap", "lbHeap_80015F3C", "gm_Scene_Vs_OnEnter", "gm_Scene_Vs_OnExit",
}
UNSUPPORTED_AT_ENTRY = {
    "HSD_CreateMainHeap": "main-heap replacement lifetime orchestration is not implemented",
    "lbHeap_80015900": "game-heap transient/persistent partition and replacement lifetime orchestration is not implemented",
    "lbHeap_80015BD0": "game heap wrapper is beyond the declared OS/HSD prefix",
    "lbHeap_80015CA8": "game heap wrapper is beyond the declared OS/HSD prefix",
    "lbHeap_80015D6C": "game heap transition is beyond the declared OS/HSD prefix",
    "Fighter_Create": "fighter identity pools are outside the boot prefix model",
}

LBMEMORY_FUNCTIONS = {
    "lbMemory_8001564C", "lbMemory_80014E24", "lbMemory_80014EEC",
    "lbMemory_80014FC8", "lbMemFreeToHeap", "lbMemory_8001529C",
    "lbMemory_800154D4", "lbMemory_800155A4",
}


def call_chain(call: dict[str, Any], enters: dict[int, dict[str, Any]]) -> list[dict[str, Any]]:
    chain: list[dict[str, Any]] = []
    parent = call.get("parent")
    seen: set[int] = set()
    while parent is not None:
        parent_id = parse_u32(parent, "parent")
        if parent_id in seen or parent_id not in enters:
            raise ReplayProblem("stream", f"call {call.get('call')} has an invalid parent chain", call=call)
        seen.add(parent_id)
        parent_call = enters[parent_id]
        chain.append(parent_call)
        parent = parent_call.get("parent")
    return chain


def validate_boot_observation(header: dict[str, Any], boot: dict[str, Any], entry: int) -> None:
    if (header.get("start") != "original_dol_entry"
            or header.get("writes_game_state") is not False
            or header.get("initial_pc") != entry):
        raise ReplayProblem("provenance", "retail trace must start at the original DOL entry without state writes")
    observed = header.get("observed_boot_context")
    if not isinstance(observed, dict):
        raise ReplayProblem("provenance", "retail trace lacks observed boot context")
    for key in ("arena_lo", "arena_hi", "bi2", "memory_size"):
        if key not in boot or parse_u32(observed.get(key), f"observed boot {key}") != boot[key]:
            raise ReplayProblem("provenance", f"observed boot {key} differs from independent original context")


def replay(trace: Path, profile_path: Path, context_path: Path | None,
           overrides: dict[str, int | None], dol: Path | None, symbols: Path | None,
           *, checked_wasm: bool = False, require_complete: bool = False,
           verified_context: dict[str, Any] | None = None) -> dict[str, Any]:
    profile = load_profile(profile_path)
    retail = not str(profile.get("source_revision", "")).startswith("synthetic-")
    if retail and verified_context is None:
        raise ReplayProblem("provenance", "retail replay requires independently derived boot context")
    header, rows, enters, returns = load_trace(trace)
    validate_provenance(header, profile, profile_path, dol, symbols)
    if retail:
        identities = verified_context.get("identities", {})
        if (identities.get("source_revision") != profile.get("source_revision")
                or identities.get("symbols_sha256") != profile.get("symbols_sha256")
                or identities.get("dol_sha256") != sha256_file(dol)):
            raise ReplayProblem("provenance", "independent boot inputs differ from the allocation profile")
        entry = int.from_bytes(dol.read_bytes()[0xE0:0xE4], "big")
        if profile.get("entry") != entry:
            raise ReplayProblem("provenance", "profile entry differs from original DOL")
        validate_boot_observation(header, verified_context.get("boot", {}), entry)
    context = load_context(context_path, overrides, verified=verified_context)
    profile_params = profile.get("initial_dol_words", {})
    for key, profile_key in (("heap_max_num", "iparam_heap_max_num"),
                             ("audio_heap_size", "iparam_audio_heap_size")):
        if profile_key in profile_params and parse_u32(profile_params[profile_key], profile_key) != context[key]:
            raise ReplayProblem("provenance", f"context {key} differs from DOL-initial {profile_key}")

    if retail:
        from .allocation_lifetime_replay import replay_lifetimes
        return replay_lifetimes(trace, profile_path, profile, header, rows, enters, returns,
                                context, verified_context, checked_wasm=checked_wasm,
                                require_complete=require_complete)

    expected_names = {item.get("name") for item in profile.get("functions", []) if isinstance(item, dict)}
    supported_names = expected_names | CONTEXT_ONLY | set(UNSUPPORTED_AT_ENTRY)
    actions: list[Action] = []
    skipped_raw: dict[int, list[dict[str, Any]]] = {}
    observed_to_derived: dict[int, int] = {}
    pool_heap: dict[int, int] = {}
    current_os_heap = -1
    current_hsd_heap = -1
    os_create_index = 0
    os_init_seen = False
    object_heap_top = 0
    initialized_pools: set[int] = set()
    first_unsupported: dict[str, Any] | None = None
    first_call_seen = None
    rows_by_sequence = {row["sequence"]: row for row in rows}
    repeated_counts: dict[int, int] = {}
    handle_initialized = False
    aram_initialized = False
    aram_cursor: int | None = None
    aram_lengths: list[int] = []
    handle_root: dict[str, int] | None = None
    static_layout = verified_context.get("static_layout", {}) if verified_context is not None else {}
    # The fixed offsets are source layout facts from lbmemory.c, while the
    # allocator global address comes from the pinned profile's DOL symbols.
    global_symbol = profile.get("globals", {}).get("lbMemory_804318B0")
    if isinstance(global_symbol, dict) and "address" in global_symbol:
        global_base = parse_u32(global_symbol["address"], "profile.globals.lbMemory_804318B0.address")
        if retail and global_base != static_layout.get("lbmemory_allocator"):
            raise ReplayProblem("provenance", "lbMemory global differs from independently derived source layout")
        if (("lbmemory_arena_lo" in context and "lbmemory_arena_hi" in context)
                or ("aram_base" in context and "aram_size" in context)):
            handle_root = {
                "allocator": global_base,
                "mem_entries": global_base + 0x8,
                "heap_handles": global_base + 0x638,
                "current_handle_slot": global_base + 0x69C,
            }

    def require_handle_root(call: dict[str, Any]) -> bool:
        if handle_root is None:
            unsupported(call, "lbMemory requires a source/DOL-derived global layout and declared ARAM bounds")
            return False
        return True

    static_pool_addresses = set()
    if isinstance(static_layout, dict) and isinstance(static_layout.get("pool_descriptors"), dict):
        for address in static_layout["pool_descriptors"].values():
            if isinstance(address, int) and 0 <= address <= 0xFFFFFFFF:
                static_pool_addresses.add(address)
    static_pool_ids = {}
    if isinstance(static_layout, dict) and isinstance(static_layout.get("pool_descriptors"), dict):
        for index, (_, address) in enumerate(sorted(static_layout["pool_descriptors"].items()), 1):
            if isinstance(address, int) and 0 <= address <= 0xFFFFFFFF:
                static_pool_ids[address] = index
    static_lbheap_descriptors = static_layout.get("lbheap_descriptors") if isinstance(static_layout, dict) else None

    def unsupported(call: dict[str, Any], reason: str) -> None:
        nonlocal first_unsupported
        if first_unsupported is None:
            first_unsupported = {"call": call.get("call"), "function": call.get("function"),
                                 "reason": reason, "sequence": call.get("sequence")}

    for row in rows:
        record = row.get("record")
        if record == "repeated_stop":
            original_sequence = row.get("original_sequence")
            if not isinstance(original_sequence, int) or original_sequence not in rows_by_sequence:
                raise ReplayProblem("stream", "repeated_stop references an unknown original sequence")
            if original_sequence >= row.get("sequence", -1):
                raise ReplayProblem("stream", "repeated_stop must follow its original boundary")
            original = rows_by_sequence[original_sequence]
            expected_phase = "enter" if original.get("record") == "enter" else "return" if original.get("record") == "return" else None
            if expected_phase is None or row.get("phase") != expected_phase or row.get("function") != original.get("function"):
                raise ReplayProblem("stream", "repeated_stop boundary identity differs from its original row")
            if row.get("machine") != original.get("machine") or row.get("observed") != original.get("observed"):
                raise ReplayProblem("stream", "repeated_stop machine or allocator metadata changed")
            repeated_counts[original_sequence] = repeated_counts.get(original_sequence, 0) + 1
            if repeated_counts[original_sequence] > 32:
                raise ReplayProblem("stream", "repeated_stop exceeded the source progress bound")
            continue
        if record == "enter":
            call = row
            first_call_seen = call if first_call_seen is None else first_call_seen
            function = call.get("function")
            if function not in supported_names:
                unsupported(call, f"function {function!r} is absent from the declared source profile")
                break
            if function in UNSUPPORTED_AT_ENTRY:
                unsupported(call, UNSUPPORTED_AT_ENTRY[function])
                break
            if function in LBMEMORY_FUNCTIONS:
                if not require_handle_root(call):
                    break
                if function != "lbMemory_8001564C" and not handle_initialized:
                    # The only pre-initialization handle call in the observed
                    # boot stream is the nested new_handle made by
                    # lbMemory_8001564C itself.
                    parent_id = call.get("parent")
                    parent = enters.get(parse_u32(parent_id, "parent")) if parent_id is not None else None
                    if not (parent and parent.get("function") == "lbMemory_8001564C"):
                        unsupported(call, "lbMemory handle operation precedes an independently-replayed initializer")
                        break
            continue
        if record != "return":
            continue
        call_id = parse_u32(row.get("call"), "return.call")
        call = enters.get(call_id)
        if call is None:
            raise ReplayProblem("stream", f"return {call_id} has no entry")
        if call.get("function") != row.get("function"):
            raise ReplayProblem("stream", f"call {call_id} function differs between entry and return", call=call)
        chain = call_chain(call, enters)
        ancestors = [item.get("function") for item in chain]
        function = call.get("function")
        result = parse_u32(row.get("result", 0), f"return {call_id}.result")
        args = [parse_u32(item, f"call {call_id}.args") for item in call.get("args", [])]

        if function in ("ARInit", "ARAlloc", "ARFree", "ARGetSize"):
            if "aram_base" not in context or "aram_size" not in context:
                unsupported(call, "ARAM operation requires independently-derived ARAM base and hardware size")
                break
            if function == "ARInit":
                if len(args) != 2:
                    raise ReplayProblem("validation", "ARInit arguments are incomplete", call=call)
                if retail:
                    stack_table = static_layout.get("aram_stack_table", {})
                    if (args[0] != stack_table.get("address")
                            or args[1] * 4 != stack_table.get("size")):
                        raise ReplayProblem("validation", "ARInit stack differs from independently derived static table", call=call)
                actions.append(Action(json.dumps({"op": "aram_init",
                                                   "initial_base": context["aram_base"],
                                                   "capacity": args[1],
                                                   "hardware_size": context["aram_size"]}),
                                      call, row, "aram_init", expected=result))
                aram_initialized = True
                aram_cursor = context["aram_base"]
                aram_lengths.clear()
                continue
            if not aram_initialized or aram_cursor is None:
                unsupported(call, "ARAM operation precedes independently-replayed ARInit")
                break
            if function == "ARAlloc":
                if len(args) != 1:
                    raise ReplayProblem("validation", "ARAlloc arguments are incomplete", call=call)
                expected = aram_cursor
                length = args[0]
                if length % 32 or expected + length > context["aram_size"]:
                    unsupported(call, "ARAM request is outside the declared source stack context")
                    break
                actions.append(Action(json.dumps({"op": "aram_alloc", "requested": length}),
                                      call, row, "aram_alloc", expected=expected,
                                      custom={"length": length}))
                aram_cursor += length
                aram_lengths.append(length)
                continue
            if function == "ARFree":
                if not aram_lengths:
                    unsupported(call, "ARFree has no independently-replayed LIFO allocation")
                    break
                length = aram_lengths.pop()
                aram_cursor -= length
                actions.append(Action('{"op":"aram_free"}', call, row,
                                      "aram_free", expected=aram_cursor,
                                      custom={"length": length}))
                continue
            if result != context["aram_size"]:
                raise ReplayProblem("validation", "ARGetSize differs from declared hardware context", call=call)
            actions.append(Action('{"op":"aram_size"}', call, row,
                                  "aram_size", expected=result))
            continue

        if function in LBMEMORY_FUNCTIONS:
            if handle_root is None:
                unsupported(call, "lbMemory requires a source/DOL-derived global layout and declared ARAM bounds")
                break
            if function in ("lbMemory_800154D4", "lbMemory_80014EEC", "lbMemory_80014FC8",
                            "lbMemFreeToHeap", "lbMemory_8001529C", "lbMemory_800155A4"):
                unsupported(call, "lbMemory handle lifetime/partition lacks an independently-derived source context")
                break
            if function == "lbMemory_80014E24":
                # The initializer's nested call consumes the first heap
                # descriptor itself.  Validate its source-visible fields and
                # map the observed identity to the model's current label; it
                # must not consume a second descriptor in the driver.
                parent_id = call.get("parent")
                parent = enters.get(parse_u32(parent_id, "parent")) if parent_id is not None else None
                if parent and parent.get("function") == "lbMemory_8001564C":
                    if aram_cursor is None:
                        unsupported(call, "lbMemory initializer lacks the derived ARAM stack boundary")
                        break
                    source_bounds = [aram_cursor, context["aram_size"]]
                    if len(args) != 2 or args != source_bounds:
                        raise ReplayProblem("validation", "lbMemory initializer bounds differ from declared ARAM context", call=call)
                    words = row.get("observed", {}).get("handle_words")
                    if words and words[:3] != [0, args[0], args[1]]:
                        raise ReplayProblem("validation", "lbMemory initializer handle fields differ from source context", call=call)
                    continue
                if len(args) != 2:
                    raise ReplayProblem("validation", "lbMemory handle creation arguments are incomplete", call=call)
                # No child handle bounds may enter the model from the
                # observation. The bounded replay has no independent source
                # expression for post-main-heap partitions yet.
                unsupported(call, "lbMemory child handle bounds lack an independently-derived source partition")
                break
            if function == "lbMemory_8001564C":
                if args:
                    raise ReplayProblem("validation", "lbMemory initializer unexpectedly received arguments", call=call)
                # Context::initialize contains the source initializer's
                # nested new_current/new_handle operation.  The child return
                # is checked above; this action validates the outer return and
                # drives the model exactly once.
                actions.append(Action(json.dumps({"op": "handle_init_from_aram", **handle_root}),
                                      call, row, "handle_init_from_aram", expected=None,
                                      custom={"label": "current"}))
                handle_initialized = True
                continue
        if function == "OSInitAlloc":
            if args != [context["arena_lo"], context["arena_hi"], context["heap_max_num"]]:
                raise ReplayProblem("validation", "OSInitAlloc arguments differ from independently-derived context", call=call)
            if result != context["arena_start"]:
                raise ReplayProblem("validation", "OSInitAlloc returned arena start differs from source derivation", call=call)
            os_init_seen = True
            continue
        if function == "OSCreateHeap":
            if not os_init_seen or os_create_index > 1:
                unsupported(call, "OS heap creation is outside the declared bootstrap root sequence")
                break
            expected = ((context["audio_begin"], context["audio_end"]),
                        (context["main_begin"], context["main_end"]))[os_create_index]
            if args != list(expected):
                raise ReplayProblem("validation", "OSCreateHeap bounds differ from source-derived root", call=call)
            if result != os_create_index:
                raise ReplayProblem("validation", "OSCreateHeap handle differs from source descriptor order", call=call)
            actions.append(Action(json.dumps({"op": "heap_create", "heap": result,
                                               "begin": expected[0], "end": expected[1]}), call, row, "heap_create"))
            os_create_index += 1
            continue
        if function == "OSSetCurrentHeap":
            new_heap = args[0] if args else 0xFFFFFFFF
            if result != (current_os_heap & 0xFFFFFFFF):
                raise ReplayProblem("validation", "OSSetCurrentHeap did not return previous source handle", call=call)
            current_os_heap = new_heap if new_heap != 0xFFFFFFFF else -1
            continue
        if function == "HSD_SetHeap":
            if args:
                current_hsd_heap = args[0]
            continue
        if function == "HSD_ObjSetHeap":
            if len(args) != 2:
                raise ReplayProblem("validation", "HSD_ObjSetHeap argument count is incomplete", call=call)
            if args[1] != 0:
                unsupported(call, "dedicated HSD object heap mode is not implemented")
                break
            object_heap_top = args[1]
            expected_size = context["main_end"] - context["main_begin"]
            if args[0] != expected_size and os_create_index == 2:
                raise ReplayProblem("validation", "HSD object heap size differs from source-derived main heap", call=call)
            continue
        if function == "HSD_OSInit":
            # HSD_OSInit selects the second descriptor after creating the
            # audio heap at index zero.  This is a source-derived handle, not
            # a pointer copied from a return observation.
            current_hsd_heap = 1
            continue
        if function == "HSD_ObjAllocInit":
            if len(args) != 3 or current_hsd_heap < 0:
                unsupported(call, "object pool initialization has no selected source heap")
                break
            init_words = row.get("observed", {}).get("pool_words")
            if init_words and init_words[0] & 3:
                unsupported(call, "HSD object pool limit flags are not implemented")
                break
            observed_pool = args[0]
            if verified_context is not None:
                if not static_pool_addresses:
                    unsupported(call, "object pool initialization lacks the DOL-derived static pool table")
                    break
                if observed_pool not in static_pool_ids:
                    raise ReplayProblem("validation", "HSD object pool descriptor is not a DOL-derived static object", call=call)
                pool = static_pool_ids[observed_pool]
            else:
                pool = observed_pool
            if pool in initialized_pools:
                unsupported(call, "same-generation HSD pool reset requires an explicit source reset boundary")
                break
            pool_heap[pool] = current_hsd_heap
            initialized_pools.add(pool)
            actions.append(Action(json.dumps({"op": "pool_init", "pool": pool,
                                               "heap": current_hsd_heap, "size": args[1],
                                               "align": args[2], "dedicated": 0,
                                               "number_limit": 0, "heap_limit": 0}),
                                  call, row, "pool_init", pool=pool))
            continue
        if function == "lbHeap_800158D0":
            if len(args) != 2:
                raise ReplayProblem("validation", "lbHeap lifetime setter arguments are incomplete", call=call)
            if not isinstance(static_lbheap_descriptors, list):
                unsupported(call, "lbHeap lifetime setter lacks the DOL-derived descriptor table")
                break
            # The terminating zero-type row does not describe a game heap.
            source_heaps = {item[0] for item in static_lbheap_descriptors
                            if len(item) == 4 and item[1] != 0}
            if args[0] not in source_heaps or args[1] not in (0, 1):
                unsupported(call, "lbHeap lifetime setter is outside the decoded game-heap descriptor domain")
                break
            # This source setter only changes the selected lifetime partition;
            # the actual planner remains unsupported at lbHeap_80015900.
            continue
        if function in ("HSD_ObjAllocAddFree", "HSD_ObjAlloc", "HSD_ObjFree"):
            observed_pool = args[0] if args else 0
            if verified_context is not None:
                if observed_pool not in static_pool_ids:
                    raise ReplayProblem("validation", "HSD object operation references a non-DOL pool descriptor", call=call)
                pool = static_pool_ids[observed_pool]
            else:
                pool = observed_pool
            if pool not in pool_heap:
                unsupported(call, "object operation references a pool without an independently replayed init")
                break
            entry_words = call.get("observed", {}).get("pool_words")
            if entry_words and entry_words[0] & 3:
                unsupported(call, "HSD object pool limit flags are not implemented")
                break
            # The descriptor is global, but HSD_MemAlloc consults the current
            # HSD heap at each refill.  This bounded model only admits the
            # source mode where that selected heap is the pool's initializer
            # heap; a different selection needs an independently-derived
            # refill/lifetime planner.
            heap_id = current_hsd_heap
            if heap_id < 0:
                unsupported(call, "object operation has no selected current HSD heap")
                break
            if heap_id != pool_heap[pool]:
                unsupported(call, "object pool refill selected a heap different from its independently-replayed initializer heap")
                break
            if function == "HSD_ObjAllocAddFree":
                if "HSD_ObjAlloc" in ancestors:
                    nested = skipped_raw.pop(call_id, [])
                    outer = next(item for item in chain if item.get("function") == "HSD_ObjAlloc")
                    outer_id = parse_u32(outer["call"], "objalloc.call")
                    skipped_raw.setdefault(outer_id, []).extend(nested)
                    continue
                if len(args) != 2:
                    raise ReplayProblem("validation", "HSD_ObjAllocAddFree arguments are incomplete", call=call)
                actions.append(Action(json.dumps({"op": "pool_add", "pool": pool,
                                                   "heap": heap_id, "count": args[1]}),
                                      call, row, "pool_add", expected=args[1], pool=pool,
                                      nested_raw=skipped_raw.pop(call_id, [])))
            elif function == "HSD_ObjAlloc":
                nested = skipped_raw.pop(call_id, [])
                for ancestor in chain:
                    if ancestor.get("function") == "HSD_ObjAllocAddFree":
                        nested.extend(skipped_raw.pop(parse_u32(ancestor["call"], "addfree.call"), []))
                actions.append(Action(json.dumps({"op": "pool_alloc", "pool": pool,
                                                   "heap": heap_id}), call, row, "pool_alloc",
                                      pool=pool, nested_raw=nested))
            else:
                if len(args) != 2:
                    raise ReplayProblem("validation", "HSD_ObjFree arguments are incomplete", call=call)
                unsupported(call, "HSD_ObjFree pointer producer identity is deferred until source lifetime replay")
                break
            continue
        if function == "OSAllocFromHeap":
            if len(args) != 2:
                raise ReplayProblem("validation", "OSAllocFromHeap arguments are incomplete", call=call)
            if any(parent in ("HSD_ObjAllocAddFree", "HSD_ObjAlloc") for parent in ancestors):
                addfree = next((item for item in chain if item.get("function") == "HSD_ObjAllocAddFree"), None)
                if addfree is not None:
                    skipped_raw.setdefault(parse_u32(addfree["call"], "addfree.call"), []).append(row)
                continue
            actions.append(Action(json.dumps({"op": "alloc", "heap": args[0], "requested": args[1]}),
                                  call, row, "alloc", expected=result))
            continue
        if function == "OSFreeToHeap":
            if len(args) != 2:
                raise ReplayProblem("validation", "OSFreeToHeap arguments are incomplete", call=call)
            if "HSD_ObjAlloc" in ancestors:
                continue
            unsupported(call, "OSFreeToHeap pointer producer identity is deferred until source lifetime replay")
            break
            continue
        if function == "OSDestroyHeap":
            unsupported(call, "heap destruction requires a declared replacement/lifetime context")
            break
        if function == "_HSD_ObjAllocForgetMemory":
            unsupported(call, "pool registry forget requires a declared heap replacement context")
            break
        if function in CONTEXT_ONLY:
            continue
        if function in ("Fighter_FirstInitialize_80067A84", "Fighter_Unload_8006DABC"):
            unsupported(call, "fighter pool lifetime is outside the boot prefix model")
            break
        # A function in the profile without a replay rule is deliberately
        # visible as the first missing dependency.
        unsupported(call, f"no replay rule for source function {function}")
        break

    driver = ModelDriver(wasm=False)
    outputs = driver.run([action.command for action in actions])
    if checked_wasm:
        wasm_driver = ModelDriver(wasm=True)
        wasm_outputs = wasm_driver.run([action.command for action in actions])
        if wasm_outputs != outputs:
            raise ReplayProblem("model", "native and checked-Wasm allocator drivers diverged")
    if len(outputs) != len(actions):
        raise ReplayProblem("model", f"allocator driver emitted {len(outputs)} rows for {len(actions)} actions")

    pointer_aliases = 0
    for action, output in zip(actions, outputs):
        status = output.get("status")
        call = action.call
        row = action.row
        if action.kind in ("aram_init", "aram_alloc", "aram_free", "aram_size"):
            if status != "ok":
                raise ReplayProblem("validation", f"ARAM model rejected {action.kind}: {status}", call=call)
            if action.kind == "aram_size":
                if output.get("size") != action.expected:
                    raise ReplayProblem("validation", "ARGetSize differs from source model", call=call)
            elif output.get("address") != action.expected:
                raise ReplayProblem("validation", "ARAM stack identity differs from source result", call=call)
            if action.kind == "aram_alloc":
                if output.get("size") != (action.custom or {}).get("length"):
                    raise ReplayProblem("validation", "ARAlloc length differs from source model", call=call)
            if action.kind == "aram_free" and output.get("size") != (action.custom or {}).get("length"):
                raise ReplayProblem("validation", "ARFree length differs from source model", call=call)
            pointer_aliases += 1 if action.kind in ("aram_alloc", "aram_free") else 0
            continue
        if action.kind == "handle_init_from_aram":
            if status != "ok":
                raise ReplayProblem("validation", f"source-handle model rejected {action.kind}: {status}", call=call)
            nested = [item for item in returns.values()
                      if item.get("function") == "lbMemory_80014E24"
                      and enters.get(parse_u32(item.get("call"), "nested handle call"), {}).get("parent") == call.get("call")]
            if not nested or output.get("handle") != parse_u32(nested[0].get("result", 0), "nested handle result"):
                raise ReplayProblem("validation", "lbMemory initializer handle differs from nested source result", call=call)
            continue
        if action.kind == "alloc":
            if action.expected == 0 and status in ("exhausted", "invalid_request"):
                continue
            if status != "ok":
                raise ReplayProblem("validation", f"model rejected OS allocation: {status}", call=call)
            if output.get("address") != action.expected:
                raise ReplayProblem("validation", "OS allocation result differs from observed source pointer", call=call)
            observed_to_derived[action.expected or 0] = output["address"]
            pointer_aliases += 1
        elif action.kind == "pool_init":
            if status != "ok":
                raise ReplayProblem("validation", f"model rejected pool init: {status}", call=call)
            words = row.get("observed", {}).get("pool_words")
            if words and (words[2], words[3], words[4], words[8], words[9]) != (0, 0, 0, output["size"], output["align_mask"]):
                raise ReplayProblem("validation", "pool init metadata differs from source model", call=call)
        elif action.kind in ("pool_add", "pool_alloc"):
            if status != "ok":
                raise ReplayProblem("validation", f"model rejected pool operation: {status}", call=call)
            if action.kind == "pool_add" and output.get("size") is not None:
                if output.get("backing") is not None:
                    for nested in action.nested_raw or []:
                        observed = parse_u32(nested.get("result", 0), "nested allocation result")
                        if observed != output["backing"]:
                            raise ReplayProblem("validation", "pool backing pointer differs from source allocation", call=call)
                        observed_to_derived[observed] = output["backing"]
                        pointer_aliases += 1
                if action.expected is not None and parse_u32(row.get("result", 0), "pool refill result") != action.expected:
                    raise ReplayProblem("validation", "pool refill count differs from source result", call=call)
            if action.kind == "pool_alloc":
                observed = parse_u32(row.get("result", 0), "pool object result")
                if observed == 0 and status in ("exhausted", "invalid_request"):
                    continue
                if observed != output.get("address"):
                    raise ReplayProblem("validation", "pool object pointer differs from source result", call=call)
                observed_to_derived[observed] = output["address"]
                pointer_aliases += 1
                if output.get("backing") is not None:
                    for nested in action.nested_raw or []:
                        nested_result = parse_u32(nested.get("result", 0), "nested allocation result")
                        if nested_result != output["backing"]:
                            raise ReplayProblem("validation", "automatic pool backing differs from source allocation", call=call)
                        observed_to_derived[nested_result] = output["backing"]
                        pointer_aliases += 1
            words = row.get("observed", {}).get("pool_words")
            if words and (words[2], words[3], words[4]) != (output["used"], output["free_count"], output["peak"]):
                raise ReplayProblem("validation", "pool counters differ from source model", call=call)
        elif action.kind == "pool_free":
            if status != "ok":
                raise ReplayProblem("validation", f"model rejected pool free: {status}", call=call)
            words = row.get("observed", {}).get("pool_words")
            if words and (words[2], words[3], words[4]) != (output["used"], output["free_count"], output["peak"]):
                raise ReplayProblem("validation", "pool free counters differ from source model", call=call)
        elif action.kind == "free" and status != "ok":
            raise ReplayProblem("validation", f"model rejected OS free: {status}", call=call)

    retained_actions = [
        {"call": action.call.get("call"), "sequence": action.call.get("sequence"),
         "function": action.call.get("function"), "kind": action.kind,
         "command": json.loads(action.command)}
        for action in actions
    ]
    end_rows = [row for row in rows if row.get("record") == "end"]
    end_status = end_rows[-1].get("status") if end_rows else None
    pending_calls = sorted(set(enters) - set(returns))
    has_error_record = any(row.get("record") == "error" for row in rows)
    # This implementation is intentionally bounded at the first unsupported
    # game-heap/fighter lifetime operation.  Even a captured input stream or a
    # caller-supplied full_scenario scope therefore cannot become a complete
    # gameplay claim.
    complete = False
    status = "validated_complete" if complete else "validated_prefix"
    if not end_rows or has_error_record or pending_calls:
        status = "incomplete_prefix"
    result = {
        "schema": "melee-web-original-allocation-replay",
        "version": 1,
        "status": status,
        "complete": complete,
        "scope": "full_scenario" if complete else "boot_prefix",
        "trace_end_status": end_status,
        "trace_complete": bool(end_rows and end_status == "captured"),
        "context_provenance": "independent_boot" if verified_context is not None else "synthetic_declared",
        "calls_seen": len(enters),
        "calls_replayed": len({action.call.get("call") for action in actions}),
        "model_actions": len(actions),
        "pointer_aliases": pointer_aliases,
        "pending_calls": pending_calls,
        "replay_actions": retained_actions,
        "model_outputs": outputs,
        "checked_wasm_matches_native": True if checked_wasm else None,
        "replay_hashes": {
            "actions_sha256": canonical_sha256(retained_actions),
            "outputs_sha256": canonical_sha256(outputs),
        },
        "derived_context": context,
        "provenance": {"dol_sha1": profile.get("dol_sha1"),
                       "source_revision": profile.get("source_revision"),
                       "profile_sha256": sha256_file(profile_path),
                       "trace_sha256": sha256_file(trace)},
    }
    result["boot_context_provenance"] = {
        "mode": "independent_boot" if verified_context is not None else "synthetic_declared",
    }
    if verified_context is not None:
        result["boot_context_provenance"].update({
            "schema": verified_context.get("schema"),
            "version": verified_context.get("version"),
            "derivation": verified_context.get("derivation"),
            "identities": verified_context.get("identities"),
        })
    if "observed_boot_context" in header:
        result["observed_boot_context"] = header["observed_boot_context"]
    if first_unsupported is not None:
        result["first_unsupported"] = first_unsupported
    if require_complete and not complete:
        raise ReplayProblem("incomplete", "allocation history did not reach a complete supported scenario")
    return result


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--trace", "--allocations", "--stream", dest="trace", type=Path,
                        required=True, help="raw allocation JSONL")
    result.add_argument("--profile", "--allocation-profile", dest="profile", type=Path,
                        required=True, help="retail allocation profile JSON")
    result.add_argument("--cold-boot-context", "--context", dest="cold_boot_context", type=Path,
                        help="JSON declaring arena_lo/arena_hi/heap_max_num/audio_heap_size")
    result.add_argument("--arena-lo", type=lambda value: parse_u32(value, "--arena-lo"))
    result.add_argument("--arena-hi", type=lambda value: parse_u32(value, "--arena-hi"))
    result.add_argument("--heap-max-num", type=lambda value: parse_u32(value, "--heap-max-num"))
    result.add_argument("--audio-heap-size", type=lambda value: parse_u32(value, "--audio-heap-size"))
    result.add_argument("--lbmemory-arena-lo", type=lambda value: parse_u32(value, "--lbmemory-arena-lo"))
    result.add_argument("--lbmemory-arena-hi", type=lambda value: parse_u32(value, "--lbmemory-arena-hi"))
    result.add_argument("--dol", type=Path, help="pinned GALE01 main.dol for SHA validation")
    result.add_argument("--disc", type=Path, help="owned GALE01 disc image for independent boot-root derivation")
    result.add_argument("--symbols", type=Path, help="pinned GALE01 symbols.txt for SHA validation")
    result.add_argument("--source-root", type=Path, default=ROOT / ".deps/melee",
                        help="pinned original source root used for boot-root derivation")
    result.add_argument("--checked-wasm", action="store_true", help="also run the checked Wasm driver")
    result.add_argument("--require-complete", action="store_true")
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        verified_context = None
        source_revision = None
        try:
            profile_value = json.loads(args.profile.read_text())
            source_revision = profile_value.get("source_revision") if isinstance(profile_value, dict) else None
        except (OSError, json.JSONDecodeError):
            pass
        if source_revision and not str(source_revision).startswith("synthetic-"):
            if args.dol is None or args.symbols is None or args.disc is None:
                raise ReplayProblem("provenance", "retail replay requires --dol, --disc and --symbols for independent boot-root derivation")
            try:
                try:
                    from .original_boot_context import derive_boot_context
                except ImportError:
                    from tools.original_boot_context import derive_boot_context
                verified_context = derive_boot_context(args.dol, args.disc, args.symbols,
                                                       args.source_root)
            except (OSError, ValueError, RuntimeError) as error:
                raise ReplayProblem("provenance", f"independent boot-root derivation failed: {error}") from error
        report = replay(args.trace, args.profile, args.cold_boot_context,
                        {"arena_lo": args.arena_lo, "arena_hi": args.arena_hi,
                         "heap_max_num": args.heap_max_num, "audio_heap_size": args.audio_heap_size,
                         "lbmemory_arena_lo": args.lbmemory_arena_lo,
                         "lbmemory_arena_hi": args.lbmemory_arena_hi},
                        args.dol, args.symbols, checked_wasm=args.checked_wasm,
                        require_complete=args.require_complete,
                        verified_context=verified_context)
    except ReplayProblem as error:
        report = {"schema": "melee-web-original-allocation-replay", "version": 1,
                  "status": error.kind, "complete": False, "error": error.message}
        if error.call is not None:
            report["error_call"] = {"call": error.call.get("call"),
                                     "function": error.call.get("function"),
                                     "sequence": error.call.get("sequence")}
        print(json.dumps(report, sort_keys=True))
        return 2
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
