#!/usr/bin/env python3
"""Prepare, audit and HTTP-verify a separate audio-enabled staging preview.

This schema is deliberately not accepted by the silent production packager or
staging deployment wrapper. Only an explicit file inventory is uploaded; the
producer identity and manifest remain outside the upload directory.
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


def notices(text):
    # Preserve the surrounding contact/rights and dependency notices, while
    # replacing paragraphs that describe a different, deliberately silent build.
    paragraphs = {
        '<p>When the player is available, the current prototype supports':
            '<p>This staging preview enables the replacement audio implementations for listening tests. '
            'It is experimental: complete original-game accuracy, audio fidelity, all content, '
            'mobile support and full performance are not claimed.</p>',
        '<h2>What the public artifact contains</h2><p>':
            '<h2>What this preview contains</h2><p>The package contains the compiled player, '
            'replacement resampler and coefficient generator, browser audio transport and generated '
            'renderer pipeline data. The selected disc and its extracted audio stay in the browser; '
            'no game image, extracted archive or recording is hosted.</p>',
        '<li><strong>Dolphin-derived audio:</strong>':
            '<li><strong>Replacement audio:</strong> this preview uses newly authored implementations '
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


def license_notice(text):
    start = text.index('Public alpha profile\n')
    end = text.index('The aggregate delivers license', start)
    text = text[:start] + (
        'Audio staging preview\n---------------------\n'
        'This preview enables the replacement audio implementations and browser transport.\n'
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


def expected_files():
    files, identity_hash = runtime()
    files.update({name: read(ROOT / 'web' / name) for name in MODULES})
    shell = read(ROOT / 'web/player/player-shell.mjs').decode()
    files['player/player-shell.mjs'] = replace_once(
        shell, "from '../melee-runtime.mjs'", "from '../audio-preview-runtime.mjs'").encode()
    files['player/player.css'] = read(ROOT / 'web/player/player.css')
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
            text = notices(text)
        output[name] = public._replace_html(
            text.encode(), OPERATOR, CONTACT,
            f'/runtime/{group}/player/player.css' if name == 'index.html' else '/' + css_path,
            f'/runtime/{group}/player/player-shell.mjs', 'preview')
    output['licenses/runtime-third-party.txt'] = license_notice(read(ROOT / public.LEGAL_NOTICE_SOURCE).decode()).encode()
    output['licenses/dolphin-gpl-2.0-or-later.txt'] = read(ROOT / 'docs/licenses/dolphin-gpl-2.0-or-later.txt')
    output['_headers'] = public._headers('preview', False, 'player').encode()
    output['_redirects'] = b'# Audio listening preview only. No host redirects.\n'
    output['robots.txt'] = public._robots('preview', False).encode()
    require(sum(map(len, output.values())) <= public.RUNTIME_MAX_TOTAL_BYTES, 'Preview exceeds size limit')
    return output, {'source_sha': source_commit(), 'runtime_hash': group, 'identity_sha256': identity_hash}


def inventory(files):
    return [{'path': name, 'size': len(data), 'sha256': digest(data)} for name, data in sorted(files.items())]


def audit(output, manifest):
    require(output.is_dir() and not output.is_symlink(), 'Missing preview directory')
    expected, meta = expected_files()
    actual = {}
    for path in output.rglob('*'):
        require(not path.is_symlink(), 'Symlink in preview')
        if path.is_file():
            actual[path.relative_to(output).as_posix()] = path.read_bytes()
    require(actual == expected, 'Preview bytes or inventory differ from reviewed sources')
    record = json.loads(manifest.read_bytes())
    require(record == {'schema': SCHEMA, 'project': 'webmelee-staging', 'profile': 'audio-preview',
                       **meta, 'files': inventory(expected)}, 'Preview manifest mismatch')
    return record


def prepare(output, manifest):
    require(not output.exists() and not output.is_symlink() and not manifest.exists(), 'Use fresh output paths')
    require(not manifest.resolve().is_relative_to(output.resolve()), 'Manifest must remain outside upload')
    files, meta = expected_files()
    output.mkdir(parents=True)
    for name, data in files.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    manifest.write_text(json.dumps({'schema': SCHEMA, 'project': 'webmelee-staging', 'profile': 'audio-preview',
                                   **meta, 'files': inventory(files)}, indent=2, sort_keys=True) + '\n')
    return audit(output, manifest)


def verify(origin, manifest):
    require(re.fullmatch(r'https://[a-z0-9-]+\.webmelee-staging\.pages\.dev', origin) or
            re.fullmatch(r'http://(?:127\.0\.0\.1|localhost):\d+', origin),
            'Audio verification is restricted to staging previews or loopback')
    record = json.loads(manifest.read_bytes())
    require(record.get('schema') == SCHEMA and record.get('project') == 'webmelee-staging', 'Wrong preview manifest')
    checked = []
    for item in record['files']:
        if item['path'] in ('_headers', '_redirects'):
            continue
        http.check_resource(origin, '/' + item['path'], item, True, 'player')
        checked.append(item['path'])
    for path in (*http.BLOCKED_PATHS, '/manifest.json', '/runtime-audio-preview-identity.json', '/src/'):
        status, headers, _, destination = http.get(origin + path)
        http.check_destination(origin, destination, {path})
        require(status == 404, f'Unexpected exposed route: {path}')
        require('noindex' in headers.get('X-Robots-Tag', ''), 'Missing preview noindex on 404')
    return {'result': 'pass', 'origin': origin, 'source_sha': record['source_sha'], 'resources': checked,
            'scope': 'Hosted bytes, headers and missing routes; browser audio checked separately.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'audit', 'verify'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--url')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if args.action == 'verify':
        result = verify(args.url, args.manifest)
    else:
        require(args.output is not None, '--output is required')
        result = (prepare if args.action == 'prepare' else audit)(args.output, args.manifest)
    if args.report:
        with args.report.open('x') as stream:
            json.dump(result, stream, indent=2, sort_keys=True)
            stream.write('\n')
    print(json.dumps({key: value for key, value in result.items() if key != 'files'}, sort_keys=True))


if __name__ == '__main__':
    main()
