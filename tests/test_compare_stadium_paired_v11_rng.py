"""Synthetic ownership negatives; retained original data has a separate opt-in check."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("paired_rng", ROOT / "tools/compare_stadium_paired_v11_rng.py")
compare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compare)
_MISSING = object()


def fixture():
    matches, rows, probes = [], [], []
    for mi in range(2):
        action = {"kind": "tick", "action_index": 0, "source_tick": 0,
                  "draw_ordinal": 0, "frame_index": 10 + mi}
        matches.append({"match_index": mi, "actions": [action], "selected": {0: []},
                        "selected_event_count": 0, "unpaired_original_guard_events": 0})
        for kind in ("action_begin", "action_end"):
            rows.append({"record": "stadium_paired_v11", "schema": compare.ROW_SCHEMA,
                         "kind": kind, "order": len(rows), "match_index": mi,
                         "local_action_index": 0, "diagnostic_action_index": mi,
                         "source_tick": 0, "draw_ordinal": 0, "input_cursor": 10 + mi,
                         "world_generation": 5 + mi,
                         "payload": {"action": "tick", "seed_before_hex": "00000001",
                                     "seed_after_hex": "00000001"}})
        probes.append({"schema": "melee-web-rng-draw-probe", "version": 3,
                       "source_cursor": mi, "overflowed": False, "draws": []})
    return matches, rows, probes


class PairedRngTests(unittest.TestCase):
    def _load_reference_profile(self, capture_profile, launch, *, include_launch=True):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            compact_path = root / "compact.mwrl"
            compact_bytes = b"compact fixture bytes"
            compact_path.write_bytes(compact_bytes)
            status_path = root / "native-primary-status.json"
            status_path.write_text(json.dumps({"state": "completed", "completed": True,
                                               "invalid": False, "error": None}))
            source_files = {
                "stadium-whole-paired-compact-rng.mwrl": self._pin(compact_path),
                "native-primary-status.json": self._pin(status_path),
            }
            if include_launch:
                launch_path = root / "launch.json"
                launch_path.write_text(json.dumps(launch))
                source_files["launch.json"] = self._pin(launch_path)
            projection = {
                "compact_sha256": hashlib.sha256(compact_bytes).hexdigest(),
                "source_files": source_files,
            }
            if capture_profile is not _MISSING:
                projection["capture_profile"] = capture_profile
            projection_path = root / "projection.json"
            projection_path.write_text(json.dumps(projection))
            captured = {}

            def decode(raw, **kwargs):
                captured.update(kwargs)
                return {"records": [], "windows": [], "owner_comparison_ready": True,
                        "unresolved_owner_events": 0}

            with patch.object(compare.compact, "decode_live_compact", side_effect=decode), \
                 patch.object(compare, "derive_windows", return_value=([], {})):
                compare.load_reference(projection_path, compare.identity(projection_path)["sha256"])
            return captured

    @staticmethod
    def _pin(path):
        data = path.read_bytes()
        return {"path": str(path.resolve()), "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest()}

    def test_legacy_missing_profile_keeps_historical_compact_identity(self):
        launch = {"stadium_whole_paired_idle": {
            "profile": "paired_idle_scene_endpoints_v1", "selected_matches": [0, 1],
            "match_count": 2, "comparison_post_go_ticks": 3500, "natural_tail_max": 4,
            "raw_omission_kinds": ["draw_enter", "draw_return"]}}
        decoded = self._load_reference_profile(_MISSING, launch)
        self.assertEqual(decoded["capture_id"], "stadium-whole-paired-idle")
        self.assertEqual(decoded["sequence_id"], "primary2-paired-idle-scene-endpoints")

    def test_registration_profile_uses_only_exact_pinned_launch_and_compact_identity(self):
        launch = {"stadium_whole_paired_idle": compare.REGISTRATION_LAUNCH_PROFILE,
                  "stadium_whole_paired_registration": True}
        decoded = self._load_reference_profile(compare.REGISTRATION_CAPTURE_PROFILE, launch)
        self.assertEqual(decoded["capture_id"], "stadium-whole-paired-registration")
        self.assertEqual(decoded["sequence_id"], "primary2-paired-registration")

    def test_registration_and_mixed_profile_downgrades_refuse(self):
        exact_launch = {"stadium_whole_paired_idle": compare.REGISTRATION_LAUNCH_PROFILE,
                        "stadium_whole_paired_registration": True}
        invalid = [
            ("unknown-profile", exact_launch, True),
            (compare.REGISTRATION_CAPTURE_PROFILE,
             {"stadium_whole_paired_idle": dict(compare.REGISTRATION_LAUNCH_PROFILE,
                                                natural_tail_max=5),
              "stadium_whole_paired_registration": True}, True),
            (compare.REGISTRATION_CAPTURE_PROFILE,
             {"stadium_whole_paired_idle": compare.REGISTRATION_LAUNCH_PROFILE,
              "stadium_whole_paired_registration": False}, True),
            (compare.REGISTRATION_CAPTURE_PROFILE, {}, False),
            (_MISSING, exact_launch, True),
        ]
        messages = (
            "capture_profile is unsupported",
            "exact registration profile",
            "explicit paired-registration opt-in",
            "pinned launch.json",
            "missing its projection capture_profile marker",
        )
        for (capture_profile, launch, include_launch), message in zip(invalid, messages):
            with self.subTest(message=message), self.assertRaisesRegex(compare.semantics.EvidenceError, message):
                self._load_reference_profile(capture_profile, launch, include_launch=include_launch)

    def test_declared_empty_actions_require_actual_probe_rows(self):
        matches, rows, probes = fixture()
        self.assertEqual([r["compared_actions"] for r in compare.compare_browser(matches, rows, probes)], [1, 1])
        with self.assertRaises(compare.semantics.EvidenceError):
            compare.compare_browser(matches, rows, probes[:-1])

    def test_duplicate_owner_or_wrong_input_cursor_refuses_even_with_equal_seeds(self):
        matches, rows, probes = fixture()
        for field, value in (("world_generation", 5), ("input_cursor", 10)):
            changed = copy.deepcopy(rows)
            for row in changed[2:]:
                row[field] = value
            with self.assertRaises(compare.semantics.EvidenceError):
                compare.compare_browser(matches, changed, probes)

    def test_probe_reorder_and_missing_action_end_refuse(self):
        matches, rows, probes = fixture()
        with self.assertRaises(compare.semantics.EvidenceError):
            compare.compare_browser(matches, rows, list(reversed(probes)))
        with self.assertRaises(compare.semantics.EvidenceError):
            compare.compare_browser(matches, rows[:-1], probes)

    def test_missing_rng_is_not_inferred_from_unchanged_event_count(self):
        matches, rows, probes = fixture()
        rows[1]["payload"]["seed_after_hex"] = "00000002"
        with self.assertRaises(compare.semantics.EvidenceError):
            compare.compare_browser(matches, rows, probes)

    def test_guard_action_remains_unpaired_and_seed_chain_is_checked(self):
        matches, rows, probes = fixture()
        matches[0]["selected"] = {}
        owner = {"status": "outside_proc", "callback_symbol": None,
                 "gobj": "0000000000000000", "proc": "0000000000000000",
                 "callback": "0000000000000000", "classifier": 0, "p_link": 0, "s_link": 0,
                 "fighter": None}
        seed = f"{(214013 + 2531011) & 0xffffffff:08x}"
        probes[0]["draws"] = [{"kind": "HSD_Rand", "seed_after": seed, "owner": owner}]
        rows[1]["payload"]["seed_after_hex"] = seed
        report = compare.compare_browser(matches, rows, probes)[0]
        self.assertEqual(report["unpaired_browser_events"], 1)
        self.assertEqual(report["compared_actions"], 0)
        probes[0]["draws"][0]["seed_after"] = "00000002"
        with self.assertRaises(compare.semantics.EvidenceError):
            compare.compare_browser(matches, rows, probes)

    def test_selected_event_compares_semantics_and_refuses_extra_event(self):
        matches, rows, probes = fixture()
        seed = f"{(214013 + 2531011) & 0xffffffff:08x}"
        native = {"kind": "HSD_Rand", "seed_after": seed, "owner": {
            "status": "scheduled_proc", "callback_symbol": "Fighter_8006A360",
            "gobj": "0000000010000000", "proc": "0000000010000004", "callback": "0000000000000042",
            "classifier": 4, "p_link": 8, "s_link": 1,
            "fighter": {"slot": 0, "entity": 0, "kind": 8}}}
        original = {"rng_ordinal": 0, "primitive": "HSD_Rand", "seed_after": seed, "owner": {
            "status": "scheduled_proc", "callback_symbol": "Fighter_8006A360",
            "gobj": "0x80001000", "proc": "0x80001004", "callback": "0x8006a360",
            "classifier": 4, "p_link": 8, "s_link": 1,
            "source_slot": 0, "entity": 0, "fighter_kind": 8}}
        matches[0]["selected"][0] = [original]
        matches[0]["selected_event_count"] = 1
        probes[0]["draws"] = [native]
        rows[1]["payload"]["seed_after_hex"] = seed
        self.assertEqual(compare.compare_browser(matches, rows, probes)[0]["selected_event_count"], 1)
        native["owner"]["fighter"]["slot"] = 1
        with self.assertRaises(compare.semantics.EvidenceError):
            compare.compare_browser(matches, rows, probes)

    @unittest.skipUnless(os.environ.get("MELEE_PAIRED_FIRST_GO_PROJECTION"), "retained paired projection unavailable")
    def test_retained_original_windows_bind_all_selected_events_and_guards(self):
        path = Path(os.environ["MELEE_PAIRED_FIRST_GO_PROJECTION"])
        matches, _ = compare.load_reference(path, compare.identity(path)["sha256"])
        self.assertEqual([m["selected_event_count"] for m in matches], [118035, 94417])
        self.assertEqual([m["unpaired_original_guard_events"] for m in matches], [8, 29])


if __name__ == "__main__":
    unittest.main()
