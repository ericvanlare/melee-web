#!/usr/bin/env python3
"""Generate exact pointer-free common-data fields and source ABI assertions.

The supported source grammar is intentionally narrow. Offsets come from C field
sizes/order, then are checked against source annotations (with one documented
pinned x808 annotation typo). No game bytes are read by this generator.
"""
from __future__ import annotations
import argparse
import ast
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


class SchemaError(ValueError):
    pass


def block(text, marker):
    at = text.find(marker)
    if at < 0 or text.find(marker, at + 1) >= 0:
        raise SchemaError(f"Expected exactly one source block: {marker}")
    start = text.find('{', at)
    end = text.find('}', start)
    if start < 0 or end < 0 or '{' in text[start + 1:end]:
        raise SchemaError("Unsupported nested source declaration")
    return text[start + 1:end]


def array_count(expression):
    if expression is None:
        return 1
    def number(node):
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Sub):
            return number(node.left) - number(node.right)
        raise SchemaError("Unsupported source array count expression")
    try:
        value = number(ast.parse(expression, mode='eval').body)
    except SyntaxError as error:
        raise SchemaError("Unsupported source array count syntax") from error
    if not 1 <= value <= 64:
        raise SchemaError("Source common array count is outside the schema budget")
    return value


def fields(text):
    pattern = re.compile(r'/\*\s*\+([0-9A-Fa-f]+)\s*\*/\s*(\w+)\s+(\w+)\s*(?:\[([^]]+)\])?\s*;')
    result = []
    cursor = 0
    def empty(fragment):
        return not re.sub(r'/\*.*?\*/|//[^\n]*', '', fragment, flags=re.S).strip()
    for match in pattern.finditer(text):
        if not empty(text[cursor:match.start()]):
            raise SchemaError("Common source field lacks a supported typed declaration and offset")
        result.append((int(match[1], 16), match[2], match[3], array_count(match[4]), match[4] is not None))
        cursor = match.end()
    if not result or not empty(text[cursor:]):
        raise SchemaError("Unparsed common source field")
    return result


SCALARS = {'float': ('float', 'F32', 4), 'int': ('int32_t', 'I32', 4),
           'u32': ('uint32_t', 'U32', 4), 'u8': ('uint8_t', 'BYTE', 1),
           'UNK_T': ('uint32_t', 'OPAQUE32', 4),
           'HitCapsuleState': ('int32_t', 'I32', 4), 'enum_t': ('int32_t', 'I32', 4)}


def generate(melee):
    paths = [melee / 'src/melee/ft/types.h', melee / 'src/melee/lb/forward.h', melee / 'src/melee/ft/fighter.c']
    ft, lb, fighter = [path.read_text() for path in paths]
    nested = fields(block(lb, 'typedef struct lbColl_80008D30_arg1 {'))
    top = fields(block(ft, 'struct ftCommonData {'))
    compound = {
        'Vec2': ('MeleeWebCommonVec2', [(0,'float','x',1,False), (4,'float','y',1,False)]),
        'Vec3': ('MeleeWebCommonVec3', [(0,'float','x',1,False), (4,'float','y',1,False), (8,'float','z',1,False)]),
        'GXColor': ('MeleeWebCommonColor', [(i,'u8',name,1,False) for i,name in enumerate('rgba')]),
        'lbColl_80008D30_arg1': ('MeleeWebCommonHitbox', nested),
    }
    layouts = {}
    corrections = {'x808': (0x804, 0x808)}
    def layout(source_fields, allow_typo=False):
        offset = 0
        struct_alignment = 1
        declarations, leaves = [], []
        for annotated, kind, name, count, is_array in source_fields:
            if kind in SCALARS:
                ctype, tag, width = SCALARS[kind]
                element = [(0, tag, '')]
                alignment = width
            elif kind in compound:
                ctype, _ = compound[kind]
                _, element, width, alignment = layouts[kind]
            else:
                raise SchemaError(f"Unsupported common field type: {kind}")
            # Compound alignment follows its actual members, so GXColor's four
            # bytes have alignment 1; vectors and the hitbox have alignment 4.
            struct_alignment = max(struct_alignment, alignment)
            offset = (offset + alignment - 1) // alignment * alignment
            if annotated != offset and not (allow_typo and corrections.get(name) == (annotated, offset)):
                raise SchemaError(f"Source offset differs from typed layout: {name} annotation {annotated:#x}, layout {offset:#x}")
            declarations.append(f'    {ctype} {name}' + (f'[{count}]' if is_array else '') + ';')
            for index in range(count):
                member = name + (f'[{index}]' if is_array else '')
                for relative, tag, suffix in element:
                    leaves.append((offset + width * index + relative, tag, member + ('.' + suffix if suffix else '')))
            offset += width * count
        return declarations, leaves, (offset + struct_alignment - 1) // struct_alignment * struct_alignment, struct_alignment
    for name, (_, members) in compound.items():
        layouts[name] = layout(members)
    declarations, leaves, size, _ = layout(top, True)
    if size != 0x818:
        raise SchemaError(f"Pinned common scalar size changed: {size:#x}")
    load = block(fighter, 'void Fighter_LoadCommonData(void)')
    load = re.sub(r'/\*.*?\*/|//[^\n]*', '', load, flags=re.S)
    roots = re.findall(r'\b([A-Za-z_]\w*)\s*=\s*pData\[([0-9]+)\]\s*;', load)
    if len(roots) != 23 or [int(index) for _, index in roots] != list(range(23)):
        raise SchemaError("Expected the original 23 common-root assignments in order")
    remainder = re.sub(r'\b[A-Za-z_]\w*\s*=\s*pData\[[0-9]+\]\s*;', '', load)
    remainder = re.sub(r'void\s*\*\*\s*pData\s*;', '', remainder)
    remainder = re.sub(r'lbArchive_LoadSymbols\("PlCo.dat",\s*\(void\*\*\)\s*&pData,\s*"ftLoadCommonData",\s*0\);', '', remainder)
    if remainder.strip():
        raise SchemaError("Common loader now performs unsupported work besides loading and root publication")
    digest = hashlib.sha256()
    for path in paths:
        digest.update(path.relative_to(melee).as_posix().encode() + b'\0' + path.read_bytes() + b'\0')
    out = ['// Generated by scripts/generate_common_schema.py. Do not edit.',
           '// Source SHA-256: ' + digest.hexdigest(),
           '// x808 uses its sequential C layout; the pinned +804 comment is a known typo.',
           '#ifndef MELEE_WEB_COMMON_SCHEMA_H', '#define MELEE_WEB_COMMON_SCHEMA_H',
           '#include <stdint.h>', '#include <stddef.h>',
           '#define MELEE_WEB_COMMON_SCALAR_BYTES 0x818u', '#define MELEE_WEB_COMMON_ROOT_COUNT 23u']
    for name, (ctype, _) in compound.items():
        out += ['typedef struct ' + ctype + ' {'] + layouts[name][0] + ['} ' + ctype + ';']
    out += ['// UNK_T source placeholders are opaque 32-bit scalar words, never host addresses.',
            'typedef struct MeleeWebCommonScalars {'] + declarations + ['} MeleeWebCommonScalars;', '']
    out += ['#define MELEE_WEB_COMMON_FIELDS(X) \\'] + [
        f'    X(0x{offset:03x}, {tag}, {member})' + (' \\' if i + 1 < len(leaves) else '')
        for i, (offset, tag, member) in enumerate(leaves)]
    out += ['', '#define MELEE_WEB_COMMON_ROOTS(X) \\'] + [
        f'    X({index}, {name})' + (' \\' if i + 1 < len(roots) else '')
        for i, (name, index) in enumerate(roots)]
    out += ['', '#ifdef __cplusplus', '#define MELEE_WEB_COMMON_ASSERT static_assert', '#else',
            '#define MELEE_WEB_COMMON_ASSERT _Static_assert', '#endif',
            '// Invoke for the actual original ftCommonData only in its 32-bit target ABI.',
            '#define MELEE_WEB_COMMON_ASSERT_LAYOUT(T) \\',
            '    MELEE_WEB_COMMON_ASSERT(sizeof(T) == MELEE_WEB_COMMON_SCALAR_BYTES, "common scalar size"); \\']
    for i, (offset, tag, member) in enumerate(leaves):
        width = 1 if tag == 'BYTE' else 4
        out.append(f'    MELEE_WEB_COMMON_ASSERT(offsetof(T, {member}) == 0x{offset:03x} && sizeof(((T*)0)->{member}) == {width}, "common field {member}");' + (' \\' if i + 1 < len(leaves) else ''))
    out += ['', '#endif', '']
    return '\n'.join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--melee-root', type=Path, default=ROOT / '.deps/melee')
    parser.add_argument('--output', type=Path, default=ROOT / 'src/common_schema.h')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    try:
        result = generate(args.melee_root)
        if args.check:
            if not args.output.is_file() or args.output.read_text() != result:
                raise SchemaError('Common schema differs from pinned source; regenerate and review')
        else:
            args.output.write_text(result)
    except (OSError, ValueError) as error:
        parser.exit(1, f'Common schema generation failed: {error}\n')


if __name__ == '__main__':
    main()
