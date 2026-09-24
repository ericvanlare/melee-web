#!/usr/bin/env python3
"""Generate an identity-only C++ profile for the passive allocation observer.

The input is produced by ``retail_allocation_profile.build_profile`` from the
owned DOL and pinned symbol map.  This generator carries addresses, bounds,
return PCs, instruction words, hashes, and ABI arities into the observer; it
never copies executable bytes or allocator payloads into the generated file.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

try:
    from .retail_allocation_profile import DOL_SHA1, FUNCTIONS, GLOBALS, SOURCE_REVISION
except ImportError:
    from retail_allocation_profile import DOL_SHA1, FUNCTIONS, GLOBALS, SOURCE_REVISION


PROFILE_SCHEMA = "melee-web-original-allocation-profile"
PROFILE_VERSION = 1
HEX256 = re.compile(r"^[0-9a-fA-F]{64}$")


def _integer(value: object, field: str, *, maximum: int = 0xFFFFFFFF) -> int:
    if isinstance(value, bool):
        raise ValueError(f"{field} must be an integer")
    if isinstance(value, str):
        try:
            value = int(value, 0)
        except ValueError as error:
            raise ValueError(f"{field} is not an integer") from error
    if not isinstance(value, int) or value < 0 or value > maximum:
        raise ValueError(f"{field} is outside its unsigned bound")
    return value


def validate_profile(value: object) -> dict:
    if not isinstance(value, dict) or value.get("schema") != PROFILE_SCHEMA:
        raise ValueError("unsupported allocation profile schema")
    if value.get("version") != PROFILE_VERSION:
        raise ValueError("unsupported allocation profile version")
    if value.get("dol_sha1") != DOL_SHA1:
        raise ValueError("profile DOL identity differs from the pinned GALE01r2 DOL")
    if value.get("source_revision") != SOURCE_REVISION:
        raise ValueError("profile source revision differs from the pinned retail source")
    if not isinstance(value.get("symbols_sha256"), str) or not HEX256.fullmatch(value["symbols_sha256"]):
        raise ValueError("profile symbols_sha256 must bind the pinned symbol input")
    entry = _integer(value.get("entry"), "entry")
    if not entry or entry % 4 or entry < 0x80000000 or entry + 4 > 0x81800000:
        raise ValueError("profile entry must be an aligned address in bounded MEM1")
    entry_word = _integer(value.get("entry_word"), "entry_word")
    if not entry_word:
        raise ValueError("profile entry_word must be a nonzero instruction word")
    functions = value.get("functions")
    if not isinstance(functions, list) or len(functions) != len(FUNCTIONS):
        raise ValueError(f"profile must contain exactly {len(FUNCTIONS)} functions")
    names = set()
    addresses = set()
    for index, function in enumerate(functions):
        if not isinstance(function, dict):
            raise ValueError(f"functions[{index}] is not an object")
        name = function.get("name")
        if name not in FUNCTIONS or name in names:
            raise ValueError(f"functions[{index}] has an unexpected or duplicate name")
        names.add(name)
        address = _integer(function.get("address"), f"functions[{index}].address")
        size = _integer(function.get("size"), f"functions[{index}].size", maximum=0x100000)
        entry_word = _integer(function.get("entry_word"), f"functions[{index}].entry_word")
        argc = _integer(function.get("argc"), f"functions[{index}].argc", maximum=8)
        if not address or address % 4 or not size or size % 4 or argc != FUNCTIONS[name]:
            raise ValueError(f"functions[{index}] has an invalid aligned body")
        if address < 0x80000000 or address + size > 0x81800000:
            raise ValueError(f"functions[{index}] body is outside bounded MEM1")
        if address in addresses:
            raise ValueError(f"functions[{index}] shares an address")
        addresses.add(address)
        returns = function.get("returns")
        if not isinstance(returns, list) or not returns or len(returns) > 256:
            raise ValueError(f"functions[{index}].returns is outside its bound")
        parsed_returns = [_integer(item, f"functions[{index}].returns[]") for item in returns]
        if parsed_returns != sorted(set(parsed_returns)):
            raise ValueError(f"functions[{index}].returns must be sorted and unique")
        if any(item < address or item + 4 > address + size or item % 4 for item in parsed_returns):
            raise ValueError(f"functions[{index}].returns escapes its body")
        body_hash = function.get("body_sha256")
        if not isinstance(body_hash, str) or not HEX256.fullmatch(body_hash):
            raise ValueError(f"functions[{index}].body_sha256 is not a SHA-256 identity")
    if names != set(FUNCTIONS):
        raise ValueError("profile function set differs from the pinned source table")

    globals_value = value.get("globals")
    if not isinstance(globals_value, dict) or set(globals_value) != set(GLOBALS):
        raise ValueError("profile global set differs from the pinned source table")
    global_addresses = set()
    for name in GLOBALS:
        item = globals_value[name]
        if not isinstance(item, dict):
            raise ValueError(f"globals.{name} is not an object")
        address = _integer(item.get("address"), f"globals.{name}.address")
        size = _integer(item.get("size"), f"globals.{name}.size", maximum=0x100000)
        if (not address or address % 4 or size < 4 or address < 0x80000000 or
                address + size > 0x81800000):
            raise ValueError(f"globals.{name} is outside bounded MEM1")
        if address in global_addresses:
            raise ValueError(f"global {name} shares an address")
        global_addresses.add(address)
    initial_words = value.get("initial_dol_words")
    if not isinstance(initial_words, dict) or set(initial_words) != {
            "seed_ptr", "__OSArenaLo", "current_heap", "iparam_audio_heap_size",
            "iparam_heap_max_num"}:
        raise ValueError("profile initial_dol_words is required")
    return value


def _c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def render_header(profile: dict, profile_sha256: str) -> str:
    functions = profile["functions"]
    globals_value = profile["globals"]
    entry_word = _integer(profile["entry_word"], "entry_word")
    lines = [
        "// Generated by tools/generate_reference_allocation_profile.py.",
        "// Identity metadata only; no original executable bodies are embedded.",
        "// DOL SHA-1: " + profile["dol_sha1"],
        "// Source revision: " + profile["source_revision"],
        "// Symbols SHA-256: " + profile["symbols_sha256"],
        "// Profile SHA-256: " + profile_sha256,
        "#pragma once",
        "#include <array>",
        '#include "Core/PowerPC/ReferenceAllocationObserver.h"',
        "namespace ReferenceAllocation::Generated {",
    ]
    return_names: list[str] = []
    for index, function in enumerate(functions):
        name = f"kReturns{index}"
        return_names.append(name)
        values = ", ".join(f"0x{_integer(item, 'return'):08x}u" for item in function["returns"])
        lines.append(f"inline constexpr std::array<u32, {len(function['returns'])}> {name} = {{{{{values}}}}};")
    lines.append(f"inline constexpr std::array<FunctionIdentity, {len(functions)}> kFunctions = {{ {{")
    for index, function in enumerate(functions):
        lines.append(
            "  {" + ", ".join((
                _c_string(function["name"]),
                f"0x{_integer(function['address'], 'address'):08x}u",
                f"0x{_integer(function['size'], 'size'):08x}u",
                f"0x{_integer(function['entry_word'], 'entry_word'):08x}u",
                str(_integer(function["argc"], "argc")),
                f"{return_names[index]}.data()",
                f"static_cast<u32>({return_names[index]}.size())",
                _c_string(function["body_sha256"].lower()),
            )) + "},"
        )
    lines.append("}};")
    lines.append(f"inline constexpr std::array<GlobalIdentity, {len(GLOBALS)}> kGlobals = {{ {{")
    for name in GLOBALS:
        item = globals_value[name]
        lines.append(
            "  {" + ", ".join((
                _c_string(name),
                f"0x{_integer(item['address'], 'address'):08x}u",
                f"0x{_integer(item['size'], 'size'):08x}u",
            )) + "},"
        )
    lines.extend([
        "}};",
        "inline const BoundProfile kProfile{",
        f"  {_c_string(profile['dol_sha1'].lower())},",
        f"  {_c_string(profile['source_revision'])},",
        f"  {_c_string(profile_sha256)},",
        f"  0x{_integer(profile['entry'], 'entry'):08x}u,",
        f"  0x{entry_word:08x}u,",
        "  kFunctions.data(), static_cast<u32>(kFunctions.size()),",
        "  kGlobals.data(), static_cast<u32>(kGlobals.size()), true,",
        "};",
        "}  // namespace ReferenceAllocation::Generated",
        "",
    ])
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true",
                        help="verify the existing output is the deterministic generated header")
    args = parser.parse_args(argv)
    try:
        raw = args.profile.read_bytes()
        profile = validate_profile(json.loads(raw))
        rendered = render_header(profile, hashlib.sha256(raw).hexdigest())
        if args.check:
            if args.output.read_text(encoding="utf-8") != rendered:
                raise ValueError(f"generated output differs: {args.output}")
            return 0
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"reference allocation profile: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
