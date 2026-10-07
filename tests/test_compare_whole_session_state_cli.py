import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "compare_whole_session_state_cli", ROOT / "scripts" / "compare_whole_session_state.py")
CLI = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(CLI)


class CompareWholeSessionStateCliTests(unittest.TestCase):
    def test_default_scope_preserves_whole_session_and_existing_arguments(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            out = root / "out.json"
            argv = ["--reference", str(root / "reference.mwro"),
                    "--recipe", str(root / "recipe.mwrc"),
                    "--port-trace", str(root / "port.jsonl"), "--out", str(out)]
            with mock.patch.object(CLI, "compare_paths", return_value={"result": "incomplete"}) as compare, \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(CLI.main(argv), 1)
            self.assertEqual(compare.call_args.kwargs["scope"], "whole-session")
            self.assertIsNone(compare.call_args.kwargs["expectations"])
            self.assertEqual(json.loads(out.read_text(encoding="utf-8")),
                             {"result": "incomplete"})

    def test_v10_scope_forwards_external_packet_and_sidecars(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = ("reference.mwro", "recipe.mwrc", "port.jsonl", "expectations.json",
                     "source-manifest.json", "source-report.json", "source-audit.json",
                     "capture-report.json", "producer-manifest.json", "out.json")
            paths = {name: root / name for name in names}
            argv = ["--reference", str(paths["reference.mwro"]),
                    "--recipe", str(paths["recipe.mwrc"]),
                    "--port-trace", str(paths["port.jsonl"]),
                    "--scope", "v10-first-setup-tick0",
                    "--expectations", str(paths["expectations.json"]),
                    "--source-manifest", str(paths["source-manifest.json"]),
                    "--source-report", str(paths["source-report.json"]),
                    "--source-audit", str(paths["source-audit.json"]),
                    "--browser-capture-report", str(paths["capture-report.json"]),
                    "--browser-producer-manifest", str(paths["producer-manifest.json"]),
                    "--out", str(paths["out.json"])]
            with mock.patch.object(CLI, "compare_paths", return_value={"result": "incomplete"}) as compare, \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(CLI.main(argv), 1)
            kwargs = compare.call_args.kwargs
            self.assertEqual(kwargs["scope"], "v10-first-setup-tick0")
            self.assertEqual(kwargs["expectations"], paths["expectations.json"])
            self.assertEqual(kwargs["source_manifest"], paths["source-manifest.json"])
            self.assertEqual(kwargs["source_report"], paths["source-report.json"])
            self.assertEqual(kwargs["source_audit"], paths["source-audit.json"])
            self.assertEqual(kwargs["browser_capture_report"], paths["capture-report.json"])
            self.assertEqual(kwargs["browser_producer_manifest"], paths["producer-manifest.json"])

    def test_first_positive_scope_requires_and_forwards_its_source_audit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = ("reference.mwro", "recipe.mwrc", "port.jsonl", "expectations.json",
                     "source-manifest.json", "source-report.json", "source-audit.json",
                     "positive-audit.json", "capture-report.json", "producer-manifest.json",
                     "out.json")
            paths = {name: root / name for name in names}
            argv = ["--reference", str(paths["reference.mwro"]),
                    "--recipe", str(paths["recipe.mwrc"]),
                    "--port-trace", str(paths["port.jsonl"]),
                    "--scope", "v10-first-positive-match-frame",
                    "--expectations", str(paths["expectations.json"]),
                    "--source-manifest", str(paths["source-manifest.json"]),
                    "--source-report", str(paths["source-report.json"]),
                    "--source-audit", str(paths["source-audit.json"]),
                    "--positive-boundary-audit", str(paths["positive-audit.json"]),
                    "--browser-capture-report", str(paths["capture-report.json"]),
                    "--browser-producer-manifest", str(paths["producer-manifest.json"]),
                    "--out", str(paths["out.json"])]
            with mock.patch.object(CLI, "compare_paths", return_value={"result": "incomplete"}) as compare, \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(CLI.main(argv), 1)
            kwargs = compare.call_args.kwargs
            self.assertEqual(kwargs["scope"], "v10-first-positive-match-frame")
            self.assertEqual(kwargs["positive_boundary_audit"], paths["positive-audit.json"])

    def test_clock60_scope_requires_and_forwards_both_source_audits(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = ("reference.mwro", "recipe.mwrc", "port.jsonl", "expectations.json",
                     "source-manifest.json", "source-report.json", "source-audit.json",
                     "positive-audit.json", "clock60-audit.json", "capture-report.json",
                     "producer-manifest.json", "out.json")
            paths = {name: root / name for name in names}
            argv = ["--reference", str(paths["reference.mwro"]),
                    "--recipe", str(paths["recipe.mwrc"]),
                    "--port-trace", str(paths["port.jsonl"]),
                    "--scope", "v10-first-match-clock-ge60",
                    "--expectations", str(paths["expectations.json"]),
                    "--source-manifest", str(paths["source-manifest.json"]),
                    "--source-report", str(paths["source-report.json"]),
                    "--source-audit", str(paths["source-audit.json"]),
                    "--positive-boundary-audit", str(paths["positive-audit.json"]),
                    "--clock60-boundary-audit", str(paths["clock60-audit.json"]),
                    "--browser-capture-report", str(paths["capture-report.json"]),
                    "--browser-producer-manifest", str(paths["producer-manifest.json"]),
                    "--out", str(paths["out.json"])]
            with mock.patch.object(CLI, "compare_paths", return_value={"result": "incomplete"}) as compare, \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(CLI.main(argv), 1)
            kwargs = compare.call_args.kwargs
            self.assertEqual(kwargs["scope"], "v10-first-match-clock-ge60")
            self.assertEqual(kwargs["positive_boundary_audit"], paths["positive-audit.json"])
            self.assertEqual(kwargs["clock60_boundary_audit"], paths["clock60-audit.json"])


if __name__ == "__main__":
    unittest.main()
