import importlib.util
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
                'deployment_configs': {'production': {'env_vars': None}, 'preview': {'env_vars': None}}}

    def test_production_aliases_and_wrong_branch_fail_closed(self):
        staging.validate_project(self.project())
        for field, value in (('name', 'webmelee'), ('production_branch', 'main'),
                             ('subdomain', 'webmelee.pages.dev'),
                             ('domains', ['webmelee.gg']), ('domains', ['www.webmelee.gg'])):
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                staging.validate_project({**self.project(), field: value})

    def test_service_bindings_functions_and_analytics_are_rejected(self):
        cases = [dict(uses_functions=True), dict(source={'type': 'github'}),
                 dict(build_config={'web_analytics_token': 'token'}),
                 dict(deployment_configs={}),
                 dict(deployment_configs={'preview': {'env_vars': {'SECRET': 'value'}}, 'production': {}}),
                 dict(deployment_configs={'production': {'r2_buckets': {'GAME': 'bucket'}}, 'preview': {}})]
        for case in cases:
            with self.subTest(case=case), self.assertRaises(ValueError):
                staging.validate_project({**self.project(), **case})

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


if __name__ == '__main__':
    unittest.main()
