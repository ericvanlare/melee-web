"""Focused source/DOL Synth parameter derivation checks."""

from __future__ import annotations

import copy
import hashlib
import os
from pathlib import Path
import tempfile
import unittest

from tools import source_synth_parameters as probe


ROOT = Path(__file__).resolve().parents[1]
PINNED_SOURCE_ROOT = ROOT / ".deps/melee"
DOL = Path(os.environ["MELEE_WEB_OWNED_DOL"]) if os.environ.get("MELEE_WEB_OWNED_DOL") else ROOT / "assets-local/results-mario/main.dol"
SYMBOLS = PINNED_SOURCE_ROOT / "config/GALE01/symbols.txt"
SOURCE = PINNED_SOURCE_ROOT / "src/melee/lb/lbaudio_ax.c"
STATIC_HEADER = PINNED_SOURCE_ROOT / "src/melee/lb/lbaudio_ax.static.h"


class SourceSynthParameterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before = probe.verify_pinned_inputs(SOURCE, STATIC_HEADER,
            PINNED_SOURCE_ROOT / "extern/dolphin/include/dolphin/ax.h", SYMBOLS)

    @classmethod
    def tearDownClass(cls):
        after = probe.verify_pinned_inputs(SOURCE, STATIC_HEADER,
            PINNED_SOURCE_ROOT / "extern/dolphin/include/dolphin/ax.h", SYMBOLS)
        if after != cls.before:
            raise AssertionError("pinned input changed during source tests")

    def _artifact_dir(self, prefix: str) -> Path:
        (ROOT / "work").mkdir(parents=True, exist_ok=True)
        return Path(tempfile.mkdtemp(prefix=prefix, dir=ROOT / "work"))

    def _require_owned_inputs(self):
        if not DOL.is_file() or not SYMBOLS.is_file():
            self.skipTest("owned DOL or symbol map unavailable")

    def test_owned_dol_tables_match_authored_source(self):
        self._require_owned_inputs()
        tables = probe.hydrate_tables(DOL, SYMBOLS, STATIC_HEADER)
        self.assertEqual(len(tables["s32_table"]), 0x38)
        self.assertEqual(len(tables["offsets_table"]), 0x38)
        self.assertEqual(tables["symbols"][probe.S32_NAME]["size"], 0xE0)
        self.assertEqual(tables["symbols"][probe.OFFSETS_NAME]["size"], 0x1C0)
        self.assertEqual(tables["dol"]["sha1"], probe.DOL_SHA1)

    def test_table_shape_and_bounds_reject_malformed_inputs(self):
        self._require_owned_inputs()
        tables = probe.hydrate_tables(DOL, SYMBOLS, STATIC_HEADER)
        bad_s32 = copy.deepcopy(tables["s32_table"])
        bad_s32[0].append(0)
        with self.assertRaisesRegex(probe.SynthParameterError, "four bytes"):
            probe.validate_tables(bad_s32, tables["offsets_table"])
        bad_offsets = copy.deepcopy(tables["offsets_table"])
        bad_offsets[0][0] = 0x1_0000_0000
        with self.assertRaisesRegex(probe.SynthParameterError, "u32 bounds"):
            probe.validate_tables(tables["s32_table"], bad_offsets)
        bad_symbols = probe.read_symbols(SYMBOLS)
        bad_symbols[probe.S32_NAME]["size"] = 0xDC
        with self.assertRaisesRegex(probe.SynthParameterError, "authored size"):
            probe._validate_symbol(bad_symbols, probe.S32_NAME, probe.S32_BYTES)

    def test_wasm_executes_exact_parameter_statements(self):
        self._require_owned_inputs()
        tables = probe.hydrate_tables(DOL, SYMBOLS, STATIC_HEADER)
        fixture = probe.render_fixture(SOURCE, tables)
        artifact_dir = self._artifact_dir("source-synth-parameters-owned-")
        result = probe.compile_and_run(fixture, root=ROOT, work_dir=artifact_dir)
        self.assertEqual(result["bank_size_total"], sum(result["bank_sizes"]))
        self.assertEqual(result["bank_sizes"], [2045824, 911456, 3291584])
        self.assertEqual(result["driver_call"], [64, 0, 0x40, result["bank_size_total"]])
        self.assertGreater(result["bank_sizes"][0], 0)
        self.assertGreater(result["bank_sizes"][1], 0)
        self.assertGreater(result["bank_sizes"][2], 0)

    def test_source_only_fixture_executes_selection_without_private_dol(self):
        if not (ROOT / ".deps/emsdk/upstream/emscripten/emcc.py").is_file():
            self.skipTest("configured Emscripten compiler unavailable")
        source_s32, source_offsets = probe._source_tables(STATIC_HEADER)
        fixture = probe.render_fixture(
            SOURCE, {"s32_table": source_s32, "offsets_table": source_offsets}
        )
        artifact_dir = self._artifact_dir("source-synth-parameters-source-")
        result = probe.compile_and_run(fixture, root=ROOT, work_dir=artifact_dir)
        # Source-derived fixture: this intentionally exercises authored
        # selection logic without claiming ownership of a private DOL.
        self.assertEqual(result["bank_sizes"], [2045824, 911456, 3291584])
        self.assertEqual(result["driver_call"], [64, 0, 0x40, 6248864])

    def test_pinned_input_mutation_is_rejected(self):
        self.assertEqual(probe.SOURCE_REVISION, "b43912cc78606f96c9569f5d6229bc9d7e265ea5")
        with tempfile.TemporaryDirectory(prefix="source-synth-parameters-mutation-") as temp:
            mutated_source = Path(temp) / SOURCE.name
            data = bytearray(SOURCE.read_bytes())
            data[0] ^= 1
            mutated_source.write_bytes(data)
            with self.assertRaisesRegex(probe.SynthParameterError, "pinned input changed"):
                probe.verify_pinned_inputs(
                    mutated_source, STATIC_HEADER,
                    PINNED_SOURCE_ROOT / "extern/dolphin/include/dolphin/ax.h",
                    SYMBOLS, source_root=PINNED_SOURCE_ROOT,
                )
            mutated_symbols = Path(temp) / SYMBOLS.name
            symbol_data = bytearray(SYMBOLS.read_bytes())
            symbol_data[-1] ^= 1
            mutated_symbols.write_bytes(symbol_data)
            with self.assertRaisesRegex(probe.SynthParameterError, "pinned input changed"):
                probe.verify_pinned_inputs(
                    SOURCE, STATIC_HEADER,
                    PINNED_SOURCE_ROOT / "extern/dolphin/include/dolphin/ax.h",
                    mutated_symbols, source_root=PINNED_SOURCE_ROOT,
                )

    def test_source_revision_and_fixture_provenance_are_stable(self):
        source_hash = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
        self.assertEqual(source_hash, probe.KNOWN_LBAUDIO_SHA256)
        self.assertEqual(probe.SOURCE_REVISION, "b43912cc78606f96c9569f5d6229bc9d7e265ea5")


if __name__ == "__main__":
    unittest.main()
