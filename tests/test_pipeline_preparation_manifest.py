"""Focused tests for the finite preparation-union code generator."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from scripts.generate_pipeline_preparation import (
    PipelinePreparationError,
    canonical_json,
    generate_preparation,
    sha256_bytes,
    sha256_json,
)


class PreparationManifestTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)

    def tearDown(self) -> None:
        self.directory.cleanup()

    @staticmethod
    def _descriptor(kind: int, ref: int, digest_byte: str) -> dict:
        return {
            "type": kind,
            "ref_hex": f"{ref:016x}",
            "config_version": 7,
            "size": 32,
            "sha256": digest_byte * 64,
        }

    @staticmethod
    def _route(fighters: list[int], costumes: list[int], stage: int = 3,
               ground: int = 37) -> dict:
        return {
            "route_id": "route-placeholder",
            "fighter_numeric_ids": fighters,
            "costume_numeric_ids": costumes,
            "stage_numeric_id": stage,
            "ground_numeric_id": ground,
        }

    def _write_json(self, name: str, value: object) -> Path:
        path = self.root / name
        path.write_bytes(canonical_json(value) + b"\n")
        return path

    def _pair(self, name: str, route_id: str, route: dict | None = None,
              *, binding_overrides: dict | None = None,
              descriptor_conflict: bool = False,
              include_boot: bool = True,
              include_match: bool = True,
              include_teardown: bool = True,
              clear_match: bool = False,
              coverage_case_id: int = 17) -> dict[str, Path]:
        route = dict(route or self._route([8, 8], [1, 0]))
        route["route_id"] = route_id
        case_id = f"case-{name}"
        capture_id = f"capture-{name}"
        input_sha = "9" * 64
        coverage = {
            "schema": "melee-web-pipeline-coverage-v1",
            "version": 1,
            "cases": [{
                "case_id": case_id,
                "coverage_case_id": coverage_case_id,
                "route_id": route_id,
                "route": route,
                "expected_phases": [
                    {"scene": "css", "phase": "preparation"},
                    {"scene": "sss", "phase": "preparation"},
                    {"scene": "match", "phase": "interactive"},
                ],
                "expected_actions": [14],
                "expected_costumes": [0, 1],
                "lifecycle": ["interactive", "teardown"],
                "input": {"capture_id": capture_id, "sha256": input_sha},
            }],
        }
        coverage_path = self._write_json(f"{name}-coverage.json", coverage)
        coverage_digest = sha256_bytes(coverage_path.read_bytes())

        binding = {
            "seed_decoded_sha256": "a" * 64,
            "source_head": "b" * 40,
            "dirty_overlay_sha256": "c" * 64,
            "dependencies": {"aurora": "d" * 40, "melee": "e" * 40},
            "renderer": {
                "version": "renderer-v1",
                "config_layout": "layout-v1",
                "config_layout_sha256": "f" * 64,
            },
            "registry_sha256": "1" * 64,
            "coverage_manifest_sha256": coverage_digest,
        }
        if binding_overrides:
            binding.update(binding_overrides)

        boot = self._descriptor(0, 0x10, "0")
        css = self._descriptor(1, 0x20, "1")
        sss = self._descriptor(2, 0x30, "2")
        match = self._descriptor(0 if clear_match else 1, 0x40, "3")
        teardown = self._descriptor(1, 0x50, "4")
        descriptors = [boot, css, sss, match, teardown]
        if descriptor_conflict:
            # The CSS member remains keyed by (type, ref_hex), but its complete
            # identity conflicts with the shared CSS descriptor in another pair.
            css = dict(css, sha256="6" * 64)
            descriptors[1] = css
        provenance = [{"case_id": case_id, "sequence": 1}]

        def member(descriptor: dict) -> dict:
            return dict(descriptor, provenance=provenance)

        groups = [
            {"id": f"{route_id}:empty", "route_id": route_id, "scene": 6,
             "phase": 9, "members": []},
            {"id": f"{route_id}:css", "route_id": route_id, "scene": 2,
             "phase": 1, "members": [member(css), member(css)]},
            {"id": f"{route_id}:sss", "route_id": route_id, "scene": 3,
             "phase": 1, "members": [member(sss)]},
        ]
        if include_boot:
            groups.append({"id": f"{route_id}:boot", "route_id": route_id, "scene": 1,
                           "phase": 1, "members": [member(boot)]})
        if include_match:
            groups.append({"id": f"{route_id}:match", "route_id": route_id, "scene": 4,
                           "phase": 4, "members": [member(match)]})
        if include_teardown:
            groups.append({"id": f"{route_id}:teardown", "route_id": route_id, "scene": 5,
                           "phase": 8, "members": [member(teardown)]})

        # This catalog entry is deliberately unused and must never leak into
        # either generated descriptor union.
        unused = self._descriptor(1, 0x99, "9")
        requirements = {
            "schema": "melee-web-pipeline-requirements-v1",
            "version": 1,
            "status": {
                "kind": "certificate",
                "valid": True,
                "certified": True,
                "errors": [],
                "incomplete_scopes": [],
            },
            "binding": binding,
            "capture": {"capture_sha256": "2" * 64},
            "descriptors": descriptors + [unused],
            "groups": groups,
            "coverage": {
                "manifest_sha256": coverage_digest,
                "cases": [{
                    "case_id": case_id,
                    "coverage_case_id": coverage_case_id,
                    "route_id": route_id,
                    "route_identity_sha256": sha256_json(route),
                    "expected_phases": [1, 1, 4],
                    "expected_contexts": [
                        {"scene": 2, "phase": 1},
                        {"scene": 3, "phase": 1},
                        {"scene": 4, "phase": 4},
                    ],
                    "expected_actions": [14],
                    "expected_costumes": [0, 1],
                    "lifecycle": ["interactive", "teardown"],
                    "expected_source_ticks": [],
                    "input_sha256": input_sha,
                    "capture_id": capture_id,
                }],
            },
        }
        requirements_path = self._write_json(f"{name}-requirements.json", requirements)
        return {"requirements": requirements_path, "coverage": coverage_path}

    @staticmethod
    def _error_code(callable_object, *args, **kwargs) -> str:
        with unittest.TestCase().assertRaises(PipelinePreparationError) as context:
            callable_object(*args, **kwargs)
        return context.exception.code

    def test_generates_one_compact_finite_union_without_capture_or_route_data(self) -> None:
        pair = self._pair("one", "route-one")
        metadata, header = generate_preparation([pair])

        self.assertEqual(metadata["schema"], "melee-web-pipeline-preparation-v1")
        self.assertEqual(metadata["version"], 1)
        self.assertTrue(metadata["finite"])
        self.assertFalse(metadata["exhaustive"])
        self.assertEqual(metadata["kind"], "conservative_certified_union")
        self.assertEqual(metadata["descriptor_count"], 5)
        self.assertEqual(metadata["config_bytes"], 5 * 32)
        all_descriptors = metadata["descriptor_union"]
        self.assertNotIn("0000000000000099", [descriptor["ref_hex"] for descriptor in all_descriptors])
        self.assertIn("melee_web_pipeline_preparation_union", header)
        self.assertIn("melee_web_pipeline_preparation_unionCount", header)
        self.assertIn(f'#define MELEE_WEB_PIPELINE_PREPARATION_BINDING_SHA256 "{sha256_json(metadata)}"', header)
        self.assertIn('#define MELEE_WEB_PIPELINE_PREPARATION_SEED_DECODED_SHA256 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"', header)
        self.assertIn("UINT64_C(0x0000000000000040)", header)
        self.assertNotIn("MeleeWebPipelinePreparationRoute", header)
        self.assertNotIn("capture-one", json.dumps(metadata) + header)
        self.assertNotIn("route-one", json.dumps(metadata) + header)
        self.assertNotIn(str(self.root), json.dumps(metadata) + header)

    def test_descriptor_union_is_deduplicated_and_input_order_is_deterministic(self) -> None:
        first = self._pair("one", "route-one")
        second = self._pair("two", "route-two",
                            self._route([8, 1], [1, 0], stage=4, ground=38),
                            coverage_case_id=18)
        metadata_a, header_a = generate_preparation([first, second])
        metadata_b, header_b = generate_preparation([second, first])

        self.assertEqual(metadata_a, metadata_b)
        self.assertEqual(header_a, header_b)
        self.assertEqual(metadata_a["descriptor_count"], 5)
        self.assertEqual(len(metadata_a["descriptor_union"]), 5)

    def test_conservative_union_retains_shared_clear_exactly_once(self) -> None:
        pair = self._pair("clear", "route-clear", include_boot=False, clear_match=True)
        metadata, _header = generate_preparation([pair])
        clear = next(descriptor for descriptor in metadata["descriptor_union"]
                     if descriptor["type"] == 0)
        self.assertEqual(metadata["descriptor_union"].count(clear), 1)

    def test_rejects_descriptors_outside_runtime_abi(self) -> None:
        for field, value, expected in [("type", 3, "descriptor_type"),
                                       ("size", 0, "descriptor_config"),
                                       ("size", 16 * 1024 * 1024 + 1, "descriptor_config")]:
            with self.subTest(field=field, value=value):
                pair = self._pair("bad", "route-bad")
                requirements = json.loads(pair["requirements"].read_text())
                requirements["descriptors"][0][field] = value
                pair["requirements"].write_bytes(canonical_json(requirements) + b"\n")
                self.assertEqual(self._error_code(generate_preparation, [pair]), expected)

    def test_rejects_cross_type_global_reference_collision(self) -> None:
        pair = self._pair("collision", "route-collision")
        requirements = json.loads(pair["requirements"].read_text())
        for descriptor in requirements["descriptors"]:
            if descriptor["type"] == 2:
                descriptor["ref_hex"] = "0000000000000020"
        for group in requirements["groups"]:
            for member in group["members"]:
                if member["type"] == 2:
                    member["ref_hex"] = "0000000000000020"
        pair["requirements"].write_bytes(canonical_json(requirements) + b"\n")
        self.assertEqual(self._error_code(generate_preparation, [pair]), "descriptor_conflict")

    def test_rejects_invalid_certificate_status(self) -> None:
        pair = self._pair("one", "route-one")
        requirements = json.loads(pair["requirements"].read_text())
        requirements["status"]["certified"] = False
        pair["requirements"].write_bytes(canonical_json(requirements) + b"\n")
        self.assertEqual(self._error_code(generate_preparation, [pair]), "certificate_status")

    def test_rejects_stale_common_binding(self) -> None:
        first = self._pair("one", "route-one")
        second = self._pair("two", "route-two",
                            self._route([8, 1], [1, 0], stage=4, ground=38),
                            binding_overrides={"seed_decoded_sha256": "7" * 64},
                            coverage_case_id=18)
        self.assertEqual(self._error_code(generate_preparation, [first, second]), "binding_conflict")

    def test_rejects_descriptor_conflict_across_inputs(self) -> None:
        first = self._pair("one", "route-one")
        second = self._pair("two", "route-two",
                            self._route([8, 1], [1, 0], stage=4, ground=38),
                            descriptor_conflict=True, coverage_case_id=18)
        self.assertEqual(self._error_code(generate_preparation, [first, second]), "descriptor_conflict")

    def test_rejects_forged_or_crossed_route_identity(self) -> None:
        pair = self._pair("one", "route-one")
        requirements = json.loads(pair["requirements"].read_text())
        requirements["coverage"]["cases"][0]["route_id"] = "route-crossed"
        pair["requirements"].write_bytes(canonical_json(requirements) + b"\n")
        self.assertEqual(self._error_code(generate_preparation, [pair]), "route_binding")

        pair = self._pair("unknown", "route-known")
        requirements = json.loads(pair["requirements"].read_text())
        next(group for group in requirements["groups"] if group["members"])["route_id"] = "route-unknown"
        pair["requirements"].write_bytes(canonical_json(requirements) + b"\n")
        self.assertEqual(self._error_code(generate_preparation, [pair]), "unknown_route")

    def test_rejects_missing_union_and_bootstrap(self) -> None:
        pair = self._pair("one", "route-one", include_match=False, include_teardown=False)
        requirements = json.loads(pair["requirements"].read_text())
        for group in requirements["groups"]:
            group["members"] = []
        pair["requirements"].write_bytes(canonical_json(requirements) + b"\n")
        self.assertEqual(self._error_code(generate_preparation, [pair]), "missing_union")
        pair = self._pair("two", "route-two", include_boot=False)
        self.assertEqual(self._error_code(generate_preparation, [pair]), "bootstrap_missing")

        pair = self._pair("nonclear-boot", "route-nonclear-boot")
        requirements = json.loads(pair["requirements"].read_text())
        boot_group = next(group for group in requirements["groups"] if group["scene"] == 1)
        boot_group["members"][0]["type"] = 1
        boot_catalog = next(descriptor for descriptor in requirements["descriptors"]
                            if descriptor["ref_hex"] == "0000000000000010")
        boot_catalog["type"] = 1
        pair["requirements"].write_bytes(canonical_json(requirements) + b"\n")
        self.assertEqual(self._error_code(generate_preparation, [pair]), "bootstrap_missing")

    def test_empty_boot_group_can_use_a_demanded_clear_descriptor(self) -> None:
        pair = self._pair("clear", "route-clear", include_boot=False, clear_match=True)
        metadata, _header = generate_preparation([pair])
        clear = next(descriptor for descriptor in metadata["descriptor_union"] if descriptor["type"] == 0)
        self.assertEqual(clear["type"], 0)
        self.assertEqual(clear["ref_hex"],
                         "0000000000000040")

    def test_return_scene_joins_the_global_css_union(self) -> None:
        pair = self._pair("return", "route-return")
        requirements = json.loads(pair["requirements"].read_text())
        css_group = next(group for group in requirements["groups"] if group["scene"] == 2)
        css_member = css_group["members"][0]
        css_group["members"] = []
        requirements["groups"].append({
            "id": "route-return:6:9", "route_id": "route-return", "scene": 6,
            "phase": 9, "members": [css_member],
        })
        pair["requirements"].write_bytes(canonical_json(requirements) + b"\n")
        metadata, _header = generate_preparation([pair])
        self.assertEqual(metadata["descriptor_count"], 5)

    def test_results_scene_is_recognized_but_unknown_scene_is_rejected(self) -> None:
        pair = self._pair("results", "route-results")
        requirements = json.loads(pair["requirements"].read_text())
        match_group = next(group for group in requirements["groups"]
                           if group["scene"] == 4)
        requirements["groups"].append({
            "id": "route-results:results",
            "route_id": "route-results",
            "scene": 7,
            "phase": 1,
            "members": [match_group["members"][0]],
        })
        pair["requirements"].write_bytes(canonical_json(requirements) + b"\n")
        metadata, _header = generate_preparation([pair])
        self.assertEqual(metadata["descriptor_count"], 5)

        requirements["groups"][-1]["scene"] = 8
        pair["requirements"].write_bytes(canonical_json(requirements) + b"\n")
        metadata, _header = generate_preparation([pair])
        self.assertEqual(metadata["descriptor_count"], 5)

        requirements["groups"][-1]["scene"] = 9
        pair["requirements"].write_bytes(canonical_json(requirements) + b"\n")
        self.assertEqual(self._error_code(generate_preparation, [pair]), "group_scene")

    def test_empty_groups_and_unused_dictionary_rows_do_not_add_descriptors(self) -> None:
        pair = self._pair("one", "route-one")
        metadata, _header = generate_preparation([pair])
        all_descriptors = metadata["descriptor_union"]
        self.assertNotIn("0000000000000099", [descriptor["ref_hex"] for descriptor in all_descriptors])
        self.assertEqual(len([d for d in all_descriptors if d["ref_hex"] == "0000000000000020"]), 1)

        pair = self._pair("provenance", "route-provenance")
        requirements = json.loads(pair["requirements"].read_text())
        next(group for group in requirements["groups"] if group["scene"] == 2)["members"][0]["provenance"] = []
        pair["requirements"].write_bytes(canonical_json(requirements) + b"\n")
        self.assertEqual(self._error_code(generate_preparation, [pair]), "provenance")


if __name__ == "__main__":
    unittest.main()
