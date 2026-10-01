"""Run the compiled source-file bridge against mocked browser stream hooks."""

from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class GameplaySourceFilesTests(unittest.TestCase):
    def test_compiled_emscripten_stream_bridge(self):
        candidates = [
            ROOT / "build" / name / "gameplay_source_files_trace.js"
            for name in ("browser", "browser-release", "browser-public-release",
                         "browser-audio-preview-release")
        ]
        targets = [path for path in candidates if path.is_file()]
        if not targets:
            self.skipTest("Build the Emscripten source-file bridge trace first")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        run = subprocess.run([str(node_runtime()), str(target)], cwd=ROOT,
                             capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "source RuntimeFiles exact DVD reads, ownership, cancellation and lifecycle trace: passed",
            run.stdout,
        )


if __name__ == "__main__":
    unittest.main()
