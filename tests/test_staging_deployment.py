import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('staging_deployment',
    Path(__file__).resolve().parents[1] / 'scripts/deploy_staging.py')
staging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(staging)


class StagingDeploymentTests(unittest.TestCase):
    def project(self):
        return {'name': 'webmelee-staging', 'production_branch': 'staging',
                'subdomain': 'webmelee-staging.pages.dev',
                'domains': ['webmelee-staging.pages.dev', 'staging.webmelee.gg'],
                'uses_functions': True,
                'deployment_configs': {
                    'production': self.function_config(),
                    'preview': self.function_config(),
                }}

    @staticmethod
    def function_config():
        return {
            'env_vars': {
                'DIAGNOSTICS_RATE_LIMIT': '60',
                'DIAGNOSTICS_DAILY_REPORT_CAP': '1000',
                'DIAGNOSTICS_DAILY_BYTE_CAP': '16777216',
                'DIAGNOSTICS_ALLOWED_RELEASES': '{"staging":[],"production":[]}',
                # Pages API metadata intentionally carries the secret type but
                # not a value.  The deploy config must preserve this project
                # secret by omitting it from generated vars.
                'DIAGNOSTICS_ADMIN_TOKEN': {'type': 'secret_text'},
            },
            'd1_databases': [{
                'binding': 'DIAGNOSTICS_DB',
                'database_id': 'a' * 32,
                'database_name': 'webmelee-diagnostics',
            }],
        }

    def test_production_aliases_and_wrong_branch_fail_closed(self):
        staging.validate_project(self.project())
        for field, value in (('name', 'webmelee'), ('production_branch', 'main'),
                             ('subdomain', 'webmelee.pages.dev'),
                             ('domains', ['webmelee.gg']), ('domains', ['www.webmelee.gg'])):
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                staging.validate_project({**self.project(), field: value})

    def test_service_bindings_functions_and_analytics_are_rejected(self):
        cases = [dict(uses_functions=False), dict(source={'type': 'github'}),
                 dict(build_config={'web_analytics_token': 'token'}),
                 dict(deployment_configs={}),
                 dict(deployment_configs={'preview': {'env_vars': {'SECRET': 'value'}},
                                          'production': self.function_config()}),
                 dict(deployment_configs={'production': {'r2_buckets': {'GAME': 'bucket'}},
                                          'preview': self.function_config()}),
                 dict(deployment_configs={'production': {**self.function_config(),
                                                         'd1_databases': [{
                                                             'binding': 'OTHER_DB',
                                                             'database_id': 'a' * 32,
                                                             'database_name': 'other',
                                                         } ]},
                                          'preview': self.function_config()}),
                 dict(deployment_configs={'production': {**self.function_config(),
                                                         'd1_databases': [{
                                                             **self.function_config()['d1_databases'][0],
                                                             'migrations_dir': './secret',
                                                         }]},
                                          'preview': self.function_config()})]
        for case in cases:
            with self.subTest(case=case), self.assertRaises(ValueError):
                staging.validate_project({**self.project(), **case})

    def test_generated_wrangler_config_is_narrow_and_uses_project_bindings(self):
        config = staging.diagnostics_wrangler_config(self.project())
        self.assertEqual(config['name'], 'webmelee-staging')
        self.assertEqual(config['pages_build_output_dir'], './public')
        self.assertEqual(config['d1_databases'], [{
            'binding': 'DIAGNOSTICS_DB',
            'database_id': 'a' * 32,
            'database_name': 'webmelee-diagnostics',
        }])
        self.assertEqual(set(config), {
            '$schema', 'name', 'compatibility_date', 'pages_build_output_dir',
            'd1_databases', 'vars',
        })
        uuid_project = self.project()
        uuid_project['deployment_configs']['production']['d1_databases'][0]['database_id'] = (
            '00000000-0000-0000-0000-000000000001'
        )
        staging.validate_project(uuid_project)

        api_project = self.project()
        for config in api_project['deployment_configs'].values():
            config['env_vars'] = {
                key: ({'type': 'secret_text'} if key == 'DIAGNOSTICS_ADMIN_TOKEN'
                      else {'type': 'plain_text', 'value': value})
                for key, value in config['env_vars'].items()
            }
            database = config['d1_databases'][0]
            config['d1_databases'] = {
                'DIAGNOSTICS_DB': {
                    'id': database['database_id'],
                    'name': database['database_name'],
                }
            }
        staging.validate_project(api_project)
        self.assertEqual(staging.diagnostics_wrangler_config(api_project)['vars']['DIAGNOSTICS_RATE_LIMIT'], '60')
        self.assertNotIn('DIAGNOSTICS_ADMIN_TOKEN', staging.diagnostics_wrangler_config(api_project)['vars'])

        secret_project = self.project()
        secret_project['deployment_configs']['production']['env_vars']['DIAGNOSTICS_RATE_LIMIT'] = {
            'type': 'secret_text', 'value': '60'
        }
        with self.assertRaisesRegex(ValueError, 'non-secret'):
            staging.validate_project(secret_project)

    def redacted_pages_project(self):
        project = self.project()
        project['domains'] = ['webmelee-staging.pages.dev']
        for environment, config in project['deployment_configs'].items():
            config.update(fail_open=True, always_use_latest_compatibility_date=False,
                          compatibility_date=('2026-10-01' if environment == 'production' else '2026-09-13'),
                          compatibility_flags=[], build_image_major_version=3, usage_model='standard')
            config['env_vars'] = {
                key: ({'type': 'secret_text', 'value': ''} if key == 'DIAGNOSTICS_ADMIN_TOKEN'
                      else {'type': 'plain_text', 'value': value})
                for key, value in config['env_vars'].items()
            }
            config['d1_databases'] = {'DIAGNOSTICS_DB': {'id': '2a94860d-c09a-46a3-883b-00af5308b0b1'}}
        project['deployment_configs']['production']['wrangler_config_hash'] = 'opaque-provider-metadata'
        return project

    def test_observed_redacted_pages_metadata_uses_authenticated_d1_name(self):
        project = self.redacted_pages_project()
        with self.assertRaisesRegex(ValueError, 'lacks an authenticated name'):
            staging.validate_project(project)
        database = {'database_id': '2a94860d-c09a-46a3-883b-00af5308b0b1',
                    'database_name': 'webmelee-staging-diagnostics'}
        staging.validate_project(project, authenticated_database=database)
        config = staging.diagnostics_wrangler_config(project, authenticated_database=database)
        self.assertEqual(config['compatibility_date'], '2026-10-01')
        self.assertEqual(config['d1_databases'], [{'binding': 'DIAGNOSTICS_DB', **database}])
        self.assertEqual(config['vars'], {
            'DIAGNOSTICS_RATE_LIMIT': '60',
            'DIAGNOSTICS_DAILY_REPORT_CAP': '1000',
            'DIAGNOSTICS_DAILY_BYTE_CAP': '16777216',
            'DIAGNOSTICS_ALLOWED_RELEASES': '{"staging":[],"production":[]}',
        })
        self.assertEqual(project, self.redacted_pages_project())

    def test_provider_placement_accepts_only_an_empty_object(self):
        project = self.project()
        production = project['deployment_configs']['production']
        production['placement'] = {}
        staging.validate_project(project)
        for placement in ({'mode': 'smart'}, [], None):
            with self.subTest(placement=placement):
                production['placement'] = placement
                with self.assertRaisesRegex(ValueError, r'production\.placement'):
                    staging.validate_project(project)

    def test_provider_metadata_and_d1_identity_changes_fail_closed(self):
        database = {'database_id': '2a94860d-c09a-46a3-883b-00af5308b0b1',
                    'database_name': 'webmelee-staging-diagnostics'}
        for key, value in (
            ('kv_namespaces', {'OTHER': {'namespace_id': 'a' * 32}}),
            ('fail_open', 1),
            ('fail_open', False),
            ('always_use_latest_compatibility_date', True),
            ('compatibility_date', '2026-10-02'),
            ('compatibility_flags', ['nodejs_compat']),
            ('build_image_major_version', 4),
            ('usage_model', 'bundled'),
            ('wrangler_config_hash', None),
            ('d1_databases', {'DIAGNOSTICS_DB': {'id': 'b' * 32}}),
            ('d1_databases', {'DIAGNOSTICS_DB': {'id': database['database_id'], 'name': 'other'}}),
            ('d1_databases', {'DIAGNOSTICS_DB': {'id': database['database_id'], 'binding': 'OTHER'}}),
        ):
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                project = self.redacted_pages_project()
                project['deployment_configs']['production'][key] = value
                staging.validate_project(project, authenticated_database=database)

    def test_authenticated_d1_lookup_is_pinned_and_checks_the_frozen_identity(self):
        api = object.__new__(staging.Pages)
        api.wrangler = '/path/to/pinned/wrangler'
        api.env = {'WRANGLER_SEND_METRICS': 'false'}
        match = {'uuid': '2a94860d-c09a-46a3-883b-00af5308b0b1', 'name': 'webmelee-staging-diagnostics'}
        with patch.object(staging, '_validate_wrangler_version') as pin, \
                patch.object(staging.subprocess, 'run',
                             return_value=SimpleNamespace(returncode=0, stdout=json.dumps(match))) as run:
            self.assertEqual(api.diagnostics_database(), {'database_id': match['uuid'], 'database_name': match['name']})
            pin.assert_called_once_with(api.wrangler)
            self.assertEqual(run.call_args.args[0], [api.wrangler, 'd1', 'info', match['name'], '--json'])
            self.assertIs(run.call_args.kwargs['env'], api.env)
            run.return_value.stdout = json.dumps({**match, 'uuid': 'b' * 32})
            with self.assertRaisesRegex(ValueError, 'differs from the frozen'):
                api.diagnostics_database()

    def test_admin_secret_metadata_is_required_exact_and_never_uploaded(self):
        missing = self.project()
        del missing['deployment_configs']['production']['env_vars']['DIAGNOSTICS_ADMIN_TOKEN']
        with self.assertRaisesRegex(ValueError, 'missing.*admin secret'):
            staging.validate_project(missing)

        wrong_type = self.project()
        wrong_type['deployment_configs']['production']['env_vars']['DIAGNOSTICS_ADMIN_TOKEN'] = {
            'type': 'plain_text', 'value': 'token'
        }
        with self.assertRaisesRegex(ValueError, 'secret_text'):
            staging.validate_project(wrong_type)

        extra_metadata = self.project()
        extra_metadata['deployment_configs']['production']['env_vars']['DIAGNOSTICS_ADMIN_TOKEN'] = {
            'type': 'secret_text', 'value': 'redacted', 'label': 'unexpected'
        }
        with self.assertRaisesRegex(ValueError, 'secret_text'):
            staging.validate_project(extra_metadata)

        project_with_api_secret_value = self.project()
        project_with_api_secret_value['deployment_configs']['production']['env_vars'][
            'DIAGNOSTICS_ADMIN_TOKEN'] = {'type': 'secret_text', 'value': 'opaque-test-secret'}
        staging.validate_project(project_with_api_secret_value)
        generated = staging.diagnostics_wrangler_config(project_with_api_secret_value)
        self.assertNotIn('DIAGNOSTICS_ADMIN_TOKEN', generated['vars'])
        self.assertNotIn('opaque-test-secret', json.dumps(generated, sort_keys=True))

        for value in ('short', None):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'at least 16 characters'):
                invalid_api_secret = self.project()
                invalid_api_secret['deployment_configs']['production']['env_vars'][
                    'DIAGNOSTICS_ADMIN_TOKEN'] = {'type': 'secret_text', 'value': value}
                staging.validate_project(invalid_api_secret)

    def test_origin_cannot_escape_staging(self):
        self.assertEqual(staging.immutable_origin('https://1234abcd.webmelee-staging.pages.dev/'),
                         'https://1234abcd.webmelee-staging.pages.dev')
        for url in ('https://webmelee.gg', 'https://8f59ed0b.webmelee.pages.dev',
                    'https://928714aa.webmelee.pages.dev', 'https://webmelee-staging.pages.dev',
                    'http://1234abcd.webmelee-staging.pages.dev',
                    'https://1234abcd.webmelee-staging.pages.dev@evil.example',
                    'https://1234abcd.webmelee-staging.pages.dev/path',
                    'https://1234abcd.webmelee-staging.pages.dev?host=webmelee.gg'):
            with self.subTest(url=url), self.assertRaises(ValueError):
                staging.immutable_origin(url)

    def test_candidate_must_match_the_successful_staging_deployment(self):
        deployment = {'id': '1234abcd-0000-0000-0000-000000000000',
                      'url': 'https://1234abcd.webmelee-staging.pages.dev',
                      'project_name': 'webmelee-staging', 'environment': 'production',
                      'latest_stage': {'status': 'success'},
                      'deployment_trigger': {'metadata': {'branch': 'staging', 'commit_hash': 'a' * 40}}}
        self.assertEqual(staging.validate_deployment(deployment, 'a' * 40)['environment'], 'staging')
        for change in ({'project_name': 'webmelee'}, {'environment': 'preview'},
                       {'latest_stage': {'status': 'active'}},
                       {'deployment_trigger': {'metadata': {'branch': 'main', 'commit_hash': 'a' * 40}}}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                staging.validate_deployment({**deployment, **change}, 'a' * 40)
        with self.assertRaises(ValueError):
            staging.validate_deployment(deployment, 'b' * 40)

    def test_api_client_rejects_other_projects_dns_and_arbitrary_posts(self):
        api = object.__new__(staging.Pages)
        api.account = 'a' * 32
        api.token = 'not-a-real-credential'
        with patch.object(staging.urllib.request, 'urlopen') as network:
            for suffix, rollback in (('/../../webmelee', False), ('/domains', False),
                                     ('/zones/test/dns_records', True), ('', True),
                                     ('/deployments/1234?project=webmelee', True)):
                with self.subTest(suffix=suffix), self.assertRaises(ValueError):
                    api.request(suffix, rollback=rollback)
            network.assert_not_called()

    def test_changed_frozen_manifest_stops_before_any_account_access(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = root / 'candidate.manifest.json'
            manifest.write_text('{}')
            arguments = ['deploy_staging.py', 'deploy', '--source-root', str(root),
                '--sha', 'a' * 40, '--base-output', str(root / 'base'),
                '--base-manifest', str(root / 'base.manifest.json'),
                '--base-manifest-sha256', 'b' * 64, '--output', str(root / 'public'),
                '--manifest', str(manifest), '--manifest-sha256', 'c' * 64,
                '--report-dir', str(root / 'reports')]
            with patch.object(staging.sys, 'argv', arguments), \
                    patch.object(staging, 'tooling_head', return_value='d' * 40), \
                    patch.object(staging, 'Pages') as account, \
                    self.assertRaisesRegex(ValueError, 'manifest identity mismatch'):
                staging.main()
            account.assert_not_called()
            self.assertFalse((root / 'reports').exists())

    def test_partial_rollback_producer_inputs_stop_before_account_access(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = root / 'candidate.manifest.json'
            manifest.write_text('{}')
            arguments = ['deploy_staging.py', 'deploy', '--source-root', str(root),
                '--sha', 'a' * 40, '--base-output', str(root / 'base'),
                '--base-manifest', str(root / 'base.manifest.json'),
                '--base-manifest-sha256', 'b' * 64, '--output', str(root / 'public'),
                '--manifest', str(manifest), '--manifest-sha256',
                staging.hashlib.sha256(manifest.read_bytes()).hexdigest(),
                '--report-dir', str(root / 'reports'), '--rollback-build', str(root / 'producer')]
            with patch.object(staging.sys, 'argv', arguments), \
                    patch.object(staging, 'tooling_head', return_value='d' * 40), \
                    patch.object(staging, 'Pages') as account, \
                    self.assertRaisesRegex(ValueError, 'all-or-none'):
                staging.main()
            account.assert_not_called()
            self.assertFalse((root / 'reports').exists())

    def test_repeat_frozen_audio_deploy_verifies_canonical_overlay_before_upload(self):
        source_root = Path(staging.__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            base = {'runtime_hash': '1234567890abcdef', 'backend': {'directory': 'base.functions'}, 'files': []}
            base_manifest = root / 'base.manifest.json'
            base_manifest.write_text(json.dumps(base))
            pin = {'deployment_id': '9876abcd-0000-0000-0000-000000000000',
                   'manifest_sha256': staging.hashlib.sha256(base_manifest.read_bytes()).hexdigest()}
            source_sha = 'a' * 40
            deployment = {'id': '1234abcd-0000-0000-0000-000000000000',
                          'url': 'https://1234abcd.webmelee-staging.pages.dev',
                          'project_name': staging.PROJECT, 'environment': 'production',
                          'latest_stage': {'status': 'success'},
                          'deployment_trigger': {'metadata': {'branch': staging.BRANCH, 'commit_hash': source_sha}}}
            record = staging.validate_deployment(deployment, source_sha)
            manifest = root / 'candidate.manifest.json'
            manifest.write_text('{}')
            previous_manifest = root / 'previous.manifest.json'
            previous_record = root / 'previous.json'
            argv = ['deploy_staging.py', 'deploy', '--source-root', str(source_root),
                    '--sha', 'b' * 40, '--base-output', str(root / 'base'),
                    '--base-manifest', str(base_manifest), '--base-manifest-sha256', pin['manifest_sha256'],
                    '--output', str(root / 'public'), '--manifest', str(manifest),
                    '--manifest-sha256', staging.hashlib.sha256(manifest.read_bytes()).hexdigest(),
                    '--rollback-build', str(root / 'build'), '--rollback-linked', str(root / 'linked'),
                    '--rollback-linked-receipt-sha256', 'e' * 64, '--frozen-audio-proof', str(root / 'proof'),
                    '--wrangler', str(root / 'wrangler'), '--account-id', 'f' * 32,
                    '--previous-record', str(previous_record)]
            real_load = staging.load_module
            real_verify = staging.verify_http
            stage = SimpleNamespace(FROZEN_AUDIO_BASE=pin, audit_staging=lambda *_args, **_kwargs: {})
            for case, error in (
                ('missing', 'supply its overlay manifest'),
                ('id', 'current rollback target'),
                ('url', 'canonical immutable URL'),
                ('source', 'requested staging SHA'),
                ('player-source', 'player source differs'),
                ('hash', 'does not match the deployment record'),
                ('base', 'changed the frozen audio identity'),
                ('backend', 'changed the frozen audio identity'),
                ('verified-hash', 'changed during HTTP verification'),
                ('verified', 'upload boundary'),
                ('first-overlay', 'upload boundary'),
            ):
                with self.subTest(case=case):
                    rollback = {'audio_base': pin if case != 'base' else {**pin, 'runtime_hash': 'changed'},
                                'player': {'sourceSha': 'c' * 40 if case == 'player-source' else source_sha}}
                    previous = {**base, 'rollback': rollback}
                    if case == 'backend':
                        previous['backend'] = {'directory': 'different.functions'}
                    previous_manifest.write_text(json.dumps(previous))
                    digest = staging.hashlib.sha256(previous_manifest.read_bytes()).hexdigest()
                    receipt = {**record, 'manifest_sha256': digest}
                    canonical = deployment
                    if case == 'id':
                        receipt['deployment_id'] = pin['deployment_id']
                    elif case == 'url':
                        receipt['immutable_url'] = 'https://2345abcd.webmelee-staging.pages.dev'
                    elif case == 'source':
                        receipt['source_sha'] = 'c' * 40
                    elif case == 'hash':
                        receipt['manifest_sha256'] = 'c' * 64
                    elif case == 'first-overlay':
                        canonical = {**deployment, 'id': pin['deployment_id']}
                        receipt.update(deployment_id=pin['deployment_id'], manifest_sha256=pin['manifest_sha256'])
                    previous_record.write_text(json.dumps(receipt))
                    report_dir = root / 'reports' / case
                    arguments = argv + ['--report-dir', str(report_dir)]
                    if case not in ('missing', 'first-overlay'):
                        arguments += ['--previous-manifest', str(previous_manifest)]

                    def load(path, name):
                        if name == '_staging_package':
                            return stage
                        return real_load(path, name)

                    def assert_upload_boundary(*_args, **_kwargs):
                        self.assertTrue((report_dir / 'previous-staging-deployment.json').is_file())
                        if case == 'verified':
                            self.assertEqual(json.loads((report_dir / 'previous-staging-http.json').read_text()),
                                             {'manifest_sha256': digest})
                        raise ValueError('upload boundary')

                    with patch.object(staging.sys, 'argv', arguments), \
                            patch.dict(staging.sys.modules, {'stage_public':
                                SimpleNamespace(FROZEN_AUDIO_BASE=pin, ROLLBACK_RELAY='unused')}), \
                            patch.object(staging.sys, 'path', [str(source_root / 'scripts'), *staging.sys.path]), \
                            patch.object(staging, 'tooling_head', return_value='d' * 40), \
                            patch.object(staging, 'load_module', side_effect=load), \
                            patch.object(staging, 'Pages') as account, \
                            patch.object(staging, 'validate_project'), \
                            patch.object(staging, 'verify_http',
                                side_effect=real_verify if case in ('base', 'backend') else None,
                                return_value={'manifest_sha256': 'c' * 64 if case == 'verified-hash' else digest}
                            ) as verify, \
                            patch.object(staging.tempfile, 'TemporaryDirectory',
                                         side_effect=assert_upload_boundary) as upload, \
                            patch.object(staging.urllib.request, 'urlopen') as network, \
                            self.assertRaisesRegex(ValueError, error):
                        account.return_value.request.return_value = {'canonical_deployment': canonical}
                        staging.main()
                    network.assert_not_called()
                    if case in ('verified', 'first-overlay'):
                        upload.assert_called_once()
                    else:
                        upload.assert_not_called()
                    if case in ('base', 'backend', 'verified-hash', 'verified'):
                        verify.assert_called_once_with(source_root, record['immutable_url'], previous_manifest,
                                                       rollback_staging=True, base_audio_manifest=base_manifest)
                    else:
                        verify.assert_not_called()

    def test_http_verification_rejects_raw_wasm_alias(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / 'candidate.manifest.json'
            manifest.write_text('{"files": []}')
            verifier = SimpleNamespace(verify=lambda *_: {'result': 'pass'},
                get=lambda url: (200, {}, b'wasm accidentally exposed', url),
                check_destination=lambda *_: None)
            with patch.object(staging, 'load_module', return_value=verifier), \
                self.assertRaisesRegex(ValueError, '/gameplay_public.wasm returned 200'):
                staging.verify_http(Path(directory), staging.STABLE, manifest)

    def test_api_verification_requires_private_response_headers(self):
        headers = {
            'Cache-Control': 'no-store',
            'X-Content-Type-Options': 'nosniff',
            'Referrer-Policy': 'no-referrer',
            'Permissions-Policy': 'camera=(), microphone=(), geolocation=()',
        }
        responses = iter((
            (422, headers, b'{}'),
            (401, headers, b'{}'),
            (401, headers, b'{}'),
            (401, headers, b'{}'),
            (401, headers, b'{}'),
        ))
        with patch.object(staging, '_api_request', side_effect=lambda *args, **kwargs: next(responses)):
            report = staging._verify_api_routes(staging.STABLE)
        self.assertEqual([item['status'] for item in report], [422, 401, 401, 401, 401])

        bad = dict(headers)
        bad.pop('Permissions-Policy')
        responses = iter(((422, bad, b'{}'),))
        with patch.object(staging, '_api_request', side_effect=lambda *args, **kwargs: next(responses)), \
                self.assertRaisesRegex(ValueError, 'permissions policy'):
            staging._verify_api_routes(staging.STABLE)


if __name__ == '__main__':
    unittest.main()
