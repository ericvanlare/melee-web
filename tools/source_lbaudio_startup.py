"""Compile the untouched original ``lbAudioAx`` startup unit.

This is a bounded source/object profile, not an audio runtime.  The generated
translation unit includes the pinned raw ``lbaudio_ax.c`` exactly once and
places narrow wrappers around the authored AR/AXDriver/SFX calls only to
observe their arguments.  The wrappers call the original functions, so the
source still owns ARInit, ARQInit, AIInit, AXDriver, AXFX, bank allocation, and
SFX bookkeeping in its original order.  No gameplay compatibility macro,
source statement copy, host success stub, or captured address is used.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / ".deps/emsdk"
MELEE = ROOT / ".deps/melee"
EMCC = SDK / "upstream/emscripten/emcc.py"
LLVM_NM = SDK / "upstream/bin/llvm-nm"
SOURCE = MELEE / "src/melee/lb/lbaudio_ax.c"
STATIC_HEADER = MELEE / "src/melee/lb/lbaudio_ax.static.h"
AX_HEADER = MELEE / "extern/dolphin/include/dolphin/ax.h"
SYMBOLS = MELEE / "config/GALE01/symbols.txt"
ACCESSOR_HEADER = ROOT / "tests/source_lbaudio_startup_accessors.h"
SOURCE_REVISION = "b43912cc78606f96c9569f5d6229bc9d7e265ea5"
SOURCE_SHA256 = "11e237a0af742dee1502cdab1454191301179af2f9030958324dec0886ccf5dc"
STATIC_HEADER_SHA256 = "9db87e298a942ac3b7a5f047b0dbba6c31a9939e3128a4a29676e7dae409578e"
AX_HEADER_SHA256 = "4c355504b9901229681da12080a6a6214e5a9b111d1834988c8a1b00651a2c9e"
SYMBOLS_SHA256 = "214477d5b27989a9675c4b881f4db2dad5eeaada39dbac17c57c70d5ba84d579"
MSL_BOOL_SHA256 = "0265041ee1108a45dc88b724b887533b1d89169ee4089f9e6567063b2015797e"


class LBAudioStartupError(RuntimeError):
    """Raised when the bounded original-source profile cannot be established."""


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _env() -> dict[str, str]:
    env = dict(os.environ)
    env.update({
        "EMSDK": str(SDK),
        "EM_CONFIG": str(SDK / ".emscripten"),
        "EM_CACHE": str(SDK / "upstream/emscripten/cache"),
        "EMSDK_PYTHON": sys.executable,
    })
    return env


def _run(
    command: list[str], *, cwd: Path, env: dict[str, str], evidence: Path,
    name: str, timeout: int = 120,
) -> subprocess.CompletedProcess[str]:
    """Run a bounded command while retaining stdout/stderr and timeout data."""

    stdout = evidence / f"{name}.stdout"
    stderr = evidence / f"{name}.stderr"
    (evidence / f"{name}.command.json").write_text(
        json.dumps(command, indent=2) + "\n", encoding="utf-8"
    )
    try:
        result = subprocess.run(
            command, cwd=cwd, env=env, text=True, capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as error:
        out = error.stdout.decode() if isinstance(error.stdout, bytes) else (error.stdout or "")
        err = error.stderr.decode() if isinstance(error.stderr, bytes) else (error.stderr or "")
        stdout.write_text(out, encoding="utf-8")
        stderr.write_text(err, encoding="utf-8")
        (evidence / f"{name}.timeout").write_text(
            f"command timed out after {timeout}s\n", encoding="utf-8"
        )
        raise LBAudioStartupError(
            f"{name} timed out after {timeout}s; see {stderr}"
        ) from error
    stdout.write_text(result.stdout, encoding="utf-8")
    stderr.write_text(result.stderr, encoding="utf-8")
    return result


def _verify_inputs() -> dict[str, object]:
    if not MELEE.is_dir() or not (MELEE / ".git").is_dir():
        raise LBAudioStartupError(f"pinned source checkout unavailable: {MELEE}")
    revision = subprocess.check_output(
        ["git", "-C", str(MELEE), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != SOURCE_REVISION:
        raise LBAudioStartupError(
            f"pinned source revision changed: {revision} != {SOURCE_REVISION}"
        )
    dirty = subprocess.check_output(
        ["git", "-C", str(MELEE), "status", "--porcelain", "--untracked-files=all"],
        text=True,
    )
    if dirty:
        raise LBAudioStartupError("pinned melee source checkout is dirty")
    expected = {
        SOURCE: SOURCE_SHA256,
        STATIC_HEADER: STATIC_HEADER_SHA256,
        AX_HEADER: AX_HEADER_SHA256,
        SYMBOLS: SYMBOLS_SHA256,
        MELEE / "src/MSL/stdbool.h": MSL_BOOL_SHA256,
    }
    hashes: dict[str, str] = {}
    for path, wanted in expected.items():
        if not path.is_file():
            raise LBAudioStartupError(f"missing pinned input: {path}")
        actual = sha256(path)
        if actual != wanted:
            raise LBAudioStartupError(
                f"pinned input changed: {path} has {actual}, expected {wanted}"
            )
        hashes[str(path)] = actual
    return {"source_revision": revision, "files": hashes}


def _source_tables() -> dict[str, object]:
    """Cross-check authored table dimensions without hydrating runtime state."""

    text = STATIC_HEADER.read_text(encoding="utf-8")
    if not re.search(r"static\s+s8\s+s32_arr_803BB5D0\[0x38\]\[4\]", text):
        raise LBAudioStartupError("authored SFX table bound/layout changed")
    if not re.search(r"static\s+u32\s+offsets_arr_803BC4E4\[\]\[2\]", text):
        raise LBAudioStartupError("authored offsets table bound/layout changed")
    # Use the reviewed parameter parser for initializer extraction and DOL
    # cross-checks when an owned DOL is explicitly provided to the profile.
    if str(ROOT / "tools") not in sys.path:
        sys.path.insert(0, str(ROOT / "tools"))
    from source_synth_parameters import _source_tables

    signed, offsets = _source_tables(STATIC_HEADER)
    if len(signed) != 0x38 or len(offsets) != 0x38:
        raise LBAudioStartupError("authored SFX tables have unexpected row count")
    return {
        "s32_rows": len(signed),
        "offset_rows": len(offsets),
        "s32_initializer_sha256": hashlib.sha256(
            json.dumps(signed, separators=(",", ":")).encode()
        ).hexdigest(),
        "offset_initializer_sha256": hashlib.sha256(
            json.dumps(offsets, separators=(",", ":")).encode()
        ).hexdigest(),
    }


def _dol_provenance() -> dict[str, object]:
    """Optionally validate the owned DOL tables; never require a guessed path."""

    raw = os.environ.get("MELEE_WEB_OWNED_DOL")
    if not raw:
        return {"status": "source_only", "reason": "MELEE_WEB_OWNED_DOL not set"}
    dol = Path(raw)
    if not dol.is_file():
        raise LBAudioStartupError(f"owned DOL path is not a file: {dol}")
    if not SYMBOLS.is_file():
        raise LBAudioStartupError(f"owned symbol map unavailable: {SYMBOLS}")
    if str(ROOT / "tools") not in sys.path:
        sys.path.insert(0, str(ROOT / "tools"))
    from source_synth_parameters import hydrate_tables

    tables = hydrate_tables(dol, SYMBOLS, STATIC_HEADER)
    return {
        "status": "dol_cross_checked",
        "path": str(dol),
        "sha1": tables["dol"]["sha1"],
        "sha256": tables["dol"]["sha256"],
        "s32_symbol": tables["symbols"]["s32_arr_803BB5D0"],
        "offsets_symbol": tables["symbols"]["offsets_arr_803BC4E4"],
    }


def _stage_abi(evidence: Path) -> dict[str, str]:
    include = evidence / "include"
    include.mkdir()
    msl_bool = MELEE / "src/MSL/stdbool.h"
    (include / "stdbool.h").write_bytes(msl_bool.read_bytes())
    (include / "source_abi.h").write_text(
        "#ifndef MELEE_WEB_SOURCE_LBAUDIO_ABI_H\n"
        "#define MELEE_WEB_SOURCE_LBAUDIO_ABI_H\n"
        "typedef signed int ssize_t;\n"
        "#define __DEFINED_ssize_t 1\n"
        "_Static_assert(sizeof(bool) == 4, \"retail MSL bool must be four bytes\");\n"
        "_Static_assert(sizeof(unsigned int) == 4, \"retail u32 must be four bytes\");\n"
        "_Static_assert(sizeof(long) == 4, \"retail long must be four bytes\");\n"
        "_Static_assert(sizeof(void*) == 4, \"retail pointer ABI must be Wasm32\");\n"
        "#endif\n", encoding="utf-8"
    )
    return {
        "msl_bool_sha256": sha256(msl_bool),
        "source_abi_sha256": sha256(include / "source_abi.h"),
        "stdbool_sha256": sha256(include / "stdbool.h"),
    }


def _header_inventory(evidence: Path) -> dict[str, str]:
    """Return a regular-file hash inventory for generated compile headers."""

    include = evidence / "include"
    if include.is_symlink() or not include.is_dir():
        raise LBAudioStartupError("generated include directory is not a regular directory")
    result: dict[str, str] = {}
    for path in sorted(include.rglob("*")):
        if path.is_symlink():
            raise LBAudioStartupError("generated include tree contains a symlink")
        if path.is_file():
            result[path.relative_to(evidence).as_posix()] = sha256(path)
    return result


def _include_flags(evidence: Path) -> list[str]:
    directories = [
        evidence / "include",
        ROOT / "tests",
        ROOT / "src",
        MELEE / "src",
        MELEE,
        MELEE / "extern/dolphin/include",
        MELEE / "extern/dolphin/src/dolphin/gx",
        MELEE / "include",
        ROOT / ".deps/aurora/include",
    ]
    return [item for directory in directories for item in ("-I", str(directory))]


def _write_wrapper(evidence: Path) -> Path:
    """Generate only call-boundary observation hooks around the raw source."""

    wrapper = evidence / "source_lbaudio_startup_wrapper.c"
    source_literal = str(SOURCE.resolve()).replace("\\", "\\\\").replace('"', '\\"')
    wrapper.write_text(
        '#include <stdint.h>\n'
        '#include <stddef.h>\n'
        '#include <dolphin/ar.h>\n'
        '#include <sysdolphin/baselib/axdriver.h>\n'
        '#include <sysdolphin/baselib/synth.h>\n'
        '\n'
        'static uint32_t melee_web_source_lbaudio_ar_stack_address;\n'
        'static uint32_t melee_web_source_lbaudio_ar_stack_entries;\n'
        'static uint32_t melee_web_source_lbaudio_fx_count;\n'
        'static uint32_t melee_web_source_lbaudio_fx_addresses[2];\n'
        'static uint32_t melee_web_source_lbaudio_fx_sizes[2];\n'
        'static uint32_t melee_web_source_lbaudio_fx_success[2];\n'
        'static uint32_t melee_web_source_lbaudio_driver_call_count;\n'
        'static uint32_t melee_web_source_lbaudio_driver_voices;\n'
        'static uint32_t melee_web_source_lbaudio_driver_priority;\n'
        'static uint32_t melee_web_source_lbaudio_driver_sample_rate;\n'
        'static uint32_t melee_web_source_lbaudio_driver_aram_size;\n'
        'static uint32_t melee_web_source_lbaudio_bank_call_count;\n'
        'static uint32_t melee_web_source_lbaudio_bank_call_sizes[3];\n'
        '\n'
        'static u32 melee_web_source_lbaudio_ARInit(u32* stack, u32 entries)\n'
        '{\n'
        '    melee_web_source_lbaudio_ar_stack_address = (u32)(uintptr_t)stack;\n'
        '    melee_web_source_lbaudio_ar_stack_entries = entries;\n'
        '    return ARInit(stack, entries);\n'
        '}\n'
        '\n'
        'static void melee_web_source_lbaudio_AXDriver_8038E498(\n'
        '    int voices, int priority, int sample_rate, int aram_size)\n'
        '{\n'
        '    melee_web_source_lbaudio_driver_call_count++;\n'
        '    melee_web_source_lbaudio_driver_voices = (u32)voices;\n'
        '    melee_web_source_lbaudio_driver_priority = (u32)priority;\n'
        '    melee_web_source_lbaudio_driver_sample_rate = (u32)sample_rate;\n'
        '    melee_web_source_lbaudio_driver_aram_size = (u32)aram_size;\n'
        '    AXDriver_8038E498(voices, priority, sample_rate, aram_size);\n'
        '}\n'
        '\n'
        'static bool melee_web_source_lbaudio_AXDriver_8038E30C(\n'
        '    s32 channel, s32 type, void* param, u8* heap, size_t heap_size)\n'
        '{\n'
        '    uint32_t i = melee_web_source_lbaudio_fx_count++;\n'
        '    bool result = AXDriver_8038E30C(channel, type, param, heap, heap_size);\n'
        '    if (i < 2) {\n'
        '        melee_web_source_lbaudio_fx_addresses[i] = (u32)(uintptr_t)heap;\n'
        '        melee_web_source_lbaudio_fx_sizes[i] = (u32)heap_size;\n'
        '        melee_web_source_lbaudio_fx_success[i] = result ? 1 : 0;\n'
        '    }\n'
        '    return result;\n'
        '}\n'
        '\n'
        'static void melee_web_source_lbaudio_HSD_SynthSFXAllocateBank(int size)\n'
        '{\n'
        '    if (melee_web_source_lbaudio_bank_call_count < 3) {\n'
        '        melee_web_source_lbaudio_bank_call_sizes[\n'
        '            melee_web_source_lbaudio_bank_call_count++] = (u32)size;\n'
        '    } else {\n'
        '        melee_web_source_lbaudio_bank_call_count++;\n'
        '    }\n'
        '    HSD_SynthSFXAllocateBank(size);\n'
        '}\n'
        '\n'
        '#define ARInit melee_web_source_lbaudio_ARInit\n'
        '#define AXDriver_8038E498 melee_web_source_lbaudio_AXDriver_8038E498\n'
        '#define AXDriver_8038E30C melee_web_source_lbaudio_AXDriver_8038E30C\n'
        '#define HSD_SynthSFXAllocateBank melee_web_source_lbaudio_HSD_SynthSFXAllocateBank\n'
        f'#include "{source_literal}"\n'
        '#undef HSD_SynthSFXAllocateBank\n'
        '#undef AXDriver_8038E30C\n'
        '#undef AXDriver_8038E498\n'
        '#undef ARInit\n'
        '#define MELEE_WEB_LBAUDIO_ACCESSOR_IMPLEMENTATION 1\n'
        '#include "source_lbaudio_startup_accessors.h"\n',
        encoding="utf-8",
    )
    return wrapper


def _compile_command(evidence: Path, wrapper: Path, output: Path) -> list[str]:
    # Deliberately omit MELEE_WEB_GAMEPLAY and MELEE_WEB_ORIGINAL_STARTUP_FIXTURE:
    # the original AR/ARQ/AI/AXDriver/FX/SFX path must remain compiled.
    return [
        sys.executable, str(EMCC), "-c", "-std=gnu11", "-O0",
        "-ffp-contract=off", "-DDEBUG=1", "-Wall", "-Wextra", "-Werror",
        "-Wno-array-parameter", "-Wno-unused-parameter", "-Wno-unused-variable",
        "-Wno-unused-function", "-Wno-sign-compare", "-Wno-incompatible-library-redeclaration",
        "-Wno-builtin-requires-header", "-Wno-missing-braces", "-include",
        str(evidence / "include/stdbool.h"),
        "-include", str(evidence / "include/source_abi.h"), *_include_flags(evidence),
        str(wrapper), "-o", str(output),
    ]


def _defined_and_undefined(path: Path, evidence: Path | None = None) -> tuple[list[str], list[str]]:
    result = subprocess.run(
        [str(LLVM_NM), str(path)], cwd=ROOT, text=True, capture_output=True,
        timeout=30,
    )
    if evidence is not None:
        (evidence / "lbaudio.nm").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise LBAudioStartupError("llvm-nm could not read the retained compiled object")
    defined: list[str] = []
    undefined: list[str] = []
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) < 2:
            continue
        if fields[-2].upper() == "U":
            undefined.append(fields[-1])
        elif len(fields) >= 3 and fields[-2].upper() not in {"U", "?"}:
            defined.append(fields[-1])
    return sorted(set(defined)), sorted(set(undefined))


def build_profile(work_dir: Path | None = None) -> dict[str, object]:
    """Compile the raw source and return an object/unresolved-symbol receipt."""

    if not EMCC.is_file() or not LLVM_NM.is_file():
        raise LBAudioStartupError("configured Emscripten compiler or llvm-nm unavailable")
    (ROOT / "work").mkdir(parents=True, exist_ok=True)
    if work_dir is None:
        work_dir = Path(tempfile.mkdtemp(prefix="source-lbaudio-startup-", dir=ROOT / "work"))
    work_dir = work_dir if work_dir.is_absolute() else ROOT / work_dir
    work_dir = work_dir.absolute()
    if work_dir.is_symlink():
        raise LBAudioStartupError("artifact directory must not be a symlink")
    if work_dir.exists() and any(work_dir.iterdir()):
        raise LBAudioStartupError(f"artifact directory must be new or empty: {work_dir}")
    work_dir.mkdir(parents=True, exist_ok=True)
    env = _env()
    source_before = _verify_inputs()
    accessor_hash_before = sha256(ACCESSOR_HEADER)
    builder_hash_before = sha256(Path(__file__))
    table_provenance = _source_tables()
    dol_provenance = _dol_provenance()
    abi = _stage_abi(work_dir)
    header_inventory = _header_inventory(work_dir)
    wrapper = _write_wrapper(work_dir)
    output = work_dir / "lbaudio_ax_original.o"
    command = _compile_command(work_dir, wrapper, output)
    result = _run(command, cwd=ROOT, env=env, evidence=work_dir, name="compile", timeout=180)
    if result.returncode:
        raise LBAudioStartupError(f"raw lbAudioAx compile failed; see {work_dir / 'compile.stderr'}")
    if not output.is_file():
        raise LBAudioStartupError("compiler reported success without an object")
    defined, undefined = _defined_and_undefined(output, work_dir)
    required = {
        "lbAudioAx_8002838C",
        "melee_web_source_lbaudio_snapshot",
    }
    missing = sorted(required - set(defined))
    if missing:
        raise LBAudioStartupError("object is missing required symbols: " + ", ".join(missing))
    source_after = _verify_inputs()
    if source_after != source_before:
        raise LBAudioStartupError("pinned source inputs changed during compile")
    if sha256(ACCESSOR_HEADER) != accessor_hash_before:
        raise LBAudioStartupError("accessor identity check failed")
    if sha256(Path(__file__)) != builder_hash_before:
        raise LBAudioStartupError("profile builder changed during compile")
    categories = {
        "os_ai_ar_dsp": sorted(s for s in undefined if s.startswith(("OS", "AI", "AR", "DSP", "__OS"))),
        "ax": sorted(s for s in undefined if s.startswith("AX")),
        "hsd": sorted(s for s in undefined if s.startswith("HSD_")),
        "gameplay": sorted(s for s in undefined if s.startswith(("gm", "ft", "it", "gr", "pl", "lb"))),
    }
    object_record = {
        "path": str(output),
        "sha256": sha256(output),
        "bytes": output.stat().st_size,
    }
    receipt: dict[str, object] = {
        "kind": "source_lbaudio_startup_compile",
        "version": 1,
        "source_revision": SOURCE_REVISION,
        "source_before": source_before,
        "source_after": source_after,
        "source": {"path": str(SOURCE), "sha256": SOURCE_SHA256},
        "source_hashes": {
            "lbaudio_ax.c": SOURCE_SHA256,
            "lbaudio_ax.static.h": STATIC_HEADER_SHA256,
            "dolphin/ax.h": AX_HEADER_SHA256,
            "symbols.txt": SYMBOLS_SHA256,
            "source_lbaudio_startup_accessors.h": accessor_hash_before,
            "source_lbaudio_startup.py": builder_hash_before,
        },
        "static_header": {"path": str(STATIC_HEADER), "sha256": STATIC_HEADER_SHA256},
        "table_provenance": table_provenance,
        "dol_provenance": dol_provenance,
        "abi": abi,
        "header_inventory": header_inventory,
        "accessor_header": {"path": str(ACCESSOR_HEADER), "sha256": sha256(ACCESSOR_HEADER)},
        "wrapper": {"path": str(wrapper), "sha256": sha256(wrapper)},
        "compile": {
            "status": "pass", "command": command, "object": str(output),
            "object_bytes": output.stat().st_size, "object_sha256": object_record["sha256"],
            "defines_original_startup_macro": False,
            "defines_melee_web_gameplay": False,
        },
        "defined_symbols": defined,
        "undefined_symbols": undefined,
        "undefined_categories": categories,
        "runtime_claim": False,
        "limits": [
            "Object compilation only; no AR/AI/DSP/AX service runtime was executed.",
            "Function-local AR/FX spans are observed through call-boundary hooks; the hooks call original functions.",
            "No host success stubs, source statement copies, or captured addresses were used.",
        ],
        "evidence_dir": str(work_dir),
        "object": object_record,
    }
    (work_dir / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return receipt


def validate_profile(receipt_path: Path) -> dict[str, object]:
    """Validate a previously retained object receipt before composition."""

    receipt_path = Path(receipt_path)
    if receipt_path.is_symlink() or not receipt_path.is_file():
        raise LBAudioStartupError(f"lbaudio profile receipt is unavailable: {receipt_path}")
    try:
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise LBAudioStartupError(f"invalid lbaudio profile receipt: {receipt_path}") from error
    if receipt.get("kind") != "source_lbaudio_startup_compile" or receipt.get("version") != 1:
        raise LBAudioStartupError("lbaudio profile receipt schema is unsupported")
    if receipt.get("runtime_claim") is not False:
        raise LBAudioStartupError("lbaudio profile must not claim runtime execution")
    if receipt.get("source_revision") != SOURCE_REVISION:
        raise LBAudioStartupError("lbaudio profile source revision is not pinned")
    evidence_dir = Path(str(receipt.get("evidence_dir", "")))
    if evidence_dir.absolute() != receipt_path.absolute().parent:
        raise LBAudioStartupError("lbaudio receipt is outside its evidence directory")
    if evidence_dir.is_symlink() or not evidence_dir.is_dir():
        raise LBAudioStartupError("lbaudio profile evidence directory is not regular")
    current_inputs = _verify_inputs()
    source_hashes = receipt.get("source_hashes")
    expected_source_hashes = {
        "lbaudio_ax.c": SOURCE_SHA256,
        "lbaudio_ax.static.h": STATIC_HEADER_SHA256,
        "dolphin/ax.h": AX_HEADER_SHA256,
        "symbols.txt": SYMBOLS_SHA256,
        "source_lbaudio_startup_accessors.h": sha256(ACCESSOR_HEADER),
        "source_lbaudio_startup.py": sha256(Path(__file__)),
    }
    if source_hashes != expected_source_hashes:
        raise LBAudioStartupError("lbaudio profile source hash is not pinned")
    if receipt.get("source_before") != receipt.get("source_after"):
        raise LBAudioStartupError("lbaudio profile source provenance changed during compile")
    if receipt.get("source_before") != current_inputs:
        raise LBAudioStartupError("lbaudio profile source provenance is stale")
    if receipt.get("header_inventory") != _header_inventory(evidence_dir):
        raise LBAudioStartupError("generated ABI header inventory changed")
    object_record = receipt.get("object")
    if not isinstance(object_record, dict):
        raise LBAudioStartupError("lbaudio profile object record is missing")
    object_path = Path(str(object_record.get("path", "")))
    if object_path.is_symlink() or not object_path.is_file():
        raise LBAudioStartupError(f"lbaudio profile object is unavailable: {object_path}")
    if object_path.resolve().parent != evidence_dir.resolve():
        raise LBAudioStartupError("lbaudio profile object is outside its evidence directory")
    actual = sha256(object_path)
    if actual != object_record.get("sha256"):
        raise LBAudioStartupError("lbaudio profile object hash changed")
    if receipt.get("compile", {}).get("status") != "pass":
        raise LBAudioStartupError("lbaudio profile compile did not pass")
    if receipt.get("compile", {}).get("defines_melee_web_gameplay") is not False:
        raise LBAudioStartupError("lbaudio profile used the gameplay source branch")
    if receipt.get("compile", {}).get("defines_original_startup_macro") is not False:
        raise LBAudioStartupError("lbaudio profile used an original-startup macro")
    required = {"lbAudioAx_8002838C", "melee_web_source_lbaudio_snapshot"}
    defined, undefined = _defined_and_undefined(object_path)
    if defined != receipt.get("defined_symbols") or undefined != receipt.get("undefined_symbols"):
        raise LBAudioStartupError("lbaudio object symbol inventory differs from receipt")
    if not required.issubset(set(defined)):
        raise LBAudioStartupError("lbaudio profile required symbol inventory is incomplete")
    wrapper_record = receipt.get("wrapper")
    if not isinstance(wrapper_record, dict):
        raise LBAudioStartupError("lbaudio profile wrapper record is missing")
    wrapper_path = Path(str(wrapper_record.get("path", "")))
    if wrapper_path.is_symlink() or not wrapper_path.is_file() or sha256(wrapper_path) != wrapper_record.get("sha256"):
        raise LBAudioStartupError("lbaudio profile wrapper changed")
    return receipt


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--artifact-dir", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(build_profile(args.artifact_dir), indent=2))
    except LBAudioStartupError as error:
        print(f"source_lbaudio_startup: {error}", file=sys.stderr)
        raise SystemExit(1)
