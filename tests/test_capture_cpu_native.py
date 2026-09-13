"""Focused checks for the native CPU trace capture boundary."""

from __future__ import annotations

import json
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/capture_cpu_native.py"


class NativeCpuCaptureTests(unittest.TestCase):
    def test_successful_process_cannot_change_an_owned_asset(self):
        with tempfile.TemporaryDirectory(prefix="cpu-native-assets-") as directory:
            root = Path(directory)
            for name in ('build', 'menu', 'game'):
                (root / name).mkdir()
            for name in ('gameplay_retail_trace.js', 'gameplay_retail_trace.wasm'):
                (root / 'build' / name).write_bytes(b'synthetic executable')
            (root / 'recipe').write_bytes(b'synthetic recipe')
            (root / 'game' / 'owned.dat').write_bytes(b'original asset')
            fake_node = root / 'fake-node'
            fake_node.write_text('#!/usr/bin/env python3\nimport pathlib, sys\n'
                "pathlib.Path(sys.argv[3]).joinpath('owned.dat').write_bytes(b'changed asset')\n")
            fake_node.chmod(0o755)
            run = subprocess.run([sys.executable, str(SCRIPT), '--build-directory', str(root / 'build'),
                '--menu-assets', str(root / 'menu'), '--game-assets', str(root / 'game'),
                '--recipe', str(root / 'recipe'), '--output', str(root / 'capture'),
                '--node', str(fake_node)], capture_output=True, text=True)
            self.assertNotEqual(run.returncode, 0)
            result = json.loads((root / 'capture' / 'capture-result.json').read_text())
            self.assertEqual(result['returncode'], 0)
            self.assertFalse(result['full_completion'])
            self.assertTrue(result['artifact_change'])
            self.assertNotEqual(result['runtime_inputs_before'], result['runtime_inputs_after'])
            self.assertEqual(result['runtime_inputs_before']['node']['sha256'],
                             hashlib.sha256(fake_node.read_bytes()).hexdigest())

    def test_failed_process_keeps_partial_outputs_and_rejects_reuse(self):
        with tempfile.TemporaryDirectory(prefix="cpu-native-capture-") as directory:
            root = Path(directory)
            build = root / "build"
            menu = root / "menu"
            game = root / "game"
            build.mkdir()
            menu.mkdir()
            game.mkdir()
            trace = build / "gameplay_retail_trace.js"
            wasm = build / "gameplay_retail_trace.wasm"
            trace.write_bytes(b"synthetic trace executable")
            wasm.write_bytes(b"original wasm")
            recipe = root / "recipe.mwrc"
            recipe.write_bytes(b"synthetic recipe")
            fake_node = root / "fake-node"
            fake_node.write_text(
                "#!/usr/bin/env python3\n"
                "import pathlib, sys\n"
                "if '--require-match-complete' not in sys.argv or '--cpu-hitlag-diagnostic' not in sys.argv: raise SystemExit(8)\n"
                "pathlib.Path(sys.argv[1]).parent.joinpath('gameplay_retail_trace.wasm').write_bytes(b'changed wasm')\n"
                "print('{\\\"record\\\":\\\"core\\\"}')\n"
                "print('CPU_AUDIT {\\\"record\\\":\\\"frame\\\"}', file=sys.stderr)\n"
                "print('HITLAG_AUDIT tick=3 slot=1', file=sys.stderr)\n"
                "raise SystemExit(7)\n",
                encoding="utf-8",
            )
            fake_node.chmod(fake_node.stat().st_mode | 0o111)
            output = root / "capture"
            command = [
                sys.executable, str(SCRIPT),
                "--build-directory", str(build),
                "--menu-assets", str(menu),
                "--game-assets", str(game),
                "--recipe", str(recipe),
                "--output", str(output),
                "--node", str(fake_node),
                "--cpu-hitlag-diagnostic",
            ]
            run = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertNotEqual(run.returncode, 0, run.stdout + run.stderr)

            self.assertEqual((output / "trace.jsonl").read_text(),
                             '{"record":"core"}\n')
            self.assertIn("CPU_AUDIT", (output / "stderr.log").read_text())
            self.assertEqual((output / "cpu-observation.jsonl").read_text(),
                             '{"record":"frame"}\n')
            self.assertEqual((output / "hitlag-audit.log").read_text(),
                             "HITLAG_AUDIT tick=3 slot=1\n")
            result = json.loads((output / "capture-result.json").read_text())
            self.assertEqual(result["status"], "failed")
            self.assertEqual(result["returncode"], 7)
            self.assertTrue(result["artifact_change"])
            self.assertNotEqual(result["artifacts_before"]["gameplay_retail_trace.wasm"],
                                result["artifacts_after"]["gameplay_retail_trace.wasm"])

            retained = {
                path.relative_to(output): path.read_bytes()
                for path in output.rglob("*") if path.is_file()
            }
            reused = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertNotEqual(reused.returncode, 0)
            self.assertEqual(retained, {
                path.relative_to(output): path.read_bytes()
                for path in output.rglob("*") if path.is_file()
            })

    def test_timeout_keeps_partial_streams_and_output_hashes(self):
        with tempfile.TemporaryDirectory(prefix="cpu-native-timeout-") as directory:
            root = Path(directory)
            build = root / "build"
            menu = root / "menu"
            game = root / "game"
            build.mkdir()
            menu.mkdir()
            game.mkdir()
            (build / "gameplay_retail_trace.js").write_bytes(b"synthetic trace executable")
            (build / "gameplay_retail_trace.wasm").write_bytes(b"synthetic wasm")
            recipe = root / "recipe.mwrc"
            recipe.write_bytes(b"synthetic recipe")
            fake_node = root / "fake-node"
            fake_node.write_text(
                "#!/usr/bin/env python3\n"
                "import sys, time\n"
                "print('partial core', flush=True)\n"
                "print('CPU_AUDIT {\\\"record\\\":\\\"partial\\\"}', file=sys.stderr, flush=True)\n"
                "time.sleep(2)\n",
                encoding="utf-8",
            )
            fake_node.chmod(fake_node.stat().st_mode | 0o111)
            output = root / "capture"
            run = subprocess.run([
                sys.executable, str(SCRIPT),
                "--build-directory", str(build),
                "--menu-assets", str(menu),
                "--game-assets", str(game),
                "--recipe", str(recipe),
                "--output", str(output),
                "--node", str(fake_node),
                "--timeout", "1.0",
            ], capture_output=True, text=True, check=False)
            self.assertNotEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual((output / "trace.jsonl").read_text(), "partial core\n")
            self.assertEqual((output / "cpu-observation.jsonl").read_text(),
                             '{"record":"partial"}\n')
            result = json.loads((output / "capture-result.json").read_text())
            self.assertEqual(result["status"], "failed")
            self.assertIsNone(result["returncode"])
            self.assertTrue(result["timed_out"])
            self.assertEqual(result["outputs"]["trace.jsonl"],
                             hashlib.sha256((output / "trace.jsonl").read_bytes()).hexdigest())
            self.assertEqual(len(result["capture_config_sha256"]), 64)


if __name__ == "__main__":
    unittest.main()
