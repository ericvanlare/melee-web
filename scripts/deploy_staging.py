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
    '/gameplay_audio_resample.c', '/cpu-address-audit.json',
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


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
    require(not project.get('source') and not project.get('uses_functions'),
            'Staging must remain a static direct-upload project')
    require(set(project.get('domains', [])) <= {urlsplit(PAGES).netloc, urlsplit(STABLE).netloc},
            'Unexpected custom domain on staging project')
    config = project.get('build_config') or {}
    require(not config.get('web_analytics_tag') and not config.get('web_analytics_token'),
            'Staging analytics must be disabled')
    configurations = project.get('deployment_configs') or {}
    require(set(configurations) == {'preview', 'production'}, 'Missing staging environment configuration')
    for config in configurations.values():
        for key, value in config.items():
            if key.endswith('_bindings') or key in ('env_vars', 'kv_namespaces', 'r2_buckets',
                    'd1_databases', 'services', 'durable_object_namespaces', 'ai', 'analytics_engine_datasets'):
                require(not value, 'Staging must not have environment variables or service bindings')


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
    return report


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
    record = {'tooling_sha': head, 'source_sha': args.sha, 'manifest_sha256': args.manifest_sha256,
              'project': PROJECT, 'environment': 'staging', 'stable_url': PAGES,
              'custom_domain_url': STABLE, 'action': args.action,
              'candidate': evidence, 'recorded_at': datetime.now(timezone.utc).isoformat()}
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
                command = [api.wrangler, 'pages', 'deploy', str(upload), '--project-name', PROJECT,
                           '--branch', BRANCH, '--commit-hash', args.sha,
                           '--commit-message', 'staging ' + args.sha, '--commit-dirty=false', '--no-bundle']
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
