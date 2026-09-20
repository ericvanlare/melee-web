#!/usr/bin/env python3
"""Inventory the pinned original game and declared port work without inventing coverage.

This read-only source report never compiles, launches, or credits linked/compared
code. Its outputs belong under ignored work/. See docs/FULL_GAME_PORT.md.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
IMPLEMENTATION = {'not_integrated', 'partial', 'integrated'}
ACCEPTANCE = {'not_evaluated', 'partial', 'passed'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def relative_path(value):
    if not isinstance(value, str) or not value or '\\' in value:
        raise ValueError('Expected a repository-relative path')
    path = Path(value)
    if path.is_absolute() or '..' in path.parts:
        raise ValueError('Paths must remain inside their declared source root')
    return path


def validate_inventory(data, root, source):
    if not isinstance(data, dict):
        raise ValueError('Inventory must be an object')
    if data.get('schema') != 'melee-web-full-game-inventory' or data.get('version') != 1:
        raise ValueError('Unsupported full-game inventory schema')
    rows = data.get('features')
    if not isinstance(rows, list) or not rows:
        raise ValueError('Inventory must retain its full nonempty feature denominator')
    ids = set()
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError('Feature row must be an object')
        identity = row.get('id', '')
        if not isinstance(identity, str) or not re.fullmatch(r'[a-z][a-z0-9-]*', identity) or identity in ids:
            raise ValueError('Feature identities must be unique stable kebab-case names')
        ids.add(identity)
        for key in ('category', 'title', 'next_step'):
            if not isinstance(row.get(key), str) or not row[key].strip():
                raise ValueError(f'{identity}: missing {key}')
        if row.get('implementation') not in IMPLEMENTATION or row.get('acceptance') not in ACCEPTANCE:
            raise ValueError(f'{identity}: unknown implementation or acceptance declaration')
        for key in ('source_refs', 'depends_on', 'pass_criteria', 'evidence'):
            if not isinstance(row.get(key), list) or any(not isinstance(v, str) or not v for v in row[key]):
                raise ValueError(f'{identity}: invalid {key}')
        if not row['source_refs'] or not row['pass_criteria']:
            raise ValueError(f'{identity}: source and acceptance criteria are required')
        if row['acceptance'] == 'passed' and (not row['evidence'] or row['implementation'] != 'integrated'):
            raise ValueError(f'{identity}: acceptance requires integrated implementation and evidence')
        for base, paths in ((source / 'src', row['source_refs']), (root, row['evidence'])):
            for value in paths:
                path = base / relative_path(value)
                if not path.resolve().is_relative_to(base.resolve()) or not path.exists():
                    raise ValueError(f'{identity}: missing or escaping evidence/source path {value}')
    by_id = {r['id']: r for r in rows}
    visiting, visited = set(), set()

    def visit(identity):
        if identity in visiting:
            raise ValueError('Feature dependency cycle')
        if identity in visited:
            return
        visiting.add(identity)
        for dependency in by_id[identity]['depends_on']:
            if dependency not in ids:
                raise ValueError(f'{identity}: unknown dependency {dependency}')
            visit(dependency)
        visiting.remove(identity)
        visited.add(identity)
    for identity in ids:
        visit(identity)
    return rows


def source_functions(symbols, splits):
    """Keep aliases and unknown ownership/size visible in the original denominator."""
    unit, ranges = None, []
    for line in splits.splitlines():
        if line and not line[0].isspace() and line.endswith(':'):
            unit = line[:-1]
        match = re.fullmatch(r'\s+(\S+)\s+start:(0x[0-9A-Fa-f]+)\s+end:(0x[0-9A-Fa-f]+)(?:\s+.*)?', line)
        if match and unit != 'Sections':
            section, start, end = match.groups()
            if int(start, 16) >= int(end, 16):
                raise ValueError('Invalid original source interval')
            ranges.append((section, int(start, 16), int(end, 16), unit))
    functions, addresses = [], set()
    pattern = re.compile(r'(.+?)\s*=\s*(\S+):(0x[0-9A-Fa-f]+);\s*//\s*(.*)')
    for line in symbols.splitlines():
        if not re.search(r'\btype:function\b', line):
            continue
        match = pattern.fullmatch(line)
        if not match:
            raise ValueError('Unrecognized original function symbol declaration')
        name, section, address, metadata = match.groups()
        address = int(address, 16)
        size_match = re.search(r'\bsize:(0x[0-9A-Fa-f]+|[0-9]+)\b', metadata)
        size = int(size_match.group(1), 0) if size_match else None
        owners = sorted({u for s, begin, end, u in ranges
                         if s == section and begin <= address < end})
        location = (section, address)
        alias = location in addresses
        addresses.add(location)
        functions.append({'id': f'{section}:{address:08x}:{name}', 'name': name,
                          'section': section, 'address': f'0x{address:08x}', 'size': size,
                          'units': owners, 'alias_address': alias,
                          'browser_linkage': 'not_collected', 'reference_tested': 'not_collected'})
    if not functions:
        raise ValueError('Original function denominator is empty')
    # Byte accounting uses interval union, so aliases and overlapping extents
    # cannot inflate it. Unknown sizes remain a separate visible count.
    by_section = {}
    for row in functions:
        if row['size'] is not None:
            start = int(row['address'], 16)
            by_section.setdefault(row['section'], []).append((start, start + row['size']))
    total = 0
    for spans in by_section.values():
        edge = -1
        for start, end in sorted(spans):
            total += max(0, end - max(start, edge))
            edge = max(edge, end)
    return functions, {'function_symbols': len(functions), 'unique_function_addresses': len(addresses),
                       'known_function_bytes_union': total,
                       'unknown_size_symbols': sum(r['size'] is None for r in functions),
                       'unassigned_symbols': sum(not r['units'] for r in functions),
                       'ambiguous_owner_symbols': sum(len(r['units']) > 1 for r in functions)}


def make_report(root, inventory_path):
    source = root / '.deps/melee'
    lock = json.loads((root / 'dependencies.lock.json').read_text())
    pin = lock['repositories']['melee']['commit']
    actual = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != pin:
        raise ValueError('Original checkout does not match dependency pin')
    changes = subprocess.check_output(
        ['git', '-C', str(source), 'status', '--porcelain=v1', '--untracked-files=all'],
        text=True)
    if changes:
        raise ValueError('Original checkout contains local changes')
    data = json.loads(inventory_path.read_text())
    if data.get('source', {}).get('commit') != pin:
        raise ValueError('Acceptance inventory source pin is stale')
    features = validate_inventory(data, root, source)
    symbols = source / 'config/GALE01/symbols.txt'
    splits = source / 'config/GALE01/splits.txt'
    functions, counts = source_functions(symbols.read_text(), splits.read_text())
    categories = {}
    for row in features:
        categories.setdefault(row['category'], []).append(row)
    return {'schema': 'melee-web-full-game-report', 'version': 1,
            'repository_commit': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
            'tracked_diff_sha256': hashlib.sha256(subprocess.check_output(['git', '-C', str(root), 'diff', 'HEAD', '--'])).hexdigest(),
            'source': data['source'], 'scope': data['scope'],
            'provenance': {'inventory_sha256': sha(inventory_path), 'dependency_lock_sha256': sha(root / 'dependencies.lock.json'),
                           'original_symbols_sha256': sha(symbols), 'original_splits_sha256': sha(splits),
                           'generator_sha256': sha(Path(__file__))},
            'original_source_inventory': counts,
            'coverage': {'browser_linked_functions': None, 'reference_tested_functions': None,
                         'full_game_accepted_fraction': None,
                         'reason': 'Linker reachability and scenario-to-source comparison evidence are not yet ingested. Feature states are declarations, not automatically verified acceptance or engineering-effort percentages.'},
            'feature_summary': {category: {'total': len(rows),
                'implementation_declarations': dict(Counter(r['implementation'] for r in rows)),
                'acceptance_declarations': dict(Counter(r['acceptance'] for r in rows))}
                for category, rows in sorted(categories.items())},
            'features': features, 'functions': functions}


def report_html(report):
    payload = json.dumps(report).replace('<', '\\u003c')
    return '''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Melee Web full-game inventory</title><style>
body{font:16px/1.5 system-ui;margin:32px auto;padding:0 20px;max-width:1200px;color:#20242c;background:#fafbfc}
h1{font-size:30px}input,select{font:inherit;padding:8px;margin:0 8px 12px 0;max-width:90%}
table{border-collapse:collapse;width:100%;font-size:14px}td,th{padding:10px;text-align:left;vertical-align:top;border-bottom:1px solid #dce1e8}
td:first-child{min-width:170px}small{color:#526174}.notice{background:#e9eef5;padding:16px;border-radius:8px}code{overflow-wrap:anywhere}
</style><h1>Melee Web full-game inventory</h1>
<p class="notice">Source inventory and declared feature work. Browser linkage and reference-tested source coverage are not collected yet. A compiled or exercised function does not establish full behavior, and counts do not estimate time remaining.</p>
<p id="identity"></p><p id="counts"></p><h2>Feature work</h2>
<label>Find a feature <input id="search" type="search"></label>
<label>Category <select id="category"><option value="">All categories</option></select></label>
<p id="visible"></p><table><thead><tr><th>Feature</th><th>Declared state</th><th>Acceptance and next step</th></tr></thead><tbody id="features"></tbody></table>
<h2>Original source functions</h2><p>Aliases remain visible; known byte totals use the union of original intervals. Unknown owners and sizes remain in the denominator.</p>
<label>Find a function or unit <input id="source-search" type="search"></label><p id="source-count"></p>
<table><thead><tr><th>Symbol</th><th>Original source unit</th><th>Bytes</th></tr></thead><tbody id="functions"></tbody></table>
<script type="application/json" id="data">''' + payload + '''</script><script>
const r=JSON.parse(document.querySelector('#data').textContent), $=s=>document.querySelector(s);
$('#identity').textContent=`Port commit ${r.repository_commit}; original ${r.source.commit}.`;
const n=r.original_source_inventory;
$('#counts').textContent=`${r.features.length} declared feature entries; ${n.function_symbols} original function symbols; ${n.known_function_bytes_union.toLocaleString()} known original function bytes; ${n.unknown_size_symbols} unknown sizes; ${n.unassigned_symbols} unassigned source owners.`;
for(const name of Object.keys(r.feature_summary)){const o=document.createElement('option');o.value=name;o.textContent=name;$('#category').append(o);}
function cell(tr,text){const td=document.createElement('td');td.textContent=text;tr.append(td);}
function features(){const q=$('#search').value.toLowerCase(),category=$('#category').value,rows=r.features.filter(f=>(!category||f.category===category)&&JSON.stringify(f).toLowerCase().includes(q));$('#features').replaceChildren();for(const f of rows){const tr=document.createElement('tr');cell(tr,f.title+' ['+f.id+']');cell(tr,`Implementation: ${f.implementation}; acceptance: ${f.acceptance}`);cell(tr,f.pass_criteria.join(' ')+' Next: '+f.next_step);$('#features').append(tr);}$('#visible').textContent=`${rows.length} of ${r.features.length} entries shown.`;}
function functions(){const q=$('#source-search').value.toLowerCase(),rows=r.functions.filter(f=>(f.name+' '+f.units.join(' ')).toLowerCase().includes(q));$('#functions').replaceChildren();for(const f of rows.slice(0,200)){const tr=document.createElement('tr');cell(tr,f.name+' @ '+f.address);cell(tr,f.units.join(', ')||'Unassigned');cell(tr,f.size===null?'Unknown':String(f.size));$('#functions').append(tr);}$('#source-count').textContent=`${rows.length} matching symbols; first ${Math.min(rows.length,200)} displayed. All entries retained in report.json.`;}
$('#search').addEventListener('input',features);$('#category').addEventListener('change',features);$('#source-search').addEventListener('input',functions);features();functions();
</script></html>'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, default=ROOT / 'docs/full-game-inventory.json')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    output = args.out.resolve()
    if not output.is_relative_to((ROOT / 'work').resolve()) or output.exists():
        parser.error('Use a new output directory under ignored work/')
    try:
        report = make_report(ROOT, args.inventory)
        output.mkdir(parents=True, exist_ok=False)
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        (output / 'index.html').write_text(report_html(report))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f'full-game inventory: {error}\n')
    print(json.dumps({'features': len(report['features']), **report['original_source_inventory'],
                      'coverage': report['coverage'], 'output': str(output.relative_to(ROOT))}))


if __name__ == '__main__':
    main()
