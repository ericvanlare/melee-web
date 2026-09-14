"""Adversarial lifecycle tests for the reference-session inbox.

These fixtures contain only synthetic typed metadata.  They deliberately do
not contain a game image, private observer payload, or a path from a user's
machine; a synthetic run is useful for testing state transitions but can never
be accepted without the declared lifecycle and semantic checks.
"""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from reference_session_bundle import (  # noqa: E402
    ACCEPTED_UNPROCESSED,
    ACTIVE_PARTIALS,
    BundleExistsError,
    BundleValidationError,
    DERIVED,
    INGESTED,
    ReferenceCaptureInbox,
    ReferenceSessionBundle,
    RAW_OBSERVER_NAME,
    UnsafePathError,
    validate_bundle,
)


def append_lifecycle(bundle: ReferenceSessionBundle, *, payload=None, start=1):
    events = ("entry", "gameplay", "result", "teardown", "observer_end")
    for seq, event in enumerate(events, start):
        bundle.append(
            {
                "seq": seq,
                "event": event,
                "source_tick": seq if event == "gameplay" else None,
                "payload": payload if payload is not None else {"observed": event},
            }
        )


def semantic_report():
    return {
        "complete": True,
        "source_ticks": 1,
        "source_draws": 1,
        "pad_polls": 1,
        "result": {"outcome": "synthetic-test-only"},
        "missing_coverage": [],
    }


class ReferenceSessionBundleTests(unittest.TestCase):
    def test_discovery_associates_nested_derivations_by_session_and_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            inbox = ReferenceCaptureInbox(directory)
            bundle = inbox.begin("retail-a", "GALE01r2", "run-a")
            append_lifecycle(bundle)
            final = bundle.complete(semantic_validator=lambda *_: None)
            binding = json.loads((final / "manifest.json").read_text())["manifest_sha256"]
            for revision, source_hash in (("v1", binding), ("wrong-source", "a" * 64)):
                output = Path(directory) / DERIVED / revision / "retail-a"
                output.mkdir(parents=True)
                (output / "derived-manifest.json").write_text(json.dumps({
                    "session_id": "retail-a", "manifest_sha256": "b" * 64,
                    "source": {"manifest_sha256": source_hash}}))
            (Path(directory) / DERIVED / "outside").symlink_to(Path(directory), target_is_directory=True)
            rows = inbox.list_runs()
            raw = next(row for row in rows if row["state"] == ACCEPTED_UNPROCESSED)
            self.assertEqual([row["path"] for row in raw["derived_outputs"]], ["derived/v1/retail-a"])
            self.assertEqual(len([row for row in rows if row["state"] == DERIVED]), 2)

    def test_begin_creates_unique_partial_and_preserves_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            inbox = ReferenceCaptureInbox(directory)
            bundle = inbox.begin("retail-a", "GALE01r2", "run-a")
            self.assertTrue(bundle.path.name.endswith(".partial"))
            self.assertTrue((Path(directory) / ACTIVE_PARTIALS / "retail-a.partial").is_dir())
            with self.assertRaises(BundleExistsError):
                inbox.begin("retail-a", "GALE01r2", "run-b")
            bundle.close()

    def test_complete_requires_every_phase_and_writes_raw_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = ReferenceSessionBundle.begin(directory, "retail-a", "GALE01r2", "run-a")
            append_lifecycle(bundle)
            accepted = bundle.complete(semantic_report=semantic_report())
            self.assertEqual(accepted.parent.name, ACCEPTED_UNPROCESSED)
            manifest = json.loads((accepted / "manifest.json").read_text())
            self.assertTrue(manifest["raw_immutable"])
            self.assertEqual(manifest["record_count"], 5)
            self.assertEqual(validate_bundle(accepted).valid, True)
            self.assertEqual((accepted / "records.jsonl").read_text().count("\n"), 5)

    def test_empty_or_untyped_events_cannot_certify(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = ReferenceSessionBundle.begin(directory, "retail-a", "GALE01r2", "run-a")
            for seq, event in enumerate(("entry", "gameplay", "result", "teardown", "observer_end"), 1):
                with self.subTest(event=event):
                    bundle.append({"seq": seq, "event": event, "payload": {}})
            with self.assertRaises(BundleValidationError):
                bundle.complete(semantic_report=semantic_report())
            partial = Path(directory) / ACTIVE_PARTIALS / "retail-a.partial"
            self.assertTrue(partial.is_dir())
            self.assertTrue((partial / "failure.json").is_file())

    def test_sequence_gap_is_rejected_and_partial_is_retained(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = ReferenceSessionBundle.begin(directory, "retail-a", "GALE01r2", "run-a")
            bundle.append({"seq": 1, "event": "entry", "payload": {"state": "entry"}})
            with self.assertRaisesRegex(BundleValidationError, "expected 2"):
                bundle.append({"seq": 3, "event": "gameplay", "payload": {"state": "tick"}})
            self.assertTrue(bundle.partial_path.is_dir())
            report = validate_bundle(bundle.partial_path, require_complete=False)
            self.assertFalse(report.valid)
            self.assertTrue(any("sequence" in error for error in report.errors))
            bundle.close()

    def test_faults_and_identity_mismatch_fail_without_final_rename(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = ReferenceSessionBundle.begin(directory, "retail-a", "GALE01r2", "run-a")
            append_lifecycle(bundle)
            bundle.append({"seq": 6, "event": "fault", "payload": {"overflow": True}})
            with self.assertRaises(BundleValidationError):
                bundle.complete(expected_run_id="run-other")
            self.assertTrue(bundle.partial_path.is_dir())
            self.assertFalse((Path(directory) / ACCEPTED_UNPROCESSED / "retail-a").exists())

    def test_unclean_observer_end_preserves_partial(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = ReferenceSessionBundle.begin(directory, "retail-a", "GALE01r2", "run-a")
            for seq, event in enumerate(("entry", "gameplay", "result", "teardown"), 1):
                bundle.append({"seq": seq, "event": event, "payload": {"observed": event}})
            bundle.append(
                {
                    "seq": 5,
                    "event": "observer_end",
                    "payload": {"clean": False, "sequence_ok": True},
                }
            )
            with self.assertRaises(BundleValidationError):
                bundle.complete(semantic_report=semantic_report())
            self.assertTrue(bundle.partial_path.is_dir())
            self.assertFalse((Path(directory) / ACCEPTED_UNPROCESSED / "retail-a").exists())

    def test_semantic_callback_sees_all_source_records(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = ReferenceSessionBundle.begin(directory, "retail-a", "GALE01r2", "run-a")
            append_lifecycle(bundle)
            observed = []

            def semantic_check(header, records):
                observed.extend(records)
                return ["domain observation missing"] if not any(
                    row.get("event") == "gameplay" and row.get("source_tick") == 2
                    for row in records
                ) else []

            accepted = bundle.complete(semantic_validator=semantic_check)
            self.assertEqual(len(observed), 5)
            self.assertTrue(accepted.is_dir())

    def test_raw_observer_bytes_and_extra_logs_are_manifest_bound(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = ReferenceSessionBundle.begin(directory, "retail-a", "GALE01r2", "run-a")
            bundle.append_raw(b"fixed binary observer bytes")
            append_lifecycle(bundle)
            bundle.partial_path.joinpath("collector.log").write_bytes(b"diagnostic log")
            accepted = bundle.complete(semantic_report=semantic_report())
            manifest = json.loads((accepted / "manifest.json").read_text())
            names = {entry["name"] for entry in manifest["files"]}
            self.assertIn(RAW_OBSERVER_NAME, names)
            self.assertIn("collector.log", names)
            self.assertTrue(validate_bundle(accepted).valid)

    def test_manifest_tamper_is_detected_and_raw_is_not_rewritten(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = ReferenceSessionBundle.begin(directory, "retail-a", "GALE01r2", "run-a")
            append_lifecycle(bundle)
            accepted = bundle.complete(semantic_report=semantic_report())
            records = accepted / "records.jsonl"
            original = records.read_bytes()
            records.write_bytes(original + b"\n")
            report = validate_bundle(accepted)
            self.assertFalse(report.valid)
            self.assertTrue(any("manifest hash mismatch" in error for error in report.errors))
            self.assertEqual(records.read_bytes(), original + b"\n")

    def test_manifest_count_binding_cannot_claim_a_different_record_stream(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = ReferenceSessionBundle.begin(directory, "retail-a", "GALE01r2", "run-a")
            append_lifecycle(bundle)
            accepted = bundle.complete(semantic_report=semantic_report())
            manifest_path = accepted / "manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["record_count"] = 1
            unsigned = dict(manifest)
            unsigned.pop("manifest_sha256")
            import hashlib
            manifest["manifest_sha256"] = hashlib.sha256(
                json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()
            ).hexdigest()
            manifest_path.write_text(json.dumps(manifest))
            report = validate_bundle(accepted)
            self.assertFalse(report.valid)
            self.assertIn("manifest record count mismatch", report.errors)

    def test_inbox_ingest_and_separate_derived_hash_binding(self):
        with tempfile.TemporaryDirectory() as directory:
            inbox = ReferenceCaptureInbox(directory)
            bundle = inbox.begin("retail-a", "GALE01r2", "run-a")
            append_lifecycle(bundle)
            accepted = bundle.complete(semantic_report=semantic_report())
            derived = inbox.store_derived("retail-a", "comparison", {"semantic": "pass"})
            self.assertEqual(derived.parent.parent.name, DERIVED)
            derived_manifest = json.loads((derived.parent / "derived-manifest.json").read_text())
            raw_manifest = json.loads((accepted / "manifest.json").read_text())
            self.assertEqual(derived_manifest["source"]["manifest_sha256"], raw_manifest["manifest_sha256"])
            ingested = inbox.ingest("retail-a", expected_manifest_sha256=raw_manifest["manifest_sha256"])
            self.assertEqual(ingested.parent.name, INGESTED)
            self.assertFalse(accepted.exists())

    def test_symlinked_state_child_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as target:
            state = Path(directory) / ACTIVE_PARTIALS
            state.mkdir(parents=True)
            (state / "escape.partial").symlink_to(target, target_is_directory=True)
            with self.assertRaises(UnsafePathError):
                ReferenceSessionBundle.begin(directory, "escape", "GALE01r2", "run-a")

    def test_failed_partial_can_be_quarantined_without_deletion(self):
        with tempfile.TemporaryDirectory() as directory:
            inbox = ReferenceCaptureInbox(directory)
            bundle = inbox.begin("retail-a", "GALE01r2", "run-a")
            bundle.fail("observer crashed", error_type="crash")
            failed = inbox.quarantine("retail-a")
            self.assertEqual(failed.parent.name, "failed")
            self.assertTrue(failed.is_dir())
            self.assertTrue((failed / "failure.json").is_file())


if __name__ == "__main__":
    unittest.main()
