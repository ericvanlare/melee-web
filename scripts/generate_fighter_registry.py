#!/usr/bin/env python3
"""Generate source-only fighter identity metadata from narrow C initializers.

This does not execute upstream code or read any game assets. Unknown initializer
syntax fails instead of guessing names, IDs, aliases, or costume ordering.
"""
from __future__ import annotations
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


class RegistryError(ValueError):
    pass


def strip_comments(text):
    return re.sub(r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*',
                  lambda m: m.group() if m.group().startswith('"') else " ", text, flags=re.S)


def initializer(text, name):
    matches = list(re.finditer(r'\b' + re.escape(name) + r'\s*\[[^]]*\]\s*=\s*\{', text))
    if len(matches) != 1:
        raise RegistryError(f"Expected exactly one array initializer: {name}")
    begin = matches[0].end() - 1
    depth = 0
    for i in range(begin, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if not depth:
                return parse_list(text[begin:i + 1])
    raise RegistryError(f"Unterminated initializer: {name}")


def parse_list(text):
    token = re.compile(r'\s*(\{|\}|,|[A-Za-z_]\w*|[0-9]+)')
    tokens = []
    at = 0
    while at < len(text):
        match = token.match(text, at)
        if not match:
            raise RegistryError("Unsupported registry initializer syntax")
        if re.fullmatch(r'0[0-9]+', match[1]):
            raise RegistryError("Nondecimal integer syntax requires explicit source interpretation")
        tokens.append(match[1]); at = match.end()
    if not tokens:
        raise RegistryError("Empty registry initializer")
    cursor = 0

    def parse():
        nonlocal cursor
        if tokens[cursor] != '{':
            value = tokens[cursor]; cursor += 1
            return value
        cursor += 1
        result = []
        while cursor < len(tokens) and tokens[cursor] != '}':
            result.append(parse())
            if cursor < len(tokens) and tokens[cursor] == ',':
                cursor += 1
            elif cursor >= len(tokens) or tokens[cursor] != '}':
                raise RegistryError("Missing registry initializer separator")
        if cursor >= len(tokens):
            raise RegistryError("Unterminated registry initializer")
        cursor += 1
        return result
    result = parse()
    if cursor != len(tokens):
        raise RegistryError("Trailing registry initializer tokens")
    return result


def generate(melee):
    source = melee / 'src/melee/ft'
    texts = {path: strip_comments(path.read_text()) for path in sorted(source.rglob('*.c'))}
    main = texts[source / 'ftdata.c']
    forward = strip_comments((source / 'forward.h').read_text())
    enum = re.search(r'typedef\s+enum\s+FighterKind\s*\{(.*?)\}', forward, re.S)
    if not enum:
        raise RegistryError("FighterKind enum is missing")
    kinds = []
    for entry in enum[1].split(','):
        entry = entry.strip()
        if entry == 'FTKIND_NONE':
            break
        if not re.fullmatch(r'FTKIND_[A-Z0-9_]+', entry):
            raise RegistryError("FighterKind requires explicit enum interpretation")
        kinds.append(entry)
    if not 1 <= len(kinds) <= 64:
        raise RegistryError("Unexpected fighter kind count")
    pairs = initializer(main, 'ftData_803C1F40')
    costumes = initializer(main, 'ftData_803C2360')
    animations = initializer(main, 'ftData_803C23E4')
    motions = initializer(main, 'ftData_Table_Unk0')
    if any(len(table) != len(kinds) for table in (pairs, costumes, animations, motions)):
        raise RegistryError("Fighter source registry table counts differ")
    used = {source / 'ftdata.c', source / 'forward.h'}
    declarations = {}
    pattern = re.compile(r'\bchar\s+(\w+)\s*\[[^]]*\]\s*=\s*((?:"(?:\\.|[^"\\])*"\s*)+);')
    for path, text in texts.items():
        for match in pattern.finditer(text):
            literals = re.findall(r'"(?:\\.|[^"\\])*"', match[2])
            value = ''.join(ast.literal_eval(literal) for literal in literals)
            declarations.setdefault(match[1], []).append((path, value))

    def string(name, local=None):
        if name == 'NULL':
            return ''
        matches = declarations.get(name, [])
        scoped = [entry for entry in matches if entry[0] == local]
        matches = scoped or matches
        if len(matches) != 1:
            raise RegistryError(f"Missing or ambiguous literal string: {name}")
        path, value = matches[0]
        if not value or not value.isascii() or any(ord(c) < 32 for c in value):
            raise RegistryError(f"Unsupported source registry string: {name}")
        used.add(path)
        return value

    rows = []
    for kind, name in enumerate(kinds):
        if len(pairs[kind]) != 2 or len(motions[kind]) != 2 or motions[kind][0] != '0':
            raise RegistryError("Unsupported fighter data or motion count initializer")
        count = int(motions[kind][1])
        if not 0 < count <= 1024:
            raise RegistryError("Motion count outside source inspection budget")
        definition = [path for path, text in texts.items() if re.search(
            r'\bFighter_CostumeStrings\s+' + re.escape(costumes[kind]) + r'\s*\[[^]]*\]\s*=', text)]
        if len(definition) != 1:
            raise RegistryError(f"Missing or ambiguous costume array: {costumes[kind]}")
        path = definition[0]; used.add(path)
        variants = initializer(texts[path], costumes[kind])
        if not 1 <= len(variants) <= 32:
            raise RegistryError("Costume initializer outside supported source budget")
        for costume, values in enumerate(variants):
            if len(values) != 3:
                raise RegistryError("Costume record must contain exactly three strings")
            model_file, model_symbol, material_animation_symbol = [string(value, path) for value in values]
            if not model_file or not model_symbol:
                raise RegistryError("Costume has no model identity")
            rows.append((kind, costume, count, name, string(pairs[kind][0]),
                         string(pairs[kind][1]), string(animations[kind]), model_file, model_symbol,
                         material_animation_symbol))
    digest = hashlib.sha256()
    paths = sorted(used)
    for path in paths:
        digest.update(path.relative_to(melee).as_posix().encode() + b'\0' + path.read_bytes() + b'\0')
    lines = ['// Generated by scripts/generate_fighter_registry.py. Do not edit.',
             '// Source-only initializer SHA-256: ' + digest.hexdigest(),
             '// Registries: ftData_803C1F40, ftData_803C2360, ftData_803C23E4, ftData_Table_Unk0.',
             f'constexpr std::uint32_t source_fighter_kind_count = {len(kinds)};',
             'constexpr FighterCostume source_costumes[] = {']
    for row in rows:
        lines.append('    {' + ', '.join(str(value) if isinstance(value, int) else json.dumps(value) for value in row) + '},')
    lines += ['};', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--melee-root', type=Path, default=ROOT / '.deps/melee')
    parser.add_argument('--output', type=Path, default=ROOT / 'src/fighter_registry.inc')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    try:
        content = generate(args.melee_root)
        if args.check:
            if not args.output.is_file() or args.output.read_text() != content:
                raise RegistryError("Generated registry differs from pinned source; regenerate and review")
        else:
            args.output.write_text(content)
    except (OSError, ValueError) as error:
        parser.exit(1, f'Registry generation failed: {error}\n')


if __name__ == '__main__':
    main()
