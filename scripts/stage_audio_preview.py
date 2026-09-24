#!/usr/bin/env python3
"""Prepare, audit and HTTP-verify the explicit audio-enabled release graph.

This entry point remains staging-only. release_audio_player.py selects the
production package policy while reusing the same source-bound native producer.
Neither schema is accepted by the legacy silent packager. Only the explicit
file inventory is uploaded; producer identities and manifests stay outside it.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

import build as producer
import build_public as public
import verify_public_http as http

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = 'melee-web-audio-preview-package-v1'
PRODUCTION_SCHEMA = 'melee-web-audio-player-package-v1'
BUILD_DIR = 'build/browser-audio-preview-release'
IDENTITY = 'build/runtime-audio-preview-identity.json'
NATIVE = tuple('gameplay_audio_preview.' + ext for ext in ('js', 'wasm', 'data'))
MODULES = (*public.PLAYER_SOURCE_RUNTIME_FILES, 'audio-preview-runtime.mjs',
           'runtime-audio-assets.mjs', 'runtime-audio.mjs', 'dsp-coefficients.mjs',
           'audio-worklet.js', 'audio-ring.mjs')
OPERATOR = 'NaiadAI, LLC'
CONTACT = 'legal@webmelee.gg'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read(path):
    path = Path(path)
    require(not path.is_symlink() and path.is_file(), f'Expected regular file: {path.name}')
    require(path.resolve().is_relative_to(ROOT.resolve()), 'Input escapes the source checkout')
    return path.read_bytes()


def source_commit():
    require(not subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=normal'],
                                       cwd=ROOT, text=True).strip(), 'Commit the source before packaging')
    return subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()


def runtime():
    raw = read(ROOT / IDENTITY)
    identity = json.loads(raw)
    require(identity.get('schema') == 'melee-web-runtime-audio-preview-build-v1' and
            identity.get('target') == 'runtime-audio-preview' and
            identity.get('configuration') == 'Release' and identity.get('artifact_root') == BUILD_DIR,
            'Expected the separate Release audio-preview producer')
    require(identity.get('source_inputs') == producer._source_inputs_record(ROOT, ROOT / 'build/gameplay-source/src'),
            'Native sources differ from the producer identity')
    tool_hashes = identity.get('toolchain', {}).get('sha256', {})
    require(set(tool_hashes) == public.RUNTIME_TOOLCHAIN_PATHS, 'Unexpected producer toolchain inventory')
    for name, expected in tool_hashes.items():
        require(digest(read(ROOT / name)) == expected, f'Stale producer tool: {name}')
    directory = ROOT / BUILD_DIR
    files = {name: read(directory / name) for name in NATIVE}
    records = [producer._file_record(directory / name, ROOT) for name in NATIVE]
    require(identity.get('artifacts') == records, 'Native artifact identity mismatch')
    require(identity.get('wasm_exports') == producer._verify_public_exports(
        directory / NATIVE[1], directory / NATIVE[0]), 'Native export surface changed')
    require(identity.get('audio_graph') == producer._audio_preview_graph_proof(ROOT, directory),
            'Audio preview compile/link proof changed')
    require(identity.get('pipeline_seed') == producer._pipeline_seed_record(ROOT, directory),
            'Pipeline seed identity changed')
    require(digest(files[NATIVE[2]]) == identity['pipeline_seed']['expected_sha256'],
            'Native data must contain only the pinned pipeline seed')
    require(identity.get('audio_policy') == {
        'mode': 'enabled-preview', 'pcm_output': True, 'dsp_resampler': True,
        'dsp_coefficients_required': True,
    }, 'Audio-preview policy mismatch')
    return files, digest(raw)


def replace_once(text, old, new):
    require(text.count(old) == 1, 'Reviewed preview template boundary changed')
    return text.replace(old, new, 1)


def player_html(text):
    text, count = re.subn(r'<span id="audio-note">.*?</span></span>', '', text)
    require(count == 1, 'Reviewed preview template boundary changed')
    return replace_once(text, 'Audio is disabled in this alpha. ', '')


def notices(text, *, production=False):
    # Preserve the surrounding contact/rights and dependency notices, while
    # replacing paragraphs that describe a different, deliberately silent build.
    paragraphs = {
        '<p>When the player is available, the current prototype supports':
            ('<p>This public alpha enables music and sound effects using the replacement audio implementations. '
             if production else '<p>This staging preview enables the replacement audio implementations for listening tests. ') +
            'It is experimental: complete original-game accuracy, audio fidelity, all content, '
            'mobile support and full performance are not claimed.</p>',
        '<h2>What the public artifact contains</h2><p>':
            ('<h2>What the public artifact contains</h2><p>' if production else
             '<h2>What this preview contains</h2><p>') + 'The package contains the compiled player, '
            'replacement resampler and coefficient generator, browser audio transport and generated '
            'renderer pipeline data. The selected disc and its extracted audio stay in the browser; '
            'no game image, extracted archive or recording is hosted.</p>',
        '<li><strong>Dolphin-derived audio:</strong>':
            '<li><strong>Replacement audio:</strong> this player uses newly authored implementations '
            'that preserve the previous port behavior. The coefficient table retains numerical parameters '
            'and 20 compatibility values from Dolphin revision '
            '<code>a2efdf1197be8132674b90fe9cf4761df39752ed</code>. Data provenance and licensing review '
            'remain open; this is not a formal clean-room or licensing-clearance claim. The '
            '<a href="/licenses/dolphin-gpl-2.0-or-later.txt">historical GPL notice</a> is retained.</li>',
        '<p>Runtime license and attribution texts':
            '<p>Dependency notices are available in the '
            '<a href="/licenses/runtime-third-party.txt">runtime third-party notices</a>. '
            'Recovered source, retained coefficient data and distribution rights remain review items.</p>',
    }
    for start, replacement in paragraphs.items():
        end = '</li>' if start.startswith('<li>') else '</p>'
        pattern = re.escape(start) + r'.*?' + re.escape(end)
        text, count = re.subn(pattern, lambda _: replacement, text, count=0, flags=re.S)
        require(count == 1, 'Audio preview notice boundary changed')
    return text


def license_notice(text, *, production=False):
    start = text.index('Public alpha profile\n')
    end = text.index('The aggregate delivers license', start)
    text = text[:start] + (
        ('Audio-enabled public alpha\n--------------------------\n' if production else
         'Audio staging preview\n---------------------\n') +
        'This player enables the replacement audio implementations and browser transport.\n'
        'The coefficient table retains Dolphin-derived numerical parameters and 20\n'
        'compatibility values. Provenance/licensing review remains open. This record\n'
        'makes no hardware accuracy, formal clean-room or licensing-clearance claim.\n\n') + text[end:]
    text = text.replace('Release `gameplay_public` target', 'Release `gameplay_audio_preview` target')
    start = text.index('EXCLUDED FROM PUBLIC ALPHA: Dolphin-derived audio adaptation')
    end = text.index('==============================================================================', start)
    text = text[:start] + (
        'HISTORICAL AUDIO IMPLEMENTATION AND RETAINED NUMERICAL DATA\n'
        'Dolphin revision a2efdf1197be8132674b90fe9cf4761df39752ed\n'
        'https://github.com/dolphin-emu/dolphin/tree/a2efdf1197be8132674b90fe9cf4761df39752ed\n'
        'The previous GPL-derived implementations were replaced. Numerical table\n'
        'parameters and compatibility values remain. No complete licensing-clearance\n'
        'claim is made. Historical source and artifacts retain their obligations.\n'
        'The GPL text is supplied at licenses/dolphin-gpl-2.0-or-later.txt.\n\n') + text[end:]
    return text


def expected_files(*, production=False):
    mode = 'production' if production else 'preview'
    files, identity_hash = runtime()
    files.update({name: read(ROOT / 'web' / name) for name in MODULES})
    shell = read(ROOT / 'web/player/player-shell.mjs').decode()
    files['player/player-shell.mjs'] = replace_once(
        shell, "from '../melee-runtime.mjs'", "from '../audio-preview-runtime.mjs'").encode()
    files['player/player.css'] = read(ROOT / 'web/player/player.css')
    if production:
        public._validate_runtime_graph(files, audio=True)
    for name, data in files.items():
        require(len(data) <= public.MAX_FILE_BYTES, f'Asset exceeds Pages limit: {name}')
        require(not any(marker in data for marker in (
            b'/Users/', b'/private/var/', b'CPU_ADDRESS_AUDIT', b'melee-web-native-cpu-address-audit',
            b'__melee_evidence', b'sendBeacon(', b'runtime-diagnostics.mjs', b'runtime-cache.js',
        )), f'Private or diagnostic content in preview: {name}')
    group = public._runtime_graph_hash(files)
    output = {f'runtime/{group}/{name}': data for name, data in files.items()}
    css = read(ROOT / 'web/public/site.css')
    css_path = f'assets/site.{digest(css)[:16]}.css'
    output[css_path] = css
    for name in public.HTML_INPUTS:
        text = read(ROOT / ('web/player' if name == 'index.html' else 'web/public') / name).decode()
        if name == 'index.html':
            text = player_html(text)
        elif name == 'notices.html':
            text = notices(text, production=production)
        output[name] = public._replace_html(
            text.encode(), OPERATOR, CONTACT,
            f'/runtime/{group}/player/player.css' if name == 'index.html' else '/' + css_path,
            f'/runtime/{group}/player/player-shell.mjs', mode)
    output['licenses/runtime-third-party.txt'] = license_notice(
        read(ROOT / public.LEGAL_NOTICE_SOURCE).decode(), production=production).encode()
    output['licenses/dolphin-gpl-2.0-or-later.txt'] = read(ROOT / 'docs/licenses/dolphin-gpl-2.0-or-later.txt')
    output['_headers'] = public._headers(mode, False, 'player').encode()
    output['_redirects'] = (public._redirects(mode).encode() if production else
                            b'# Audio listening preview only. No host redirects.\n')
    output['robots.txt'] = public._robots(mode, False).encode()
    require(all(len(data) <= public.MAX_FILE_BYTES for data in output.values()), 'Audio package asset exceeds Pages limit')
    require(sum(map(len, output.values())) <= public.RUNTIME_MAX_TOTAL_BYTES, 'Preview exceeds size limit')
    return output, {'source_sha': source_commit(), 'runtime_hash': group, 'identity_sha256': identity_hash}


def inventory(files):
    return [{'path': name, 'size': len(data), 'sha256': digest(data)} for name, data in sorted(files.items())]


def read_manifest(path):
    require(path.is_file() and not path.is_symlink(), 'Manifest must be a regular file')
    record = json.loads(path.read_bytes(), object_pairs_hook=public._unique_object)
    require(isinstance(record, dict), 'Manifest must be an object')
    return record


def package_record(files, meta, *, production=False):
    return {'schema': PRODUCTION_SCHEMA if production else SCHEMA,
            'project': 'webmelee' if production else 'webmelee-staging',
            'profile': 'audio-player' if production else 'audio-preview',
            **meta, 'files': inventory(files)}


def audit(output, manifest, *, production=False):
    require(output.is_dir() and not output.is_symlink(), 'Missing preview directory')
    expected, meta = expected_files(production=production)
    actual = {}
    for path in output.rglob('*'):
        require(not path.is_symlink(), 'Symlink in preview')
        require(path.is_file() or path.is_dir(), 'Non-file entry in audio package')
        if path.is_file():
            actual[path.relative_to(output).as_posix()] = path.read_bytes()
    require(actual == expected, 'Preview bytes or inventory differ from reviewed sources')
    record = read_manifest(manifest)
    require(record == package_record(expected, meta, production=production), 'Audio manifest mismatch')
    return record


def prepare(output, manifest, *, production=False):
    require(not output.exists() and not output.is_symlink() and not manifest.exists() and
            not manifest.is_symlink(), 'Use fresh output paths')
    require(not manifest.resolve().is_relative_to(output.resolve()), 'Manifest must remain outside upload')
    files, meta = expected_files(production=production)
    output.mkdir(parents=True)
    for name, data in files.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    manifest.write_text(json.dumps(package_record(files, meta, production=production),
                                   indent=2, sort_keys=True) + '\n')
    return audit(output, manifest, production=production)


def verify(origin, manifest, *, production=False):
    loopback = bool(re.fullmatch(r'http://(?:127\.0\.0\.1|localhost):\d+', origin or ''))
    allowed = (origin == 'https://webmelee.gg' or
               re.fullmatch(r'https://[0-9a-f]{8}\.webmelee\.pages\.dev', origin or '') or
               origin == 'https://webmelee-staging.pages.dev' or
               re.fullmatch(r'https://[0-9a-f]{8}\.webmelee-staging\.pages\.dev', origin or '')) if production else \
              re.fullmatch(r'https://[a-z0-9-]+\.webmelee-staging\.pages\.dev', origin or '')
    require(allowed or loopback, 'Audio verification is restricted to the selected release origins or loopback')
    record = read_manifest(manifest)
    require(record.get('schema') == (PRODUCTION_SCHEMA if production else SCHEMA) and
            record.get('project') == ('webmelee' if production else 'webmelee-staging') and
            record.get('profile') == ('audio-player' if production else 'audio-preview'), 'Wrong audio manifest')
    group = record.get('runtime_hash', '')
    require(re.fullmatch(r'[0-9a-f]{16}', group or '') and
            re.fullmatch(r'[0-9a-f]{40}', record.get('source_sha', '') or '') and
            re.fullmatch(r'[0-9a-f]{64}', record.get('identity_sha256', '') or ''), 'Invalid audio manifest identity')
    records = record.get('files', [])
    require(isinstance(records, list) and all(isinstance(item, dict) for item in records), 'Invalid audio inventory')
    paths = [item.get('path') for item in records]
    require(all(isinstance(name, str) for name in paths) and len(paths) == len(set(paths)), 'Invalid audio inventory paths')
    expected_paths = {f'runtime/{group}/{name}' for name in (*MODULES, *NATIVE, 'player/player-shell.mjs', 'player/player.css')}
    expected_paths.update((*public.HTML_INPUTS, '_headers', '_redirects', 'robots.txt',
                           'licenses/runtime-third-party.txt', 'licenses/dolphin-gpl-2.0-or-later.txt'))
    css_paths = [name for name in paths if re.fullmatch(r'assets/site\.[0-9a-f]{16}\.css', name)]
    require(len(css_paths) == 1 and set(paths) == expected_paths | set(css_paths), 'Unauthorized audio inventory')
    require(all(type(item.get('size')) is int and 0 <= item['size'] <= public.MAX_FILE_BYTES and
                re.fullmatch(r'[0-9a-f]{64}', item.get('sha256', '') or '') for item in records), 'Invalid audio inventory hashes or sizes')
    require(sum(item['size'] for item in records) <= public.RUNTIME_MAX_TOTAL_BYTES, 'Audio inventory exceeds size limit')
    checked = []
    for item in records:
        if item['path'] in ('_headers', '_redirects'):
            continue
        http.check_resource(origin, '/' + item['path'], item, True, 'player')
        checked.append(item['path'])
    by_path = {item['path']: item for item in records}
    aliases = [http.check_resource(origin, route, by_path[name], True, 'player')
               for route, name in (('/', 'index.html'), ('/terms', 'terms.html'), ('/privacy', 'privacy.html'),
                                   ('/copyright', 'copyright.html'), ('/notices', 'notices.html'))]
    missing, limitations = [], []
    maps = tuple('/' + name + '.map' for name in paths if name.endswith(('.wasm', '.js', '.mjs')))
    for path in (*http.BLOCKED_PATHS, '/manifest.json', '/runtime-audio-preview-identity.json', '/src/',
                 f'/runtime/{group}/dsp_coef.bin', f'/runtime/{group}/runtime-diagnostics.mjs', *maps):
        status, headers, body, destination = http.get(origin + path)
        http.check_destination(origin, destination, {path})
        if loopback and path in ('/_headers', '/_redirects') and status == 502 and b'ENOTDIR' in body:
            limitations.append({'path': path, 'status': status, 'reason': 'Local Wrangler reserved-config routing error; hosted verification requires 404'})
            continue
        require(status == 404, f'Unexpected exposed route: {path}')
        require('noindex' in headers.get('X-Robots-Tag', ''), 'Missing preview noindex on 404')
        missing.append({'path': path, 'status': status})
    return {'result': 'pass', 'origin': origin, 'source_sha': record['source_sha'],
            'manifest_sha256': digest(manifest.read_bytes()), 'resources': checked,
            'aliases': aliases, 'missing': missing, 'local_limitations': limitations,
            'scope': 'Hosted bytes, headers and missing routes; browser audio checked separately.'}


def main(*, production=False):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'audit', 'verify'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--url')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if args.action == 'verify':
        result = verify(args.url, args.manifest, production=production)
    else:
        require(args.output is not None, '--output is required')
        result = (prepare if args.action == 'prepare' else audit)(args.output, args.manifest, production=production)
    if args.report:
        with args.report.open('x') as stream:
            json.dump(result, stream, indent=2, sort_keys=True)
            stream.write('\n')
    print(json.dumps({key: value for key, value in result.items() if key != 'files'}, sort_keys=True))


if __name__ == '__main__':
    main()
