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
        return error.code, error.headers, error.read(), error.url


def verify(url, manifest):
    origin = urllib.parse.urlsplit(url)
    if origin.scheme not in ('http', 'https') or origin.path not in ('', '/') or origin.query or origin.fragment:
        raise ValueError('URL must be a plain HTTP(S) origin')
    loopback = origin.hostname in ('localhost', '127.0.0.1', '::1')
    origin = urllib.parse.urlunsplit((origin.scheme, origin.netloc, '', '', ''))
    records = json.loads(Path(manifest).read_text())
    result = {'origin': origin, 'resources': [], 'missing': [], 'result': 'pass'}
    for record in records['files']:
        name = record['path']
        if name.startswith('_'):
            continue  # Pages consumes configuration, verified by its effects below.
        status, headers, body, canonical = get(origin + '/' + name)
        if status != 200 or len(body) != record['size'] or hashlib.sha256(body).hexdigest() != record['sha256']:
            raise ValueError(f'resource byte identity/status failed: {name}, status {status}')
        if name.endswith('.css'):
            assert headers.get_content_type() == 'text/css', name
            assert 'immutable' in headers.get('Cache-Control', ''), name
        if name.endswith('.js'):
            assert headers.get_content_type() in ('application/javascript', 'text/javascript'), name
            assert 'immutable' in headers.get('Cache-Control', ''), name
        if name.endswith('.html'):
            assert headers.get_content_type() == 'text/html', name
        assert "connect-src 'none'" in headers.get('Content-Security-Policy', ''), name
        assert headers.get('X-Content-Type-Options') == 'nosniff', name
        assert headers.get('Referrer-Policy') == 'no-referrer', name
        assert headers.get('X-Frame-Options') == 'DENY', name
        assert 'fullscreen=(self)' in headers.get('Permissions-Policy', ''), name
        if not records['index_production'] or urllib.parse.urlsplit(origin).hostname.endswith('.pages.dev'):
            assert 'noindex' in headers.get('X-Robots-Tag', ''), name
        result['resources'].append({'path': name, 'status': status, 'bytes': len(body), 'sha256': record['sha256'], 'canonical_path': urllib.parse.urlsplit(canonical).path})
    status, headers, body, _ = get(origin + '/')
    assert status == 200 and b'Gameplay is not available' in body
    assert b'<iframe' not in body and b'type="file"' not in body
    for route in BLOCKED_PATHS:
        status, _, body, _ = get(origin + route)
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
