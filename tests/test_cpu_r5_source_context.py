"""Owned-input provenance checks for the CPU r5 seed adapter."""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cpu_r5_source_context as source_context


class CpuR5SourceContextTests(unittest.TestCase):
    def test_derives_seed_word_only_after_owned_boot_attestation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dol = root / "main.dol"
            disc = root / "owned.ciso"
            symbols = root / "symbols.txt"
            for path in (dol, disc, symbols):
                path.write_bytes(b"owned input")
            context = {"identities": {
                "source_revision": "source-revision",
                "symbols_sha256": "symbols-hash",
                "dol_sha256": "dol-hash",
                "disc_boot_header_sha256": "disc-hash",
                "apploader_sha256": "app-hash",
            }}
            profile = {
                "version": 3,
                "source_revision": "source-revision",
                "symbols_sha256": "symbols-hash",
                "dol_sha1": "dol-sha1",
                "globals": {"seed_ptr": {
                    "section": ".sdata", "size": 4, "address": 0x81234010}},
                "initial_dol_words": {"seed_ptr": 0x811230A4},
            }
            with patch.object(source_context, "derive_boot_context",
                              return_value=context) as attest, \
                    patch.object(source_context, "build_profile",
                                 return_value=profile) as derive:
                binding = source_context.derive_owned_seed_binding(
                    dol_path=dol, disc_path=disc, symbols_path=symbols,
                    source_root=root)
            attest.assert_called_once_with(dol, disc, symbols, root)
            derive.assert_called_once_with(dol, symbols)
            self.assertEqual(binding["source_word"], 0x811230A4)
            self.assertEqual(binding["global_address"], 0x81234010)
            self.assertTrue(binding["independently_derived"])
            self.assertEqual(binding["provenance"]["derivation"],
                             "owned_disc_apploader_and_dol_sda_no_capture_inputs")

    def test_rejects_symlink_before_derivation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "target"
            target.write_bytes(b"owned")
            dol_link = root / "dol"
            dol_link.symlink_to(target)
            with self.assertRaisesRegex(ValueError, "DOL"):
                source_context.derive_owned_seed_binding(
                    dol_path=dol_link,
                    disc_path=target, symbols_path=target, source_root=root)


if __name__ == "__main__":
    unittest.main()
