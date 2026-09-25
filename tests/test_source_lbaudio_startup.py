"""Focused provenance and raw-source compile checks for lbAudioAx startup."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from tools import source_lbaudio_startup as probe


ROOT = Path(__file__).resolve().parents[1]


class SourceLBAudioStartupTests(unittest.TestCase):
    def test_pinned_source_and_authored_table_identity(self) -> None:
        if not probe.MELEE.is_dir():
            self.skipTest("pinned source checkout unavailable")
        provenance = probe._verify_inputs()
        self.assertEqual(provenance["source_revision"], probe.SOURCE_REVISION)
        tables = probe._source_tables()
        self.assertEqual(tables["s32_rows"], 0x38)
        self.assertEqual(tables["offset_rows"], 0x38)

    def test_wrapper_keeps_original_init_path_and_only_observes_boundaries(self) -> None:
        with tempfile.TemporaryDirectory(prefix="source-lbaudio-wrapper-") as temp:
            wrapper = probe._write_wrapper(Path(temp))
            text = wrapper.read_text(encoding="utf-8")
        self.assertIn('#define ARInit melee_web_source_lbaudio_ARInit', text)
        self.assertIn('#define AXDriver_8038E498 melee_web_source_lbaudio_AXDriver_8038E498', text)
        self.assertIn('#define AXDriver_8038E30C melee_web_source_lbaudio_AXDriver_8038E30C', text)
        self.assertIn('#define HSD_SynthSFXAllocateBank melee_web_source_lbaudio_HSD_SynthSFXAllocateBank', text)
        self.assertIn('#include "source_lbaudio_startup_accessors.h"', text)
        self.assertNotIn("MELEE_WEB_GAMEPLAY", text)
        self.assertNotIn("MELEE_WEB_ORIGINAL_STARTUP_FIXTURE", text)

    def test_accessor_exports_full_snapshot_shape(self) -> None:
        text = probe.ACCESSOR_HEADER.read_text(encoding="utf-8")
        self.assertIn("sfx_state_counters[4]", text)
        self.assertIn("lbl_804D644C", text)
        self.assertIn("MELEE_WEB_LBAUDIO_ACCESSOR_IMPLEMENTATION", text)
        self.assertIn("sizeof(lbl_80433A64) / sizeof(lbl_80433A64[0]) == 0x38", text)
        self.assertEqual(hashlib.sha256(probe.ACCESSOR_HEADER.read_bytes()).hexdigest(),
                         probe.sha256(probe.ACCESSOR_HEADER))

    def test_compile_receipt_validation_rejects_forged_and_mutated_object(self) -> None:
        if not probe.LLVM_NM.is_file() or not probe.MELEE.is_dir():
            self.skipTest("pinned source and llvm-nm unavailable")
        with tempfile.TemporaryDirectory(prefix="source-lbaudio-receipt-") as temp:
            root = Path(temp)
            (root / "include").mkdir()
            (root / "include" / "source_abi.h").write_text("fixture\n", encoding="utf-8")
            obj = root / "lbaudio_ax_original.o"
            obj.write_bytes(b"object")
            wrapper = root / "source_lbaudio_startup_wrapper.c"
            wrapper.write_text("fixture wrapper\n", encoding="utf-8")
            source_provenance = probe._verify_inputs()
            source_hashes = {
                "lbaudio_ax.c": probe.SOURCE_SHA256,
                "lbaudio_ax.static.h": probe.STATIC_HEADER_SHA256,
                "dolphin/ax.h": probe.AX_HEADER_SHA256,
                "symbols.txt": probe.SYMBOLS_SHA256,
                "source_lbaudio_startup.py": probe.sha256(ROOT / "tools/source_lbaudio_startup.py"),
                "source_lbaudio_startup_accessors.h": probe.sha256(probe.ACCESSOR_HEADER),
            }
            receipt = {
                "kind": "source_lbaudio_startup_compile",
                "version": 1,
                "runtime_claim": False,
                "source_revision": probe.SOURCE_REVISION,
                "source_before": source_provenance,
                "source_after": source_provenance,
                "source_hashes": source_hashes,
                "header_inventory": probe._header_inventory(root),
                "compile": {
                    "status": "pass",
                    "defines_melee_web_gameplay": False,
                    "defines_original_startup_macro": False,
                },
                "defined_symbols": ["lbAudioAx_8002838C", "melee_web_source_lbaudio_snapshot"],
                "object": {"path": str(obj), "sha256": probe.sha256(obj)},
                "wrapper": {"path": str(wrapper), "sha256": probe.sha256(wrapper)},
                "evidence_dir": str(root),
            }
            receipt_path = root / "receipt.json"
            receipt_path.write_text(json.dumps(receipt), encoding="utf-8")
            with self.assertRaisesRegex(probe.LBAudioStartupError, "llvm-nm"):
                probe.validate_profile(receipt_path)
            obj.write_bytes(b"mutated")
            with self.assertRaisesRegex(probe.LBAudioStartupError, "object hash changed"):
                probe.validate_profile(receipt_path)

    def test_raw_compile_profile(self) -> None:
        if not probe.EMCC.is_file() or not probe.LLVM_NM.is_file():
            self.skipTest("configured Emscripten compiler or llvm-nm unavailable")
        artifact = Path(tempfile.mkdtemp(prefix="source-lbaudio-startup-test-", dir=ROOT / "work"))
        receipt = probe.build_profile(artifact)
        self.assertFalse(receipt["runtime_claim"])
        self.assertFalse(receipt["compile"]["defines_melee_web_gameplay"])
        self.assertEqual(receipt["object"]["sha256"], probe.sha256(Path(receipt["object"]["path"])))
        self.assertIn("lbAudioAx_8002838C", receipt["defined_symbols"])
        self.assertIn("melee_web_source_lbaudio_snapshot", receipt["defined_symbols"])
        self.assertEqual(probe.validate_profile(artifact/'receipt.json'), receipt)
        altered = dict(receipt)
        altered['defined_symbols'] = ['lbAudioAx_8002838C', 'melee_web_source_lbaudio_snapshot']
        (artifact/'receipt.json').write_text(json.dumps(altered)+'\n')
        try:
            with self.assertRaisesRegex(probe.LBAudioStartupError, 'symbol inventory'):
                probe.validate_profile(artifact/'receipt.json')
        finally:
            (artifact/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')


if __name__ == "__main__":
    unittest.main()
