#!/usr/bin/env python3
"""Focused static/synthetic controls for the public pause-mask worker."""
from __future__ import annotations
import os
import importlib.util
import json
import sys
import tempfile
from pathlib import Path
import subprocess
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
WORKER = ROOT / 'tools/source_pause_mask_runtime.mjs'
LAUNCHER = ROOT / 'tools/source_pause_mask_check.py'


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=True,
                          env={**os.environ, 'PYTHONDONTWRITEBYTECODE': '1'})


class SourcePauseMaskCheck(unittest.TestCase):
    def load_launcher(self):
        spec = importlib.util.spec_from_file_location('pause_checker_controls', LAUNCHER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_symlink_input_is_rejected_before_canonicalization(self):
        module = self.load_launcher()
        spec = importlib.util.spec_from_file_location('prefix_checker_controls', ROOT / 'tools/slippi_profile_prefix_check.py')
        prefix = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(prefix)
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'source.cpp'
            target.write_text('source fixture')
            link = Path(directory) / 'link.cpp'
            link.symlink_to(target)
            expected = module.sha(target)
            self.assertEqual(module.require_file(target, expected, 'source'), expected)
            with self.assertRaisesRegex(RuntimeError, 'symlinked'):
                module.require_file(link, expected, 'source')
            with self.assertRaisesRegex(prefix.CheckError, 'non-symlink'):
                prefix.checked_hash(link, expected, 'source')

    def test_changed_worker_is_refused_before_any_child_start(self):
        module = self.load_launcher()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'tools').mkdir()
            worker = root / 'tools/source_pause_mask_runtime.mjs'
            worker.write_text('original worker')
            expected_worker = module.sha(worker)
            worker.write_text('changed worker')
            source = root / 'source.cpp'
            source.write_text('source fixture')
            runtime = root / 'runtime.js'
            runtime.write_text('runtime fixture')
            wasm = runtime.with_suffix('.wasm')
            wasm.write_bytes(b'Wasm fixture')
            assets = root / 'assets'
            assets.mkdir()
            node = Path(sys.executable).resolve()
            out = root / 'evidence'
            argv = ['--source-root', str(root), '--runtime', str(runtime), '--assets', str(assets),
                    '--out', str(out), '--node', str(node), '--node-sha256', module.sha(node),
                    '--runtime-sha256', module.sha(runtime), '--wasm-sha256', module.sha(wasm),
                    '--worker-sha256', expected_worker]
            for path_flag, hash_flag in [('--source-probe', '--probe-sha256'),
                                         ('--source-cmake', '--cmake-sha256'),
                                         ('--profile-helper', '--profile-helper-sha256'),
                                         ('--stage-kind-bridge', '--stage-kind-bridge-sha256')]:
                argv.extend([path_flag, str(source), hash_flag, module.sha(source)])
            with mock.patch.object(module, 'load_supervisor') as supervisor:
                self.assertEqual(module.main(argv), 1)
                supervisor.assert_not_called()
            report = json.loads((out / 'launcher-report.json').read_text())
            self.assertEqual(report['result'], 'failed')
            self.assertIn('pause worker SHA differs', report['failure'])
            self.assertNotIn('worker', report)

    def test_prefix_assets_refuse_root_and_nested_symlinks(self):
        spec = importlib.util.spec_from_file_location('prefix_assets_controls', ROOT / 'tools/slippi_profile_prefix_check.py')
        prefix = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(prefix)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            assets = root / 'assets'
            assets.mkdir()
            asset = assets / 'owned.dat'
            asset.write_bytes(b'owned fixture')
            self.assertEqual(len(prefix.directory_sha256(assets)), 64)
            root_link = root / 'assets-link'
            root_link.symlink_to(assets, target_is_directory=True)
            with self.assertRaisesRegex(prefix.CheckError, 'directory.*symlinked'):
                prefix.directory_sha256(root_link)
            file_link = assets / 'asset-link.dat'
            file_link.symlink_to(asset)
            with self.assertRaisesRegex(prefix.CheckError, 'entry.*symlinked'):
                prefix.directory_sha256(assets)
            file_link.unlink()
            (assets / 'nested-link').symlink_to(root, target_is_directory=True)
            with self.assertRaisesRegex(prefix.CheckError, 'entry.*symlinked'):
                prefix.directory_sha256(assets)

    def test_synthetic_worker_recipe_and_pad(self):
        run(['node', '--check', str(WORKER)])
        result = run(['node', str(WORKER), '--self-test'])
        self.assertIn('fixed-124-plus-17-recipe', result.stdout)
        self.assertIn('callback-count-mutation-detected', result.stdout)
        self.assertIn('prepared-mask-mutation-detected', result.stdout)

    def test_real_owned_group_is_reaped_and_absent(self):
        spec = importlib.util.spec_from_file_location('pause_checker_test', LAUNCHER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        supervisor = module.load_supervisor(ROOT)
        with tempfile.TemporaryDirectory() as directory:
            result = module.run_group(supervisor, [sys.executable, '-c', 'raise SystemExit(0)'],
                                      ROOT, Path(directory) / 'child' / 'worker.log', 5)
        self.assertEqual(result['wait_returncode'], 0)
        self.assertTrue(result['cleanup_succeeded'])
        self.assertTrue(result['pid_absent'])
        self.assertEqual(result['cleanup']['pid'], result['pid'])


if __name__ == '__main__':
    unittest.main()
