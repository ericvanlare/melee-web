"""Focused validation tests for the private pipeline-use requirement sidecar."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest

from scripts.generate_pipeline_requirements import (
    PipelineRequirementsError,
    _expand_capture_chunk,
    generate,
    group_union,
    sha256_json,
)


class PipelineRequirementsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.seed = self.root / "seed.db"
        db = sqlite3.connect(self.seed)
        try:
            with db:
                db.executescript(
                    """
                    CREATE TABLE aurora_schema(value INTEGER);
                    INSERT INTO aurora_schema VALUES (1);
                    CREATE TABLE pipeline_cache(
                        type INTEGER NOT NULL, hash INTEGER NOT NULL,
                        config_version INTEGER NOT NULL, config_size INTEGER NOT NULL,
                        config BLOB NOT NULL, first_frame_used INTEGER NOT NULL,
                        PRIMARY KEY(type, hash));
                    """
                )
                for row in ((1, 7, 65549, b"descriptor-seven"), (1, 8, 65549, b"descriptor-eight")):
                    kind, ref, version, payload = row
                    db.execute(
                        "INSERT INTO pipeline_cache VALUES (?, ?, ?, ?, ?, ?)",
                        (kind, ref, version, len(payload), payload, 1),
                    )
        finally:
            db.close()
        self.seed_digest = hashlib.sha256(self.seed.read_bytes()).hexdigest()
        self.input_digest = "f" * 64
        self.capture_id = "capture-test-1"
        self.coverage = {
            "schema": "melee-web-pipeline-coverage-v1",
            "version": 1,
            "cases": [{
                "case_id": "mario-fd",
                "route_id": "mario-fd",
                "route": {"route_id": "mario-fd", "fighter_numeric_ids": [1, 1], "stage_numeric_id": 3},
                "expected_phases": ["css", "match"],
                "expected_actions": ["Wait", "Jab"],
                "expected_costumes": [0],
                "lifecycle": ["entry", "interactive"],
                "input": {"capture_id": self.capture_id, "sha256": self.input_digest},
            }],
        }
        self.coverage_digest = sha256_json(self.coverage)
        self.metadata = {
            "schema": "melee-web-pipeline-requirements-input-v1",
            "version": 1,
            "seed": {"decoded_sha256": self.seed_digest},
            "source": {"head": "a" * 40, "dirty_overlay_sha256": "b" * 64},
            "dependencies": {"aurora": "c" * 40, "melee": "d" * 40},
            "renderer": {"version": "aurora-test", "config_layout": "gx-65549",
                         "config_layout_sha256": "e" * 64},
            "registry": {"fighters": [{"numeric_id": 1}], "stages": [{"numeric_id": 3}]},
            "coverage_manifest_sha256": self.coverage_digest,
        }

    def tearDown(self) -> None:
        self.directory.cleanup()

    @staticmethod
    def descriptor(ref: int, payload: bytes) -> dict:
        return {"type": 1, "ref_hex": f"{ref:016x}", "config_version": 65549,
                "size": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}

    def capture(self, records=None, *, status=None, descriptors=None):
        seven = self.descriptor(7, b"descriptor-seven")
        eight = self.descriptor(8, b"descriptor-eight")
        if records is None:
            records = [
                {"sequence": 1, "kind": "scope_begin", "scope": {"scope_id": "css-s", "case_id": "mario-fd", "route_id": "mario-fd", "phase": "css", "input_manifest_sha256": self.input_digest}},
                {"sequence": 2, "kind": "use", "scope": {"scope_id": "css-s", "case_id": "mario-fd", "route_id": "mario-fd", "phase": "css", "input_manifest_sha256": self.input_digest, "action": "Wait", "costumes": [0], "lifecycle": "entry"}, "descriptor": seven},
                {"sequence": 3, "kind": "scope_end", "scope": {"scope_id": "css-s", "case_id": "mario-fd", "route_id": "mario-fd", "phase": "css", "input_manifest_sha256": self.input_digest}},
                {"sequence": 4, "kind": "scope_begin", "scope": {"scope_id": "match-s", "case_id": "mario-fd", "route_id": "mario-fd", "phase": "match", "input_manifest_sha256": self.input_digest}},
                {"sequence": 5, "kind": "draw_use", "scope": {"scope_id": "match-s", "case_id": "mario-fd", "route_id": "mario-fd", "phase": "match", "input_manifest_sha256": self.input_digest, "action": "Jab", "costumes": [0], "lifecycle": "interactive"}, "descriptor": eight, "source_tick": 4, "render_packet_id": 12},
                {"sequence": 6, "kind": "scope_end", "scope": {"scope_id": "match-s", "case_id": "mario-fd", "route_id": "mario-fd", "phase": "match", "input_manifest_sha256": self.input_digest}},
                {"sequence": 7, "kind": "import", "context": {"scene": 1, "phase": 1},
                 "frame_id": 0, "packet_id": 0, "descriptor": seven,
                 "diagnostic_path": "/private/raw.trace"},
            ]
        return {
            "schema": "melee-web-pipeline-use-v1", "version": 1,
            "capture_id": self.capture_id, "capture_generation": 4,
            "sequence_begin": 1, "sequence_end": len(records), "records": records,
            "descriptors": descriptors if descriptors is not None else [seven, eight],
            "provenance": {
                "seed_decoded_sha256": self.seed_digest, "source_head": "a" * 40,
                "dirty_overlay_sha256": "b" * 64, "dependencies": self.metadata["dependencies"],
                "renderer": self.metadata["renderer"], "registry_sha256": sha256_json(self.metadata["registry"]),
                "coverage_manifest_sha256": self.coverage_digest,
                "input_manifest_sha256": self.input_digest,
            },
            "status": {"valid": True, "dropped": 0, "overflow": False, "errors": [],
                       "open_scopes": 0, "total_records": len(records), "drained_records": len(records)}
            if status is None else status,
            "raw_trace": {"bytes": "should-not-escape", "path": "/private/raw.trace"},
        }

    @staticmethod
    def compact_capture(capture):
        """Encode verbose per-record contexts using a chunk-local dictionary."""

        compact = json.loads(json.dumps(capture))
        contexts = []
        by_digest = {}
        for record in compact["records"]:
            direct_key = next((key for key in ("context", "scope", "source_context") if key in record), None)
            if direct_key is None:
                continue
            context = record.pop(direct_key)
            digest = json.dumps(context, sort_keys=True, separators=(",", ":"))
            index = by_digest.get(digest)
            if index is None:
                index = len(contexts)
                by_digest[digest] = index
                contexts.append(context)
            record["context_index"] = index
        compact["contexts"] = contexts
        return compact

    def generate(self, capture=None, coverage=None):
        return generate(capture or self.capture(), self.seed, self.metadata, coverage or self.coverage)

    def test_certificate_and_import_is_excluded(self):
        result = self.generate()
        self.assertEqual(result["status"]["kind"], "certificate")
        self.assertTrue(result["status"]["certified"])
        self.assertEqual([group["id"] for group in result["groups"]], ["mario-fd:css", "mario-fd:match"])
        self.assertEqual([len(group["members"]) for group in result["groups"]], [1, 1])
        serialized = json.dumps(result, sort_keys=True)
        self.assertNotIn("raw.trace", serialized)
        self.assertNotIn("should-not-escape", serialized)

    def test_group_union_deduplicates_warm_and_merged_demand(self):
        capture = self.capture()
        warm = dict(capture["records"][4])
        warm["sequence"] = 8
        warm["kind"] = "merge_use"
        warm["descriptor"] = capture["records"][1]["descriptor"]
        # Rebuild the ordered stream so the additional warm hit is valid.
        second_warm = dict(warm)
        capture["records"] = capture["records"][:5] + [warm, second_warm, capture["records"][5], capture["records"][6]]
        for index, row in enumerate(capture["records"], 1):
            row["sequence"] = index
        capture["sequence_end"] = len(capture["records"])
        capture["status"]["total_records"] = len(capture["records"])
        capture["status"]["drained_records"] = len(capture["records"])
        result = self.generate(capture)
        match = next(group for group in result["groups"] if group["phase"] == "match")
        self.assertEqual(len(match["members"]), 2)
        warm_member = next(member for member in match["members"] if member["ref_hex"] == f"{7:016x}")
        self.assertEqual(len(warm_member["provenance"]), 2)

    def test_deferred_demand_pairs_closed_scope_without_thread_aliasing(self):
        capture = self.capture()
        deferred = dict(capture["records"][4])
        deferred["sequence"] = 7
        deferred["kind"] = "merge_use"
        deferred["thread_id"] = 99
        deferred["descriptor"] = capture["records"][1]["descriptor"]
        capture["records"] = capture["records"][:6] + [deferred, capture["records"][6]]
        for index, row in enumerate(capture["records"], 1):
            row["sequence"] = index
        capture["sequence_end"] = len(capture["records"])
        capture["status"]["total_records"] = len(capture["records"])
        capture["status"]["drained_records"] = len(capture["records"])
        result = self.generate(capture)
        match = next(group for group in result["groups"] if group["phase"] == "match")
        self.assertEqual(len(match["members"]), 2)

    def test_group_union_utility_is_typed_and_deterministic(self):
        seven = self.descriptor(7, b"descriptor-seven")
        eight = self.descriptor(8, b"descriptor-eight")
        self.assertEqual(
            [row["ref_hex"] for row in group_union([eight, seven], [seven])],
            [f"{7:016x}", f"{8:016x}"],
        )

    def test_manifest_case_order_is_canonical(self):
        second = dict(self.coverage["cases"][0])
        second["case_id"] = "zzz"
        second["route_id"] = "zzz"
        second["route"] = {"route_id": "zzz"}
        second["input"] = {"capture_id": self.capture_id, "sha256": self.input_digest}
        coverage = dict(self.coverage)
        coverage["cases"] = [second, self.coverage["cases"][0]]
        metadata = dict(self.metadata)
        metadata["coverage_manifest_sha256"] = sha256_json(coverage)
        capture = self.capture()
        capture["provenance"] = dict(capture["provenance"], coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        # No zzz records means this is still a valid, explicit draft, and its
        # output order must remain stable regardless of manifest file order.
        result = generate(capture, self.seed, metadata, coverage)
        self.assertEqual([case["case_id"] for case in result["coverage"]["cases"]], ["mario-fd", "zzz"])
        self.assertEqual(result["status"]["kind"], "incomplete")

    def test_incomplete_coverage_is_a_draft(self):
        records = self.capture()["records"][:3]
        capture = self.capture(records)
        capture["sequence_end"] = 3
        capture["status"]["total_records"] = 3
        capture["status"]["drained_records"] = 3
        result = self.generate(capture)
        self.assertEqual(result["status"]["kind"], "incomplete")
        self.assertFalse(result["status"]["certified"])
        self.assertIn("match", result["status"]["incomplete_scopes"][0]["missing_phases"])

    def assert_reject(self, capture=None, *, metadata=None, coverage=None, code=None):
        with self.assertRaises(PipelineRequirementsError) as raised:
            generate(capture or self.capture(), self.seed, metadata or self.metadata, coverage or self.coverage)
        if code:
            self.assertIn(code, {item["code"] for item in raised.exception.errors})

    def test_seed_and_binding_digest_mismatch_reject(self):
        metadata = dict(self.metadata)
        metadata["seed"] = {"decoded_sha256": "0" * 64}
        self.assert_reject(metadata=metadata, code="seed_digest_mismatch")
        capture = self.capture()
        capture["provenance"] = dict(capture["provenance"])
        capture["provenance"]["source_head"] = "1" * 40
        self.assert_reject(capture, code="capture_binding_mismatch")

    def test_path_shaped_copied_identifiers_reject(self):
        coverage = json.loads(json.dumps(self.coverage))
        coverage["cases"][0]["case_id"] = "/Users/" + "private/case"
        metadata = dict(self.metadata, coverage_manifest_sha256=sha256_json(coverage))
        capture = self.capture()
        capture["provenance"] = dict(capture["provenance"], coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        self.assert_reject(capture, metadata=metadata, coverage=coverage, code="coverage_case_id")

        coverage = json.loads(json.dumps(self.coverage))
        coverage["cases"][0]["route_id"] = r"private\route"
        metadata = dict(self.metadata, coverage_manifest_sha256=sha256_json(coverage))
        capture = self.capture()
        capture["provenance"] = dict(capture["provenance"], coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        self.assert_reject(capture, metadata=metadata, coverage=coverage, code="coverage_route_id")

        coverage = json.loads(json.dumps(self.coverage))
        coverage["cases"][0]["input"]["capture_id"] = "/private/capture"
        metadata = dict(self.metadata, coverage_manifest_sha256=sha256_json(coverage))
        capture = self.capture()
        capture["provenance"] = dict(capture["provenance"], coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        self.assert_reject(capture, metadata=metadata, coverage=coverage, code="coverage_capture_id")

        metadata = dict(self.metadata)
        metadata["dependencies"] = {"/private/dependency": "c" * 40, "melee": "d" * 40}
        self.assert_reject(metadata=metadata, code="dependency_name")

        metadata = dict(self.metadata)
        metadata["renderer"] = {"version": r"/private/renderer", "config_layout": "gx-65549"}
        self.assert_reject(metadata=metadata, code="renderer_version")

        capture = self.capture()
        capture["records"][0]["scope"]["scope_id"] = r"private\scope"
        self.assert_reject(capture, code="scope_pair")

    def test_nonempty_seed_wal_rejects_unbound_rows(self):
        wal_seed = self.root / "wal-seed.db"
        db = sqlite3.connect(wal_seed)
        try:
            with db:
                db.execute("PRAGMA journal_mode=WAL")
                db.executescript(
                    """
                    CREATE TABLE aurora_schema(value INTEGER);
                    INSERT INTO aurora_schema VALUES (1);
                    CREATE TABLE pipeline_cache(
                        type INTEGER NOT NULL, hash INTEGER NOT NULL,
                        config_version INTEGER NOT NULL, config_size INTEGER NOT NULL,
                        config BLOB NOT NULL, first_frame_used INTEGER NOT NULL,
                        PRIMARY KEY(type, hash));
                    INSERT INTO pipeline_cache VALUES (1, 7, 65549, 17, X'77616C2D64657363726970746F72', 1);
                    """
                )
                db.commit()
                self.assertGreater((Path(str(wal_seed) + "-wal")).stat().st_size, 0)
                with self.assertRaises(PipelineRequirementsError) as raised:
                    generate(self.capture(), wal_seed, self.metadata, self.coverage)
        finally:
            db.close()
        self.assertIn("seed_wal", {item["code"] for item in raised.exception.errors})

    def test_unknown_and_conflicting_rows_reject(self):
        capture = self.capture()
        unknown = self.descriptor(99, b"unknown")
        capture["descriptors"] = [*capture["descriptors"], unknown]
        self.assert_reject(capture, code="unknown_descriptor")
        capture = self.capture()
        capture["descriptors"] = [capture["descriptors"][0], dict(capture["descriptors"][0], sha256="0" * 64), capture["descriptors"][1]]
        self.assert_reject(capture, code="conflicting_descriptor")

    def test_capture_status_sequence_and_scope_fail_closed(self):
        capture = self.capture()
        capture["status"] = dict(capture["status"], dropped=1)
        self.assert_reject(capture, code="capture_dropped")
        capture = self.capture()
        capture["records"][1]["sequence"] = 9
        self.assert_reject(capture, code="sequence_gap")
        capture = self.capture()
        capture["records"] = capture["records"][:5] + [capture["records"][6]]
        for index, row in enumerate(capture["records"], 1):
            row["sequence"] = index
        capture["sequence_end"] = 6
        capture["status"] = dict(capture["status"], total_records=6, drained_records=6)
        self.assert_reject(capture, code="scope_pair")

    def test_scope_route_input_and_version_mismatch_reject(self):
        capture = self.capture()
        capture["records"][1]["scope"] = dict(capture["records"][1]["scope"], route_id="other")
        self.assert_reject(capture, code="scope_mismatch")
        capture = self.capture()
        capture["version"] = 2
        self.assert_reject(capture, code="capture_version")
        capture = self.capture()
        capture["records"][1]["scope"] = dict(capture["records"][1]["scope"], input_manifest_sha256="0" * 64)
        self.assert_reject(capture, code="input_mismatch")
        capture = self.capture()
        capture["renderer_generation"] = 8
        capture["records"][1]["renderer_generation"] = 7
        self.assert_reject(capture, code="stale_generation")

    def test_native_numeric_context_case_alias_and_compact_catalog(self):
        seven = self.descriptor(7, b"descriptor-seven")
        eight = self.descriptor(8, b"descriptor-eight")
        players = [
            {"character": 1, "fighter_kind": 1, "costume": 0, "motion_id": 10, "stocks": 4},
            {"character": 1, "fighter_kind": 1, "costume": 0, "motion_id": 11, "stocks": 4},
        ]
        context = {
            "scene": 4, "phase": 1, "world_generation": 2, "route_epoch": 3,
            "source_tick": 12, "coverage_case_id": 17,
            "input_binding_sha256": self.input_digest, "stage": 3,
            "active_player_count": 2, "players": players,
        }
        records = [
            {"sequence": 1, "kind": "scope_begin", "scope_id": 1, "thread_id": 9,
             "renderer_generation": 2, "device_generation": 3, "context": context},
            {"sequence": 2, "kind": "ready", "scope_id": 1, "thread_id": 9,
             "renderer_generation": 2, "device_generation": 3, "frame_id": 20,
             "packet_id": 21, "type": 1, "pipeline_ref": "0x0000000000000007",
             "config_version": seven["config_version"], "descriptor_sha256": seven["sha256"],
             "context": context},
            {"sequence": 3, "kind": "scope_end", "scope_id": 1, "thread_id": 9,
             "renderer_generation": 2, "device_generation": 3, "context": context},
            {"sequence": 4, "kind": "import", "scope_id": 0, "thread_id": 0,
             "renderer_generation": 0, "device_generation": 0, "frame_id": 0, "packet_id": 0,
             "type": 1, "pipeline_ref": "0x0000000000000008",
             "config_version": eight["config_version"], "descriptor_sha256": eight["sha256"],
             "context": {"scene": 1, "phase": 1}},
        ]
        coverage = {
            "schema": "melee-web-pipeline-coverage-v1", "version": 1,
            "cases": [{
                "case_id": "mario-fd", "coverage_case_id": 17,
                "route_id": "mario-fd",
                "route": {"route_id": "mario-fd", "fighter_numeric_ids": [1, 1],
                          "costume_numeric_ids": [0, 0], "stage_numeric_id": 3},
                "expected_scenes": ["match"], "expected_phases": ["preparation"],
                "expected_actions": [10], "expected_source_ticks": [12],
                "expected_costumes": [0], "lifecycle": ["preparation"],
                "input": {"capture_id": self.capture_id, "sha256": self.input_digest},
            }],
        }
        metadata = dict(self.metadata)
        metadata["coverage_manifest_sha256"] = sha256_json(coverage)
        capture = {
            "schema": "melee-web-pipeline-use-v1", "version": 1,
            "capture_generation": 4, "sequence_begin": 1, "sequence_end": 4,
            "records": records,
            "descriptors": [{"sha256": seven["sha256"], "type": 1,
                             "config_version": seven["config_version"], "bytes": seven["size"]},
                            {"sha256": eight["sha256"], "type": 1,
                             "config_version": eight["config_version"], "bytes": eight["size"]}],
            "status": {"capture_active": False, "valid": True, "final": True,
                       "capture_generation": 4, "renderer_generation": 2,
                       "device_generation": 3, "errors": 0, "dropped": 0,
                       "open_scopes": 0, "total_records": 4, "drained_records": 4},
        }
        result = generate(capture, self.seed, metadata, coverage)
        self.assertTrue(result["status"]["certified"])
        self.assertEqual(result["groups"][0]["phase"], 1)
        self.assertEqual(result["groups"][0]["members"][0]["ref_hex"], f"{7:016x}")
        self.assertEqual(result["groups"][0]["members"][0]["provenance"][0]["scope_id"], "1")
        self.assertEqual(result["coverage"]["cases"][0]["coverage_case_id"], 17)
        mismatch = json.loads(json.dumps(capture))
        for record in mismatch["records"]:
            context = record.get("context")
            if isinstance(context, dict) and record.get("kind") != "import":
                context["players"][0]["costume"] = 1
        self.assert_reject(mismatch, metadata=metadata, coverage=coverage, code="content_mismatch")

    def test_native_numeric_case_without_manifest_alias_rejects(self):
        capture = self.capture()
        capture["records"][1]["scope"]["case_id"] = 17
        self.assert_reject(capture, code="unknown_case")

    def test_native_numeric_phase_without_scene_rejects(self):
        capture = json.loads(json.dumps(self.capture()))
        for record in capture["records"]:
            context = record.get("scope") or record.get("context")
            if isinstance(context, dict) and record.get("kind") != "import":
                context["phase"] = 1 if context.get("phase") == "css" else 4
                context.pop("scene", None)
        coverage = json.loads(json.dumps(self.coverage))
        coverage["cases"][0]["expected_phases"] = [1, 4]
        metadata = dict(self.metadata, coverage_manifest_sha256=sha256_json(coverage))
        capture["provenance"] = dict(capture["provenance"], coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        self.assert_reject(capture, metadata=metadata, coverage=coverage, code="scene_binding")

    def test_jsonl_drain_chunks_join_in_sequence_order(self):
        capture = self.capture()
        first = dict(capture)
        first["records"] = capture["records"][:3]
        first["sequence_begin"] = 1
        first["sequence_end"] = 3
        first["status"] = dict(capture["status"], total_records=3, drained_records=3)
        second = dict(capture)
        second["records"] = capture["records"][3:]
        second["sequence_begin"] = 4
        second["sequence_end"] = len(capture["records"])
        second["status"] = dict(capture["status"], total_records=4, drained_records=4)
        with tempfile.NamedTemporaryFile("w", suffix=".jsonl") as stream:
            stream.write(json.dumps(first) + "\n")
            stream.write(json.dumps(second) + "\n")
            stream.flush()
            result = self.generate(Path(stream.name))
        self.assertTrue(result["status"]["certified"])
        self.assertEqual([group["id"] for group in result["groups"]], ["mario-fd:css", "mario-fd:match"])

    def test_compact_contexts_are_equivalent_to_verbose_records(self):
        verbose = self.generate(self.capture())
        compact_capture = self.compact_capture(self.capture())
        compact = self.generate(compact_capture)
        # The capture hash binds the serialized transport bytes, so the
        # compact and verbose encodings necessarily have different hashes.
        # Every generated requirement and validation field must otherwise be
        # identical.
        verbose["capture"].pop("capture_sha256")
        compact["capture"].pop("capture_sha256")
        self.assertEqual(compact, verbose)

    def test_compact_context_indices_are_local_to_each_jsonl_chunk(self):
        capture = self.capture()
        first = self.compact_capture(dict(capture, records=capture["records"][:3]))
        second = self.compact_capture(dict(capture, records=capture["records"][3:]))
        first["sequence_begin"], first["sequence_end"] = 1, 3
        second["sequence_begin"], second["sequence_end"] = 4, len(capture["records"])
        first["status"] = dict(capture["status"], total_records=3, drained_records=3)
        second["status"] = dict(capture["status"], total_records=4, drained_records=4)
        with tempfile.NamedTemporaryFile("w", suffix=".jsonl") as stream:
            stream.write(json.dumps(first) + "\n")
            stream.write(json.dumps(second) + "\n")
            stream.flush()
            result = self.generate(Path(stream.name))
        expected = self.generate(capture)
        result["capture"].pop("capture_sha256")
        expected["capture"].pop("capture_sha256")
        self.assertEqual(result, expected)

    def test_compact_contexts_share_within_chunk_but_not_across_chunks(self):
        first_context = {"scope_id": "first-s", "scene": 2, "phase": 1}
        second_context = {"scope_id": "second-s", "scene": 3, "phase": 1}
        first = _expand_capture_chunk({
            "contexts": [first_context],
            "records": [
                {"sequence": 1, "kind": "scope_begin", "context_index": 0},
                {"sequence": 2, "kind": "scope_end", "context_index": 0},
            ],
        }, "first chunk")
        second = _expand_capture_chunk({
            "contexts": [second_context],
            "records": [{"sequence": 3, "kind": "scope_begin", "context_index": 0}],
        }, "second chunk")
        self.assertIs(first["records"][0]["context"], first["records"][1]["context"])
        self.assertIsNot(first["records"][0]["context"], second["records"][0]["context"])
        self.assertEqual(first["records"][0]["context"]["scope_id"], "first-s")
        self.assertEqual(second["records"][0]["context"]["scope_id"], "second-s")

    def test_many_unique_demand_events_retain_all_provenance_links(self):
        event_count = 4096
        scope = {"scope_id": "match-many-s", "case_id": "mario-fd", "route_id": "mario-fd",
                 "phase": "match", "input_manifest_sha256": self.input_digest,
                 "action": "Jab", "costumes": [0], "lifecycle": "interactive"}
        descriptor = self.descriptor(7, b"descriptor-seven")
        records = [{"sequence": 1, "kind": "scope_begin", "scope": scope}]
        records.extend(
            {"sequence": sequence, "kind": "draw_use", "scope": scope, "descriptor": descriptor}
            for sequence in range(2, event_count + 2)
        )
        records.append({"sequence": event_count + 2, "kind": "scope_end", "scope": scope})
        capture = self.capture(records=records)
        capture["sequence_end"] = len(records)
        capture["status"] = dict(capture["status"], total_records=len(records), drained_records=len(records))
        result = self.generate(capture)
        match = next(group for group in result["groups"] if group["phase"] == "match")
        member = match["members"][0]
        self.assertEqual(len(member["provenance"]), event_count)
        self.assertEqual([link["sequence"] for link in member["provenance"]],
                         list(range(2, event_count + 2)))

    def test_finite_menu_identities_match_each_observed_selection_tuple(self):
        menu_identities = [
            {"fighter_numeric_ids": [1, 0], "costume_numeric_ids": [0, 1],
             "stage_numeric_id": 3, "ground_numeric_id": 37},
            {"fighter_numeric_ids": [8, 8], "costume_numeric_ids": [1, 0],
             "stage_numeric_id": 32, "ground_numeric_id": 36},
        ]
        coverage = dict(self.coverage)
        coverage["cases"] = [{
            "case_id": "mario-fd", "route_id": "mario-fd",
            "route": {"route_id": "mario-fd", "fighter_numeric_ids": [1, 1],
                       "costume_numeric_ids": [0, 0], "stage_numeric_id": 3,
                       "menu_identities": menu_identities},
            "expected_phases": [
                {"scene": "css", "phase": "preparation"},
                {"scene": "sss", "phase": "preparation"},
            ],
            "expected_actions": ["Wait"], "expected_costumes": [0, 1],
            "lifecycle": ["preparation"],
            "input": {"capture_id": self.capture_id, "sha256": self.input_digest},
        }]
        metadata = dict(self.metadata)
        metadata["coverage_manifest_sha256"] = sha256_json(coverage)
        descriptors = [self.descriptor(7, b"descriptor-seven"), self.descriptor(8, b"descriptor-eight")]

        def scope_records(start, scope_id, scene, players, stage, ground, descriptor):
            context = {"scope_id": scope_id, "case_id": "mario-fd", "route_id": "mario-fd",
                       "scene": scene, "phase": 1, "world_generation": 1,
                       "input_manifest_sha256": self.input_digest, "active_player_count": len(players),
                       "players": players, "stage": stage, "ground": ground}
            return [
                {"sequence": start, "kind": "scope_begin", "scope": context},
                {"sequence": start + 1, "kind": "draw_use",
                 "scope": dict(context, action="Wait", lifecycle="preparation"),
                 "descriptor": descriptor},
                {"sequence": start + 2, "kind": "scope_end", "scope": context},
            ]

        records = scope_records(1, "css-menu-s", 2,
                                [{"character": 1, "costume": 0}, {"character": 0, "costume": 1}],
                                3, 37, descriptors[0])
        records += scope_records(4, "sss-menu-s", 3,
                                 [{"character": 8, "costume": 1}, {"character": 8, "costume": 0}],
                                 32, 36, descriptors[1])
        capture = self.capture(records=records, descriptors=descriptors)
        capture["sequence_end"] = len(records)
        capture["status"] = dict(capture["status"], total_records=len(records), drained_records=len(records))
        capture["provenance"] = dict(capture["provenance"],
                                      coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        result = generate(capture, self.seed, metadata, coverage)
        self.assertTrue(result["status"]["certified"])
        self.assertEqual([group["id"] for group in result["groups"]], ["mario-fd:2:1", "mario-fd:3:1"])

    def test_fixed_final_route_without_menu_binding_remains_incomplete(self):
        coverage = dict(self.coverage)
        coverage["cases"] = [{
            "case_id": "mario-fd", "route_id": "mario-fd",
            "route": {"route_id": "mario-fd", "fighter_numeric_ids": [1, 0],
                       "costume_numeric_ids": [0, 1], "stage_numeric_id": 3,
                       "ground_numeric_id": 37},
            "expected_phases": [{"scene": "css", "phase": "preparation"}],
            "expected_actions": ["Wait"], "expected_costumes": [0, 1],
            "lifecycle": ["preparation"],
            "input": {"capture_id": self.capture_id, "sha256": self.input_digest},
        }]
        metadata = dict(self.metadata, coverage_manifest_sha256=sha256_json(coverage))
        context = {"scope_id": "css-missing-menu-s", "case_id": "mario-fd", "route_id": "mario-fd",
                   "scene": 2, "phase": 1, "world_generation": 1,
                   "input_manifest_sha256": self.input_digest, "active_player_count": 2,
                   "players": [{"character": 1, "costume": 0}, {"character": 0, "costume": 1}],
                   "stage": 3, "ground": 37}
        capture = self.capture(records=[
            {"sequence": 1, "kind": "scope_begin", "scope": context},
            {"sequence": 2, "kind": "draw_use", "scope": dict(context, action="Wait", lifecycle="preparation"),
             "descriptor": self.descriptor(7, b"descriptor-seven")},
            {"sequence": 3, "kind": "scope_end", "scope": context},
        ])
        capture["sequence_end"] = 3
        capture["status"] = dict(capture["status"], total_records=3, drained_records=3)
        capture["provenance"] = dict(capture["provenance"], coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        result = generate(capture, self.seed, metadata, coverage)
        self.assertEqual(result["status"]["kind"], "incomplete")
        self.assertFalse(result["status"]["certified"])
        self.assertGreater(result["status"]["incomplete_scopes"][0]["menu_identity_missing"], 0)

    def test_crossed_menu_identity_tuple_is_rejected(self):
        coverage = dict(self.coverage)
        coverage["cases"] = [{
            "case_id": "mario-fd", "route_id": "mario-fd",
            "route": {"route_id": "mario-fd", "menu_identities": [
                {"fighter_numeric_ids": [1, 0], "costume_numeric_ids": [0, 1],
                 "stage_numeric_id": 3, "ground_numeric_id": 37},
                {"fighter_numeric_ids": [8, 8], "costume_numeric_ids": [1, 0],
                 "stage_numeric_id": 32, "ground_numeric_id": 36},
            ]},
            "expected_phases": [{"scene": "css", "phase": "preparation"}],
            "expected_actions": ["Wait"], "expected_costumes": [0, 1], "lifecycle": ["preparation"],
            "input": {"capture_id": self.capture_id, "sha256": self.input_digest},
        }]
        metadata = dict(self.metadata, coverage_manifest_sha256=sha256_json(coverage))
        context = {"scope_id": "css-crossed-s", "case_id": "mario-fd", "route_id": "mario-fd",
                   "scene": 2, "phase": 1, "world_generation": 1,
                   "input_manifest_sha256": self.input_digest, "active_player_count": 2,
                   "players": [{"character": 1, "costume": 1}, {"character": 0, "costume": 0}],
                   "stage": 32, "ground": 36}
        capture = self.capture(records=[
            {"sequence": 1, "kind": "scope_begin", "scope": context},
            {"sequence": 2, "kind": "draw_use", "scope": dict(context, action="Wait", lifecycle="preparation"),
             "descriptor": self.descriptor(7, b"descriptor-seven")},
            {"sequence": 3, "kind": "scope_end", "scope": context},
        ])
        capture["sequence_end"] = 3
        capture["status"] = dict(capture["status"], total_records=3, drained_records=3)
        capture["provenance"] = dict(capture["provenance"], coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        self.assert_reject(capture, metadata=metadata, coverage=coverage, code="content_mismatch")

    def test_unlisted_menu_identity_tuple_is_rejected(self):
        coverage = dict(self.coverage)
        coverage["cases"] = [{
            "case_id": "mario-fd", "route_id": "mario-fd",
            "route": {"route_id": "mario-fd", "menu_identities": [{
                "fighter_numeric_ids": [1, 0], "costume_numeric_ids": [0, 1],
                "stage_numeric_id": 3, "ground_numeric_id": 37,
            }]},
            "expected_phases": [{"scene": "css", "phase": "preparation"}],
            "expected_actions": ["Wait"], "expected_costumes": [0, 1], "lifecycle": ["preparation"],
            "input": {"capture_id": self.capture_id, "sha256": self.input_digest},
        }]
        metadata = dict(self.metadata, coverage_manifest_sha256=sha256_json(coverage))
        context = {"scope_id": "css-unlisted-s", "case_id": "mario-fd", "route_id": "mario-fd",
                   "scene": 2, "phase": 1, "world_generation": 1,
                   "input_manifest_sha256": self.input_digest, "active_player_count": 2,
                   "players": [{"character": 8, "costume": 1}, {"character": 8, "costume": 0}],
                   "stage": 32, "ground": 36}
        capture = self.capture(records=[
            {"sequence": 1, "kind": "scope_begin", "scope": context},
            {"sequence": 2, "kind": "draw_use", "scope": dict(context, action="Wait", lifecycle="preparation"),
             "descriptor": self.descriptor(7, b"descriptor-seven")},
            {"sequence": 3, "kind": "scope_end", "scope": context},
        ])
        capture["sequence_end"] = 3
        capture["status"] = dict(capture["status"], total_records=3, drained_records=3)
        capture["provenance"] = dict(capture["provenance"], coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        self.assert_reject(capture, metadata=metadata, coverage=coverage, code="content_mismatch")

    def test_menu_identity_route_shape_and_ambiguity_are_rejected(self):
        routes = [
            {"route_id": "mario-fd", "menu_identities": []},
            {"route_id": "mario-fd", "menu_identities": [{
                "fighter_numeric_ids": [1, 0], "costume_numeric_ids": [0, 1],
                "stage_numeric_id": 3,
            }]},
            {"route_id": "mario-fd", "menu_identities": [{
                "fighter_numeric_ids": [1, 0], "costume_numeric_ids": [0],
                "stage_numeric_id": 3, "ground_numeric_id": 37,
            }]},
            {"route_id": "mario-fd", "menu_identities": [{
                "fighter_numeric_ids": [1, 0], "costume_numeric_ids": [0, 1],
                "stage_numeric_id": None, "ground_numeric_id": 37,
            }]},
            {"route_id": "mario-fd", "menu_identities": [{
                "fighter_numeric_ids": [1, 0], "costume_numeric_ids": [0, 1],
                "stage_numeric_id": 3, "ground_numeric_id": 37, "wildcard": True,
            }]},
            {"route_id": "mario-fd", "menu_identities": [{
                "fighter_numeric_ids": [1, 0], "costume_numeric_ids": [0, 1],
                "stage_numeric_id": 3, "ground_numeric_id": 37,
            }], "menu_fighter_numeric_ids": [1, 0]},
            {"route_id": "mario-fd", "menu_identities": [
                {"fighter_numeric_ids": [1, 0], "costume_numeric_ids": [0, 1],
                 "stage_numeric_id": 3, "ground_numeric_id": 37},
                {"fighter_numeric_ids": [1, 0], "costume_numeric_ids": [0, 1],
                 "stage_numeric_id": 3, "ground_numeric_id": 37},
            ]},
            {"route_id": "mario-fd", "menu_fighter_numeric_ids": [1, 0],
             "menu_fighter_ids": [1, 0]},
        ]
        for route in routes:
            coverage = json.loads(json.dumps(self.coverage))
            coverage["cases"][0]["route"] = route
            metadata = dict(self.metadata, coverage_manifest_sha256=sha256_json(coverage))
            capture = self.capture()
            capture["provenance"] = dict(capture["provenance"],
                                          coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
            self.assert_reject(capture, metadata=metadata, coverage=coverage, code="coverage_menu_identity")

    def test_compact_context_index_validation_is_fail_closed(self):
        cases = []

        missing = self.compact_capture(self.capture())
        missing.pop("contexts")
        cases.append((missing, "capture_context_index"))

        for invalid_index in (True, "0", -1, 999):
            malformed = self.compact_capture(self.capture())
            malformed["records"][1]["context_index"] = invalid_index
            cases.append((malformed, "capture_context_index"))

        malformed_context = self.compact_capture(self.capture())
        malformed_context["contexts"][0] = ["not-a-context-object"]
        cases.append((malformed_context, "capture_context"))

        conflicting = self.compact_capture(self.capture())
        conflicting["records"][1]["context"] = conflicting["contexts"][conflicting["records"][1]["context_index"]]
        cases.append((conflicting, "capture_context_form"))

        for malformed, code in cases:
            self.assert_reject(malformed, code=code)

    def test_unused_compact_context_entry_is_ignored(self):
        compact = self.compact_capture(self.capture())
        compact["contexts"].append(["unused malformed entry"])
        # The extra entry is never referenced; it must not affect the joined
        # requirement output or force callers to synthesize a context for it.
        compact_result = self.generate(compact)
        verbose_result = self.generate(self.capture())
        compact_result["capture"].pop("capture_sha256")
        verbose_result["capture"].pop("capture_sha256")
        self.assertEqual(compact_result, verbose_result)

    def test_paired_empty_teardown_scope_counts_as_phase_coverage(self):
        coverage = dict(self.coverage)
        coverage["cases"] = [{
            "case_id": "mario-fd", "route_id": "mario-fd", "route": {"route_id": "mario-fd"},
            "expected_phases": ["teardown"], "expected_actions": [], "expected_costumes": [],
            "lifecycle": ["teardown"],
            "input": {"capture_id": self.capture_id, "sha256": self.input_digest},
        }]
        metadata = dict(self.metadata)
        metadata["coverage_manifest_sha256"] = sha256_json(coverage)
        scope = {"scope_id": "teardown-s", "case_id": "mario-fd", "route_id": "mario-fd",
                 "phase": "teardown", "input_manifest_sha256": self.input_digest}
        capture = self.capture(records=[
            {"sequence": 1, "kind": "scope_begin", "scope": scope},
            {"sequence": 2, "kind": "scope_end", "scope": scope},
        ])
        capture["descriptors"] = []
        capture["sequence_end"] = 2
        capture["status"] = dict(capture["status"], total_records=2, drained_records=2)
        capture["provenance"] = dict(capture["provenance"], coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        result = generate(capture, self.seed, metadata, coverage)
        self.assertTrue(result["status"]["certified"])
        self.assertEqual(result["groups"][0]["members"], [])

    def test_worldless_boot_empty_scope_is_a_valid_boundary(self):
        coverage = dict(self.coverage)
        coverage["cases"] = [{
            "case_id": "mario-fd", "route_id": "mario-fd", "route": {"route_id": "mario-fd"},
            "expected_phases": [{"scene": "boot", "phase": "preparation"}],
            "expected_actions": [], "expected_costumes": [], "lifecycle": ["preparation"],
            "input": {"capture_id": self.capture_id, "sha256": self.input_digest},
        }]
        metadata = dict(self.metadata)
        metadata["coverage_manifest_sha256"] = sha256_json(coverage)
        scope = {"scope_id": "boot-s", "case_id": "mario-fd", "route_id": "mario-fd",
                 "scene": "boot", "phase": "preparation", "world_generation": 0,
                 "input_manifest_sha256": self.input_digest}
        capture = self.capture(records=[
            {"sequence": 1, "kind": "scope_begin", "scope": scope},
            {"sequence": 2, "kind": "scope_end", "scope": scope},
        ])
        capture["descriptors"] = []
        capture["sequence_end"] = 2
        capture["status"] = dict(capture["status"], total_records=2, drained_records=2)
        capture["provenance"] = dict(capture["provenance"],
                                      coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        result = generate(capture, self.seed, metadata, coverage)
        self.assertEqual(result["status"]["kind"], "certificate")
        self.assertTrue(result["status"]["certified"])
        self.assertEqual(result["status"]["incomplete_scopes"], [])
        self.assertEqual(result["groups"][0]["members"], [])

    def test_empty_boot_and_preparation_scopes_do_not_validate_route_content(self):
        coverage = dict(self.coverage)
        coverage["cases"] = [{
            "case_id": "mario-fd", "route_id": "mario-fd",
            "route": {
                "route_id": "mario-fd", "fighter_numeric_ids": [1, 1],
                "costume_numeric_ids": [0, 0], "stage_numeric_id": 3,
                "ground_numeric_id": 37, "menu_fighter_numeric_ids": [8, 8],
                "menu_costume_numeric_ids": [1, 0], "menu_stage_numeric_id": 32,
                "menu_ground_numeric_id": 36,
            },
            "expected_phases": [
                {"scene": "boot", "phase": "preparation"},
                {"scene": "css", "phase": "preparation"},
            ],
            "expected_actions": ["Wait"], "expected_costumes": [],
            "lifecycle": ["preparation"],
            "input": {"capture_id": self.capture_id, "sha256": self.input_digest},
        }]
        metadata = dict(self.metadata, coverage_manifest_sha256=sha256_json(coverage))
        boot = {"scope_id": "boot-empty-s", "case_id": "mario-fd", "route_id": "mario-fd",
                "scene": 1, "phase": 1, "world_generation": 0,
                "input_manifest_sha256": self.input_digest, "stage": 0, "ground": 0}
        css = {"scope_id": "css-empty-prep-s", "case_id": "mario-fd", "route_id": "mario-fd",
               "scene": 2, "phase": 1, "world_generation": 0,
               "input_manifest_sha256": self.input_digest, "stage": 32, "ground": 37}
        capture = self.capture(records=[
            {"sequence": 1, "kind": "scope_begin", "scope": boot},
            {"sequence": 2, "kind": "scope_end", "scope": boot},
            {"sequence": 3, "kind": "scope_begin", "scope": css},
            {"sequence": 4, "kind": "scope_end", "scope": css},
        ])
        capture["sequence_end"] = 4
        capture["status"] = dict(capture["status"], total_records=4, drained_records=4)
        capture["provenance"] = dict(capture["provenance"],
                                      coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        result = generate(capture, self.seed, metadata, coverage)
        # The empty scopes are valid setup boundaries, but their default
        # context cannot satisfy the declared source action requirement.
        self.assertEqual(result["status"]["kind"], "incomplete")
        self.assertFalse(result["status"]["certified"])
        self.assertEqual(result["status"]["incomplete_scopes"][0]["missing_actions"], ["Wait"])
        self.assertEqual(result["groups"][0]["members"], [])
        self.assertNotIn("content_mismatch", {item["code"] for item in result["status"]["errors"]})

    def test_deferred_demand_after_empty_preparation_scope_keeps_route_check_strict(self):
        coverage = dict(self.coverage)
        coverage["cases"] = [{
            "case_id": "mario-fd", "route_id": "mario-fd",
            "route": {"route_id": "mario-fd", "menu_identities": [{
                "fighter_numeric_ids": [1, 1], "costume_numeric_ids": [0, 0],
                "stage_numeric_id": 3, "ground_numeric_id": 37,
            }]},
            "expected_phases": [{"scene": "css", "phase": "preparation"}],
            "expected_actions": ["Wait"], "expected_costumes": [0],
            "lifecycle": ["preparation"],
            "input": {"capture_id": self.capture_id, "sha256": self.input_digest},
        }]
        metadata = dict(self.metadata, coverage_manifest_sha256=sha256_json(coverage))
        scope = {"scope_id": "css-deferred-s", "case_id": "mario-fd", "route_id": "mario-fd",
                 "scene": 2, "phase": 1, "world_generation": 1,
                 "input_manifest_sha256": self.input_digest, "active_player_count": 2,
                 "players": [{"character": 8, "costume": 1}, {"character": 8, "costume": 0}],
                 "stage": 32, "ground": 36}
        capture = self.capture(records=[
            {"sequence": 1, "kind": "scope_begin", "scope": scope},
            {"sequence": 2, "kind": "scope_end", "scope": scope},
            {"sequence": 3, "kind": "draw_use",
             "scope": dict(scope, action="Wait", lifecycle="preparation"),
             "descriptor": self.descriptor(7, b"descriptor-seven")},
        ])
        capture["sequence_end"] = 3
        capture["status"] = dict(capture["status"], total_records=3, drained_records=3)
        capture["provenance"] = dict(capture["provenance"],
                                      coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        self.assert_reject(capture, metadata=metadata, coverage=coverage, code="content_mismatch")

    def test_worldless_interactive_source_demand_is_incomplete(self):
        coverage = dict(self.coverage)
        coverage["cases"] = [{
            "case_id": "mario-fd", "route_id": "mario-fd", "route": {"route_id": "mario-fd"},
            "expected_phases": [{"scene": "match", "phase": "interactive"}],
            "expected_actions": ["Jab"], "expected_costumes": [0], "lifecycle": ["interactive"],
            "input": {"capture_id": self.capture_id, "sha256": self.input_digest},
        }]
        metadata = dict(self.metadata)
        metadata["coverage_manifest_sha256"] = sha256_json(coverage)
        scope = {"scope_id": "match-interactive-s", "case_id": "mario-fd", "route_id": "mario-fd",
                 "scene": 4, "phase": 4, "world_generation": 0,
                 "input_manifest_sha256": self.input_digest}
        capture = self.capture(records=[
            {"sequence": 1, "kind": "scope_begin", "scope": scope},
            {"sequence": 2, "kind": "draw_use", "scope": dict(scope, action="Jab", costumes=[0], lifecycle="interactive"),
             "descriptor": self.descriptor(7, b"descriptor-seven")},
            {"sequence": 3, "kind": "scope_end", "scope": scope},
        ])
        capture["sequence_end"] = 3
        capture["status"] = dict(capture["status"], total_records=3, drained_records=3)
        capture["provenance"] = dict(capture["provenance"],
                                      coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        result = generate(capture, self.seed, metadata, coverage)
        self.assertEqual(result["status"]["kind"], "incomplete")
        self.assertFalse(result["status"]["certified"])
        self.assertGreater(result["status"]["incomplete_scopes"][0]["unbound_source_scopes"], 0)

    def test_worldless_empty_entry_scope_is_incomplete(self):
        coverage = dict(self.coverage)
        coverage["cases"] = [{
            "case_id": "mario-fd", "route_id": "mario-fd", "route": {"route_id": "mario-fd"},
            "expected_phases": [{"scene": "match", "phase": "entry"}],
            "expected_actions": [], "expected_costumes": [], "lifecycle": ["entry"],
            "input": {"capture_id": self.capture_id, "sha256": self.input_digest},
        }]
        metadata = dict(self.metadata)
        metadata["coverage_manifest_sha256"] = sha256_json(coverage)
        scope = {"scope_id": "match-entry-s", "case_id": "mario-fd", "route_id": "mario-fd",
                 "scene": 4, "phase": 2, "world_generation": 0,
                 "input_manifest_sha256": self.input_digest}
        capture = self.capture(records=[
            {"sequence": 1, "kind": "scope_begin", "scope": scope},
            {"sequence": 2, "kind": "scope_end", "scope": scope},
        ])
        capture["descriptors"] = []
        capture["sequence_end"] = 2
        capture["status"] = dict(capture["status"], total_records=2, drained_records=2)
        capture["provenance"] = dict(capture["provenance"],
                                      coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        result = generate(capture, self.seed, metadata, coverage)
        self.assertEqual(result["status"]["kind"], "incomplete")
        self.assertFalse(result["status"]["certified"])
        self.assertGreater(result["status"]["incomplete_scopes"][0]["unbound_source_scopes"], 0)

    def test_demand_without_scope_pair_is_rejected(self):
        capture = self.capture(records=[self.capture()["records"][1]])
        capture["sequence_end"] = 1
        capture["status"] = dict(capture["status"], total_records=1, drained_records=1)
        self.assert_reject(capture, code="scope_pair")

    def test_worldless_numeric_preparation_is_explicitly_incomplete(self):
        base = self.capture()
        records = base["records"][3:6]
        for index, row in enumerate(records, 1):
            row["sequence"] = index
            row["scope"]["phase"] = 1
            row["scope"]["scene"] = 4
            row["scope"]["world_generation"] = 0
        capture = self.capture(records=records)
        capture["sequence_end"] = 3
        capture["status"] = dict(capture["status"], total_records=3, drained_records=3)
        coverage = dict(self.coverage)
        coverage["cases"] = [{
            "case_id": "mario-fd", "coverage_case_id": 17, "route_id": "mario-fd",
            "route": {"route_id": "mario-fd", "fighter_numeric_ids": [1, 1], "stage_numeric_id": 3},
            "expected_scenes": ["match"], "expected_phases": ["preparation"],
            "expected_actions": ["Jab"], "expected_costumes": [0], "lifecycle": ["preparation"],
            "input": {"capture_id": self.capture_id, "sha256": self.input_digest},
        }]
        metadata = dict(self.metadata)
        metadata["coverage_manifest_sha256"] = sha256_json(coverage)
        capture["provenance"] = dict(capture["provenance"], coverage_manifest_sha256=metadata["coverage_manifest_sha256"])
        result = generate(capture, self.seed, metadata, coverage)
        self.assertEqual(result["status"]["kind"], "incomplete")
        self.assertGreater(result["status"]["incomplete_scopes"][0]["unbound_source_scopes"], 0)


if __name__ == "__main__":
    unittest.main()
