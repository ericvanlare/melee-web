"""Focused provenance/object/link checks for the joined original audio probe."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest

from tools import source_synth_joined_startup as probe


ROOT = Path(__file__).resolve().parents[1]


class SourceSynthJoinedStartupTests(unittest.TestCase):
    def test_pinned_source_identities_and_expected_entrypoints(self) -> None:
        if not probe.MELEE.is_dir() or not probe.EMCC.is_file():
            self.skipTest("pinned source or Emscripten compiler unavailable")
        provenance = probe._verify_pristine_sources()
        self.assertEqual(provenance["revision"], probe.SOURCE_REVISION)
        self.assertEqual(provenance["files"], probe.EXPECTED_SOURCE_SHA256)
        self.assertEqual(set(probe.REQUIRED_SYMBOLS), set(probe.SOURCE_FILES))

    def test_prepared_tree_has_reviewed_patch_and_source_markers(self) -> None:
        if not all(path.is_file() for path in probe.PREPARED_FILES.values()):
            self.skipTest("prepared gameplay source tree unavailable")
        patch = ROOT / "patches/melee-gameplay.patch"
        prepared = probe._check_prepared_markers(patch)
        self.assertEqual(set(probe.PREPARED_FILES) | {"composed_patch"}, set(prepared))
        self.assertEqual(len(prepared["composed_patch"]), 64)

    def test_joined_objects_and_link_report_real_service_boundary(self) -> None:
        if not probe.EMCC.is_file() or not probe.LLVM_NM.is_file():
            self.skipTest("pinned Emscripten compiler or llvm-nm unavailable")
        if not all(path.is_file() for path in probe.FMT_FILES.values()):
            self.skipTest("configured CMake fmt dependency unavailable")
        if not probe.MELEE.is_dir() or not all(path.is_file() for path in probe.PREPARED_FILES.values()):
            self.skipTest("pinned/prepared source tree unavailable")
        ar_header = Path(os.environ.get("MELEE_WEB_SOURCE_AUDIO_AR_HEADER", str(probe.AR_HEADER_SOURCE)))
        if not ar_header.is_file():
            self.skipTest("reviewed typed AR header checkout unavailable")
        (ROOT / "work").mkdir(parents=True, exist_ok=True)
        artifact = Path(tempfile.mkdtemp(prefix="source-synth-joined-startup-test-", dir=ROOT / "work"))
        receipt = probe.build_profile(artifact.relative_to(ROOT))
        self.assertEqual(receipt["source_before"], receipt["source_after"])
        self.assertEqual(set(receipt["objects"]), set(probe.SOURCE_FILES))
        self.assertTrue(all(entry["status"] == "pass" for entry in receipt["objects"].values()))
        self.assertEqual(len(receipt["ax_profile"]["objects"]), 9)
        self.assertEqual(set(receipt["prefix_objects"]), {"alloc_trace", "osmemory", "initialize"})
        self.assertTrue(all(entry["status"] == "pass" for entry in receipt["prefix_objects"].values()))
        self.assertEqual(receipt["sram_object"]["required_symbol"], "OSGetSoundMode")
        self.assertEqual(receipt["fmt_dependency"]["before"], receipt["fmt_dependency"]["after"])
        self.assertEqual(receipt["joined_sram"]["source_sha256"], probe.OSRTC_SOURCE_SHA256)
        self.assertEqual(receipt["joined_sram"]["body_sha256"], probe.OSRTC_SOUND_MODE_BODY_SHA256)
        self.assertFalse(receipt["runtime_claim"])
        self.assertEqual(receipt["link"]["status"], "expected_unresolved")
        undefined = set(receipt["link"]["undefined_symbols"])
        self.assertTrue(undefined)
        self.assertTrue({"OSAllocFromHeap", "AISetDSPSampleRate"} <= undefined)
        self.assertNotIn("AXInit", undefined)
        self.assertIn("ARQPostRequest", undefined)
        self.assertNotIn("OSGetSoundMode", undefined)
        self.assertTrue((artifact / "shared-platform-symbols.json").is_file())
        self.assertEqual(receipt["typed_ar_header_sha256"], probe.AR_HEADER_SHA256)
        self.assertEqual(receipt["prepared_source_sha256"], receipt["prepared_source_sha256_after"])
        receipt_path = artifact / "receipt.json"
        self.assertEqual(json.loads(receipt_path.read_text(encoding="utf-8"))["kind"],
                         "source_synth_joined_startup_compile")

    def test_source_abi_header_is_authored_bool4_without_runtime_claim(self) -> None:
        msl_bool = probe.MELEE / "src/MSL/stdbool.h"
        self.assertTrue(msl_bool.is_file())
        self.assertEqual(hashlib.sha256(msl_bool.read_bytes()).hexdigest(),
                         "0265041ee1108a45dc88b724b887533b1d89169ee4089f9e6567063b2015797e")
        self.assertIn("typedef int bool", msl_bool.read_text(encoding="utf-8"))

    def test_reviewed_ar_header_identity_and_request_layout(self) -> None:
        ar_header = Path(os.environ.get("MELEE_WEB_SOURCE_AUDIO_AR_HEADER", str(probe.AR_HEADER_SOURCE)))
        if not ar_header.is_file():
            self.skipTest("reviewed typed AR header checkout unavailable")
        source, digest = probe._ar_header()
        self.assertEqual(digest, probe.AR_HEADER_SHA256)
        text = source.read_text(encoding="utf-8")
        self.assertIn("typedef void (*ARQCallback)(struct ARQRequest*);", text)
        self.assertIn("typedef void (*ARDMACallback)(void);", text)
        self.assertIn("extern \"C\"", text)


if __name__ == "__main__":
    unittest.main()
