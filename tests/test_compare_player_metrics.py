import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from compare_player_metrics import SCHEMA, compare  # noqa: E402


def metrics(cold_css=20000, warm_css=8000, longest=900, heap=512 << 20):
    attempt = {"longtask_count": 3, "longtask_total_ms": 1500, "longest_task_ms": longest,
               "wasm_heap_bytes": heap, "js_heap_used_bytes": 64 << 20}
    return {"schema": SCHEMA, "page_to_import_ready_ms": 1200, "attempts": [
        {"attempt": 1, "cache": "cold", "disc_to_css_ms": cold_css, **attempt},
        {"attempt": 2, "cache": "warm", "disc_to_css_ms": warm_css, **attempt}]}


class ComparePlayerMetricsTests(unittest.TestCase):
    def test_noise_inside_slack_is_not_a_regression(self):
        _, regressions = compare(metrics(), metrics(cold_css=20400, longest=950))
        self.assertEqual(regressions, [])

    def test_growth_beyond_both_slacks_is_flagged(self):
        _, regressions = compare(metrics(), metrics(cold_css=26000, longest=1500, heap=640 << 20))
        self.assertEqual(regressions, ["cold disc → CSS (ms)", "longest task, cold import (ms)",
                                       "wasm heap at CSS (MiB)"])

    def test_improvement_is_not_flagged(self):
        _, regressions = compare(metrics(), metrics(cold_css=10000, warm_css=4000, longest=200))
        self.assertEqual(regressions, [])

    def test_cli_rejects_failed_or_metricless_reports(self):
        with tempfile.TemporaryDirectory() as temporary:
            good, failed = Path(temporary, "good.json"), Path(temporary, "failed.json")
            good.write_text(json.dumps({"result": "pass", "player_metrics": metrics()}))
            failed.write_text(json.dumps({"result": "fail", "player_metrics": metrics()}))
            script = str(ROOT / "scripts/compare_player_metrics.py")
            ok = subprocess.run([sys.executable, script, str(good), str(good)], capture_output=True, text=True)
            self.assertEqual(ok.returncode, 0, ok.stderr)
            self.assertIn("| cold disc → CSS (ms) | 20000 | 20000 | +0 | ok |", ok.stdout)
            bad = subprocess.run([sys.executable, script, str(good), str(failed)], capture_output=True, text=True)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn("compare only passing runs", bad.stderr)


if __name__ == "__main__":
    unittest.main()
