"""Focused package and Wrangler checks for the diagnostics Pages Functions graph."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))

import build_public as public  # noqa: E402
import deploy_staging as staging  # noqa: E402




def _wrangler_path():
    explicit = os.environ.get('WRANGLER_BIN')
    if explicit:
        return Path(explicit)
    candidate = ROOT / '.tools' / 'wrangler'
    return candidate if candidate.is_file() else None


class DiagnosticsBackendPackagingTests(unittest.TestCase):
    def test_wrangler_version_must_match_repository_pin(self):
        with patch.object(staging.subprocess, 'run', return_value=SimpleNamespace(
                returncode=0, stdout='4.131.1\n')):
            self.assertEqual(staging._validate_wrangler_version('/pinned/wrangler'), '4.131.1')
        with patch.object(staging.subprocess, 'run', return_value=SimpleNamespace(
                returncode=0, stdout='4.130.0\n')):
            with self.assertRaisesRegex(ValueError, 'does not match'):
                staging._validate_wrangler_version('/pinned/wrangler')

    def test_sidecar_is_exact_source_bound_graph_and_rejects_tampering(self):
        with tempfile.TemporaryDirectory(prefix='diagnostics-package-') as directory:
            root = Path(directory)
            output = root / 'site'
            output.mkdir()
            descriptor = public.stage_diagnostics_backend(output)
            self.assertIsInstance(descriptor, dict)
            self.assertEqual(descriptor['directory'], 'site.functions')
            self.assertEqual(descriptor['routes'], {
                'include': ['/api/diagnostics', '/api/diagnostics/*'],
                'exclude': [],
            })
            sidecar = root / 'site.functions'
            self.assertEqual(public.audit_diagnostics_backend(output, descriptor), descriptor)

            worker = sidecar / 'functions/api/worker.mjs'
            worker.write_bytes(worker.read_bytes() + b'\n// tampered')
            with self.assertRaisesRegex(public.BuildError, 'bytes differ'):
                public.audit_diagnostics_backend(output, descriptor)

            worker.write_bytes((ROOT / 'diagnostics/worker.mjs').read_bytes())
            (sidecar / 'private.json').write_text('{}')
            with self.assertRaisesRegex(public.BuildError, 'unauthorized files'):
                public.audit_diagnostics_backend(output, descriptor)

    def test_source_audit_rejects_private_material_and_non_api_routes(self):
        with tempfile.TemporaryDirectory(prefix='diagnostics-source-') as directory:
            fixture = Path(directory)
            shutil.copytree(ROOT / 'diagnostics', fixture / 'diagnostics')
            worker = fixture / 'diagnostics/worker.mjs'
            worker.write_bytes(worker.read_bytes() + b'\nconst privatePath = "/Users/operator";')
            with patch.object(public, 'ROOT', fixture), \
                    self.assertRaisesRegex(public.BuildError, 'private deployment material'):
                public._diagnostics_backend_source_files()

            worker.write_bytes((ROOT / 'diagnostics/worker.mjs').read_bytes())
            routes = fixture / 'diagnostics/_routes.json'
            routes.write_text(json.dumps({'version': 1, 'include': ['/'], 'exclude': []}))
            with patch.object(public, 'ROOT', fixture), \
                    self.assertRaisesRegex(public.BuildError, 'API-only route allowlist'):
                public._diagnostics_backend_source_files()

    def test_release_audit_rejects_one_sided_wire_schema_changes(self):
        with tempfile.TemporaryDirectory(prefix='diagnostics-schema-drift-') as directory:
            fixture = Path(directory)
            shutil.copytree(ROOT / 'diagnostics', fixture / 'diagnostics')
            (fixture / 'web').mkdir()
            schema = fixture / 'web/diagnostics-schema.mjs'
            schema.write_bytes((ROOT / 'web/diagnostics-schema.mjs').read_bytes())
            with patch.object(public, 'ROOT', fixture):
                self.assertIsNotNone(public._diagnostics_backend_source_files())
                schema.write_bytes(schema.read_bytes() + b'\n// one-sided change')
                with self.assertRaisesRegex(public.BuildError, 'wire schemas differ'):
                    public._diagnostics_backend_source_files()

    @unittest.skipUnless(_wrangler_path() is not None, 'set WRANGLER_BIN to the pinned Wrangler')
    def test_pinned_wrangler_compiles_exact_function_routes(self):
        with tempfile.TemporaryDirectory(prefix='diagnostics-wrangler-') as directory:
            root = Path(directory)
            sidecar = root / 'site.functions'
            source_files = public._diagnostics_backend_source_files()
            assert source_files is not None
            for source_rel, output_rel in public.DIAGNOSTICS_BACKEND_SOURCE_MAP:
                destination = sidecar / output_rel
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(source_files[source_rel])
            result = staging.validate_functions_graph(sidecar / 'functions', _wrangler_path(),
                                                     scratch_parent=root)
            self.assertEqual(result['routes'], ['/api/diagnostics', '/api/diagnostics/*'])
            self.assertEqual(result['command'][4], '<generated>/functions')
            self.assertTrue(result['validated'])


if __name__ == '__main__':
    unittest.main()
