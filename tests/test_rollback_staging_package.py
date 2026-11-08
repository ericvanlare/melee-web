"""Focused source-link projection controls for the staging-only compact table."""

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import prepare_rollback_staging as staging


def row(section, address, *, size=4, candidate=True):
    return {
        "producer_target": "fighter_source_runtime" if candidate else None,
        "source": "src/source.c" if candidate else None,
        "input_object": "libfighter_source_runtime.a(source.c.o)",
        "input_section": section,
        "address": address,
        "size": size,
        "decision": "source-c-static-candidate" if candidate else "excluded",
        "exclusion_reason": None if candidate else "source-provenance-unavailable",
    }


def map_row(source):
    return {
        "object": source["input_object"], "section": source["input_section"],
        "address": source["address"], "size": source["size"],
    }


class RollbackStagingPackageTests(unittest.TestCase):
    def setUp(self):
        self.restored = row(".bss.restore", 32)
        self.clock = row(".data.clock", 12)
        self.new = row(".bss.new", 64)
        self.excluded = row(".data.external", 76, candidate=False)
        self.current = [self.new, self.restored, self.excluded, self.clock]
        self.review = [
            dict(self.restored, address=88,
                 aggregate_policy="ORIGINAL_SOURCE_CPU_VALUE_BUNDLE_RESTORE"),
            dict(self.clock, aggregate_policy="TYPED_CLOCK_WRITER_EXCLUDE_FROM_BYTE_BUNDLE"),
        ]
        self.linked = [map_row(source) for source in self.current]

    def test_exact_join_uses_actual_addresses_and_excludes_typed_clock_and_host_rows(self):
        rows = staging.compact_rows(self.current, self.review, self.linked)
        self.assertEqual(rows, [[32, 4, 1, 1], [64, 4, 2, 1]])
        encoded = staging.canonical({"rows": rows})
        self.assertEqual(json.loads(encoded), {"rows": rows})
        self.assertNotIn(b"source.c", encoded)

    def test_missing_reviewed_row_refuses_even_when_map_and_manifest_agree(self):
        current = [r for r in self.current if r is not self.restored]
        with self.assertRaisesRegex(ValueError, "Reviewed source row missing"):
            staging.compact_rows(current, self.review, [map_row(r) for r in current])

    def test_size_exclusion_and_policy_drift_refuse(self):
        for change, error in (
            ({"size": 8}, "row facts changed"),
            ({"exclusion_reason": "different"}, "row facts changed"),
            ({"aggregate_policy": "invented"}, "Unknown reviewed source policy"),
        ):
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, error):
                staging.compact_rows(self.current, [dict(self.review[0], **change), self.review[1]],
                                     self.linked)

    def test_duplicate_keys_and_map_extent_drift_refuse(self):
        with self.assertRaisesRegex(ValueError, "Duplicate manifest row"):
            staging.compact_rows([*self.current, self.restored], self.review, self.linked)
        with self.assertRaisesRegex(ValueError, "Duplicate reviewed row"):
            staging.compact_rows(self.current, [*self.review, self.review[0]], self.linked)
        with self.assertRaisesRegex(ValueError, "exactly join"):
            staging.compact_rows(self.current, self.review,
                                 [dict(self.linked[0], size=8), *self.linked[1:]])

    def test_overlapping_output_rows_refuse(self):
        overlap = [dict(self.new, address=34), *self.current[1:]]
        with self.assertRaisesRegex(ValueError, "Invalid compact statics range"):
            staging.compact_rows(overlap, self.review, [map_row(source) for source in overlap])

    def test_wrong_policy_digest_refuses(self):
        with tempfile.TemporaryDirectory() as temporary:
            policy = Path(temporary) / "policy.json"
            raw = b'{"rows":[]}'
            policy.write_bytes(raw)
            self.assertEqual(staging.pinned_json(policy, staging.sha256(raw), "policy"), {"rows": []})
            with self.assertRaisesRegex(ValueError, "policy digest"):
                staging.pinned_json(policy, "0" * 64, "policy")
