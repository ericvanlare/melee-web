#!/usr/bin/env python3
"""Deploy or roll back an audited candidate in the isolated staging Pages project."""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import urllib.error
from urllib.parse import urlsplit

PROJECT = 'webmelee-staging'
BRANCH = 'staging'
STABLE = 'https://staging.webmelee.gg'
PAGES = 'https://webmelee-staging.pages.dev'
ROOT = Path(__file__).resolve().parents[1]
EXTRA_BLOCKED = (
    '/gameplay_public.wasm', '/gameplay_public.js', '/gameplay_public.data',
    '/gameplay_public.wasm.map', '/index.html.map', '/manifest.json',
    '/deployment.json', '/receipt.json', '/runtime-public-identity.json',
    '/staging.manifest.json', '/staging.receipt.json', '/wrangler.jsonc',
    '/.env', '/.dev.vars', '/src/', '/scripts/', '/patches/', '/.deps/',
    '/native/', '/melee-runtime.mjs', '/runtime-diagnostics.mjs',
    '/gameplay_audio_resample.c', '/cpu-address-audit.json', '/functions/',
    '/functions/api/', '/api/diagnostics.js', '/api/worker.mjs',
    '/api/schema.mjs', '/api/diagnostics/[[report]].js',
)
DIAGNOSTICS_ENV_VARS = frozenset({
    'DIAGNOSTICS_RATE_LIMIT', 'DIAGNOSTICS_DAILY_REPORT_CAP',
    'DIAGNOSTICS_DAILY_BYTE_CAP', 'DIAGNOSTICS_ALLOWED_RELEASES',
})
DIAGNOSTICS_ADMIN_SECRET = 'DIAGNOSTICS_ADMIN_TOKEN'
DIAGNOSTICS_ADMIN_SECRET_MIN_LENGTH = 16
DIAGNOSTICS_ENV_KEYS = DIAGNOSTICS_ENV_VARS | {DIAGNOSTICS_ADMIN_SECRET}
DIAGNOSTICS_BINDING = 'DIAGNOSTICS_DB'
DIAGNOSTICS_FUNCTION_ROUTES = ('/api/diagnostics', '/api/diagnostics/*')
D1_DATABASE_ID_RE = re.compile(
    r'(?:[0-9a-f]{32}|[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})'
)
WRANGLER_VERSION_RE = re.compile(r'(?<![0-9])([0-9]+\.[0-9]+\.[0-9]+)(?![0-9])')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def _plain_env_vars(value, *, require_admin_secret=False):
    require(isinstance(value, dict), 'Staging environment variables must be an object')
    result = {}
    admin_secret_present = False
    for key, item in value.items():
        require(isinstance(key, str) and key in DIAGNOSTICS_ENV_KEYS,
                'Staging has an unallowlisted environment variable')
        if key == DIAGNOSTICS_ADMIN_SECRET:
            # Pages project metadata may expose an encrypted value or only its
            # type.  Validate the metadata shape, but never return or copy the
            # secret value into a generated Wrangler config or receipt.
            require(isinstance(item, dict) and
                    set(item) in ({'type'}, {'type', 'value'}) and
                    item.get('type') == 'secret_text',
                    'Diagnostics admin token must be the configured secret_text')
            if 'value' in item:
                require(isinstance(item['value'], str) and
                        DIAGNOSTICS_ADMIN_SECRET_MIN_LENGTH <= len(item['value']) <= 64 * 1024,
                        'Diagnostics admin token metadata must be at least 16 characters')
            admin_secret_present = True
            continue
        if isinstance(item, dict):
            require(set(item) == {'type', 'value'} and item.get('type') == 'plain_text',
                    'Diagnostics environment variables must be non-secret plain text')
            item = item.get('value')
        require(isinstance(item, str) and len(item) <= 64 * 1024,
                'Diagnostics environment variable value is invalid')
        result[key] = item
    require(not require_admin_secret or admin_secret_present,
            'Staging project is missing the diagnostics admin secret')
    return result


def _d1_databases(value):
    if isinstance(value, dict):
        entries = []
        for binding, item in value.items():
            require(isinstance(item, dict), 'Diagnostics D1 binding must be an object')
            entry = dict(item)
            entry['binding'] = binding
            if 'id' in entry and 'database_id' not in entry:
                entry['database_id'] = entry.pop('id')
            if 'name' in entry and 'database_name' not in entry:
                entry['database_name'] = entry.pop('name')
            entries.append(entry)
        return entries
    if isinstance(value, list):
        entries = []
        for item in value:
            require(isinstance(item, dict), 'Diagnostics D1 binding must be an object')
            entry = dict(item)
            if 'id' in entry and 'database_id' not in entry:
                entry['database_id'] = entry.pop('id')
            if 'name' in entry and 'database_name' not in entry:
                entry['database_name'] = entry.pop('name')
            entries.append(entry)
        return entries
    return []


def _pinned_wrangler_version():
    """Read the repository's exact Wrangler pin; never infer it from a path."""
    try:
        lock = json.loads((ROOT / 'dependencies.lock.json').read_text())
        pin = lock['deployment_tools']['wrangler']
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as exc:
        raise ValueError('Wrangler deployment tool pin is missing from dependencies.lock.json') from exc
    require(isinstance(pin, dict) and set(pin) == {'package', 'version'} and
            pin.get('package') == 'wrangler' and isinstance(pin.get('version'), str) and
            re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', pin['version']),
            'Wrangler deployment tool pin is invalid')
    return pin['version']


def _validate_wrangler_version(wrangler):
    expected = _pinned_wrangler_version()
    result = subprocess.run([str(wrangler), '--version'], cwd=ROOT,
                            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, check=False)
    require(result.returncode == 0, 'Pinned Wrangler version could not be read')
    match = WRANGLER_VERSION_RE.search(result.stdout.strip())
    require(match and match.group(1) == expected,
            'Wrangler version does not match dependencies.lock.json')
    return expected


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def immutable_origin(value):
    parsed = urlsplit(value)
    require(parsed.scheme == 'https' and parsed.path in ('', '/') and
            not parsed.query and not parsed.fragment and
            re.fullmatch(r'[0-9a-f]{8}\.webmelee-staging\.pages\.dev', parsed.netloc),
            'Expected an immutable deployment in the isolated staging project')
    return 'https://' + parsed.netloc


def validate_project(project):
    require(project.get('name') == PROJECT and project.get('production_branch') == BRANCH and
            project.get('subdomain') == urlsplit(PAGES).netloc,
            'Staging project identity or branch changed; refusing deployment')
    require(not project.get('source') and project.get('uses_functions') is True,
            'Staging must be the file-based Pages Functions direct-upload project')
    require(set(project.get('domains', [])) <= {urlsplit(PAGES).netloc, urlsplit(STABLE).netloc},
            'Unexpected custom domain on staging project')
    config = project.get('build_config') or {}
    require(not config.get('web_analytics_tag') and not config.get('web_analytics_token'),
            'Staging analytics must be disabled')
    configurations = project.get('deployment_configs') or {}
    require(set(configurations) == {'preview', 'production'}, 'Missing staging environment configuration')
    for config in configurations.values():
        require(set(config) <= {'env_vars', 'd1_databases'},
                'Staging has an unallowlisted Functions binding or configuration')
        _plain_env_vars(config.get('env_vars') or {}, require_admin_secret=True)
        databases = _d1_databases(config.get('d1_databases') or [])
        require(isinstance(databases, list) and len(databases) == 1 and
                isinstance(databases[0], dict) and databases[0].get('binding') == DIAGNOSTICS_BINDING,
                'Staging must expose exactly the diagnostics D1 binding')
        require(set(databases[0]) == {'binding', 'database_id', 'database_name'},
                'Diagnostics D1 binding contains unallowlisted configuration')
        require(isinstance(databases[0]['database_id'], str)
                and D1_DATABASE_ID_RE.fullmatch(databases[0]['database_id']),
                'Diagnostics D1 database ID is invalid')
        require(isinstance(databases[0]['database_name'], str) and
                re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]{0,62}', databases[0]['database_name']),
                'Diagnostics D1 database name is invalid')


def validate_deployment(deployment, sha):
    url = immutable_origin(deployment.get('url', ''))
    metadata = (deployment.get('deployment_trigger') or {}).get('metadata') or {}
    require(deployment.get('project_name') == PROJECT and deployment.get('environment') == 'production'
            and metadata.get('branch') == BRANCH and metadata.get('commit_hash') == sha,
            'Deployment does not belong to the requested staging SHA/branch')
    require((deployment.get('latest_stage') or {}).get('status') == 'success',
            'Staging deployment has not succeeded')
    require(re.fullmatch(r'[0-9a-f-]{36}', deployment.get('id', '')),
            'Invalid staging deployment ID')
    return {'deployment_id': deployment['id'], 'immutable_url': url, 'source_sha': sha,
            'project': PROJECT, 'branch': BRANCH, 'environment': 'staging',
            'pages_environment': 'production', 'stable_url': PAGES, 'custom_domain_url': STABLE}


class Pages:
    """No arbitrary routes: this client cannot mutate another Pages project or DNS."""
    def __init__(self, account, wrangler):
        require(re.fullmatch(r'[0-9a-f]{32}', account), 'Expected Cloudflare account ID')
        self.account = account
        self.wrangler = str(Path(wrangler).resolve())
        self.env = {**os.environ, 'WRANGLER_SEND_METRICS': 'false', 'CLOUDFLARE_ACCOUNT_ID': account}
        auth = json.loads(subprocess.check_output([self.wrangler, 'auth', 'token', '--json'],
                                                  env=self.env, text=True))
        self.token = auth['token']  # Only retained in memory; never include it in a receipt.

    def request(self, suffix='', *, rollback=False):
        require(suffix in ('', '/deployments') or re.fullmatch(r'/deployments/[0-9a-f-]{36}', suffix),
                'Unsupported staging API route')
        require(not rollback or re.fullmatch(r'/deployments/[0-9a-f-]{36}', suffix),
                'Rollback requires an exact staging deployment ID')
        url = f'https://api.cloudflare.com/client/v4/accounts/{self.account}/pages/projects/{PROJECT}{suffix}'
        if rollback:
            url += '/rollback'
        request = urllib.request.Request(url, data=b'{}' if rollback else None,
            headers={'Authorization': 'Bearer ' + self.token, 'Content-Type': 'application/json'},
            method='POST' if rollback else 'GET')
        with urllib.request.urlopen(request, timeout=40) as response:
            body = json.load(response)
        require(body.get('success'), 'Cloudflare staging API request failed')
        return body['result']


def verify_http(source_root, origin, manifest):
    require(origin in (STABLE, PAGES) or immutable_origin(origin), 'Invalid staging origin')
    verifier = load_module(source_root / 'scripts/verify_public_http.py', '_staging_http')
    report = verifier.verify(origin, manifest)
    records = json.loads(manifest.read_text())['files']
    maps = tuple('/' + record['path'] + '.map' for record in records
                 if record['path'].endswith(('.wasm', '.js', '.mjs')))
    report['extra_missing'] = []
    for route in EXTRA_BLOCKED + maps:
        status, _, _, destination = verifier.get(origin + route)
        verifier.check_destination(origin, destination, {route})
        require(status == 404, f'Forbidden staging route {route} returned {status}')
        report['extra_missing'].append({'path': route, 'status': status})
    report['api'] = _verify_api_routes(origin)
    return report


def _api_request(origin, path, method, *, body=None, headers=None):
    data = None if body is None else body.encode('utf-8')
    request = urllib.request.Request(origin + path, data=data, method=method,
                                     headers={'User-Agent': 'WebMelee-Release-Audit/1.0', **(headers or {})})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.status, response.headers, response.read()
    except urllib.error.HTTPError as error:
        try:
            return error.code, error.headers, error.read()
        finally:
            error.close()


def _verify_api_routes(origin):
    probes = []
    report_path = '/api/diagnostics/' + ('0' * 64)
    for method, path, body, headers, expected in (
        ('POST', '/api/diagnostics', '{}', {'Content-Type': 'application/json', 'Origin': origin}, 422),
        ('GET', '/api/diagnostics', None, {}, 401),
        ('DELETE', '/api/diagnostics', None, {}, 401),
        ('GET', report_path, None, {}, 401),
        ('DELETE', report_path, None, {}, 401),
    ):
        status, response_headers, payload = _api_request(origin, path, method, body=body, headers=headers)
        require(status == expected, f'API {method} {path} returned {status}, expected {expected}')
        require(response_headers.get('Cache-Control', '').lower() == 'no-store',
                f'API {method} {path} must be no-store')
        require(response_headers.get('X-Content-Type-Options') == 'nosniff',
                f'API {method} {path} is missing nosniff')
        require(response_headers.get('Referrer-Policy') == 'no-referrer',
                f'API {method} {path} is missing referrer policy')
        require(response_headers.get('Permissions-Policy') == 'camera=(), microphone=(), geolocation=()',
                f'API {method} {path} is missing permissions policy')
        require('immutable' not in response_headers.get('Cache-Control', '').lower(),
                f'API {method} {path} must not be immutable-cached')
        require(len(payload) <= 4096, f'API {method} {path} returned an unbounded error body')
        probes.append({'method': method, 'path': path, 'status': status, 'bytes': len(payload)})
    return probes


def validate_functions_graph(functions_root, wrangler, scratch_parent=None):
    """Compile the exact file-based Functions graph with pinned Wrangler."""
    functions_root = Path(functions_root)
    wrangler = Path(wrangler)
    require(functions_root.is_dir() and not functions_root.is_symlink(),
            'Diagnostics Functions directory is missing')
    try:
        resolved_wrangler = wrangler.resolve(strict=True)
    except OSError as exc:
        raise ValueError('Pinned Wrangler is missing') from exc
    require(resolved_wrangler.is_file(), 'Pinned Wrangler is missing')
    # The checked-in tool path is a normal npm .bin symlink.  Permit that
    # symlink only when it resolves inside the versioned tool directory.
    require(resolved_wrangler.is_relative_to(wrangler.parent.parent.parent.resolve()),
            'Pinned Wrangler resolves outside its versioned tool directory')
    _validate_wrangler_version(wrangler)
    with tempfile.TemporaryDirectory(prefix='webmelee-functions-check-', dir=scratch_parent) as scratch:
        scratch_path = Path(scratch)
        outdir = scratch_path / '.functions-build'
        routes_path = scratch_path / '.functions-routes.json'
        config_path = scratch_path / '.functions-routing-config.json'
        command = [str(wrangler), 'pages', 'functions', 'build', str(functions_root),
                   '--outdir', str(outdir), '--output-routes-path', str(routes_path),
                   '--output-config-path', str(config_path)]
        result = subprocess.run(command, cwd=ROOT, stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, check=False)
        require(result.returncode == 0, 'Pinned Wrangler rejected the diagnostics Functions graph')
        try:
            routes = json.loads(routes_path.read_text())
            config = json.loads(config_path.read_text())
        except (OSError, json.JSONDecodeError) as exc:
            raise ValueError('Wrangler did not produce valid diagnostics route/config output') from exc
        require(routes.get('version') == 1 and routes.get('include') == list(DIAGNOSTICS_FUNCTION_ROUTES)
                and routes.get('exclude') == [], 'Diagnostics _routes graph differs from the API-only allowlist')
        actual = {(tuple(item.get('module', [])), item.get('routePath'))
                  for item in config.get('routes', []) if isinstance(item, dict)}
        require(actual == {
            (('api/diagnostics.js:onRequest',), '/api/diagnostics'),
            (('api/diagnostics/[[report]].js:onRequest',), '/api/diagnostics/:report*'),
        }, 'Wrangler routing config does not map the exact diagnostics adapters')
    # Keep local checkout and temporary directory names out of portable reports.
    return {
        'command': [
            'wrangler', 'pages', 'functions', 'build', '<generated>/functions',
            '--outdir', '<generated>/.functions-build',
            '--output-routes-path', '<generated>/.functions-routes.json',
            '--output-config-path', '<generated>/.functions-routing-config.json',
        ],
        'routes': list(DIAGNOSTICS_FUNCTION_ROUTES),
        'validated': True,
    }


def diagnostics_wrangler_config(project):
    """Return the narrow generated config used for this direct upload.

    The repository template contains placeholders and migration paths for
    local setup. Deployment uses the already-audited Pages project binding and
    vars, with the config kept outside the uploaded static directory.
    """
    production = (project.get('deployment_configs') or {}).get('production') or {}
    # The Pages secret is intentionally validated and omitted.  Wrangler keeps
    # the existing project secret when this generated config is uploaded.
    env_vars = _plain_env_vars(production.get('env_vars') or {}, require_admin_secret=True)
    databases = _d1_databases(production.get('d1_databases') or [])
    require(len(databases) == 1, 'Diagnostics D1 configuration is unavailable')
    database = databases[0]
    return {
        '$schema': 'node_modules/wrangler/config-schema.json',
        'name': PROJECT,
        'compatibility_date': '2026-10-01',
        'pages_build_output_dir': './public',
        'd1_databases': [{
            'binding': DIAGNOSTICS_BINDING,
            'database_name': database['database_name'],
            'database_id': database['database_id'],
        }],
        'vars': dict(env_vars),
    }


def write_json(path, value):
    with path.open('x') as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write('\n')
    path.chmod(0o600)


def tooling_head():
    require(not subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True),
            'Deployment tooling worktree must be clean and committed')
    return subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('deploy', 'verify', 'rollback'))
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--sha', required=True)
    parser.add_argument('--base-output', type=Path, required=True)
    parser.add_argument('--base-manifest', type=Path, required=True)
    parser.add_argument('--base-manifest-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--manifest-sha256', required=True)
    parser.add_argument('--report-dir', type=Path, required=True)
    parser.add_argument('--wrangler', type=Path)
    parser.add_argument('--account-id')
    parser.add_argument('--deployment-id')
    parser.add_argument('--immutable-url')
    parser.add_argument('--previous-record', type=Path)
    parser.add_argument('--verify-custom-domain', action='store_true',
                        help='Also require the optional staging.webmelee.gg hostname to pass')
    args = parser.parse_args()
    head = tooling_head()
    require(re.fullmatch(r'[0-9a-f]{40}', args.sha), 'Supply the explicit full source SHA')
    require(re.fullmatch(r'[0-9a-f]{64}', args.manifest_sha256) and
            hashlib.sha256(args.manifest.read_bytes()).hexdigest() == args.manifest_sha256,
            'Frozen staging manifest identity mismatch')
    args.report_dir = args.report_dir.resolve()
    require(not args.report_dir.is_relative_to(args.output.resolve()) and
            not args.report_dir.is_relative_to(args.base_output.resolve()),
            'Receipts must not enter either frozen artifact')
    args.report_dir.mkdir(parents=True, exist_ok=False, mode=0o700)
    stage = load_module(ROOT / 'scripts/stage_public.py', '_staging_package')
    evidence = stage.audit_staging(args.source_root.resolve(), args.sha, args.base_output.resolve(),
        args.base_manifest.resolve(), args.base_manifest_sha256, args.output.resolve(), args.manifest.resolve())
    write_json(args.report_dir / 'preflight.json', evidence)
    manifest_value = json.loads(args.manifest.read_text())
    backend = manifest_value.get('backend')
    functions_validation = None
    backend_sidecar = None
    if backend is not None:
        require(isinstance(backend, dict) and isinstance(backend.get('directory'), str),
                'Diagnostics backend manifest metadata is invalid')
        backend_sidecar = args.output.parent / backend['directory']
        if not backend_sidecar.is_dir():
            base_candidate = args.base_output.parent / backend['directory']
            if base_candidate.is_dir():
                backend_sidecar = base_candidate
        if args.action == 'deploy':
            require(args.wrangler, 'Deploying diagnostics Functions requires pinned Wrangler')
            functions_validation = validate_functions_graph(backend_sidecar / 'functions', args.wrangler)
        else:
            require(backend_sidecar.is_dir(), 'Diagnostics Functions sidecar is missing')
    record = {'tooling_sha': head, 'source_sha': args.sha, 'manifest_sha256': args.manifest_sha256,
              'project': PROJECT, 'environment': 'staging', 'stable_url': PAGES,
              'custom_domain_url': STABLE, 'action': args.action,
              'candidate': evidence, 'recorded_at': datetime.now(timezone.utc).isoformat()}
    if functions_validation is not None:
        record['functions_validation'] = functions_validation
    if args.action == 'verify':
        require(args.immutable_url, 'Verification requires the immutable staging URL')
        immutable = immutable_origin(args.immutable_url)
    else:
        require(args.wrangler and args.account_id, 'Deployment requires Wrangler and explicit account ID')
        api = Pages(args.account_id, args.wrangler)
        project = api.request()
        validate_project(project)
        previous = project.get('canonical_deployment')
        record['previous_staging_deployment_id'] = previous.get('id') if previous else None
        if previous and args.action == 'deploy':
            require(args.previous_record, 'Preserve the previous staging deployment record before replacing it')
            previous_record = json.loads(args.previous_record.read_text())
            require(previous_record.get('deployment_id') == previous['id'] and
                    previous_record.get('project') == PROJECT and
                    re.fullmatch(r'[0-9a-f]{64}', previous_record.get('manifest_sha256', '')),
                    'Previous staging record does not match the current rollback target')
            write_json(args.report_dir / 'previous-staging-deployment.json', previous_record)
        write_json(args.report_dir / 'operation-started.json', record)
        if args.action == 'rollback':
            require(args.deployment_id, 'Rollback requires an exact existing staging deployment ID')
            target = api.request('/deployments/' + args.deployment_id)
            record.update(validate_deployment(target, args.sha))
            immutable = record['immutable_url']
            write_json(args.report_dir / 'rollback-target-http.json',
                       verify_http(args.source_root, immutable, args.manifest))
            api.request('/deployments/' + args.deployment_id, rollback=True)
        else:
            with tempfile.TemporaryDirectory(prefix='webmelee-staging-upload-') as directory:
                upload = Path(directory) / 'public'
                shutil.copytree(args.output, upload)
                stage.audit_staging(args.source_root.resolve(), args.sha, args.base_output.resolve(),
                    args.base_manifest.resolve(), args.base_manifest_sha256, upload, args.manifest.resolve())
                if backend_sidecar is not None:
                    shutil.copytree(backend_sidecar / 'functions', Path(directory) / 'functions')
                    shutil.copy2(backend_sidecar / '_routes.json', upload / '_routes.json')
                config_path = Path(directory) / 'wrangler.json'
                write_json(config_path, diagnostics_wrangler_config(project))
                command = [api.wrangler, 'pages', 'deploy', str(upload), '--project-name', PROJECT,
                           '--branch', BRANCH, '--commit-hash', args.sha,
                           '--commit-message', 'staging ' + args.sha, '--commit-dirty=false', '--no-bundle',
                           '--config', str(config_path)]
                with (args.report_dir / 'wrangler.log').open('x') as stream:
                    result = subprocess.run(command, cwd=directory, env=api.env, stdout=stream,
                                            stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
                require(result.returncode == 0, 'Staging upload failed or is uncertain; inspect receipt before retrying')
            text = (args.report_dir / 'wrangler.log').read_text()
            urls = set(re.findall(r'https://[0-9a-f]{8}\.webmelee-staging\.pages\.dev', text))
            require(len(urls) == 1, 'Upload identity is uncertain; inspect Cloudflare before retrying')
            immutable = urls.pop()
            deployments = api.request('/deployments')
            matches = [item for item in deployments if item.get('url', '').rstrip('/') == immutable]
            require(len(matches) == 1, 'Cannot resolve unique staging deployment')
            record.update(validate_deployment(matches[0], args.sha))
        current = api.request()
        validate_project(current)
        require((current.get('canonical_deployment') or {}).get('id') == record['deployment_id'],
                'Staging canonical deployment differs from the requested operation')
    record['immutable_url'] = immutable
    write_json(args.report_dir / 'deployment.json', record)
    origins = [('immutable', immutable), ('pages', PAGES)]
    if args.verify_custom_domain:
        origins.append(('custom', STABLE))
    for name, origin in origins:
        write_json(args.report_dir / (name + '-http.json'), verify_http(args.source_root, origin, args.manifest))
    write_json(args.report_dir / 'verified.json', {**record, 'http_result': 'pass',
                'custom_domain_verified': args.verify_custom_domain,
                'browser_result': 'separate headed lifecycle check required'})
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    main()
