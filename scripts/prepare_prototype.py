#!/usr/bin/env python3
"""Assemble an isolated, local-only prototype preview from an existing build.

Does not rebuild or modify the shared runtime. Never use this as a public release
packager: the temporary iframe still contains development code.
"""
import argparse
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from browser_replay_validation import BUILD_ARTIFACTS  # noqa: E402

PROTOTYPE_FILES = (
    'prototype.html', 'prototype.css', 'prototype-shell.mjs',
    'prototype-runtime-adapter.mjs', 'prototype-content.mjs', 'prototype-keyboard-layouts.mjs',
)


def content_manifest(source):
    """Fail closed if the native row format changes; never infer support from assets."""
    tables = {}
    for kind in ('Fighter', 'Stage'):
        match = re.search(r'static const MeleeWeb' + kind + r'Content rows\[\] = \{(.*?)\n    \};', source, re.S)
        if not match:
            raise ValueError(f'Native {kind} content table was not found')
        table_body = re.sub(r'/\*.*?\*/', '', match[1], flags=re.S)
        lines = [line.strip() for line in table_body.splitlines() if line.strip()]
        pattern = (r'\{\s*(CKIND_\w+),\s*FTKIND_\w+,\s*\d+,\s*"([^"]+)".*\},'
                   if kind == 'Fighter' else r'\{\s*(St_Kind_\w+),\s*Gr_Kind_\w+,\s*"([^"]+)".*\},')
        rows = []
        for line in lines:
            row = re.fullmatch(pattern, line)
            if not row:
                raise ValueError(f'Unrecognized native {kind} row; update the prototype bridge')
            rows.append({'sourceName': row[1], 'name': row[2]})
        if not rows or len({row['sourceName'] for row in rows}) != len(rows):
            raise ValueError(f'Empty or duplicate native {kind} inventory')
        tables['fighters' if kind == 'Fighter' else 'stages'] = rows
    return {'schema': 'melee-web-prototype-content-v1', **tables}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(runtime_build, output, environment='staging'):
    runtime_build = Path(runtime_build).resolve(strict=True)
    output = Path(output).resolve()
    if environment not in ('staging', 'development'):
        raise ValueError('Only local staging/development prototypes are supported')
    # No clobbering a shared build or any pre-existing preview, even on a retry.
    if output.exists():
        raise ValueError('Output must be a new directory; choose a fresh ignored build/ or work/ path')
    if output == runtime_build or runtime_build.is_relative_to(output):
        raise ValueError('Output overlaps the runtime build')
    for name in BUILD_ARTIFACTS:
        if not (runtime_build / name).is_file():
            raise ValueError(f'Missing compiled runtime artifact: {name}')
    for name in BUILD_ARTIFACTS:
        if (ROOT / 'web' / name).is_file() and digest(ROOT / 'web' / name) != digest(runtime_build / name):
            raise ValueError(f'Runtime build does not match this checkout: {name}. Use a matching snapshot.')
    manifest = content_manifest((ROOT / 'src/gameplay_content.h').read_text())
    before = {name: digest(runtime_build / name) for name in BUILD_ARTIFACTS}
    output.mkdir(parents=True)
    for name in BUILD_ARTIFACTS:
        shutil.copyfile(runtime_build / name, output / name)
    if before != {name: digest(runtime_build / name) for name in BUILD_ARTIFACTS} or before != {
        name: digest(output / name) for name in BUILD_ARTIFACTS
    }:
        raise ValueError('The runtime build changed during copying. Keep this failed snapshot local and retry in a new directory.')
    for name in PROTOTYPE_FILES:
        shutil.copyfile(ROOT / 'web' / name, output / name)
    # Retain the notice for the keyboard mapping compiled into the runtime.
    shutil.copyfile(ROOT / 'licenses/b0xx-ahk.txt', output / 'b0xx-ahk-LICENSE.txt')
    page = (output / 'prototype.html').read_text().replace(
        'data-environment="staging"', f'data-environment="{environment}"')
    (output / 'prototype.html').write_text(page)
    (output / 'prototype-content.json').write_text(json.dumps(manifest, indent=2) + '\n')
    identity = {
        'schema': 'melee-web-prototype-preview-v1', 'environment': environment,
        'public_release': False, 'adapter': 'temporary-same-origin-iframe',
        'runtime_sha256': before,
        'native_content_sha256': digest(ROOT / 'src/gameplay_content.h'),
        'prototype_sha256': {name: digest(output / name) for name in (*PROTOTYPE_FILES, 'prototype-content.json', 'b0xx-ahk-LICENSE.txt')},
    }
    (output / 'prototype-build.json').write_text(json.dumps(identity, indent=2) + '\n')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-build', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--environment', choices=('development', 'staging'), default='staging')
    args = parser.parse_args()
    try:
        output = prepare(args.runtime_build, args.output, args.environment)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f'Local prototype assembled at {output}')
    print(f'python3 scripts/serve.py --directory {output} --port 8794')
    print('Open http://127.0.0.1:8794/prototype.html — not a public release bundle.')


if __name__ == '__main__':
    main()
