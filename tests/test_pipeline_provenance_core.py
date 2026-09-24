"""Static contract checks for the private pipeline provenance recorder.

The capture/Dolphin worktree is intentionally active while this sidecar is
being prepared.  These checks inspect the C ABI and source only; they never
invoke a compiler, build, browser, or game capture.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src" / "pipeline_provenance.h"
SOURCE = ROOT / "src" / "pipeline_provenance.cpp"
FIXTURE = ROOT / "tests" / "pipeline_provenance_test.cpp"


class PipelineProvenanceCoreStaticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.fixture = FIXTURE.read_text(encoding="utf-8")

    def test_private_build_guards_are_present(self):
        self.assertIn("#error", self.header)
        self.assertIn("MELEE_WEB_PUBLIC_RUNTIME", self.header)
        self.assertIn("#if defined(MELEE_WEB_PIPELINE_PROVENANCE)", self.source)
        self.assertIn("#if defined(MELEE_WEB_PUBLIC_RUNTIME)", self.source)

    def test_header_is_c_compatible_and_pointer_free_in_output_contract(self):
        self.assertIn('extern "C"', self.header)
        self.assertNotIn("<string>", self.header)
        self.assertNotIn("std::", self.header)
        for field in (
            "world_generation",
            "route_epoch",
            "source_tick",
            "coverage_case_id",
            "stage",
            "ground",
            "hud_layout",
            "gobj_classifier",
            "gx_link",
            "render_pass",
            "owner_kind",
            "owner_id",
            "owner_effect_bank",
            "motion_id",
            "stocks",
        ):
            self.assertRegex(self.header, rf"\b{field}\b")

    def test_schema_and_sequence_fields_are_stable(self):
        self.assertIn('"melee-web-pipeline-use-v1"', self.header)
        for field in (
            "schema",
            "version",
            "capture_generation",
            "sequence_begin",
            "sequence_end",
            "records",
            "descriptors",
            "status",
            "descriptor_sha256",
            "pipeline_ref",
        ):
            self.assertIn(field, self.source)
        self.assertIn('"0x%016llx"', self.source)
        self.assertIn('\\"scope_id\\"', self.source)
        self.assertIn('\\"thread_id\\"', self.source)

    def test_all_lookup_and_use_events_are_named(self):
        for name in (
            "LAST_REF",
            "READY",
            "PENDING",
            "CREATE",
            "IMPORT",
            "DRAW_CACHE_REUSE",
            "MERGE",
            "PACKET_USE",
        ):
            self.assertIn(f"MELEE_WEB_PIPELINE_EVENT_{name}", self.header)
            self.assertRegex(self.source, rf'case MELEE_WEB_PIPELINE_EVENT_{name}:')
        self.assertIn("current_context_locked", self.source)
        self.assertIn("execution_depth", self.source)

    def test_descriptor_bytes_are_hashed_and_not_serialized(self):
        self.assertIn("class Sha256", self.source)
        self.assertIn("std::array<uint8_t, 32> digest", self.source)
        self.assertIn("const auto descriptor_sha256 = digest(blob, blob_length)", self.source)
        self.assertIn("descriptor_sha256", self.source)
        self.assertNotIn("output += blob", self.source)
        self.assertNotIn("config_bytes", self.source)
        self.assertIn("descriptor.bytes", self.source)

    def test_generation_and_scope_fail_closed_paths_are_present(self):
        for reason in (
            "MISSING_CONTEXT",
            "OVERFLOW",
            "DESCRIPTOR_OVERFLOW",
            "STALE_CAPTURE",
            "STALE_DEVICE",
            "STALE_RENDERER",
            "UNPAIRED_SCOPE",
        ):
            self.assertIn(f"MELEE_WEB_PIPELINE_INVALID_{reason}", self.source)
        self.assertIn("token_nonce", self.source)
        self.assertIn("token->closed", self.source)
        self.assertIn("final pipeline provenance snapshot has active source scopes", self.source)

    def test_imports_are_boot_attributed_and_invalid_records_remain_drainable(self):
        self.assertIn("const bool is_import = event_kind == MELEE_WEB_PIPELINE_EVENT_IMPORT", self.source)
        self.assertIn("if (recorder->boot_context_valid) context = &recorder->boot_context", self.source)
        self.assertIn("const uint64_t output_frame = is_import ? 0", self.source)
        self.assertIn("const uint64_t output_packet = is_import ? 0", self.source)
        self.assertNotIn('"pipeline provenance capture is already invalid"', self.source)
        self.assertIn("context.world_generation == 0", self.source)
        self.assertIn("MELEE_WEB_PIPELINE_PHASE_PREPARATION", self.source)
        self.assertIn("if (final_snapshot)", self.source)
        self.assertIn("for (const auto& thread : recorder->threads)", self.source)

    def test_scene_ids_are_stable_and_results_is_preparation_only_without_world(self):
        values = dict(re.findall(
            r"MELEE_WEB_PIPELINE_SCENE_(BOOT|CSS|SSS|MATCH|TEARDOWN|RETURN|RESULTS|PRIZE)\s*=\s*(\d+)",
            self.header))
        self.assertEqual(values, {
            "BOOT": "1", "CSS": "2", "SSS": "3", "MATCH": "4",
            "TEARDOWN": "5", "RETURN": "6", "RESULTS": "7", "PRIZE": "8",
        })
        self.assertIn("scene <= MELEE_WEB_PIPELINE_SCENE_PRIZE", self.source)
        self.assertIn("context.scene != MELEE_WEB_PIPELINE_SCENE_RESULTS", self.source)
        self.assertIn("context.scene != MELEE_WEB_PIPELINE_SCENE_PRIZE", self.source)
        self.assertIn("check_results_preparation_context", self.fixture)
        self.assertIn("MELEE_WEB_PIPELINE_PHASE_INTERACTIVE", self.fixture)

    def test_descriptor_identity_includes_type_and_config_version(self):
        self.assertRegex(self.source, r"descriptor\.type == type")
        self.assertRegex(self.source, r"descriptor\.config_version == config_version")
        self.assertRegex(self.source, r"descriptor\.type == event\.type")
        self.assertRegex(self.source, r"descriptor\.config_version == event\.config_version")

    def test_fixture_covers_deferred_use_and_ring_behaviors(self):
        for phrase in (
            "Repeated typed uses share one dictionary entry",
            "Closing the source scope before worker consumption is valid",
            "MELEE_WEB_PIPELINE_EVENT_DRAW_CACHE_REUSE",
            "MELEE_WEB_PIPELINE_EVENT_MERGE",
            "MELEE_WEB_PIPELINE_EVENT_PACKET_USE",
            "INVALID_MISSING_CONTEXT",
            "INVALID_UNPAIRED_SCOPE",
            "INVALID_OVERFLOW",
            "INVALID_STALE_DEVICE",
            "EVENT_IMPORT",
            "check_final_snapshot_preserves_unpaired_evidence",
            r'\"open_scopes\":1',
            "std::thread worker",
            "altered.context.source_tick",
        ):
            self.assertIn(phrase, self.fixture)

    def test_only_fixed_bounded_collections_back_recorder_state(self):
        self.assertIn("MELEE_WEB_PIPELINE_MAX_RECORDS", self.header)
        self.assertIn("MELEE_WEB_PIPELINE_MAX_DESCRIPTORS", self.header)
        self.assertIn("MELEE_WEB_PIPELINE_MAX_SCOPES", self.header)
        self.assertIn("MELEE_WEB_PIPELINE_MAX_EXECUTION_DEPTH", self.header)
        self.assertIn("std::array<ActiveScope", self.source)
        self.assertIn("std::array<ThreadState", self.source)
        self.assertRegex(self.source, r"records\.size\(\)\s*>=\s*recorder->max_records")
        self.assertRegex(self.source, r"descriptors\.size\(\)\s*>=\s*recorder->max_descriptors")


if __name__ == "__main__":
    unittest.main()
