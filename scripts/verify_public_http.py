#!/usr/bin/env python3
"""Verify the exact public resources and Cloudflare Pages serving behavior."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.error
import urllib.parse
import urllib.request

BLOCKED_PATHS = (
    '/runtime.html', '/prototype.html', '/viewer.html', '/native-menu.html',
    '/hitch-capture.mjs', '/tests/', '/docs/', '/work/', '/build/',
    '/.git/config', '/_worker.js', '/assets/', '/unrecognized-test-route',
    '/_headers', '/_redirects', '/disc-image.mjs', '/runtime-cache.js',
    '/gameplay_menu_browser.wasm', '/__melee_evidence/replay.json',
)


def get(url):
    try:
        with urllib.request.urlopen(url, timeout=30) as response:
            return response.status, response.headers, response.read(), response.url
    except urllib.error.HTTPError as error:
        try:
            body = error.read()
        finally:
            error.close()
        return error.code, error.headers, body, error.url


def require(condition, message):
    """Raise a real verification error even when Python runs with -O."""
    if not condition:
        raise ValueError(message)


def check_destination(origin, final_url, expected_paths):
    expected, actual = urllib.parse.urlsplit(origin), urllib.parse.urlsplit(final_url)
    if (actual.scheme, actual.netloc) != (expected.scheme, expected.netloc):
        raise ValueError('redirect escaped the candidate origin')
    if actual.path not in expected_paths or actual.query or actual.fragment:
        raise ValueError(f'unexpected canonical destination: {actual.path}')


def check_resource(origin, route, record, require_noindex):
    status, headers, body, canonical = get(origin + route)
    name = record['path']
    canonical_path = '/' if name == 'index.html' else '/' + name.removesuffix('.html')
    check_destination(origin, canonical, {route, canonical_path})
    if status != 200 or len(body) != record['size'] or hashlib.sha256(body).hexdigest() != record['sha256']:
        raise ValueError(f'resource byte identity/status failed: {route}, status {status}')
    if name.endswith('.css'):
        require(headers.get_content_type() == 'text/css', f'{route}: expected Content-Type text/css')
        require('immutable' in headers.get('Cache-Control', ''), f'{route}: expected immutable Cache-Control')
    if name.endswith('.js'):
        require(headers.get_content_type() in ('application/javascript', 'text/javascript'),
                f'{route}: expected JavaScript Content-Type')
        require('immutable' in headers.get('Cache-Control', ''), f'{route}: expected immutable Cache-Control')
    if name.endswith('.html'):
        require(headers.get_content_type() == 'text/html', f'{route}: expected Content-Type text/html')
    require("connect-src 'none'" in headers.get('Content-Security-Policy', ''),
            f'{route}: missing CSP connect-src none')
    require(headers.get('X-Content-Type-Options') == 'nosniff',
            f'{route}: missing X-Content-Type-Options nosniff')
    require(headers.get('Referrer-Policy') == 'no-referrer',
            f'{route}: missing Referrer-Policy no-referrer')
    require(headers.get('X-Frame-Options') == 'DENY', f'{route}: missing X-Frame-Options DENY')
    require('fullscreen=(self)' in headers.get('Permissions-Policy', ''),
            f'{route}: missing Permissions-Policy fullscreen self')
    if require_noindex:
        require('noindex' in headers.get('X-Robots-Tag', ''), f'{route}: missing X-Robots-Tag noindex')
    return {'path': route, 'status': status, 'bytes': len(body), 'sha256': record['sha256'],
            'canonical_path': urllib.parse.urlsplit(canonical).path}


def verify(url, manifest):
    origin = urllib.parse.urlsplit(url)
    if origin.scheme not in ('http', 'https') or origin.path not in ('', '/') or origin.query or origin.fragment:
        raise ValueError('URL must be a plain HTTP(S) origin')
    loopback = origin.hostname in ('localhost', '127.0.0.1', '::1')
    origin = urllib.parse.urlunsplit((origin.scheme, origin.netloc, '', '', ''))
    records = json.loads(Path(manifest).read_text())
    result = {'origin': origin, 'resources': [], 'missing': [], 'result': 'pass'}
    require_noindex = not records['index_production'] or urllib.parse.urlsplit(origin).hostname.endswith('.pages.dev')
    for record in records['files']:
        name = record['path']
        if name.startswith('_'):
            continue  # Pages consumes configuration, verified by its effects below.
        result['resources'].append(check_resource(origin, '/' + name, record, require_noindex))
    by_path = {record['path']: record for record in records['files']}
    # The actual user links must match the same complete HTML as the file URLs.
    result['aliases'] = [check_resource(origin, route, by_path[name], require_noindex)
                         for route, name in (('/', 'index.html'), ('/terms', 'terms.html'),
                                             ('/privacy', 'privacy.html'), ('/copyright', 'copyright.html'),
                                             ('/notices', 'notices.html'))]
    for route in BLOCKED_PATHS:
        status, _, body, final_url = get(origin + route)
        check_destination(origin, final_url, {route})
        if loopback and route in ('/_headers', '/_redirects') and status == 502 and b'ENOTDIR' in body:
            result.setdefault('local_limitations', []).append({
                'path': route, 'status': status,
                'reason': 'Wrangler 4.131.1 reserved-config routing error; hosted verification still requires 404',
            })
            continue
        if status != 404:
            raise ValueError(f'forbidden/unknown route must return 404: {route}, got {status}')
        result['missing'].append({'path': route, 'status': status})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.url, args.manifest)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(f"HTTP verification passed: {len(result['resources'])} exact resources, {len(result['missing'])} missing routes")


if __name__ == '__main__':
    main()
