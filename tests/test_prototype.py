"""Prototype packaging/content boundary; no gameplay or performance claims."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from prepare_prototype import BUILD_ARTIFACTS, content_manifest, prepare
from serve import create_server
from check_gameplay import node_runtime


class PrototypeTests(unittest.TestCase):
    def test_generated_content_and_shared_roster_agree(self):
        manifest = content_manifest((ROOT / 'src/gameplay_content.h').read_text())
        # Current implementation inventory, not an acceptance statement.
        self.assertEqual([row['name'] for row in manifest['fighters']],
                         ['Mario', 'Fox', 'Falco', 'Marth', 'Dr. Mario', 'Roy',
                          'Link', 'Young Link', 'Captain Falcon', 'Ganondorf'])
        self.assertEqual([row['name'] for row in manifest['stages']], [
            'Final Destination', 'Battlefield', "Yoshi's Story", 'Dream Land', 'Hyrule Temple'])
        result = subprocess.run([str(node_runtime()), str(ROOT / 'tests/prototype_content_test.mjs')],
                                input=json.dumps(manifest), text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_native_table_drift_fails_closed(self):
        source = (ROOT / 'src/gameplay_content.h').read_text()
        with self.assertRaisesRegex(ValueError, 'row'):
            content_manifest(source.replace('{ CKIND_MARIO,', '{ UNSUPPORTED_MACRO(),'))
        with self.assertRaisesRegex(ValueError, 'not found'):
            content_manifest('')

    def test_preview_over_real_http_and_no_unlisted_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / 'runtime'; build.mkdir()
            for name in BUILD_ARTIFACTS:
                source = ROOT / 'web' / name
                (build / name).write_bytes(source.read_bytes() if source.is_file() else b'synthetic packaging fixture')
            (build / 'private-disc.iso').write_bytes(b'not a disc')
            output = prepare(build, root / 'preview', 'development')
            self.assertFalse((output / 'private-disc.iso').exists())
            self.assertIn('data-environment="development"', (output / 'prototype.html').read_text())
            with self.assertRaisesRegex(ValueError, 'new directory'):
                prepare(build, output)
            with self.assertRaisesRegex(ValueError, 'Only local'):
                prepare(build, root / 'production', 'production')
            with create_server(output, 0) as server:
                thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
                try:
                    base = f'http://127.0.0.1:{server.server_port}'
                    with urlopen(base + '/prototype.html') as response:
                        self.assertEqual(response.status, 200)
                        self.assertEqual(response.headers['Cross-Origin-Opener-Policy'], 'same-origin')
                        self.assertEqual(response.headers['Cross-Origin-Embedder-Policy'], 'require-corp')
                    with urlopen(base + '/prototype-content.json') as response:
                        self.assertEqual(len(json.load(response)['fighters']), 10)
                    with self.assertRaises(HTTPError) as error:
                        urlopen(base + '/private-disc.iso')
                    error.exception.close()
                finally:
                    server.shutdown(); thread.join()
            (build / 'runtime.html').write_text('stale')
            with self.assertRaisesRegex(ValueError, 'does not match'):
                prepare(build, root / 'mismatched')


if __name__ == '__main__':
    unittest.main()
