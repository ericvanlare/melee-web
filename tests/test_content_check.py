"""The porting command must build before execution and retain the first failure."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import check_content as content


class ContentCheckTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / "assets"
        self.menu = self.root / "menus"
        self.game.mkdir()
        self.menu.mkdir()
        (self.game / "fighter.dat").write_bytes(b"owned test fixture")
        (self.menu / "menu.usd").write_bytes(b"menu fixture")
        (self.root / "dependencies.lock.json").write_text('{"fixture": true}')
        self.node = self.root / "node"
        self.node.write_bytes(b"test node")
        self.manifest = self.root / "content.json"
        self.out = self.root / "work/result"
        self.spec = {
            "checks": [{"path": "assets/fighter.dat", "symbol": "fighter_root"}],
            "lifecycles": [{"target": content.MATCH, "menus": "menus", "assets": "assets",
                            "stkind": 32, "p1_ckind": 20, "p2_ckind": 8},
                           {"target": content.BATTLEFIELD, "assets": "assets"}],
        }
        self.manifest.write_text(json.dumps(self.spec))
        self.calls = []

    def fake_run(self, command, *, stdout, **kwargs):
        self.calls.append(command)
        if "--trace-target" in command:
            build_dir = content.build_directory(self.root, configuration="Release")
            build_dir.mkdir(parents=True, exist_ok=True)
            for target in (content.MATCH, content.BATTLEFIELD):
                for suffix in (".js", ".wasm"):
                    (build_dir / (target + suffix)).write_bytes(b"fresh selected build")
        elif command[0] == str(self.node):
            marker = ("Original Battlefield" if content.BATTLEFIELD in command[1]
                      else "Mixed source content intro, costumes, stage lifecycle, combat, pause and repeat teardown passed")
            stdout.write(marker.encode())
        return subprocess.CompletedProcess(command, 0)

    def invoke(self, side_effect=None, node_error=None):
        with (patch.object(content.subprocess, "run", side_effect=side_effect or self.fake_run),
              patch.object(content.subprocess, "check_output", side_effect=lambda args, **kw:
                           "fixture-head\n" if kw.get("text") else b"fixture-diff"),
              patch.object(content, "node_runtime", return_value=self.node, side_effect=node_error),
              contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO())):
            status = content.validate(self.manifest, self.out, configuration="Release", jobs=2, root=self.root)
        return status, json.loads((self.out / "report.json").read_text())

    def test_explicit_build_then_both_orientations_and_stage_with_bound_inputs(self):
        # A newer stale binary in the other configuration must never be chosen.
        stale = self.root / "build/browser" / (content.MATCH + ".js")
        stale.parent.mkdir(parents=True)
        stale.write_text("stale")
        status, report = self.invoke()
        self.assertEqual(status, 0)
        self.assertEqual([step["id"] for step in report["steps"]], [
            "assets", "build", "lifecycle-00-p20-p8", "lifecycle-00-p8-p20", "lifecycle-01-battlefield"])
        build = report["steps"][1]
        self.assertEqual(build["command"][1], str(self.root / "scripts/build.py"))
        self.assertEqual([build["command"][i + 1] for i, arg in enumerate(build["command"])
                          if arg == "--trace-target"], sorted([content.MATCH, content.BATTLEFIELD]))
        self.assertEqual(report["steps"][2]["command"][-3:], ["32", "20", "8"])
        self.assertEqual(report["steps"][3]["command"][-3:], ["32", "8", "20"])
        self.assertIn("/build/browser-release/", report["steps"][2]["command"][1])
        self.assertEqual(len(report["inputs"]), 2)
        self.assertEqual(len(report["build"]), 4)
        self.assertEqual(report["not_run"], [])
        self.assertIn("cold/warm live timing", report["unverified"])

    def test_stage_lifecycle_without_parser_rows_builds_and_reports_omitted_scope(self):
        self.spec["checks"] = []
        self.spec["lifecycles"] = [self.spec["lifecycles"][1]]
        self.manifest.write_text(json.dumps(self.spec))
        status, report = self.invoke()
        self.assertEqual(status, 0)
        self.assertEqual([step["id"] for step in report["steps"]], ["build", "lifecycle-00-battlefield"])
        self.assertIn("asset parser checks (checks: [])", report["unverified"])
        self.assertEqual(len(report["inputs"]), 1)
        self.assertFalse((self.out / "assets.json").exists())
        self.assertEqual(report["steps"][0]["command"][-2:], ["--trace-target", content.BATTLEFIELD])

    def test_lifecycle_only_still_rejects_missing_inputs_before_build(self):
        self.spec["checks"] = []
        self.spec["lifecycles"][0]["assets"] = "missing-assets"
        self.manifest.write_text(json.dumps(self.spec))
        status, report = self.invoke()
        self.assertEqual(status, 1)
        self.assertEqual(self.calls, [])
        self.assertEqual(report["first_failure"]["boundary"], "inputs")

    def test_empty_checks_must_be_explicit_and_lifecycles_cannot_be_empty(self):
        for checks in (None, {}, "", False):
            self.spec["checks"] = checks
            self.manifest.write_text(json.dumps(self.spec))
            with self.subTest(checks=checks), self.assertRaises(ValueError):
                content.load_manifest(self.manifest)
        self.spec["checks"] = []
        self.spec["lifecycles"] = []
        self.manifest.write_text(json.dumps(self.spec))
        with self.assertRaisesRegex(ValueError, "lifecycles must be a nonempty array"):
            content.load_manifest(self.manifest)

    def test_missing_input_fails_before_any_build_and_is_not_a_skip(self):
        (self.game / "fighter.dat").unlink()
        status, report = self.invoke()
        self.assertEqual(status, 1)
        self.assertEqual(self.calls, [])
        self.assertEqual(report["first_failure"]["boundary"], "inputs")
        self.assertIn("fighter.dat", report["first_failure"]["message"])

    def test_asset_failure_stops_before_build_with_diagnostic_and_pending_runs(self):
        def fail(command, *, stdout, **kwargs):
            self.calls.append(command)
            stdout.write(b'{"root":"fighter_root","status":"rejected","reason":"Unsupported command 12"}\n')
            return subprocess.CompletedProcess(command, 1)
        status, report = self.invoke(fail)
        self.assertEqual(status, 1)
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(report["first_failure"]["boundary"], "assets")
        self.assertIn("Unsupported command 12", report["first_failure"]["diagnostic_tail"])
        self.assertEqual(report["first_failure"]["asset"]["root"], "fighter_root")
        self.assertEqual(len(report["not_run"]), 3)

    def test_build_failure_cannot_run_preexisting_binary(self):
        def fail(command, **kwargs):
            result = self.fake_run(command, **kwargs)
            if "--trace-target" in command:
                kwargs["stdout"].write(b"link: missing original service\n")
                return subprocess.CompletedProcess(command, 1)
            return result
        status, report = self.invoke(fail)
        self.assertEqual(status, 1)
        self.assertEqual(len(self.calls), 2)
        self.assertEqual(report["first_failure"]["boundary"], "build")

    def test_lifecycle_failure_retains_log_and_does_not_run_remaining_rows(self):
        def fail(command, **kwargs):
            if command[0] == str(self.node):
                self.calls.append(command)
                kwargs["stdout"].write(b"Construct mixed content stage=32 costume=0\nMissing Article dependency\n")
                return subprocess.CompletedProcess(command, 1)
            return self.fake_run(command, **kwargs)
        status, report = self.invoke(fail)
        self.assertEqual(status, 1)
        self.assertEqual(len(self.calls), 3)
        self.assertIn("Missing Article", report["first_failure"]["diagnostic_tail"])
        self.assertEqual(report["not_run"], ["lifecycle-00-p8-p20", "lifecycle-01-battlefield"])

    def test_zero_exit_without_completion_marker_is_a_failure(self):
        def silent(command, **kwargs):
            if command[0] == str(self.node):
                return subprocess.CompletedProcess(command, 0)
            return self.fake_run(command, **kwargs)
        status, report = self.invoke(silent)
        self.assertEqual(status, 1)
        self.assertIn("completion marker", report["first_failure"]["message"])

    def test_timeout_retains_partial_diagnostic_and_stops(self):
        def timeout(command, **kwargs):
            kwargs["stdout"].write(b"preparation checkpoint before timeout\n")
            raise subprocess.TimeoutExpired(command, 600)
        status, report = self.invoke(timeout)
        self.assertEqual(status, 1)
        self.assertEqual(len(report["steps"]), 1)
        self.assertIn("checkpoint before timeout", report["first_failure"]["diagnostic_tail"])

    def test_modified_input_does_not_produce_a_pass(self):
        def mutate(command, **kwargs):
            result = self.fake_run(command, **kwargs)
            if command[0] == str(self.node):
                (self.game / "fighter.dat").write_bytes(b"changed during run")
            return result
        status, report = self.invoke(mutate)
        self.assertEqual(status, 1)
        self.assertEqual(report["first_failure"]["boundary"], "input-integrity")
        change = report["input_changes"][0]
        self.assertEqual(Path(change["path"]).name, "fighter.dat")
        self.assertNotEqual(change["before"]["sha256"], change["after"]["sha256"])

    def test_duplicate_manifest_keys_cannot_silently_change_selected_content(self):
        for raw in ['{"checks":[],"checks":[],"lifecycles":[]}',
                    json.dumps(self.spec).replace('"stkind": 32', '"stkind": 8, "stkind": 32')]:
            self.manifest.write_text(raw)
            with self.assertRaisesRegex(ValueError, "duplicate JSON key"):
                content.load_manifest(self.manifest)

    def test_node_failure_is_reported_before_any_compilation(self):
        status, report = self.invoke(node_error=ValueError("Project Emscripten version differs"))
        self.assertEqual(status, 1)
        self.assertEqual(self.calls, [])
        self.assertEqual(report["first_failure"]["boundary"], "runtime")

    def test_missing_artifact_after_successful_build_has_its_own_boundary(self):
        def missing(command, **kwargs):
            result = self.fake_run(command, **kwargs)
            if "--trace-target" in command:
                (self.root / "build/browser-release" / (content.MATCH + ".wasm")).unlink()
            return result
        status, report = self.invoke(missing)
        self.assertEqual(status, 1)
        self.assertEqual(report["first_failure"]["boundary"], "build-artifacts")
        self.assertIn(content.MATCH + ".wasm", report["first_failure"]["message"])
        self.assertEqual(len(self.calls), 2)

    def test_unknown_trace_or_invalid_source_identity_is_rejected(self):
        for change in ({"target": "arbitrary_executable"}, {"p1_ckind": True}, {"stkind": -1}, {"args": []}):
            with self.subTest(change=change):
                value = json.loads(json.dumps(self.spec))
                value["lifecycles"][0].update(change)
                self.manifest.write_text(json.dumps(value))
                with self.assertRaises(ValueError):
                    content.load_manifest(self.manifest)

    def test_self_match_is_not_duplicated(self):
        self.spec["lifecycles"][0]["p2_ckind"] = 20
        self.manifest.write_text(json.dumps(self.spec))
        _, rows = content.load_manifest(self.manifest)
        self.assertEqual(len(content.trace_runs(rows, self.root, self.node)), 2)

    def test_existing_output_is_preserved(self):
        self.out.mkdir(parents=True)
        evidence = self.out / "report.json"
        evidence.write_text("prior failed evidence")
        with self.assertRaises(FileExistsError):
            self.invoke()
        self.assertEqual(evidence.read_text(), "prior failed evidence")


if __name__ == "__main__":
    unittest.main()
