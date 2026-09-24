"""Fresh checked runtime and synthetic input rejection tests; no retained inputs."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from tools import source_synth_joined_runtime as runtime
from tools import source_synth_joined_startup as startup
from test_original_startup_fixture import context

ROOT = Path(__file__).resolve().parents[1]


def synthetic_inputs(directory):
    directory.mkdir(parents=True, exist_ok=True)
    settings = bytes(19) + b'\x04' + bytes(44)
    values = {
        'boot.json': context(),
        'parameters.json': {
            'schema': 'melee-web-source-synth-parameters', 'status': 'derived',
            'source_revision': startup.SOURCE_REVISION,
            'result': {'driver_call': [64, 0, 64, 192],
                       'bank_sizes': [32, 64, 96], 'bank_size_total': 192}},
        'envelope.json': {'bytes': 68, 'settings_offset': 4, 'settings_bytes': 64,
                          'flags_offset': 19,
                          'settings_sha256': hashlib.sha256(settings).hexdigest()},
    }
    for name, value in values.items():
        (directory / name).write_text(json.dumps(value)+'\n')
    (directory / 'settings.bin').write_bytes(settings)
    return dict(boot_path=directory/'boot.json', parameters_path=directory/'parameters.json',
                settings_path=directory/'settings.bin', envelope_path=directory/'envelope.json')


class JoinedRuntimeInputTests(unittest.TestCase):
    def test_receipt_labels_cannot_authorize_owned_execution(self):
        with tempfile.TemporaryDirectory() as temporary:
            inputs = synthetic_inputs(Path(temporary))
            with self.assertRaises(runtime.JoinedRuntimeError):
                runtime.derive_runtime_inputs(**inputs, synthetic=False)
            plan = runtime.derive_runtime_inputs(**inputs, synthetic=True)
            self.assertTrue(plan['synthetic'])
            self.assertEqual(plan['arguments'][1:5], ['64', '0', '64', '192'])

    def test_capture_shaped_boot_and_mutated_settings_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            inputs = synthetic_inputs(Path(temporary))
            boot = json.loads(inputs['boot_path'].read_text())
            boot['allocations'] = []
            inputs['boot_path'].write_text(json.dumps(boot))
            with self.assertRaisesRegex(runtime.JoinedRuntimeError, 'captures'):
                runtime.derive_runtime_inputs(**inputs, synthetic=True)
            inputs['boot_path'].write_text(json.dumps(context()))
            inputs['settings_path'].write_bytes(bytes(64))
            with self.assertRaisesRegex(runtime.JoinedRuntimeError, 'match'):
                runtime.derive_runtime_inputs(**inputs, synthetic=True)

    def test_inconsistent_bank_total_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            inputs = synthetic_inputs(Path(temporary))
            value = json.loads(inputs['parameters_path'].read_text())
            value['result']['driver_call'][3] += 32
            inputs['parameters_path'].write_text(json.dumps(value))
            with self.assertRaises(runtime.JoinedRuntimeError):
                runtime.derive_runtime_inputs(**inputs, synthetic=True)

    def test_owned_sram_extraction_reads_actual_envelope(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            envelope = directory/'sram.bin'
            data = bytes(range(68))
            envelope.write_bytes(data)
            extracted, _ = runtime.read_owned_sram_envelope(envelope, directory/'out')
            self.assertEqual(extracted.read_bytes(), data[4:])
            envelope.write_bytes(data[:-1])
            with self.assertRaises(runtime.JoinedRuntimeError):
                runtime.read_owned_sram_envelope(envelope, directory/'bad-out')


    def test_owned_sram_rejects_redirected_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            envelope = directory/'sram.bin'
            envelope.write_bytes(bytes(68))
            target = directory/'target'
            target.mkdir()
            alias = directory/'alias'
            alias.symlink_to(target, target_is_directory=True)
            with self.assertRaisesRegex(runtime.JoinedRuntimeError, 'symlink'):
                runtime.read_owned_sram_envelope(envelope, alias)
            self.assertEqual(list(target.iterdir()), [])


class JoinedRuntimeExecutionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        required = [startup.EMXX, ROOT/'.venv/bin/cmake', ROOT/'.venv/bin/ninja',
                    ROOT/'build/browser/CMakeCache.txt',
                    ROOT/'build/browser/_deps/fmt-src/include/fmt/base.h']
        if not all(path.is_file() for path in required):
            raise unittest.SkipTest('configured pinned source/CMake runtime dependencies unavailable')
        (ROOT/'work').mkdir(exist_ok=True)
        cls.work = Path(tempfile.mkdtemp(prefix='source-synth-runtime-test-', dir=ROOT/'work'))
        profile = startup.build_profile(cls.work/'profile')
        cls.inputs = synthetic_inputs(cls.work/'inputs')
        cls.output = cls.work/'runtime'
        cls.receipt = runtime.build_runtime(
            cls.work/'profile/receipt.json', artifact_dir=cls.output.relative_to(ROOT), run=True,
            inputs=cls.inputs, synthetic_inputs=True)
        cls.command = json.loads((cls.output/'run.command.json').read_text())

    def test_profile_reuse_rejects_changed_headers_and_builder(self):
        profile_path = self.work/'profile/receipt.json'
        header = self.work/'profile/include/source_abi.h'
        original = header.read_bytes()
        try:
            header.write_bytes(original + b'\n#define ALTERED_ABI 1\n')
            with self.assertRaisesRegex(runtime.JoinedRuntimeError, 'headers changed'):
                runtime.validate_profile(profile_path)
        finally:
            header.write_bytes(original)
        receipt_bytes = profile_path.read_bytes()
        try:
            receipt = json.loads(receipt_bytes)
            receipt['builder_sha256'] = '0'*64
            profile_path.write_text(json.dumps(receipt))
            with self.assertRaisesRegex(runtime.JoinedRuntimeError, 'builder changed'):
                runtime.validate_profile(profile_path)
        finally:
            profile_path.write_bytes(receipt_bytes)
        extra = self.work/'profile/include/unexpected.h'
        try:
            extra.write_text('#define UNEXPECTED 1\n')
            with self.assertRaisesRegex(runtime.JoinedRuntimeError, 'headers changed'):
                runtime.validate_profile(profile_path)
        finally:
            extra.unlink()

    def test_runtime_rejects_redirected_artifact_directory(self):
        target = self.work/'empty-output'
        target.mkdir()
        alias = self.work/'redirected-output'
        alias.symlink_to(target, target_is_directory=True)
        with self.assertRaisesRegex(runtime.JoinedRuntimeError, 'symlink'):
            runtime.build_runtime(self.work/'profile/receipt.json', artifact_dir=alias)
        self.assertEqual(list(target.iterdir()), [])

    def test_fresh_synthetic_runtime_preserves_source_ownership(self):
        rows = [json.loads(line) for line in (self.output/'run.stdout').read_text().splitlines()
                if line.startswith('{')]
        allocations = next(row for row in rows if row.get('kind') == 'source_synth_allocations')
        joined = next(row for row in rows if row.get('kind') == 'source_synth_joined')
        self.assertEqual(allocations['aram_blocks'], [1280, 192, 196608])
        self.assertEqual(allocations['deferred_bytes_verified'], 1280)
        self.assertEqual(allocations['sram_reads'], 1)
        self.assertEqual(joined['arq_pumps'], 1)
        self.assertFalse(joined['runtime_claim'])
        self.assertTrue(self.receipt['input_plan']['synthetic'])

    def test_current_binary_rejects_each_negative_boundary(self):
        for mode, diagnostic in runtime.EXPECTED_NEGATIVE_DIAGNOSTICS.items():
            with self.subTest(mode=mode):
                command = self.command.copy()
                command[2] = mode
                result = runtime._run(command, cwd=ROOT, work=self.output,
                                      label='negative-'+mode, timeout=30)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stderr)

if __name__ == '__main__':
    unittest.main()
