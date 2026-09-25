"""Compile the original post-audio memory allocation source units.

This is a compile-only boundary.  It verifies and compiles the pinned source
bodies for ``lbMemory_8001564C`` and ``lbHeap_80015F3C`` to Wasm32 objects and
compiles a typed entrypoint accessor.  It does not link, invoke, or emulate the
units, and it accepts no captured allocation rows or runtime addresses.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DEPS = Path(os.environ.get("MELEE_DEPS_ROOT", ROOT / ".deps")).resolve()
MELEE = DEPS / "melee"
SDK = DEPS / "emsdk"
EMCC = SDK / "upstream/emscripten/emcc.py"
LLVM_NM = SDK / "upstream/bin/llvm-nm"
PREPARED = ROOT / "build/gameplay-source"
ACCESSOR_HEADER = ROOT / "tests/source_post_audio_allocations_accessors.h"

SOURCE_REVISION = "b43912cc78606f96c9569f5d6229bc9d7e265ea5"
SOURCE_UNITS: dict[str, dict[str, Any]] = {
    "lbmemory": {
        "path": MELEE / "src/melee/lb/lbmemory.c",
        "prepared": PREPARED / "src/melee/lb/lbmemory.c",
        "sha256": "ae5d116c931db0788f8532014ad0ab7009c5a711ced6028302e4c97573126fc2",
        "prepared_symbol": "lbMemory_8001564C",
        "source_symbols": [
            "lbMemory_8001564C",
            "lbMemory_80014E24",
            "lbMemory_800154BC",
        ],
    },
    "lbheap": {
        "path": MELEE / "src/melee/lb/lbheap.c",
        "prepared": PREPARED / "src/melee/lb/lbheap.c",
        "sha256": "01c80d426e5b53659b5b430493b51d8d46a21f88840ac9a17dba769a6a14b96e",
        "prepared_symbol": "lbHeap_80015F3C",
        "source_symbols": [
            "lbHeap_80015F3C",
            "lbHeap_80015900",
            "lbHeap_80015BD0",
        ],
    },
}

# This is a source contract, not a link recipe.  It records what must be
# supplied by the next source-owned startup owner, without inventing service
# implementations or using captured values as arguments.
DEPENDENCY_CLOSURE: dict[str, dict[str, Any]] = {
    "lbMemory_8001564C": {
        "direct_source_calls": [
            "ARAlloc",
            "ARFree",
            "ARGetSize",
            "lbMemory_80014E24",
        ],
        "owned_state": [
            "lbMemory_804318B0.a_arenaLo/a_arenaHi",
            "0x83 movable MemEntry slots",
            "six Handle slots",
        ],
        "arguments": [],
        "argument_origin": "none; ARAM bounds and root handle derive inside the source body",
    },
    "lbHeap_80015F3C": {
        "direct_source_calls": [
            "HSD_GetNextArena",
            "lbMemory_800154BC",
        ],
        "owned_state": [
            "lbHeap_80431FA0.heap_array[0..5]",
            "lbHeap_80431FA0.arena_lo/arena_hi",
            "lbHeap_80431FA0.aram_lo/aram_hi",
            "lbHeap_803BA380 authored descriptor table",
        ],
        "arguments": [],
        "argument_origin": "none; arena and ARAM bounds derive from the two source calls, then descriptor starts/sizes derive from the authored table",
    },
    "lbHeap_80015900_future": {
        "direct_source_calls": [
            "HSD_GetNextArena",
            "lbMemory_800154BC",
            "HSD_CreateMainHeap",
            "lbMemory_800155A4",
            "lbMemory_800154D4",
            "OSCreateHeap",
        ],
        "arguments": ["none at the public entrypoint"],
        "argument_origin": "arena and ARAM bounds are derived by the source providers; no captured pointer is valid",
    },
}

ARGUMENT_CONTRACT: dict[str, dict[str, Any]] = {
    "lbMemory_8001564C": {
        "prototype": "void lbMemory_8001564C(void)",
        "independently_derived": [
            "ARAlloc(0x20) low marker",
            "ARFree output size",
            "ARGetSize() clamped by the source body to 0x01000000",
            "root Handle from lbMemory_80014E24(derived_low, derived_high)",
        ],
        "captured_inputs_forbidden": True,
    },
    "lbHeap_80015F3C": {
        "prototype": "void lbHeap_80015F3C(void)",
        "independently_derived": [
            "HSD_GetNextArena writes arena_lo/arena_hi",
            "lbMemory_800154BC writes aram_lo/aram_hi",
            "lbHeap_803BA380 descriptor rows",
            "previous descriptor state while applying rows",
        ],
        "captured_inputs_forbidden": True,
    },
}


class PostAudioCompileError(RuntimeError):
    """Raised when the compile-only boundary cannot be established."""


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _source_abi_text() -> str:
    return (
        "#ifndef MELEE_WEB_POST_AUDIO_SOURCE_ABI_H\n"
        "#define MELEE_WEB_POST_AUDIO_SOURCE_ABI_H\n"
        "typedef signed int ssize_t;\n"
        "#define __DEFINED_ssize_t 1\n"
        "_Static_assert(sizeof(bool) == 4, \"source MSL bool must be four bytes\");\n"
        "_Static_assert(sizeof(unsigned int) == 4, \"source u32 ABI changed\");\n"
        "_Static_assert(sizeof(long) == 4, \"source long ABI changed\");\n"
        "_Static_assert(sizeof(void*) == 4, \"compile boundary requires Wasm32 pointers\");\n"
        "#endif\n"
    )


def _run(command: list[str], *, stdout: Path, stderr: Path,
         timeout: int = 120) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            timeout=timeout)
    stdout.write_text(result.stdout, encoding="utf-8")
    stderr.write_text(result.stderr, encoding="utf-8")
    return result


def _verify_source_tree() -> dict[str, Any]:
    if not MELEE.is_dir() or not (MELEE / ".git").is_dir():
        raise PostAudioCompileError(f"pinned Melee source checkout is unavailable: {MELEE}")
    revision = subprocess.check_output(
        ["git", "-C", str(MELEE), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != SOURCE_REVISION:
        raise PostAudioCompileError(
            f"pinned source revision changed: {revision} != {SOURCE_REVISION}"
        )
    dirty = subprocess.check_output(
        ["git", "-C", str(MELEE), "status", "--porcelain", "--untracked-files=all"],
        text=True,
    )
    if dirty:
        raise PostAudioCompileError("pinned Melee source checkout is dirty")
    hashes: dict[str, str] = {}
    for name, unit in SOURCE_UNITS.items():
        path = unit["path"]
        if not path.is_file():
            raise PostAudioCompileError(f"missing source unit: {path}")
        digest = sha256(path)
        if digest != unit["sha256"]:
            raise PostAudioCompileError(
                f"source identity changed for {name}: {digest} != {unit['sha256']}"
            )
        hashes[name] = digest
    return {"revision": revision, "files": hashes}


def _prepare_sources() -> None:
    if all(unit["prepared"].is_file() for unit in SOURCE_UNITS.values()):
        return
    scripts = str(ROOT / "scripts")
    if scripts not in sys.path:
        sys.path.insert(0, scripts)
    try:
        from gameplay_sources import prepare_sources
        prepare_sources(ROOT)
    except Exception as error:  # retain the real preparation failure
        raise PostAudioCompileError(f"source preparation failed: {error}") from error


def _verify_prepared_units() -> dict[str, str]:
    """Allow only the reviewed MSL bool token transform in prepared bodies."""
    if not all(unit["prepared"].is_file() for unit in SOURCE_UNITS.values()):
        raise PostAudioCompileError("prepared gameplay source tree is incomplete")
    bool_script = str(ROOT / "scripts")
    if bool_script not in sys.path:
        sys.path.insert(0, bool_script)
    from gameplay_bool import replace_bool_tokens

    result: dict[str, str] = {}
    for name, unit in SOURCE_UNITS.items():
        original = unit["path"].read_text(encoding="utf-8")
        expected = replace_bool_tokens(original)
        prepared = unit["prepared"].read_text(encoding="utf-8")
        if prepared != expected:
            raise PostAudioCompileError(
                f"prepared {name} body differs beyond the reviewed MSL bool transform"
            )
        result[name] = sha256(unit["prepared"])
    return result


def _write_abi_headers(directory: Path) -> dict[str, str]:
    include = directory / "include"
    include.mkdir(parents=True, exist_ok=True)
    msl = MELEE / "src/MSL/stdbool.h"
    if not msl.is_file():
        raise PostAudioCompileError(f"source MSL bool header is unavailable: {msl}")
    bool_header = include / "source_msl_stdbool.h"
    bool_header.write_bytes(msl.read_bytes())
    abi = include / "source_abi.h"
    abi.write_text(_source_abi_text(), encoding="utf-8")
    return {"msl_bool_sha256": sha256(bool_header), "abi_sha256": sha256(abi)}


def _header_inventory(directory: Path) -> dict[str, str]:
    inventory: dict[str, str] = {}
    for relative in ("include/source_msl_stdbool.h", "include/source_abi.h"):
        path = directory / relative
        if path.is_symlink() or not path.is_file():
            raise PostAudioCompileError(f"generated ABI header is unavailable: {path}")
        inventory[relative] = sha256(path)
    return inventory


def _include_flags(directory: Path) -> list[str]:
    include = directory / "include"
    directories = [
        include,
        PREPARED / "src",
        PREPARED,
        MELEE / "extern/dolphin/include",
        MELEE / "extern/dolphin/src/dolphin/gx",
        MELEE / "include",
        MELEE / "src",
        DEPS / "aurora/include",
        ROOT / "src",
        ROOT / "tests",
    ]
    return [item for path in directories for item in ("-I", str(path))]


def _compile_flags(directory: Path) -> list[str]:
    include = directory / "include"
    return [
        "-c", "-std=gnu11", "-O0", "-ffp-contract=off", "-DDEBUG=1",
        "-DTARGET_PC", "-DMELEE_WEB_GAMEPLAY",
        "-DMELEE_WEB_ORIGINAL_STARTUP_FIXTURE",
        "-Wall", "-Wextra", "-Werror",
        "-Wno-array-parameter", "-Wno-visibility", "-Wno-macro-redefined",
        "-Wno-unused-value", "-Wno-unused-but-set-global",
        "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-unused-function",
        "-Wno-sign-compare", "-Wno-incompatible-library-redeclaration",
        "-Wno-builtin-requires-header",
        "-include", str(include / "source_msl_stdbool.h"),
        "-include", str(include / "source_abi.h"),
        *_include_flags(directory),
    ]


def _accessor_wrapper_text(name: str, source: Path) -> str:
    escaped = str(source).replace("\\", "\\\\").replace('"', '\\"')
    macro = {
        "lbmemory": "MELEE_WEB_SOURCE_POST_AUDIO_MEMORY_ACCESSOR_IMPLEMENTATION",
        "lbheap": "MELEE_WEB_SOURCE_POST_AUDIO_HEAP_ACCESSOR_IMPLEMENTATION",
    }.get(name)
    if macro is None:
        raise PostAudioCompileError(f"unsupported post-audio accessor unit: {name}")
    return (
        "/* Generated wrapper: include one pinned source body, then read-only accessors. */\n"
        f'#include "{escaped}"\n'
        f"#define {macro} 1\n"
        '#include "source_post_audio_allocations_accessors.h"\n'
    )


def _write_accessor_wrapper(directory: Path, name: str, source: Path) -> Path:
    wrapper = directory / f"{name}_accessors.c"
    wrapper.write_text(_accessor_wrapper_text(name, source), encoding="utf-8")
    return wrapper


def _nm_symbols(object_path: Path, directory: Path, *, mode: str,
                suffix: str) -> list[str]:
    if not LLVM_NM.is_file():
        raise PostAudioCompileError(f"pinned llvm-nm is unavailable: {LLVM_NM}")
    command = [str(LLVM_NM), mode, str(object_path)]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            timeout=30)
    (directory / f"{object_path.stem}{suffix}").write_text(
        result.stdout + result.stderr, encoding="utf-8"
    )
    if result.returncode:
        raise PostAudioCompileError(f"llvm-nm failed for {object_path}")
    return sorted(set(re.findall(r"\s+[A-Za-z]\s+(\S+)$", result.stdout, re.MULTILINE)))


def _defined_symbols(object_path: Path, directory: Path) -> list[str]:
    return _nm_symbols(object_path, directory, mode="--defined-only", suffix=".nm")


def _undefined_symbols(object_path: Path, directory: Path) -> list[str]:
    return _nm_symbols(object_path, directory, mode="--undefined-only", suffix=".undefined.nm")


def compile_profile(output: Path | None = None) -> dict[str, Any]:
    """Compile source bodies plus same-TU snapshots, without linking/running."""
    if not EMCC.is_file() or not LLVM_NM.is_file():
        raise PostAudioCompileError("pinned Emscripten compiler and llvm-nm are required")
    source = _verify_source_tree()
    _prepare_sources()
    prepared = _verify_prepared_units()
    builder_sha256 = sha256(Path(__file__))
    accessor_header_sha256 = sha256(ACCESSOR_HEADER)
    if output is None:
        (ROOT / "work").mkdir(parents=True, exist_ok=True)
        directory = Path(tempfile.mkdtemp(
            prefix="source-post-audio-allocations-", dir=ROOT / "work"
        ))
    else:
        directory = Path(output)
        if not directory.is_absolute():
            directory = ROOT / directory
        if directory.is_symlink():
            raise PostAudioCompileError("artifact directory must not be a symlink")
        directory.mkdir(parents=True, exist_ok=False)
    if ACCESSOR_HEADER.is_symlink() or not ACCESSOR_HEADER.is_file():
        raise PostAudioCompileError(f"snapshot accessor header is unavailable: {ACCESSOR_HEADER}")
    headers = _write_abi_headers(directory)
    header_inventory = _header_inventory(directory)
    units: dict[str, Any] = {}
    flags = _compile_flags(directory)
    for name, unit in SOURCE_UNITS.items():
        wrapper = _write_accessor_wrapper(directory, name, unit["prepared"])
        object_path = directory / f"{name}.o"
        command = [sys.executable, str(EMCC), *flags, str(wrapper), "-o", str(object_path)]
        (directory / f"{name}.command.json").write_text(
            json.dumps(command, indent=2) + "\n", encoding="utf-8"
        )
        result = _run(command, stdout=directory / f"{name}.stdout",
                      stderr=directory / f"{name}.stderr")
        if result.returncode:
            raise PostAudioCompileError(
                f"{name} source/accessor compile failed; see {directory / (name + '.stderr')}"
            )
        symbols = _defined_symbols(object_path, directory)
        undefined = _undefined_symbols(object_path, directory)
        required = [*unit["source_symbols"],
                    f"melee_web_source_{name}_snapshot"]
        missing = [symbol for symbol in required if symbol not in symbols]
        if missing:
            raise PostAudioCompileError(
                f"{name} object lacks expected source/accessor symbols: {', '.join(missing)}"
            )
        units[name] = {
            "source": str(unit["path"]),
            "prepared": str(unit["prepared"]),
            "source_sha256": unit["sha256"],
            "prepared_sha256": prepared[name],
            "wrapper": str(wrapper),
            "wrapper_sha256": sha256(wrapper),
            "object": str(object_path),
            "object_sha256": sha256(object_path),
            "defined_symbols": symbols,
            "undefined_symbols": undefined,
            "status": "compiled",
        }

    source_after = _verify_source_tree()
    prepared_after = _verify_prepared_units()
    if source_after != source:
        raise PostAudioCompileError("pinned source inputs changed during compile")
    if prepared_after != prepared:
        raise PostAudioCompileError("prepared source inputs changed during compile")
    if sha256(Path(__file__)) != builder_sha256:
        raise PostAudioCompileError("profile builder changed during compile")
    if sha256(ACCESSOR_HEADER) != accessor_header_sha256:
        raise PostAudioCompileError("snapshot accessor header changed during compile")

    receipt = {
        "schema": "melee-web-source-post-audio-allocations-compile",
        "version": 2,
        "source": source,
        "source_after": source_after,
        "prepared_source_sha256": prepared,
        "prepared_source_sha256_after": prepared_after,
        "source_hashes": {
            "source_post_audio_allocations.py": builder_sha256,
            "source_post_audio_allocations_accessors.h": accessor_header_sha256,
        },
        "abi": headers,
        "header_inventory": header_inventory,
        "units": units,
        "accessors": {
            "header": str(ACCESSOR_HEADER),
            "header_sha256": accessor_header_sha256,
            "same_translation_unit": True,
            "unit_names": sorted(units),
        },
        "dependency_closure": DEPENDENCY_CLOSURE,
        "argument_contract": ARGUMENT_CONTRACT,
        "link_performed": False,
        "runtime_executed": False,
        "captured_inputs_consumed": False,
        "runtime_claim": False,
        "evidence_dir": str(directory),
    }
    (directory / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n",
                                             encoding="utf-8")
    return receipt


def _regular_file(path: Path, label: str) -> Path:
    if path.is_symlink() or not path.is_file():
        raise PostAudioCompileError(f"{label} is not a regular file: {path}")
    return path


def _regular_directory(path: Path, label: str) -> Path:
    if path.is_symlink() or not path.is_dir():
        raise PostAudioCompileError(f"{label} is not a regular directory: {path}")
    return path


def validate_profile(receipt_path: Path) -> dict[str, Any]:
    """Validate a retained compile profile before another object is linked."""
    receipt_path = Path(receipt_path)
    _regular_file(receipt_path, "compile receipt")
    try:
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PostAudioCompileError(f"invalid compile receipt: {receipt_path}") from error
    if receipt.get("schema") != "melee-web-source-post-audio-allocations-compile" or receipt.get("version") != 2:
        raise PostAudioCompileError("compile receipt schema is unsupported")
    for field in ("link_performed", "runtime_executed", "captured_inputs_consumed", "runtime_claim"):
        if receipt.get(field) is not False:
            raise PostAudioCompileError(f"compile receipt has unsupported {field}")
    evidence = Path(str(receipt.get("evidence_dir", "")))
    _regular_directory(evidence, "compile evidence directory")
    if evidence.resolve() != receipt_path.resolve().parent:
        raise PostAudioCompileError("compile receipt is outside its evidence directory")

    current_source = _verify_source_tree()
    if receipt.get("source") != current_source or receipt.get("source_after") != current_source:
        raise PostAudioCompileError("compile source provenance is stale")
    current_prepared = _verify_prepared_units()
    if receipt.get("prepared_source_sha256") != current_prepared or receipt.get("prepared_source_sha256_after") != current_prepared:
        raise PostAudioCompileError("prepared source provenance is stale")
    expected_source_hashes = {
        "source_post_audio_allocations.py": sha256(Path(__file__)),
        "source_post_audio_allocations_accessors.h": sha256(ACCESSOR_HEADER),
    }
    if receipt.get("source_hashes") != expected_source_hashes:
        raise PostAudioCompileError("builder or accessor source changed")
    accessor = receipt.get("accessors")
    if not isinstance(accessor, dict) or accessor.get("same_translation_unit") is not True:
        raise PostAudioCompileError("same-translation-unit accessor contract is missing")
    if accessor.get("header") != str(ACCESSOR_HEADER) or accessor.get("header_sha256") != expected_source_hashes["source_post_audio_allocations_accessors.h"]:
        raise PostAudioCompileError("snapshot accessor header provenance is stale")
    expected_headers = _header_inventory(evidence)
    if receipt.get("header_inventory") != expected_headers:
        raise PostAudioCompileError("generated ABI header inventory changed")
    bool_header = evidence / "include/source_msl_stdbool.h"
    abi_header = evidence / "include/source_abi.h"
    _regular_file(bool_header, "generated MSL bool header")
    _regular_file(abi_header, "generated ABI header")
    if bool_header.read_bytes() != (MELEE / "src/MSL/stdbool.h").read_bytes():
        raise PostAudioCompileError("generated MSL bool header changed")
    if abi_header.read_text(encoding="utf-8") != _source_abi_text():
        raise PostAudioCompileError("generated ABI header changed")

    units = receipt.get("units")
    if not isinstance(units, dict) or set(units) != set(SOURCE_UNITS):
        raise PostAudioCompileError("compile unit inventory is incomplete")
    for name, unit in SOURCE_UNITS.items():
        record = units[name]
        if not isinstance(record, dict) or record.get("status") != "compiled":
            raise PostAudioCompileError(f"compile unit {name} did not pass")
        wrapper = _regular_file(Path(str(record.get("wrapper", ""))), f"{name} wrapper")
        object_path = _regular_file(Path(str(record.get("object", ""))), f"{name} object")
        for label, path in ((f"{name} wrapper", wrapper), (f"{name} object", object_path)):
            if path.resolve().parent != evidence.resolve():
                raise PostAudioCompileError(f"{label} is outside its evidence directory")
        if wrapper.read_text(encoding="utf-8") != _accessor_wrapper_text(name, unit["prepared"]):
            raise PostAudioCompileError(f"{name} wrapper contents changed")
        if record.get("wrapper_sha256") != sha256(wrapper):
            raise PostAudioCompileError(f"{name} wrapper hash changed")
        if record.get("object_sha256") != sha256(object_path):
            raise PostAudioCompileError(f"{name} object hash changed")
        if record.get("source") != str(unit["path"]) or record.get("prepared") != str(unit["prepared"]):
            raise PostAudioCompileError(f"{name} source path changed")
        # Recompute both inventories from the retained object.  The receipt's
        # symbol lists are provenance evidence, not an authority: trusting
        # them would allow a replacement object to pass profile validation as
        # long as its recorded fields were copied along with it.
        defined = _defined_symbols(object_path, evidence)
        undefined = _undefined_symbols(object_path, evidence)
        if defined != record.get("defined_symbols"):
            raise PostAudioCompileError(f"{name} defined symbol inventory changed")
        if undefined != record.get("undefined_symbols"):
            raise PostAudioCompileError(f"{name} undefined symbol inventory changed")
        required = [*unit["source_symbols"], f"melee_web_source_{name}_snapshot"]
        if not set(required).issubset(set(defined)):
            raise PostAudioCompileError(f"{name} defined symbol inventory is incomplete")
    return receipt


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        help="new directory in which to retain compile artifacts")
    parser.add_argument("--validate", type=Path,
                        help="validate a retained compile receipt instead of compiling")
    args = parser.parse_args(argv)
    try:
        receipt = validate_profile(args.validate) if args.validate else compile_profile(args.output)
    except (OSError, PostAudioCompileError, subprocess.SubprocessError) as error:
        print(f"source post-audio compile failed: {error}", file=sys.stderr)
        return 2
    print(json.dumps(receipt, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
