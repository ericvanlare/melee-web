"""Focused compile/provenance checks for the post-audio source units."""
from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from tools import source_post_audio_allocations as probe


class SourcePostAudioAllocationTests(unittest.TestCase):
    def test_contract_is_argumentless_and_does_not_admit_capture_inputs(self):
        self.assertEqual(
            probe.ARGUMENT_CONTRACT["lbMemory_8001564C"]["prototype"],
            "void lbMemory_8001564C(void)",
        )
        self.assertEqual(
            probe.ARGUMENT_CONTRACT["lbHeap_80015F3C"]["prototype"],
            "void lbHeap_80015F3C(void)",
        )
        for contract in probe.ARGUMENT_CONTRACT.values():
            self.assertEqual(contract["captured_inputs_forbidden"], True)
            self.assertNotIn("capture.mwro", json.dumps(contract))
        self.assertEqual(
            probe.DEPENDENCY_CLOSURE["lbHeap_80015F3C"]["direct_source_calls"],
            ["HSD_GetNextArena", "lbMemory_800154BC"],
        )
        self.assertIn("lbHeap_803BA380 authored descriptor table",
                      probe.DEPENDENCY_CLOSURE["lbHeap_80015F3C"]["owned_state"])

    def test_pinned_source_identity_and_prepared_body_scope(self):
        if not probe.MELEE.is_dir() or not (probe.MELEE / ".git").is_dir():
            self.skipTest("pinned Melee checkout unavailable")
        source = probe._verify_source_tree()
        self.assertEqual(source["revision"], probe.SOURCE_REVISION)
        self.assertEqual(source["files"], {
            name: unit["sha256"] for name, unit in probe.SOURCE_UNITS.items()
        })
        if not probe.EMCC.is_file():
            self.skipTest("pinned Emscripten compiler unavailable")
        probe._prepare_sources()
        prepared = probe._verify_prepared_units()
        self.assertEqual(set(prepared), set(probe.SOURCE_UNITS))

    def test_compile_profile_is_object_only_and_has_typed_accessors(self):
        if not probe.EMCC.is_file() or not probe.LLVM_NM.is_file():
            self.skipTest("pinned Emscripten compiler or llvm-nm unavailable")
        if not probe.MELEE.is_dir() or not (probe.MELEE / ".git").is_dir():
            self.skipTest("pinned Melee checkout unavailable")
        with tempfile.TemporaryDirectory(prefix="post-audio-compile-test-") as temporary:
            artifact = Path(temporary) / "compile"
            receipt = probe.compile_profile(artifact)
            self.assertEqual(receipt["schema"],
                             "melee-web-source-post-audio-allocations-compile")
            self.assertEqual(receipt["version"], 2)
            self.assertFalse(receipt["link_performed"])
            self.assertFalse(receipt["runtime_executed"])
            self.assertFalse(receipt["captured_inputs_consumed"])
            self.assertFalse(receipt["runtime_claim"])
            for name, unit in probe.SOURCE_UNITS.items():
                compiled = receipt["units"][name]
                self.assertEqual(compiled["status"], "compiled")
                self.assertIn(unit["prepared_symbol"], compiled["defined_symbols"])
                self.assertTrue((artifact / f"{name}.o").is_file())
                command = json.loads((artifact / f"{name}.command.json").read_text())
                self.assertIn("-c", command)
                self.assertNotIn("-sEXIT_RUNTIME=1", command)
                self.assertIn(f"melee_web_source_{name}_snapshot",
                              compiled["defined_symbols"])
            self.assertIn("ARAlloc", receipt["units"]["lbmemory"]["undefined_symbols"])
            self.assertIn("HSD_GetNextArena", receipt["units"]["lbheap"]["undefined_symbols"])
            accessor = receipt["accessors"]
            self.assertTrue(accessor["same_translation_unit"])
            self.assertEqual(accessor["unit_names"], sorted(probe.SOURCE_UNITS))
            self.assertTrue((artifact / "lbmemory_accessors.c").is_file())
            self.assertTrue((artifact / "lbheap_accessors.c").is_file())
            self.assertFalse((artifact / "source_post_audio_allocations_accessors.o").exists())
            self.assertEqual(json.loads((artifact / "receipt.json").read_text()), receipt)
            self.assertEqual(probe.validate_profile(artifact / "receipt.json"), receipt)

            receipt_path = artifact / "receipt.json"
            forged = json.loads(receipt_path.read_text())
            forged["units"]["lbheap"]["defined_symbols"] = [
                "lbHeap_80015F3C", "melee_web_source_lbheap_snapshot"
            ]
            receipt_path.write_text(json.dumps(forged) + "\n")
            with self.assertRaisesRegex(probe.PostAudioCompileError,
                                        "lbheap defined symbol inventory changed"):
                probe.validate_profile(receipt_path)
            receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")

            object_path = artifact / "lbheap.o"
            original = object_path.read_bytes()
            object_path.write_bytes(original + b"\x00")
            with self.assertRaisesRegex(probe.PostAudioCompileError,
                                        "lbheap object hash changed"):
                probe.validate_profile(artifact / "receipt.json")

    def test_snapshot_header_exposes_source_relationships(self):
        header = probe.ACCESSOR_HEADER.read_text(encoding="utf-8")
        for field in (
            "root_handle_index", "free_mem_head_index", "free_heap_head_index",
            "root_matches_arena", "heap_count", "descriptor_count",
            "melee_web_source_lbmemory_snapshot",
            "melee_web_source_lbheap_snapshot",
        ):
            self.assertIn(field, header)
        self.assertIn("MELEE_WEB_SOURCE_POST_AUDIO_HELPERS", header)
        self.assertIn("melee_web_source_lbmemory_handle_location", header)
        self.assertNotIn(
            "out->root_handle = melee_web_source_post_audio_pointer", header
        )
        self.assertNotIn(
            "out->free_mem_head = melee_web_source_post_audio_pointer", header
        )
        self.assertEqual(
            header.count("static uint32_t melee_web_source_post_audio_pointer"), 1
        )


if __name__ == "__main__":
    unittest.main()
