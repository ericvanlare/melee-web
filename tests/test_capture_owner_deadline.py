import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from capture_owner_deadline import prepare_output_parents


class CaptureOwnerOutputTests(unittest.TestCase):
    def test_browser_report_parent_is_created_by_capture_runner(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            browser_output = root / "run" / "browser"
            browser_report = browser_output / "report.json"
            owner_report = root / "run" / "owner.json"
            process_trace = root / "run" / "processes.jsonl"
            server_log = root / "run" / "server.log"

            prepare_output_parents(
                [owner_report, browser_report, process_trace, server_log],
                browser_report,
            )

            self.assertTrue(owner_report.parent.is_dir())
            self.assertFalse(browser_output.exists())
            browser_output.mkdir()
            browser_report.write_text("{}\n")
            self.assertTrue(browser_report.is_file())


if __name__ == "__main__":
    unittest.main()
