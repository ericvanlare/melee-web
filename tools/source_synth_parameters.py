"""Derive the original HSD Synth startup parameters from source and the DOL.

This module is a bounded diagnostic probe.  It does not use captured allocator
sizes or addresses as inputs.  The generated Wasm fixture contains the exact
``fn_80023254``/``fn_80023254_shift`` bodies and the exact bank-size statements
from ``lbAudioAx_8002838C``; only the two authored static tables are hydrated
from the owned DOL after their symbol extents and source initializers agree.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterable


DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"
DOL_SHA256 = "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646"
SOURCE_REVISION = "b43912cc78606f96c9569f5d6229bc9d7e265ea5"
KNOWN_LBAUDIO_SHA256 = "11e237a0af742dee1502cdab1454191301179af2f9030958324dec0886ccf5dc"
KNOWN_LBAUDIO_STATIC_SHA256 = "9db87e298a942ac3b7a5f047b0dbba6c31a9939e3128a4a29676e7dae409578e"
KNOWN_AX_HEADER_SHA256 = "4c355504b9901229681da12080a6a6214e5a9b111d1834988c8a1b00651a2c9e"
KNOWN_SYMBOLS_SHA256 = "214477d5b27989a9675c4b881f4db2dad5eeaada39dbac17c57c70d5ba84d579"
S32_NAME = "s32_arr_803BB5D0"
OFFSETS_NAME = "offsets_arr_803BC4E4"
TABLE_COUNT = 0x38
S32_BYTES = TABLE_COUNT * 4
OFFSETS_BYTES = TABLE_COUNT * 8
MAX_TABLE_VALUE = 0xFFFFFFFF


class SynthParameterError(ValueError):
    """Raised when source, symbol, or DOL provenance cannot be established."""


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _source_checkout_root(source_path: Path) -> Path:
    for parent in source_path.resolve().parents:
        if (parent / ".git").is_dir():
            return parent
    raise SynthParameterError("source file is not inside a pinned git checkout")


def verify_pinned_inputs(
    source_path: Path,
    static_header: Path,
    ax_header: Path,
    symbols_path: Path,
    *,
    source_root: Path | None = None,
) -> dict[str, str]:
    """Require the exact clean pinned source inputs before or after execution."""

    if source_root is None:
        source_root = _source_checkout_root(source_path)
    revision = subprocess.check_output(
        ["git", "-C", str(source_root), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != SOURCE_REVISION:
        raise SynthParameterError(
            f"pinned source revision changed: {revision} != {SOURCE_REVISION}"
        )
    dirty = subprocess.check_output(
        ["git", "-C", str(source_root), "status", "--porcelain", "--untracked-files=all"],
        text=True,
    )
    if dirty:
        raise SynthParameterError("pinned source checkout is dirty")
    expected = {
        source_root / "src/MSL/stdbool.h": "0265041ee1108a45dc88b724b887533b1d89169ee4089f9e6567063b2015797e",
        source_path: KNOWN_LBAUDIO_SHA256,
        static_header: KNOWN_LBAUDIO_STATIC_SHA256,
        ax_header: KNOWN_AX_HEADER_SHA256,
        symbols_path: KNOWN_SYMBOLS_SHA256,
    }
    hashes: dict[str, str] = {}
    for path, known in expected.items():
        actual = sha256(path)
        if actual != known:
            raise SynthParameterError(
                f"pinned input changed: {path} has {actual}, expected {known}"
            )
        hashes[str(path)] = actual
    return {"source_revision": revision, **hashes}


def read_symbols(path: Path) -> dict[str, dict[str, int | str]]:
    """Read the pinned symbol map without silently accepting malformed rows."""

    pattern = re.compile(
        r"^(\S+) = (\.[^:]+):0x([0-9A-Fa-f]+);"
        r"\s*//\s*type:(\w+)\s+size:0x([0-9A-Fa-f]+)"
    )
    result: dict[str, dict[str, int | str]] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = pattern.match(line)
        if not match:
            continue
        name, section, address, kind, size = match.groups()
        row = {
            "address": int(address, 16),
            "section": section,
            "kind": kind,
            "size": int(size, 16),
            "line": line_number,
        }
        # The map legitimately contains repeated local names from unrelated
        # translation units.  Only the two identities this probe consumes must
        # be unique; retaining the first unrelated row keeps the parser from
        # silently changing the selected table identity.
        if name in result and name in {S32_NAME, OFFSETS_NAME}:
            raise SynthParameterError(f"duplicate table symbol {name} at line {line_number}")
        if name in result:
            continue
        result[name] = row
    return result


class Dol:
    def __init__(self, path: Path):
        self.path = path
        self.raw = path.read_bytes()
        if hashlib.sha1(self.raw).hexdigest() != DOL_SHA1:
            raise SynthParameterError("owned DOL differs from pinned GALE01r2")
        if hashlib.sha256(self.raw).hexdigest() != DOL_SHA256:
            raise SynthParameterError("owned DOL SHA-256 differs from pinned GALE01r2")
        self.sections: list[tuple[int, int, int]] = []
        for index in range(18):
            file_offset = struct.unpack_from(">I", self.raw, index * 4)[0]
            address = struct.unpack_from(">I", self.raw, 0x48 + index * 4)[0]
            size = struct.unpack_from(">I", self.raw, 0x90 + index * 4)[0]
            if size:
                self.sections.append((address, size, file_offset))

    def read(self, address: int, size: int) -> bytes:
        if address < 0 or size < 0 or address + size > 0x100000000:
            raise SynthParameterError("DOL read overflows the 32-bit address space")
        for section_address, section_size, file_offset in self.sections:
            if section_address <= address and address + size <= section_address + section_size:
                start = file_offset + address - section_address
                return self.raw[start:start + size]
        raise SynthParameterError(
            f"DOL read 0x{address:08x}+0x{size:x} escapes an owned section"
        )


def _initializer(source: str, marker: str) -> str:
    start = source.index(marker)
    equal = source.index("=", start)
    opening = source.index("{", equal)
    depth = 0
    for index in range(opening, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise SynthParameterError(f"unterminated initializer for {marker}")


def _numbers(initializer: str) -> list[int]:
    return [int(token, 0) for token in re.findall(r"0[xX][0-9A-Fa-f]+|\d+", initializer)]


def _source_tables(source_header: Path) -> tuple[list[list[int]], list[list[int]]]:
    source = source_header.read_text(encoding="utf-8")
    if not re.search(rf"static\s+s8\s+{S32_NAME}\[0x38\]\[4\]", source):
        raise SynthParameterError(f"{S32_NAME} declaration does not retain its authored bound/layout")
    if not re.search(rf"static\s+u32\s+{OFFSETS_NAME}\[\]\[2\]", source):
        raise SynthParameterError(f"{OFFSETS_NAME} declaration does not retain its authored layout")
    signed = _numbers(_initializer(source, f"static s8 {S32_NAME}"))
    offsets = _numbers(_initializer(source, f"static u32 {OFFSETS_NAME}"))
    # The authored declaration has a 0x38-row bound but only 0x37 explicit
    # initializers.  C zero-initializes the final row; require that implicit
    # row explicitly instead of shortening the table or guessing its value.
    if len(signed) == S32_BYTES - 4:
        signed.extend([0, 0, 0, 0])
    if len(signed) != S32_BYTES:
        raise SynthParameterError(f"source {S32_NAME} has {len(signed)} bytes, expected {S32_BYTES}")
    if len(offsets) != TABLE_COUNT * 2:
        raise SynthParameterError(
            f"source {OFFSETS_NAME} has {len(offsets)} words, expected {TABLE_COUNT * 2}"
        )
    signed = [value - 0x100 if value >= 0x80 else value for value in signed]
    return (
        [signed[index:index + 4] for index in range(0, S32_BYTES, 4)],
        [offsets[index:index + 2] for index in range(0, len(offsets), 2)],
    )


def _validate_symbol(symbols: dict, name: str, expected_size: int) -> dict:
    try:
        item = symbols[name]
    except KeyError as error:
        raise SynthParameterError(f"missing authored symbol {name}") from error
    if item.get("section") != ".data" or item.get("kind") != "object":
        raise SynthParameterError(f"{name} is not a .data object")
    address = item.get("address")
    size = item.get("size")
    if not isinstance(address, int) or address < 0x80000000 or address % 4:
        raise SynthParameterError(f"{name} has an invalid MEM1 address")
    if size != expected_size:
        raise SynthParameterError(
            f"{name} has size 0x{size:x}, expected authored size 0x{expected_size:x}"
        )
    if address + expected_size > 0x81800000:
        raise SynthParameterError(f"{name} escapes bounded MEM1")
    return item


def validate_tables(s32_table: object, offsets_table: object) -> None:
    """Validate exact authored dimensions and unsigned offset bounds."""

    if not isinstance(s32_table, list) or len(s32_table) != TABLE_COUNT:
        raise SynthParameterError("s32 table must contain exactly 0x38 rows")
    for row in s32_table:
        if not isinstance(row, list) or len(row) != 4:
            raise SynthParameterError("s32 table rows must contain exactly four bytes")
        if any(type(value) is not int or value < -128 or value > 127 for value in row):
            raise SynthParameterError("s32 table contains a value outside signed-byte bounds")
    if not isinstance(offsets_table, list) or len(offsets_table) != TABLE_COUNT:
        raise SynthParameterError("offset table must contain exactly 0x38 rows")
    for row in offsets_table:
        if not isinstance(row, list) or len(row) != 2:
            raise SynthParameterError("offset rows must contain exactly two words")
        if any(type(value) is not int or value < 0 or value > MAX_TABLE_VALUE for value in row):
            raise SynthParameterError("offset table contains a value outside u32 bounds")


def hydrate_tables(
    dol_path: Path,
    symbols_path: Path,
    source_header: Path,
) -> dict:
    """Hydrate the two source tables from a verified DOL and cross-check source."""

    dol = Dol(dol_path)
    symbols = read_symbols(symbols_path)
    s32_symbol = _validate_symbol(symbols, S32_NAME, S32_BYTES)
    offsets_symbol = _validate_symbol(symbols, OFFSETS_NAME, OFFSETS_BYTES)
    source_s32, source_offsets = _source_tables(source_header)
    s32_bytes = dol.read(int(s32_symbol["address"]), S32_BYTES)
    offset_bytes = dol.read(int(offsets_symbol["address"]), OFFSETS_BYTES)
    dol_s32_values = list(s32_bytes)
    dol_s32 = [
        [value - 0x100 if value >= 0x80 else value for value in dol_s32_values[index:index + 4]]
        for index in range(0, S32_BYTES, 4)
    ]
    dol_offsets = [
        [int.from_bytes(offset_bytes[index:index + 4], "big"),
         int.from_bytes(offset_bytes[index + 4:index + 8], "big")]
        for index in range(0, OFFSETS_BYTES, 8)
    ]
    validate_tables(dol_s32, dol_offsets)
    if dol_s32 != source_s32:
        raise SynthParameterError(f"{S32_NAME} DOL bytes differ from source initializer")
    if dol_offsets != source_offsets:
        raise SynthParameterError(f"{OFFSETS_NAME} DOL bytes differ from source initializer")
    return {
        "s32_table": dol_s32,
        "offsets_table": dol_offsets,
        "dol": {"path": str(dol_path), "sha1": DOL_SHA1, "sha256": DOL_SHA256},
        "symbols": {
            "path": str(symbols_path),
            "sha256": sha256(symbols_path),
            S32_NAME: {"address": int(s32_symbol["address"]), "size": S32_BYTES},
            OFFSETS_NAME: {"address": int(offsets_symbol["address"]), "size": OFFSETS_BYTES},
        },
        "source": {"path": str(source_header), "sha256": sha256(source_header)},
    }


def _extract_function(source: str, marker: str) -> str:
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 0
    in_string: str | None = None
    escaped = False
    in_line_comment = False
    in_block_comment = False
    index = opening
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""
        if in_line_comment:
            if char == "\n":
                in_line_comment = False
        elif in_block_comment:
            if char == "*" and next_char == "/":
                in_block_comment = False
                index += 1
        elif in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == in_string:
                in_string = None
        elif char == "/" and next_char == "/":
            in_line_comment = True
            index += 1
        elif char == "/" and next_char == "*":
            in_block_comment = True
            index += 1
        elif char in "\"'":
            in_string = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
        index += 1
    raise SynthParameterError(f"unterminated source function {marker}")


def _parameter_statements(source: str) -> str:
    start = source.index("void lbAudioAx_8002838C(void)")
    begin = source.index("    lbl_804D643C =", start)
    end = source.index("    AXDriver_8038E498", begin)
    statements = source[begin:end]
    if "fn_80023254(3);" not in statements or "fn_80023254(5);" not in statements:
        raise SynthParameterError("bank-size source statements were not extracted completely")
    return statements


def _driver_call_statement(source: str) -> str:
    start = source.index("void lbAudioAx_8002838C(void)")
    match = re.search(
        r"^\s*AXDriver_8038E498\(AX_MAX_VOICES, 0, 0x40, lbl_804D3870\);\s*$",
        source[source.index("    lbl_804D3870 =", start):],
        re.MULTILINE,
    )
    if match is None:
        raise SynthParameterError("authored AXDriver_8038E498 startup call was not found")
    return match.group(0)


def _initializer_rows(rows: Iterable[Iterable[int]], *, signed: bool) -> str:
    rendered = []
    for row in rows:
        values = []
        for value in row:
            if signed:
                values.append(str(value))
            else:
                values.append(f"0x{value:08x}u")
        rendered.append("    { " + ", ".join(values) + " },")
    return "\n".join(rendered)


def render_fixture(source_path: Path, tables: dict) -> str:
    validate_tables(tables["s32_table"], tables["offsets_table"])
    source = source_path.read_text(encoding="utf-8")
    shift = _extract_function(source, "static inline void fn_80023254_shift")
    sorter = _extract_function(source, "static void fn_80023254")
    statements = _parameter_statements(source)
    driver_call = _driver_call_statement(source)
    ax_header = source_path.parents[3] / "extern/dolphin/include/dolphin/ax.h"
    ax_text = ax_header.read_text(encoding="utf-8")
    match = re.search(r"^#define\s+AX_MAX_VOICES\s+(\d+)\s*$", ax_text, re.MULTILINE)
    if not match:
        raise SynthParameterError("AX_MAX_VOICES definition is missing from pinned source")
    voices = int(match.group(1))
    return f'''/* Generated from pinned source; do not hand-edit. */
#include <stdio.h>
/* Load libc's ssize_t before the source-compatible ABI shim. */
#include "original_startup_compat.h"
#include <placeholder.h>
_Static_assert(sizeof(bool) == 4, "source bool ABI must be four bytes");
_Static_assert(sizeof(void*) == 4, "source pointers must be Wasm32");
/* AX_MAX_VOICES is copied from the pinned dolphin/ax.h declaration. */
#define AX_MAX_VOICES {voices}

static s8 {S32_NAME}[0x38][4] = {{
{_initializer_rows(tables["s32_table"], signed=True)}
}};
static u32 {OFFSETS_NAME}[0x38][2] = {{
{_initializer_rows(tables["offsets_table"], signed=False)}
}};
static int lbl_80433B44[0x38];
static int lbl_804D6438;
static int lbl_804D643C;
static int lbl_804D6440;
static int lbl_804D6444;
static int lbl_804D3870;
static u32 driver_call[4];

static void source_capture_AXDriver_8038E498(int voices, int priority,
                                             int sample_rate, int bank_size) {{
    driver_call[0] = (u32) voices;
    driver_call[1] = (u32) priority;
    driver_call[2] = (u32) sample_rate;
    driver_call[3] = (u32) bank_size;
}}
#define AXDriver_8038E498 source_capture_AXDriver_8038E498

{shift}

{sorter}

void source_synth_parameter_probe(u32 out[8]) {{
{statements}
{driver_call}
#undef AXDriver_8038E498
    out[0] = (u32) lbl_804D643C;
    out[1] = (u32) lbl_804D6440;
    out[2] = (u32) lbl_804D6444;
    out[3] = (u32) lbl_804D6438;
    (void) lbl_804D3870;
    out[4] = driver_call[0];
    out[5] = driver_call[1];
    out[6] = driver_call[2];
    out[7] = driver_call[3];
}}

int main(void) {{
    u32 out[8];
    source_synth_parameter_probe(out);
    printf("{{\\"bank_sizes\\":[%lu,%lu,%lu],\\"bank_size_total\\":%lu,\\"driver_call\\":[%lu,%lu,%lu,%lu]}}\\n",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7]);
    return 0;
}}
'''


def _configured_node(sdk: Path) -> Path | None:
    config = sdk / ".emscripten"
    if not config.is_file():
        return None
    tree = ast.parse(config.read_text(encoding="utf-8"), filename=str(config))
    for statement in tree.body:
        if not isinstance(statement, ast.Assign):
            continue
        if not any(isinstance(target, ast.Name) and target.id == "NODE_JS"
                   for target in statement.targets):
            continue
        value = ast.literal_eval(statement.value)
        if isinstance(value, str):
            return Path(value.replace("$CFGDIR", str(sdk))).resolve()
    return None


def compile_and_run(
    fixture: str,
    *,
    root: Path,
    work_dir: Path,
) -> dict:
    sdk = root / ".deps/emsdk"
    sdk = sdk.resolve()
    compiler = sdk / "upstream/emscripten/emcc.py"
    node = _configured_node(sdk)
    if not compiler.is_file() or node is None or not node.is_file():
        raise SynthParameterError("configured Emscripten compiler or Node is unavailable")
    if work_dir.exists() and any(work_dir.iterdir()):
        raise SynthParameterError("artifact directory must be new or empty")
    work_dir.mkdir(parents=True, exist_ok=True)
    # Only the authored boolean header shadows libc; other MSL headers do not.
    include_dir = work_dir / "include"
    include_dir.mkdir()
    bool_header = root / ".deps/melee/src/MSL/stdbool.h"
    (include_dir / "stdbool.h").write_bytes(bool_header.read_bytes())
    fixture_path = work_dir / "source_synth_parameter_fixture.c"
    output = work_dir / "source_synth_parameter_fixture.js"
    fixture_path.write_text(fixture, encoding="utf-8")
    command = [
        sys.executable, str(compiler), "-std=gnu11", "-O0", "-ffp-contract=off",
        "-DDEBUG=1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
        "-Wno-array-parameter",
        "-I", str(include_dir), "-I", str(root / "src"), "-I", str(root / ".deps/melee/src"),
        "-I", str(root / ".deps/melee/extern/dolphin/include"),
        "-I", str(root / ".deps/melee/include"),
        "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
        str(fixture_path), "-o", str(output),
    ]
    env = dict(os.environ)
    env.update({"EMSDK": str(sdk), "EM_CONFIG": str(sdk / ".emscripten"),
                "EM_CACHE": str(sdk / "upstream/emscripten/cache"),
                "EMSDK_PYTHON": sys.executable})
    (work_dir / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    try:
        completed = subprocess.run(command, cwd=root, env=env, text=True,
                                   capture_output=True, timeout=60)
    except subprocess.TimeoutExpired as error:
        (work_dir / "compile.timeout").write_text(
            f"compile timed out after 60 seconds\nstdout={error.stdout!r}\nstderr={error.stderr!r}\n",
            encoding="utf-8",
        )
        (work_dir / "compile.stdout").write_text(str(error.stdout or ""), encoding="utf-8")
        (work_dir / "compile.stderr").write_text(str(error.stderr or ""), encoding="utf-8")
        raise SynthParameterError(f"fixture compile timed out; see {work_dir / 'compile.timeout'}") from error
    (work_dir / "compile.stdout").write_text(completed.stdout, encoding="utf-8")
    (work_dir / "compile.stderr").write_text(completed.stderr, encoding="utf-8")
    (work_dir / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    if completed.returncode:
        raise SynthParameterError(f"fixture compile failed; see {work_dir / 'compile.stderr'}")
    run_command = [str(node), str(output)]
    (work_dir / "run-command.json").write_text(json.dumps(run_command, indent=2) + "\n")
    try:
        run = subprocess.run(run_command, cwd=root, env=env,
                             text=True, capture_output=True, timeout=30)
    except subprocess.TimeoutExpired as error:
        (work_dir / "run.timeout").write_text(
            f"run timed out after 30 seconds\nstdout={error.stdout!r}\nstderr={error.stderr!r}\n",
            encoding="utf-8",
        )
        (work_dir / "run.stdout").write_text(str(error.stdout or ""), encoding="utf-8")
        (work_dir / "run.stderr").write_text(str(error.stderr or ""), encoding="utf-8")
        raise SynthParameterError(f"fixture run timed out; see {work_dir / 'run.timeout'}") from error
    (work_dir / "run.stdout").write_text(run.stdout, encoding="utf-8")
    (work_dir / "run.stderr").write_text(run.stderr, encoding="utf-8")
    if run.returncode:
        raise SynthParameterError(f"fixture run failed; see {work_dir / 'run.stderr'}")
    lines = [line for line in run.stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise SynthParameterError("fixture did not emit exactly one result record")
    return json.loads(lines[0])


def derive_parameters(dol_path: Path, symbols_path: Path, source_path: Path,
                      work_dir: Path | None = None) -> dict:
    static_header = source_path.with_name("lbaudio_ax.static.h")
    ax_header = source_path.parents[3] / "extern/dolphin/include/dolphin/ax.h"
    before = verify_pinned_inputs(source_path, static_header, ax_header, symbols_path)
    tables = hydrate_tables(dol_path, symbols_path, static_header)
    fixture = render_fixture(source_path, tables)
    workspace = next(
        (parent for parent in source_path.parents if (parent / ".deps/emsdk").is_dir()),
        None,
    )
    if workspace is None:
        raise SynthParameterError("could not locate workspace .deps/emsdk for checked Wasm")
    if work_dir is None:
        (workspace / "work").mkdir(parents=True, exist_ok=True)
        work_dir = Path(tempfile.mkdtemp(prefix="source-synth-parameters-", dir=workspace / "work"))
    result = compile_and_run(fixture, root=workspace, work_dir=work_dir)
    after = verify_pinned_inputs(source_path, static_header, ax_header, symbols_path)
    if after != before:
        raise SynthParameterError("pinned source inputs changed during fixture execution")
    source_text = source_path.read_text(encoding="utf-8")
    source_metadata = {
        "path": str(source_path),
        "sha256": sha256(source_path),
        "table_header_path": tables["source"]["path"],
        "table_header_sha256": tables["source"]["sha256"],
        "fn_80023254_shift_sha256": hashlib.sha256(
            _extract_function(source_text, "static inline void fn_80023254_shift").encode()
        ).hexdigest(),
        "fn_80023254_sha256": hashlib.sha256(
            _extract_function(source_text, "static void fn_80023254").encode()
        ).hexdigest(),
        "parameter_statements_sha256": hashlib.sha256(
            _parameter_statements(source_text).encode()
        ).hexdigest(),
    }
    return {"schema": "melee-web-source-synth-parameters", "version": 1,
            "status": "derived", "source_revision": SOURCE_REVISION,
            "dol": tables["dol"], "symbols": tables["symbols"],
            "source": source_metadata, "result": result,
            "fixture_sha256": hashlib.sha256(fixture.encode()).hexdigest(),
            "provenance_before": before, "provenance_after": after,
            "artifact_dir": str(work_dir)}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dol", type=Path, required=True)
    parser.add_argument("--symbols", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True,
                        help="pinned Melee source root containing src/melee/lb")
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        result = derive_parameters(
            args.dol, args.symbols,
            args.source_root / "src/melee/lb/lbaudio_ax.c",
            args.work_dir,
        )
    except (OSError, SynthParameterError, subprocess.TimeoutExpired) as error:
        print(f"source Synth parameter probe failed: {error}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
