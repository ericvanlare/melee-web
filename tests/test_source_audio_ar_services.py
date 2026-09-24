"""Checked original ARInit plus deferred original ARQ transfer; retained evidence."""
from __future__ import annotations
import ast
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / '.deps/emsdk'
AR_SOURCE = ROOT / '.deps/melee/extern/dolphin/src/dolphin/ar/ar.c'
ARQ_SOURCE = AR_SOURCE.with_name('arq.c')
OS_SOURCE = ROOT / '.deps/melee/extern/dolphin/src/dolphin/os/OSInterrupt.c'
EXPECTED = {
    OS_SOURCE: '69ba045af10aaede13f77fa1040af7fa6e79658e2d317bcee1afc5992d3fca7e',
    AR_SOURCE: 'dc2ad84463413ef6b91a8f8fe8acefb26ed35743d63dd85bf6590cec28d95cc8',
    ARQ_SOURCE: 'b8ce29ad2773743b3f657cc1203799baad5efcc65d8381beb60f4922b37a1be1',
}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def source_hashes():
    actual = {str(p.relative_to(ROOT)): sha(p) for p in EXPECTED}
    if actual != {str(p.relative_to(ROOT)): h for p, h in EXPECTED.items()}:
        raise AssertionError('Pinned AR/ARQ source changed')
    return actual

def retained_run(command, directory, name, timeout):
    (directory / f'{name}.command.json').write_text(json.dumps(command, indent=2)+'\n')
    env = dict(os.environ, EM_CONFIG=str(SDK / '.emscripten'),
               EM_CACHE=str(SDK / 'upstream/emscripten/cache'),
               EMSDK=str(SDK), EMSDK_PYTHON=sys.executable)
    try:
        result = subprocess.run(command, cwd=ROOT, env=env, text=True,
                                capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        for label, data in (('stdout', exc.stdout), ('stderr', exc.stderr)):
            (directory / f'{name}.{label}').write_text(
                data.decode(errors='replace') if isinstance(data, bytes) else data or '')
        (directory / f'{name}.timeout').write_text(str(timeout)+'\n')
        raise AssertionError(f'{name} timed out; retained {directory}') from exc
    for label, data in (('stdout', result.stdout), ('stderr', result.stderr)):
        (directory / f'{name}.{label}').write_text(data)
    (directory / f'{name}.exit').write_text(str(result.returncode)+'\n')
    return result

class SourceAudioArServiceTests(unittest.TestCase):
    @staticmethod
    def compatibility_copy(source):
        # Exact three C-to-C++ pointer conversions documented by the existing
        # patches/melee-source-ar-init-cxx.patch; no source behavior changes.
        for name in ('test_data_pad', 'dummy_data_pad', 'buffer_pad'):
            old = f'(void*) (((u32) &{name} + 0x1F) & 0xFFFFFFE0)'
            if source.count(old) != 1:
                raise AssertionError('AR compatibility occurrence changed')
            source = source.replace(old, old.replace('(void*)', '(u32*)'))
        return source

    @classmethod
    def setUpClass(cls):
        compiler = SDK / 'upstream/emscripten/em++.py'
        if not compiler.is_file() or not AR_SOURCE.is_file() or not ARQ_SOURCE.is_file():
            raise unittest.SkipTest('pinned Emscripten and AR/ARQ sources required')
        cls.before = source_hashes()
        (ROOT / 'work').mkdir(exist_ok=True)
        cls.work = Path(tempfile.mkdtemp(prefix='source-audio-ar-services-', dir=ROOT/'work'))
        generated = cls.work / 'ar_compat.c'
        generated.write_text(cls.compatibility_copy(AR_SOURCE.read_text()))
        os_source = OS_SOURCE.read_text()
        start = os_source.index('static u32 SetInterruptMask')
        end = os_source.index('\nOSInterruptMask OSGetInterruptMask', start)
        unmask = os_source.index('OSInterruptMask __OSUnmaskInterrupts', end)
        dispatch = os_source.index('\nvoid __OSDispatchInterrupt', unmask)
        body = os_source[start:end] + '\n\n' + os_source[unmask:dispatch]
        template = (ROOT / 'tests/source_audio_ar_services.cpp').read_text()
        marker = '/* MELEE_WEB_PINNED_OS_INTERRUPT */'
        if template.count(marker) != 1:
            raise AssertionError('Expected exactly one original OS interrupt marker')
        provider = cls.work / 'services_generated.cpp'
        provider.write_text(template.replace(marker, body))
        cls.output = cls.work / 'services.js'
        command = [sys.executable, str(compiler), '-std=gnu++17', '-O0', '-g',
                   '-Wall', '-Wextra', '-Werror', '-DDEBUG=1', '-ffp-contract=off',
                   f'-DMELEE_WEB_SOURCE_AR_PATH="{generated}"',
                   f'-DMELEE_WEB_SOURCE_ARQ_PATH="{ARQ_SOURCE}"',
                   '-Itests/source_audio_ar_services_include', '-Isrc', '-Itests',
                   '-I', str(ROOT/'.deps/melee/extern/dolphin/include'),
                   '-I', str(AR_SOURCE.parent),
                   str(provider), '-x', 'c',
                   'src/source_audio_ar_services.c', '-x', 'c++',
                   '-sENVIRONMENT=node', '-sEXIT_RUNTIME=1', '-sASSERTIONS=2',
                   '-sSAFE_HEAP=1', '-sSTACK_OVERFLOW_CHECK=2', '-o', str(cls.output)]
        result = retained_run(command, cls.work, 'compile', 120)
        if result.returncode:
            raise AssertionError(f'AR/ARQ compile failed; {cls.work}\n{result.stderr}')
        cls.node = cls.node_from_config(SDK)
        if source_hashes() != cls.before:
            raise AssertionError('Pinned sources changed during compile')

    @classmethod
    def tearDownClass(cls):
        after = source_hashes()
        if after != cls.before:
            raise AssertionError('Pinned sources changed during execution')
        (cls.work/'receipt.json').write_text(json.dumps({
            'scope': 'Original ARInit and ARQ with declared modeled services; no live AX/Synth/session claim',
            'source_hashes_before': cls.before, 'source_hashes_after': after,
            'wasm_sha256': sha(cls.output.with_suffix('.wasm')),
            'generated_ar_sha256': sha(cls.work/'ar_compat.c'),
            'generated_provider_sha256': sha(cls.work/'services_generated.cpp'),
        }, indent=2)+'\n')

    def assert_mode_passes(self, mode):
        run = retained_run([str(self.node), str(self.output), mode],
                           self.work, mode, 10)
        self.assertEqual(run.returncode, 0, f'{self.work}\n{run.stdout}\n{run.stderr}')
        self.assertEqual(run.stdout.strip(), 'source audio AR service: passed')

    def test_original_ar_init_then_deferred_arq_transfer(self):
        self.assert_mode_passes('valid')

    def test_submission_does_not_copy_inline(self):
        self.assert_mode_passes('no-inline')

    def test_cpu_masked_pump_preserves_request_for_recovery(self):
        self.assert_mode_passes('cpu-masked-pump')

    def test_invalid_ownership_and_phase_fail_closed(self):
        cases = {
            'missing-publication': 'main-memory span is outside the owned cache spans',
            'stale-probe-span': 'main-memory span is outside the owned cache spans',
            'irq-masked-pump': 'AR interrupt was raised while its source mask was set',
            'bad-span': 'main-memory span is not an owned aligned source span',
            'bad-mode': 'AR phase is unsupported',
            'recycled-span': 'AR DMA borrowed span lifetime changed before completion',
            'absent-deferred': 'deferred AR DMA is outside physical ARAM',
            'reinitialize': 'AR service reinitialized before shutdown',
            'unknown': 'unknown source audio AR fixture mode',
        }
        for mode, diagnostic in cases.items():
            with self.subTest(mode=mode):
                run = retained_run([str(self.node), str(self.output), mode],
                                   self.work, mode, 10)
                self.assertNotEqual(run.returncode, 0, f'{mode} unexpectedly passed')
                self.assertIn(diagnostic, run.stderr, f'{self.work}\n{run.stderr}')

    @staticmethod
    def node_from_config(sdk):
        for statement in ast.parse((sdk/'.emscripten').read_text()).body:
            if not isinstance(statement, ast.Assign):
                continue
            if not any(isinstance(t, ast.Name) and t.id == 'NODE_JS' for t in statement.targets):
                continue
            value = ast.literal_eval(statement.value)
            if not isinstance(value, str):
                raise AssertionError('SDK Node configuration must name a path')
            node = Path(value.replace('$CFGDIR', str(sdk))).resolve()
            if not node.is_file() or not node.is_relative_to(sdk.resolve()):
                raise AssertionError('Pinned Node is unavailable or outside SDK')
            return node
        raise AssertionError('Pinned Emscripten config lacks NODE_JS')

if __name__ == '__main__':
    unittest.main()
