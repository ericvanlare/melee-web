"""Derive HSD object-pool identities from the pinned source declarations.

The retail symbol map contains storage extents, but an extent-sized symbol is
not by itself evidence that the object is an ``HSD_ObjAllocData``.  In
particular, the particle and generator pools are fields in a 0x30-byte
wrapper whose first member is the 0x2c-byte allocator state.  This module
parses the small, source-authored declaration grammar used by the pinned
source and joins it with the untouched symbol map.

Captured allocator calls are validation targets only.  They are deliberately
not accepted as inventory input and cannot add an address to the result.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from pathlib import Path
import re
from typing import Iterable, Iterator, Mapping


SCHEMA = "melee-web-source-pool-inventory"
VERSION = 1
ALLOC_DATA_BYTES = 0x2C
_IDENT = r"[A-Za-z_]\w*"
_SYMBOL_RE = re.compile(
    r"^(\S+) = (\.[^:]+):0x([0-9A-Fa-f]+);"
    r" // type:(\w+) size:0x([0-9A-Fa-f]+)(?:\s+.*)?$"
)
_STRUCT_RE = re.compile(r"\bstruct\s+(" + _IDENT + r")\s*\{")
_DIRECT_RE = re.compile(
    r"^\s*(?P<prefix>(?:(?:static|const|volatile|register)\s+)*)"
    r"HSD_ObjAllocData\s+(?P<names>" + _IDENT + r"(?:\s*,\s*" + _IDENT + r")*)"
    r"\s*;\s*$"
)
_WRAPPED_RE = re.compile(
    r"^\s*(?P<prefix>(?:(?:static|const|volatile|register)\s+)*)"
    r"struct\s+(?P<type>" + _IDENT + r")\s+(?P<names>" + _IDENT + r"(?:\s*,\s*" + _IDENT + r")*)"
    r"\s*;\s*$"
)
_ALLOC_MEMBER_RE = re.compile(
    r"^HSD_ObjAllocData\s+(?P<name>" + _IDENT + r")\s*;\s*$"
)
_ALLOC_SIZE_RE = re.compile(
    r"ASSERT_SIZE\s*\(\s*struct\s+_HSD_ObjAllocData\s*,\s*0x([0-9A-Fa-f]+)\s*\)\s*;"
)


@dataclass(frozen=True)
class PoolDeclaration:
    """One source-backed allocator field and its symbol-map extent."""

    name: str
    address: int
    storage_size: int
    data_offset: int
    data_size: int
    declaration_type: str
    declaration_source: str
    declaration_line: int
    type_source: str

    def as_dict(self) -> dict:
        return {
            "address": self.address,
            "storage_size": self.storage_size,
            "data_offset": self.data_offset,
            "data_size": self.data_size,
            "declaration_type": self.declaration_type,
            "declaration_source": self.declaration_source,
            "declaration_line": self.declaration_line,
            "type_source": self.type_source,
        }


def _strip_comments_and_literals(text: str) -> str:
    """Remove comments and literal contents while retaining line positions."""
    out: list[str] = []
    i = 0
    state = "normal"
    while i < len(text):
        char = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "normal":
            if char == "/" and nxt == "*":
                out.extend("  ")
                i += 2
                state = "block"
            elif char == "/" and nxt == "/":
                out.extend("  ")
                i += 2
                state = "line"
            elif char == '"':
                out.append(" ")
                i += 1
                state = "string"
            elif char == "'":
                out.append(" ")
                i += 1
                state = "char"
            else:
                out.append(char)
                i += 1
        elif state == "block":
            if char == "*" and nxt == "/":
                out.extend("  ")
                i += 2
                state = "normal"
            else:
                out.append("\n" if char == "\n" else " ")
                i += 1
        elif state == "line":
            if char == "\n":
                out.append(char)
                i += 1
                state = "normal"
            else:
                out.append(" ")
                i += 1
        else:  # string or character literal
            if char == "\\" and nxt:
                out.extend("  ")
                i += 2
            elif (state == "string" and char == '"') or (state == "char" and char == "'"):
                out.append(" ")
                i += 1
                state = "normal"
            else:
                out.append("\n" if char == "\n" else " ")
                i += 1
    if state in {"block", "string", "char"}:
        raise ValueError("unterminated source comment or literal")
    return "".join(out)


def _source_files(source_root: Path) -> Iterator[Path]:
    if not source_root.is_dir():
        raise ValueError(f"source root is not a directory: {source_root}")
    for path in sorted(source_root.rglob("*")):
        if path.is_file() and path.suffix.lower() in {".c", ".h", ".cc", ".cpp", ".hpp"}:
            yield path


def _top_level_lines(clean: str) -> Iterator[tuple[int, str]]:
    """Yield lines at file scope; function-local declarations are excluded."""
    depth = 0
    for line_number, line in enumerate(clean.splitlines(), 1):
        if depth == 0:
            yield line_number, line
        # Braces in literals/comments have already been blanked.  A source
        # declaration accepted below is intentionally limited to one statement
        # on one line, matching every allocator definition in the pinned tree.
        depth += line.count("{") - line.count("}")
        # A few retail translation units contain mutually-exclusive
        # preprocessor branches whose braces are not balanced until compile
        # time.  Such a file cannot contribute a later file-scope declaration
        # once depth is positive, but it must not make the whole inventory
        # unverifiable; the declarations we accept below are all explicit
        # one-line definitions in balanced units.


def _struct_allocator_types(path: Path, clean: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for match in _STRUCT_RE.finditer(clean):
        body_start = match.end()
        depth = 1
        cursor = body_start
        while cursor < len(clean) and depth:
            if clean[cursor] == "{":
                depth += 1
            elif clean[cursor] == "}":
                depth -= 1
            cursor += 1
        if depth:
            raise ValueError(f"unterminated struct {match.group(1)} in {path}")
        body = clean[body_start:cursor - 1]
        fields = [line.strip() for line in body.splitlines()
                  if line.strip() and not line.lstrip().startswith("#")]
        if not fields:
            continue
        # We only accept the source layout that can be proven without a C ABI
        # implementation: alloc_data is the first member.  A later member
        # would require guessed sizes/alignments and is rejected fail-closed.
        field = _ALLOC_MEMBER_RE.fullmatch(fields[0])
        if field is None:
            continue
        result[match.group(1)] = field.group("name")
    return result


def _source_declarations(source_root: Path) -> tuple[list[dict], dict[str, tuple[str, str]]]:
    declarations: list[dict] = []
    allocator_types: dict[str, tuple[str, str]] = {}
    files: list[tuple[Path, str]] = []
    for path in _source_files(source_root):
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError as exc:
            raise ValueError(f"source is not UTF-8: {path}") from exc
        clean = _strip_comments_and_literals(text)
        files.append((path, clean))
        for type_name, field_name in _struct_allocator_types(path, clean).items():
            current = (field_name, path.relative_to(source_root).as_posix())
            previous = allocator_types.get(type_name)
            if previous is not None and previous != current:
                raise ValueError(f"conflicting allocator struct declaration: {type_name}")
            allocator_types[type_name] = current
    # Resolve wrappers after collecting every header.  C files sort before
    # their headers, and declaration order must not affect the result.
    for path, clean in files:
        for line_number, line in _top_level_lines(clean):
            direct = _DIRECT_RE.fullmatch(line)
            if direct:
                for name in re.split(r"\s*,\s*", direct.group("names")):
                    declarations.append({
                        "name": name,
                        "kind": "direct",
                        "type": "HSD_ObjAllocData",
                        "path": path,
                        "line": line_number,
                    })
                continue
            wrapped = _WRAPPED_RE.fullmatch(line)
            if wrapped and wrapped.group("type") in allocator_types:
                for name in re.split(r"\s*,\s*", wrapped.group("names")):
                    declarations.append({
                        "name": name,
                        "kind": "wrapped",
                        "type": wrapped.group("type"),
                        "path": path,
                        "line": line_number,
                    })
    return declarations, allocator_types


def _read_object_symbols(symbols_path: Path) -> dict[str, list[dict]]:
    if not symbols_path.is_file():
        raise ValueError(f"symbol map does not exist: {symbols_path}")
    result: dict[str, list[dict]] = {}
    for line in symbols_path.read_text(encoding="utf-8").splitlines():
        match = _SYMBOL_RE.match(line)
        if not match or match.group(4) != "object":
            continue
        name, section, address, _, size = match.groups()
        result.setdefault(name, []).append({
            "address": int(address, 16),
            "size": int(size, 16),
            "section": section,
        })
    return result


def _read_allocator_size(objalloc_header: Path) -> int:
    clean = _strip_comments_and_literals(objalloc_header.read_text(encoding="utf-8"))
    matches = _ALLOC_SIZE_RE.findall(clean)
    if len(matches) != 1 or int(matches[0], 16) != ALLOC_DATA_BYTES:
        raise ValueError("pinned HSD_ObjAllocData size assertion is missing or changed")
    return int(matches[0], 16)


def build_pool_inventory(source_root: Path, symbols_path: Path) -> dict:
    """Return source-backed pool descriptors keyed by declaration name.

    The symbol map supplies only address and storage extent.  A declaration
    must independently prove that the first 0x2c bytes are allocator state;
    symbols with a coincidentally matching size are ignored.
    """
    source_root = Path(source_root).resolve()
    symbols_path = Path(symbols_path).resolve()
    declarations, allocator_types = _source_declarations(source_root)
    symbols = _read_object_symbols(symbols_path)
    pools: dict[str, PoolDeclaration] = {}
    source_paths: set[Path] = set()
    objalloc_header = source_root / "sysdolphin/baselib/objalloc.h"
    if not objalloc_header.is_file():
        raise ValueError("pinned objalloc.h is missing")
    object_size = _read_allocator_size(objalloc_header)
    source_paths.add(objalloc_header)
    for declaration in declarations:
        name = declaration["name"]
        symbol_rows = symbols.get(name, [])
        if not symbol_rows:
            continue
        if len(symbol_rows) != 1:
            raise ValueError(f"ambiguous object symbol: {name}")
        symbol = symbol_rows[0]
        # These globals are uninitialized source storage and must be in BSS.
        if symbol["section"] != ".bss":
            raise ValueError(f"source allocator is not in BSS: {name}")
        if (not symbol["address"] or symbol["address"] & 3 or
                symbol["size"] < ALLOC_DATA_BYTES or symbol["size"] & 3 or
                symbol["address"] + symbol["size"] > 0x100000000):
            raise ValueError(f"invalid allocator storage extent for {name}")
        if name in pools:
            raise ValueError(f"duplicate source allocator declaration: {name}")
        if declaration["kind"] == "direct":
            data_offset = 0
            type_source = objalloc_header.relative_to(source_root).as_posix()
        else:
            layout = allocator_types.get(declaration["type"])
            if layout is None:
                raise ValueError(f"missing allocator struct layout: {declaration['type']}")
            # `_source_declarations` accepts only a first-member allocator,
            # so its proven offset is zero.  The tuple also carries the exact
            # header path used for the declaration.
            data_offset = 0
            _, type_source = layout
            source_paths.add(source_root / type_source)
        if data_offset + ALLOC_DATA_BYTES > symbol["size"]:
            raise ValueError(f"allocator field exceeds storage extent for {name}")
        declaration_path = declaration["path"].relative_to(source_root).as_posix()
        source_paths.add(source_root / declaration_path)
        pools[name] = PoolDeclaration(
            name=name,
            address=symbol["address"],
            storage_size=symbol["size"],
            data_offset=data_offset,
            data_size=object_size,
            declaration_type=declaration["type"],
            declaration_source=declaration_path,
            declaration_line=declaration["line"],
            type_source=type_source,
        )
    if not pools:
        raise ValueError("source declarations produced no symbol-backed allocator pools")
    source_files = []
    for path in sorted(source_paths):
        source_files.append({
            "path": path.relative_to(source_root).as_posix(),
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        })
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "object_type": "HSD_ObjAllocData",
        "object_size": object_size,
        "symbols_sha256": hashlib.sha256(symbols_path.read_bytes()).hexdigest(),
        "source_files": source_files,
        "pools": {name: pools[name].as_dict() for name in sorted(pools)},
    }


def validate_observed_pool_addresses(observed_addresses: Iterable[int], inventory: Mapping) -> None:
    """Validate capture addresses without allowing captures to extend inventory."""
    pools = inventory.get("pools") if isinstance(inventory, Mapping) else None
    if not isinstance(pools, Mapping):
        raise ValueError("invalid source pool inventory")
    known = {int(item["address"]) for item in pools.values()}
    observed = set()
    for address in observed_addresses:
        if not isinstance(address, int) or not 0 <= address <= 0xFFFFFFFF:
            raise ValueError(f"invalid observed pool address: {address!r}")
        observed.add(address)
    unknown = sorted(observed - known)
    if unknown:
        formatted = ", ".join(f"0x{address:08X}" for address in unknown)
        raise ValueError(f"observed allocator pool is absent from source inventory: {formatted}")
