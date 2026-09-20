import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
spec = importlib.util.spec_from_file_location('stage_audio_preview', ROOT / 'scripts/stage_audio_preview.py')
preview = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)


class AudioPreviewPackageTests(unittest.TestCase):
    def generated(self):
        native = {name: b'fixture' for name in preview.NATIVE}
        with patch.object(preview, 'runtime', return_value=(native, 'a' * 64)), \
                patch.object(preview, 'source_commit', return_value='b' * 40):
            return preview.expected_files()

    def test_explicit_graph_has_audio_and_no_development_surface(self):
        files, meta = self.generated()
        group = 'runtime/' + meta['runtime_hash'] + '/'
        self.assertEqual({name.removeprefix(group) for name in files if name.startswith(group)},
                         set(preview.MODULES) | set(preview.NATIVE) |
                         {'player/player-shell.mjs', 'player/player.css'})
        self.assertIn(b'../audio-preview-runtime.mjs', files[group + 'player/player-shell.mjs'])
        self.assertIn(b'createAudio: createRuntimeAudio', files[group + 'audio-preview-runtime.mjs'])
        self.assertIn(b'audio preview', files['index.html'])
        self.assertNotIn(b'no audio <button', files['index.html'])
        self.assertNotIn(b'Audio is disabled', files['index.html'])
        self.assertNotIn(b'public alpha has no audio', files['notices.html'])
        self.assertNotIn(b'EXCLUDED FROM PUBLIC ALPHA', files['licenses/runtime-third-party.txt'])
        self.assertIn(b'20 compatibility values', files['notices.html'])
        self.assertIn(b'X-Robots-Tag: noindex', files['_headers'])
        for name in ('runtime.html', 'runtime-cache.js', 'runtime-diagnostics.mjs', 'dsp_coef.bin'):
            self.assertFalse(any(path.endswith('/' + name) or path == name for path in files))

    def test_audit_rejects_changed_extra_and_symlink_files(self):
        files, meta = self.generated()
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(preview, 'expected_files', return_value=(files, meta)):
            base = Path(directory)
            output, manifest = base / 'site', base / 'site.manifest.json'
            preview.prepare(output, manifest)
            preview.audit(output, manifest)
            index = output / 'index.html'
            original = index.read_bytes()
            index.write_bytes(original + b'changed')
            with self.assertRaisesRegex(ValueError, 'bytes or inventory'):
                preview.audit(output, manifest)
            index.write_bytes(original)
            extra = output / 'private.json'
            extra.write_text('{}')
            with self.assertRaisesRegex(ValueError, 'bytes or inventory'):
                preview.audit(output, manifest)
            extra.unlink()
            extra.symlink_to(manifest)
            with self.assertRaisesRegex(ValueError, 'Symlink'):
                preview.audit(output, manifest)

    def test_manifest_tampering_rejected(self):
        files, meta = self.generated()
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(preview, 'expected_files', return_value=(files, meta)):
            base = Path(directory)
            output, manifest = base / 'site', base / 'site.manifest.json'
            preview.prepare(output, manifest)
            record = json.loads(manifest.read_text())
            record['project'] = 'webmelee'
            manifest.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError, 'manifest mismatch'):
                preview.audit(output, manifest)

    def test_manifest_must_stay_outside_upload(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'site'
            with self.assertRaisesRegex(ValueError, 'outside upload'):
                preview.prepare(output, output / 'manifest.json')

    def test_template_drift_fails_explicitly(self):
        with self.assertRaisesRegex(ValueError, 'boundary changed'):
            preview.player_html('<p>unexpected source</p>')

    def test_verification_rejects_production_before_network(self):
        for url in ('https://webmelee.gg', 'https://webmelee.pages.dev',
                    'https://webmelee-staging.pages.dev.evil.example', 'https://example.com'):
            with self.subTest(url=url), self.assertRaisesRegex(ValueError, 'restricted'):
                preview.verify(url, Path('unused.json'))


if __name__ == '__main__':
    unittest.main()
