"""Run original AXInit through checked source AI/DSP startup services."""
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
sys.path.insert(0, str(ROOT / 'tests'))
import source_ax_startup_profile as ax_profile

SDK = ROOT / '.deps/emsdk'
MELEE = ROOT / '.deps/melee'
EMXX = SDK / 'upstream/emscripten/em++.py'
AXROOT = MELEE / 'extern/dolphin/src/dolphin/ax'
DSPROOT = MELEE / 'extern/dolphin/src/dolphin/dsp'
AIROOT = MELEE / 'extern/dolphin/src/dolphin/ai'
OSINT = MELEE / 'extern/dolphin/src/dolphin/os/OSInterrupt.c'
AI_PATCH = ROOT / 'patches/source-ai-callback-stack.patch'
PINNED = {
    AIROOT / 'ai.c': 'cb8508338aa0c4b10b9134f2f51936618929fe8f63515e137a5ff2f2d4fcc0d9',
    OSINT: '69ba045af10aaede13f77fa1040af7fa6e79658e2d317bcee1afc5992d3fca7e',
    DSPROOT / 'dsp.c': '29a6864fb05a78a27e063f305844e2cf4f03af22f724863a14fbcff40ac18aa5',
    DSPROOT / 'dsp_task.c': '7b212710530cc6617d15e826c2de56caa59fb6e4bc702730812f50c2fb13964f',
    **{AXROOT / name: digest for name, digest in ax_profile.EXPECTED_SOURCE_SHA256.items()},
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes() -> dict[str, str]:
    actual = {str(path.relative_to(MELEE)): sha256(path) for path in PINNED}
    expected = {str(path.relative_to(MELEE)): digest for path, digest in PINNED.items()}
    if actual != expected:
        raise AssertionError('Pinned source identity changed: ' + json.dumps(actual, sort_keys=True))
    return actual


def environment() -> dict[str, str]:
    return dict(os.environ, EMSDK=str(SDK), EM_CONFIG=str(SDK / '.emscripten'),
                EM_CACHE=str(SDK / 'upstream/emscripten/cache'), EMSDK_PYTHON=sys.executable)


def configured_node() -> Path:
    for statement in ast.parse((SDK / '.emscripten').read_text()).body:
        if isinstance(statement, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == 'NODE_JS'
                for target in statement.targets):
            value = ast.literal_eval(statement.value)
            if not isinstance(value, str):
                raise AssertionError('Pinned Node setting must be a string')
            node = Path(value.replace('$CFGDIR', str(SDK))).resolve()
            if not node.is_relative_to(SDK.resolve()) or not node.is_file():
                raise AssertionError('Pinned Node must be an existing SDK file')
            return node
    raise AssertionError('Pinned SDK has no Node setting')


def retained_run(command: list[str], work: Path, label: str,
                 timeout: int = 60) -> subprocess.CompletedProcess[str]:
    (work / f'{label}.command.json').write_text(json.dumps(command, indent=2) + '\n')
    try:
        result = subprocess.run(command, cwd=ROOT, env=environment(), capture_output=True,
                                text=True, timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        for stream in ('stdout', 'stderr'):
            value = getattr(exc, stream) or ''
            if isinstance(value, bytes):
                value = value.decode(errors='replace')
            (work / f'{label}.{stream}').write_text(value)
        (work / f'{label}.returncode').write_text('timeout\n')
        raise AssertionError(f'{label} exceeded {timeout}s; evidence {work}') from exc
    (work / f'{label}.stdout').write_text(result.stdout)
    (work / f'{label}.stderr').write_text(result.stderr)
    (work / f'{label}.returncode').write_text(f'{result.returncode}\n')
    return result


def interrupt_source(source: str) -> str:
    start = source.index('static u32 SetInterruptMask')
    end = source.index('\nOSInterruptMask OSGetInterruptMask', start)
    unmask = source.index('OSInterruptMask __OSUnmaskInterrupts', end)
    dispatch = source.index('\nvoid __OSDispatchInterrupt', unmask)
    return source[start:end] + '\n\n' + source[unmask:dispatch]


def compile_fixture() -> tuple[Path, Path, dict]:
    # This builds all nine original AX units afresh, with source identity and
    # explained patch checks. Missing SDK is an optional dependency skip;
    # any present-source mismatch or compiler failure is a test failure.
    profile = ax_profile.build_profile()
    before = source_hashes()
    work = Path(tempfile.mkdtemp(prefix='source-ax-startup-services-', dir=ROOT / 'work'))
    (work / 'profile-receipt.json').write_text(json.dumps(profile, indent=2) + '\n')
    (work / 'source-hashes-before.json').write_text(json.dumps(before, indent=2) + '\n')
    (work / 'ai.c').write_bytes((AIROOT / 'ai.c').read_bytes())
    for label, options in [('ai-patch-check', ['--check']), ('ai-patch-apply', [])]:
        command = ['git', 'apply', *options, '--unsafe-paths', '--directory', str(work), str(AI_PATCH)]
        result = retained_run(command, work, label)
        if result.returncode:
            raise AssertionError(f'{label} failed; evidence {work}\n{result.stderr}')
    replacements = {
        '/* MELEE_WEB_PINNED_OS_INTERRUPT */': interrupt_source(OSINT.read_text()),
        '/* MELEE_WEB_PINNED_AI_SOURCE */': (work / 'ai.c').read_text(),
        '/* MELEE_WEB_PINNED_DSP_TASK_SOURCE */': (DSPROOT / 'dsp_task.c').read_text(),
        '/* MELEE_WEB_PINNED_DSP_SOURCE */': (DSPROOT / 'dsp.c').read_text(),
    }
    generated = (ROOT / 'tests/source_ax_startup_services.cpp').read_text()
    for marker, source in replacements.items():
        if generated.count(marker) != 1:
            raise AssertionError(f'Expected exactly one source insertion marker: {marker}')
        generated = generated.replace(marker, source)
    source_path = work / 'source_ax_startup_services_generated.cpp'
    source_path.write_text(generated)
    output = work / 'source_ax_startup_services.js'
    includes = [MELEE / 'extern/dolphin/include', MELEE / 'extern/dolphin/src/dolphin/gx',
                MELEE / 'src', MELEE / 'include', AXROOT, DSPROOT, ROOT / 'tests']
    command = [sys.executable, str(EMXX), '-std=gnu++17', '-O0', '-ffp-contract=off',
               '-DDEBUG=1', '-Wall', '-Wextra', '-Werror', '-Wno-writable-strings',
               '-Wno-array-parameter', '-Wno-unused-parameter', '-Wno-unused-variable',
               '-Wno-unused-function', '-Wno-unused-but-set-variable', '-sENVIRONMENT=node',
               '-sEXIT_RUNTIME=1', '-sASSERTIONS=2', '-sSAFE_HEAP=1', '-sSTACK_OVERFLOW_CHECK=2',
               *[f'-I{path}' for path in includes], str(source_path),
               *[row['path'] for row in profile['objects'].values()], '-o', str(output)]
    result = retained_run(command, work, 'compile', 180)
    if result.returncode:
        raise AssertionError(f'AX source service compile failed; evidence {work}\n{result.stderr}')
    if source_hashes() != before:
        raise AssertionError('Pinned source changed during compilation')
    return output, work, before


class SourceAxStartupServicesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.output, cls.work, cls.before = compile_fixture()
        cls.node = configured_node()

    @classmethod
    def tearDownClass(cls):
        after = source_hashes()
        if after != cls.before:
            raise AssertionError('Pinned source changed during execution')
        (cls.work / 'receipt.json').write_text(json.dumps({
            'kind': 'source_ax_startup_checked_fixture',
            'source_hashes_before': cls.before, 'source_hashes_after': after,
            'generated_cpp_sha256': sha256(cls.work / 'source_ax_startup_services_generated.cpp'),
            'wasm_sha256': sha256(cls.output.with_suffix('.wasm')),
            'ai_patch_sha256': sha256(AI_PATCH),
            'profile_receipt': 'profile-receipt.json',
            'scope': 'Original AX startup with checked modeled AI/DSP services; no firmware, PCM or timing claim.',
        }, indent=2) + '\n')

    def run_node(self, *args):
        mode = args[0] if args else 'valid'
        return retained_run([str(self.node), str(self.output), *args], self.work, f'run-{mode}', 10)

    def test_source_ax_reaches_first_ai_dma(self):
        result = self.run_node()
        self.assertEqual(result.returncode, 0, result.stdout + '\n' + result.stderr)
        row = json.loads(result.stdout.strip().splitlines()[-1])
        self.assertEqual(row['boundary'], 'first_ai_dma_enabled')
        self.assertTrue(row['ai_dma_enabled'])
        self.assertEqual(row['ai_dma_bytes'], 0x280)
        self.assertEqual(row['dsp_boot_mails'], 10)
        self.assertEqual(row['dsp_init_callback'], 1)
        self.assertFalse(row['runtime_claim'])

    def test_unknown_mode_rejected(self):
        result = self.run_node('unknown')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unknown AX startup mode', result.stderr)

    def test_actual_negative_contracts_reject(self):
        for mode, message in (
                ('bad-dma', 'AI DMA points outside AX output buffer'),
                ('bad-image', 'AX DSP image hash mismatch'),
                ('bad-mail', 'DSP boot mail mismatch'),
                ('masked-pump', 'DSP interrupt dispatched without masked interrupt owner'),
                ('bad-task-callback', 'DSP task callback identity does not match AXOut source'),
                ('bad-task-span', 'DSP task IRAM span does not match source image'),
                ('global-masked-pump', 'DSP interrupt dispatched while globally or locally masked'),
                ('local-masked-pump', 'DSP interrupt dispatched while globally or locally masked'),
                ('status-masked-pump', 'DSP interrupt pending without enabled DSP status bits'),
                ('missing-cache', 'AX output buffer was not published before DMA')):
            with self.subTest(mode=mode):
                result = self.run_node(mode)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)


if __name__ == '__main__':
    unittest.main()
