"""Focused driver controls for the paired idle scene-endpoint profile.

These exercise the actual Python route/receiver methods with modeled boundary
leaves. They do not represent a Dolphin capture or establish original-game parity.
"""
from pathlib import Path
import hashlib
import importlib.util
import json
import io
import os
import sys
import struct
import shutil
import subprocess
import tempfile
import textwrap
import unittest
import zlib
from contextlib import redirect_stdout
from unittest.mock import patch

SOURCE_ROOT = Path(os.environ.get(
    "MELEE_STADIUM_PAIRED_IDLE_SOURCE_ROOT", Path(__file__).resolve().parents[1]
)).resolve()
SUPPORT_ROOT = Path(os.environ.get(
    "MELEE_STADIUM_PAIRED_IDLE_SUPPORT_ROOT", SOURCE_ROOT
)).resolve()
DRIVER_PATH = Path(os.environ.get(
    "MELEE_STADIUM_PAIRED_IDLE_DRIVER",
    SOURCE_ROOT / "scripts" / "capture_sd_reference_prefix.py",
)).resolve()
sys.path[:0] = [
    str(SOURCE_ROOT / "scripts"),
    str(SOURCE_ROOT / "tools"),
    str(SOURCE_ROOT / "reference-capture" / "dolphin"),
    str(SUPPORT_ROOT / "scripts"),
    str(SUPPORT_ROOT / "tools"),
    str(SUPPORT_ROOT / "reference-capture" / "dolphin"),
]


def load_exact(name, path):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    if Path(module.__file__).resolve() != path.resolve():
        raise AssertionError("loaded paired-idle driver path changed")
    if hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() != digest:
        raise AssertionError("loaded paired-idle driver bytes changed")
    return module


driver = load_exact("_candidate_capture_sd_reference_prefix_paired_idle", DRIVER_PATH)
from sd_original_menu_plan import stadium_go_feedback_packet
from stadium_go_prefix import (
    CONSTRUCTOR_POINTS,
    PAIRED_IDLE_DIAGNOSTIC,
    WHOLE_FEEDBACK_DIAGNOSTIC,
    _validate_progress,
    _validate_progress_constructor,
)
import whole_session_replay
import reference_observer_stream as observer_stream
from capture_allocation_history import validate_stadium_paired_allocation_environment


class StadiumPairedIdleProfileTests(unittest.TestCase):
    def test_shared_capture_identity_requires_exact_paired_profile_and_preserves_legacy(self):
        profile = {
            "profile": "paired_idle_scene_endpoints_v1",
            "selected_matches": [0, 1],
            "match_count": 2,
            "comparison_post_go_ticks": 3500,
            "natural_tail_max": 4,
            "raw_omission_kinds": ["draw_enter", "draw_return"],
        }
        receipt = "a" * 64
        declaration = {
            "whole_session": True,
            "match_count": 2,
            "capture_id": "paired-idle-test",
            "sequence_id": "paired-idle-test-sequence",
            "stadium_whole_feedback": True,
            "setup_receipt_sha256": receipt,
            "stadium_whole_b_state": True,
            "stadium_observed_matches": [0, 1],
            "stadium_whole_long_idle": profile,
        }
        handshake = {
            "schema": whole_session_replay.EXPECTED_OBSERVER_SCHEMA,
            "version": 1,
            "dolphin_commit": whole_session_replay.EXPECTED_DOLPHIN_COMMIT,
            "dol_sha1": whole_session_replay.EXPECTED_DOL_SHA1,
            "dol_sha256": whole_session_replay.EXPECTED_DOL_SHA256,
            "cpu": "JITARM64",
            "writes_guest_memory": False,
            **declaration,
        }
        start = {**declaration, "source_revision": "GALE01r2"}

        def capture(handshake_value=handshake, start_value=start):
            return [
                {"seq": 0, "event": "handshake", "payload": handshake_value},
                {"seq": 1, "event": "start", "payload": start_value},
                {"seq": 2, "event": "end", "payload": {"status": "completed", "natural": True}},
            ]

        identity = whole_session_replay._capture_identity(capture())
        self.assertEqual(identity["match_count"], 2)

        legacy = {
            "schema": whole_session_replay.EXPECTED_OBSERVER_SCHEMA,
            "version": 1,
            "dolphin_commit": whole_session_replay.EXPECTED_DOLPHIN_COMMIT,
            "dol_sha1": whole_session_replay.EXPECTED_DOL_SHA1,
            "dol_sha256": whole_session_replay.EXPECTED_DOL_SHA256,
            "cpu": "JITARM64",
            "writes_guest_memory": False,
            "whole_session": True,
            "match_count": 3,
            "capture_id": "legacy-whole3-test",
            "sequence_id": "legacy-whole3-test-sequence",
        }
        legacy_start = {
            "whole_session": True,
            "match_count": 3,
            "capture_id": legacy["capture_id"],
            "sequence_id": legacy["sequence_id"],
            "source_revision": "GALE01r2",
        }
        identity = whole_session_replay._capture_identity(capture(legacy, legacy_start))
        self.assertEqual(identity["match_count"], 3)

        cases = [
            (dict(handshake, stadium_whole_b_state=False), start,
             "paired-idle match_count=2 profile"),
            (handshake, dict(start, stadium_observed_matches=[1, 0]),
             "profile is incomplete or malformed"),
            (handshake, dict(start, setup_receipt_sha256="b" * 64),
             "paired-idle setup_receipt_sha256 disagrees"),
            (handshake, dict(start, stadium_whole_long_idle=dict(profile, match_count=3)),
             "profile is incomplete or malformed"),
            (dict(handshake, match_count=3),
             dict(start, match_count=3),
             "paired-idle profile cannot use a legacy whole-session match count"),
        ]
        for handshake_value, start_value, reason in cases:
            with self.subTest(reason=reason), self.assertRaisesRegex(
                    whole_session_replay.WholeSessionReplayError, reason):
                whole_session_replay._capture_identity(capture(handshake_value, start_value))

    def test_actual_mwro_reader_and_replay_consumer_join_pair_announcements(self):
        profile = {
            "profile": "paired_idle_scene_endpoints_v1",
            "selected_matches": [0, 1],
            "match_count": 2,
            "comparison_post_go_ticks": 3500,
            "natural_tail_max": 4,
            "raw_omission_kinds": ["draw_enter", "draw_return"],
        }
        declaration = {
            "whole_session": True,
            "match_count": 2,
            "capture_id": "paired-reader-test",
            "sequence_id": "paired-reader-test-sequence",
            "stadium_whole_feedback": True,
            "setup_receipt_sha256": "b" * 64,
            "stadium_whole_b_state": True,
            "stadium_observed_matches": [0, 1],
            "stadium_whole_long_idle": profile,
        }
        handshake = {
            "schema": whole_session_replay.EXPECTED_OBSERVER_SCHEMA,
            "version": 1,
            "dolphin_commit": whole_session_replay.EXPECTED_DOLPHIN_COMMIT,
            "dol_sha1": whole_session_replay.EXPECTED_DOL_SHA1,
            "dol_sha256": whole_session_replay.EXPECTED_DOL_SHA256,
            "cpu": "JITARM64",
            "writes_guest_memory": False,
            **declaration,
        }
        start = {
            "status": "recording",
            "source_revision": "GALE01r2",
            **declaration,
        }

        def frame(event_code, sequence, value):
            payload = json.dumps(value, separators=(",", ":")).encode("utf-8")
            header = observer_stream.HEADER.pack(
                observer_stream.MAGIC_U32, observer_stream.SCHEMA_VERSION,
                event_code, sequence, sequence + 10, 0, 0, 0,
                len(payload), zlib.crc32(payload) & 0xffffffff,
            )
            return header + payload

        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "paired.mwro"
            path.write_bytes(
                frame(1, 0, handshake) +
                frame(2, 1, start) +
                frame(6, 2, {"status": "completed", "natural": True})
            )
            records = list(observer_stream.iter_records(path))
            identity = whole_session_replay._capture_identity(records)
            self.assertEqual(identity["match_count"], 2)

            invalid_handshake = dict(handshake)
            invalid_handshake.pop("stadium_whole_feedback")
            path.write_bytes(
                frame(1, 0, invalid_handshake) +
                frame(2, 1, start) +
                frame(6, 2, {"status": "completed", "natural": True})
            )
            with self.assertRaisesRegex(observer_stream.ObserverStreamError, "invalid whole-session match_count"):
                list(observer_stream.iter_records(path))

            legacy_handshake = {
                "schema": whole_session_replay.EXPECTED_OBSERVER_SCHEMA,
                "version": 1,
                "dolphin_commit": whole_session_replay.EXPECTED_DOLPHIN_COMMIT,
                "dol_sha1": whole_session_replay.EXPECTED_DOL_SHA1,
                "dol_sha256": whole_session_replay.EXPECTED_DOL_SHA256,
                "cpu": "JITARM64",
                "writes_guest_memory": False,
                "whole_session": True,
                "match_count": 3,
                "capture_id": "legacy-reader-test",
                "sequence_id": "legacy-reader-test-sequence",
            }
            legacy_start = {
                "status": "recording", "source_revision": "GALE01r2",
                **{key: value for key, value in legacy_handshake.items()
                   if key in ("whole_session", "match_count", "capture_id", "sequence_id")},
            }
            path.write_bytes(
                frame(1, 0, legacy_handshake) +
                frame(2, 1, legacy_start) +
                frame(6, 2, {"status": "completed", "natural": True})
            )
            identity = whole_session_replay._capture_identity(
                list(observer_stream.iter_records(path)))
            self.assertEqual(identity["match_count"], 3)

    def test_startup_profile_join_compiles_the_actual_observer_helper(self):
        source = (SOURCE_ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp").read_text()
        start = source.index("bool PairedIdleStartupProfilesAgree(")
        brace = source.index("{", start)
        depth = 0
        end = None
        for index in range(brace, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    end = index + 1
                    break
        self.assertIsNotNone(end)
        helper = source[start:end]
        fixture = textwrap.dedent(f"""\
            #include <string_view>
            using u32 = unsigned;
            {helper}
            int main() {{
              using std::string_view;
              if (!PairedIdleStartupProfilesAgree(true, 2,
                    "paired_idle_scene_endpoints_v1", "paired_idle_scene_endpoints_v1")) return 1;
              if (PairedIdleStartupProfilesAgree(true, 2, "", "paired_idle_scene_endpoints_v1")) return 2;
              if (PairedIdleStartupProfilesAgree(true, 3,
                    "paired_idle_scene_endpoints_v1", "paired_idle_scene_endpoints_v1")) return 3;
              if (PairedIdleStartupProfilesAgree(true, 2,
                    "paired_idle_scene_endpoints_v1", "selected_b_3500_v1")) return 4;
              if (!PairedIdleStartupProfilesAgree(false, 3, "", "selected_b_3500_v1")) return 5;
              if (PairedIdleStartupProfilesAgree(false, 3, "paired_idle_scene_endpoints_v1", "")) return 6;
              if (PairedIdleStartupProfilesAgree(false, 3, "", "paired_idle_scene_endpoints_v1")) return 7;
              return 0;
            }}
        """)
        with tempfile.TemporaryDirectory() as temporary:
            temp = Path(temporary)
            source_path = temp / "startup-profile-join.cpp"
            binary_path = temp / "startup-profile-join"
            source_path.write_text(fixture)
            built = subprocess.run(
                [os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra",
                 "-Werror", str(source_path), "-o", str(binary_path)],
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(binary_path)], text=True, capture_output=True, check=False)
            self.assertEqual(ran.returncode, 0, ran.stderr)

    def test_startup_environment_requires_cross_profile_join_and_exact_cap(self):
        pair = {
            "MWRC_STADIUM_WHOLE_PAIRED_IDLE": "paired_idle_scene_endpoints_v1",
            "MWRC_ALLOCATION_SCENE_ENDPOINT_PROFILE": "paired_idle_scene_endpoints_v1",
            "MWRC_STADIUM_WHOLE_LONG_IDLE": "paired_idle_scene_endpoints_v1",
            "MWRC_WHOLE_SESSION_MATCHES": "2",
            "MWRC_STADIUM_WHOLE_FEEDBACK": "1",
            "MWRC_STADIUM_WHOLE_B_STATE": "1",
            "MWRC_ALLOCATION_VS_TARGET": "2",
            "MWRC_ALLOCATION_STOP_AT": "exit",
            "MWRC_ALLOCATION_MAX_EVENTS": "64",
        }
        self.assertEqual(validate_stadium_paired_allocation_environment(pair)["match_count"], 2)
        pair_without_cap = {key: value for key, value in pair.items()
                            if key != "MWRC_ALLOCATION_MAX_EVENTS"}
        self.assertEqual(validate_stadium_paired_allocation_environment(
            pair_without_cap)["max_events"], 64)
        for field, value in (
            ("MWRC_ALLOCATION_SCENE_ENDPOINT_PROFILE", ""),
            ("MWRC_STADIUM_WHOLE_LONG_IDLE", "selected_b_3500_v1"),
            ("MWRC_WHOLE_SESSION_MATCHES", "3"),
            ("MWRC_ALLOCATION_MAX_EVENTS", "65"),
        ):
            changed = dict(pair, **{field: value})
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "paired"):
                validate_stadium_paired_allocation_environment(changed)
        self.assertIsNone(validate_stadium_paired_allocation_environment({}))
        with self.assertRaisesRegex(ValueError, "legacy primary route"):
            validate_stadium_paired_allocation_environment({
                "MWRC_ALLOCATION_SCENE_ENDPOINT_PROFILE": "paired_idle_scene_endpoints_v1",
            })
        with self.assertRaisesRegex(ValueError, "legacy primary route"):
            validate_stadium_paired_allocation_environment({
                "MWRC_STADIUM_WHOLE_LONG_IDLE": "paired_idle_scene_endpoints_v1",
            })

    def test_cli_selects_the_explicit_paired_route_without_launching(self):
        captured = {}

        def selected(**kwargs):
            captured.update(kwargs)
            return {"result": "modeled_cli_selection"}

        arguments = [
            "--dolphin", "unused-dolphin", "--disc", "unused-disc",
            "--profile", "unused-profile", "--menu-recipe", "unused-menu",
            "--output", "unused-output", "--build-manifest", "unused-manifest",
            "--stadium-whole-paired-idle",
        ]
        output = io.StringIO()
        with patch.object(driver, "run", side_effect=selected), redirect_stdout(output):
            result = driver.main(arguments)
        self.assertEqual(result, 0)
        self.assertIs(captured["stadium_whole_paired_idle"], True)
        self.assertIs(captured["stadium_whole_b_state"], False)
        self.assertIs(captured["stadium_whole_long_idle"], False)
        self.assertIn('"result": "modeled_cli_selection"', output.getvalue())

    def test_pair_compact_launch_selects_both_matches_and_requires_fresh_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "owned-output"
            output.mkdir()
            environment, receipt = driver.build_stadium_whole_long_idle_compact_launch(
                output, paired_idle=True,
            )
            compact_path = output.resolve() / driver.STADIUM_WHOLE_PAIRED_IDLE_COMPACT_BASENAME
            self.assertEqual(environment, {
                "MWRC_STADIUM_COMPACT_RNG_OUTPUT": str(compact_path),
                "MWRC_STADIUM_COMPACT_RNG_MATCHES": "0,1",
            })
            self.assertEqual(receipt["schema"], "melee-web-stadium-paired-idle-compact-launch-v1")
            self.assertEqual(receipt["matches"], [0, 1])
            self.assertEqual(receipt["output_path"], str(compact_path))
            self.assertIs(receipt["output_absent_at_prepare"], True)
            self.assertIs(receipt["lifecycle_parity"], False)
            self.assertFalse(compact_path.exists())
            compact_path.write_bytes(b"occupied")
            with self.assertRaisesRegex(ValueError, "must be fresh"):
                driver.build_stadium_whole_long_idle_compact_launch(output, paired_idle=True)

    def test_paired_receiver_requires_exact_two_match_profile_and_terminal_windows(self):
        receipt = "a" * 64
        menus = stadium_go_feedback_packet()
        profile = dict(driver.STADIUM_WHOLE_PAIRED_IDLE_PROFILE)
        receiver = driver.StadiumWholeFeedbackReceiver(
            menus, receipt, {}, stadium_whole_b_state=True,
            stadium_whole_long_idle=True, stadium_whole_paired_idle=True,
        )
        self.assertEqual(receiver.match_count, 2)
        self.assertEqual(receiver.b_state_matches, (0, 1))
        self.assertTrue(receiver.selected_b_state(0))
        self.assertTrue(receiver.selected_b_state(1))
        self.assertFalse(receiver.selected_b_state(2))
        self.assertTrue(receiver.selected_long_idle(0))
        self.assertTrue(receiver.selected_long_idle(1))
        self.assertFalse(receiver.selected_long_idle(2))
        with self.assertRaisesRegex(ValueError, "lifetime reset preceded natural ReturnCss"):
            receiver.next_lifetime()

        declaration = dict(
            whole_session=True, match_count=2, stadium_whole_feedback=True,
            setup_receipt_sha256=receipt, stadium_whole_b_state=True,
            stadium_observed_matches=[0, 1], stadium_observed_match=None,
            stadium_whole_replay_binding_sha256=None,
            stadium_whole_long_idle=profile,
        )
        receiver.accept(dict(seq=0, event="handshake", payload=declaration))
        receiver.accept(dict(seq=1, event="start", payload=declaration))
        self.assertEqual(receiver.announcements, ["handshake", "start"])

        bad = dict(declaration, match_count=3)
        wrong_count = driver.StadiumWholeFeedbackReceiver(
            menus, receipt, {}, stadium_whole_b_state=True,
            stadium_whole_long_idle=True, stadium_whole_paired_idle=True,
        )
        with self.assertRaisesRegex(ValueError, "announcement differs"):
            wrong_count.accept(dict(seq=0, event="handshake", payload=bad))

        bad = dict(declaration, stadium_observed_matches=[1, 0])
        wrong_selection = driver.StadiumWholeFeedbackReceiver(
            menus, receipt, {}, stadium_whole_b_state=True,
            stadium_whole_long_idle=True, stadium_whole_paired_idle=True,
        )
        with self.assertRaisesRegex(ValueError, "selected-match declaration differs"):
            wrong_selection.accept(dict(seq=0, event="handshake", payload=bad))

        bad_profile = dict(profile, profile="selected_b_3500_v1")
        bad = dict(declaration, stadium_whole_long_idle=bad_profile)
        wrong_profile = driver.StadiumWholeFeedbackReceiver(
            menus, receipt, {}, stadium_whole_b_state=True,
            stadium_whole_long_idle=True, stadium_whole_paired_idle=True,
        )
        with self.assertRaisesRegex(ValueError, "profile announcement differs"):
            wrong_profile.accept(dict(seq=0, event="handshake", payload=bad))

        legacy = driver.StadiumWholeFeedbackReceiver(
            menus, receipt, {}, stadium_whole_b_state=True,
            stadium_whole_long_idle=True,
        )
        legacy_declaration = dict(declaration, match_count=3)
        with self.assertRaisesRegex(ValueError, "B announcement opt-in differs"):
            legacy.accept(dict(seq=0, event="handshake", payload=legacy_declaration))

        # Model the final CSS marker with both actual windows absent: the real
        # terminal predicate must still refuse to call this paired run complete.
        receiver.match_index = 1
        receiver.returned_css = True
        with self.assertRaisesRegex(ValueError, "terminal lacks both selected observation windows"):
            receiver.accept(dict(seq=2, event="end", payload={
                "natural": True, "status": "completed",
            }))

    def test_actual_progress_parser_separates_paired_and_legacy_scopes(self):
        receipt = "a" * 64
        self.assertEqual(PAIRED_IDLE_DIAGNOSTIC, "stadium_paired_idle_feedback_v1")
        go_payload = dict(
            whole_session=True, match_index=2, stadium_b_state_observation=True,
            diagnostic=PAIRED_IDLE_DIAGNOSTIC, phase="go_after",
            setup_receipt_sha256=receipt, setup_profile_verified_by_observer=False,
        )
        with self.assertRaisesRegex(ValueError, "GO: whole-B source scope differs"):
            _validate_progress(
                {"payload": go_payload}, "go_after", receipt,
                scope=PAIRED_IDLE_DIAGNOSTIC, match_index=2,
            )

        # Match0 is admitted only by the new paired scope. Deliberately
        # malformed pinned GO identity must fail after, not at, scope admission.
        paired_zero = dict(go_payload, match_index=0, pc="0x00000000",
                           word="0x00000000", argument="0x00000000", source_tick=9)
        with self.assertRaisesRegex(ValueError, "GO marker is not the pinned post-call return"):
            _validate_progress(
                {"source_tick": 9, "payload": paired_zero}, "go_after", receipt,
                scope=PAIRED_IDLE_DIAGNOSTIC, match_index=0,
            )

        cross_used = dict(go_payload, match_index=1, diagnostic=WHOLE_FEEDBACK_DIAGNOSTIC)
        with self.assertRaisesRegex(ValueError, "named Progress identity or phase changed"):
            _validate_progress(
                {"payload": cross_used}, "go_after", receipt,
                scope=PAIRED_IDLE_DIAGNOSTIC, match_index=1,
            )

        constructor_payload = dict(
            whole_session=True, match_index=2, stadium_b_state_observation=True,
        )
        with self.assertRaisesRegex(ValueError, "constructor whole-B scope/index/receipt differs"):
            _validate_progress_constructor(
                {"payload": constructor_payload},
                scope=PAIRED_IDLE_DIAGNOSTIC, expected_receipt=receipt, match_index=2,
            )

        constructor_pc, constructor_word = CONSTRUCTOR_POINTS["constructor_entry"]
        admitted_zero = dict(
            whole_session=True, match_index=0, stadium_b_state_observation=True,
            diagnostic=PAIRED_IDLE_DIAGNOSTIC, phase="constructor_entry",
            setup_receipt_sha256=receipt, setup_profile_verified_by_observer=False,
            pc=f"0x{constructor_pc:08x}", word=constructor_word,
        )
        with self.assertRaisesRegex(ValueError, "constructor progress identity mismatch"):
            _validate_progress_constructor(
                {"event": "progress", "payload": admitted_zero},
                scope=PAIRED_IDLE_DIAGNOSTIC, expected_receipt=receipt, match_index=0,
            )

        constructor_cross_used = dict(
            whole_session=True, match_index=1, stadium_b_state_observation=True,
            diagnostic=WHOLE_FEEDBACK_DIAGNOSTIC, phase="constructor_entry",
            setup_receipt_sha256=receipt, setup_profile_verified_by_observer=False,
        )
        with self.assertRaisesRegex(ValueError, "constructor progress identity mismatch"):
            _validate_progress_constructor(
                {"event": "progress", "payload": constructor_cross_used},
                scope=PAIRED_IDLE_DIAGNOSTIC, expected_receipt=receipt, match_index=1,
            )

        # The older whole3 scope remains bound to match1, not either paired match.
        with self.assertRaisesRegex(ValueError, "GO: whole-B source scope differs"):
            _validate_progress(
                {"payload": dict(go_payload, match_index=0,
                                 diagnostic=WHOLE_FEEDBACK_DIAGNOSTIC)},
                "go_after", receipt, scope=WHOLE_FEEDBACK_DIAGNOSTIC, match_index=0,
            )

    def test_actual_constructor_witness_resets_only_between_paired_matches(self):
        source = (SOURCE_ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp").read_text()

        def extract_cpp(marker):
            start = source.index(marker)
            opening = source.index("{", start)
            depth = 0
            for index in range(opening, len(source)):
                if source[index] == "{":
                    depth += 1
                elif source[index] == "}":
                    depth -= 1
                    if depth == 0:
                        end = index + 1
                        if source[end:].lstrip().startswith(";"):
                            end = source.index(";", end) + 1
                        return source[start:end]
            raise AssertionError(f"unterminated C++ declaration after {marker!r}")

        compiler = shutil.which("clang++") or shutil.which("g++")
        if compiler is None:
            self.skipTest("Native C++ compiler unavailable")
        actual_whole = extract_cpp("struct StadiumWholeBObservation\n")
        actual_methods = "\n".join(extract_cpp(marker) for marker in (
            "bool StadiumConstructorWitnessStepValid(size_t point) const",
            "void AdvanceStadiumConstructorWitness(size_t point)",
            "bool StadiumConstructorOwnerMatches(u32 gobj, u32 ground) const",
            "bool ResetPairedConstructorWitnessForSecondMatch(u32 index, u32 whole, u8 route)",
            "bool AcceptStadiumConstructorSssExit(u32 index, u32 whole, u8 route)",
        ))
        harness = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <climits>
using u8=std::uint8_t; using u32=std::uint32_t;
''' + actual_whole + r'''
struct Harness {
  bool stadium_whole_paired_idle=false;
  StadiumWholeBObservation stadium_whole_b;
  size_t stadium_constructor_step=0, stadium_constructor_call=0;
  u32 stadium_constructor_gobj=0, stadium_constructor_ground=0;
  u32 stadium_constructor_parameter_slot=0, stadium_constructor_parameters=0;
''' + actual_methods + r'''
};
static bool witness_sequence(Harness& h, const size_t* points, size_t count,
                             u32 gobj, u32 ground, u32 slot, u32 parameters) {
  for(size_t i=0;i<count;++i) {
    const size_t point=points[i];
    if(!h.StadiumConstructorWitnessStepValid(point)) return false;
    if(point==0) { h.stadium_constructor_gobj=gobj; h.stadium_constructor_ground=ground; }
    else if(!h.StadiumConstructorOwnerMatches(gobj,ground)) return false;
    if(point==1) { h.stadium_constructor_parameter_slot=slot; h.stadium_constructor_parameters=parameters; }
    h.AdvanceStadiumConstructorWitness(point);
  }
  return true;
}
static void finish_match(Harness& h, u32 index, u32 owner, u32 tick) {
  assert(h.stadium_whole_b.Setup(index,5,owner,h.stadium_constructor_step));
  assert(h.stadium_whole_b.Go(index,5,owner,tick));
  assert(h.stadium_whole_b.Tick(index,5,owner,tick));
  assert(h.stadium_whole_b.Close(index,5,owner));
}
int main() {
  const size_t up[]={0,1,2,3,6};
  const size_t down[]={0,1,4,5,6};
  Harness paired; paired.stadium_whole_paired_idle=true;
  paired.stadium_whole_b.enabled=true; paired.stadium_whole_b.paired=true;
  paired.stadium_whole_b.selected_match=0;
  assert(paired.AcceptStadiumConstructorSssExit(0,3,1));
  const u32 owner_a=0x80401000, owner_b=0x80402000;
  assert(paired.stadium_whole_b.Entry(0,4,owner_a));
  assert(paired.stadium_whole_b.Constructor(0,5,owner_a));
  assert(witness_sequence(paired,up,5,owner_a,0x80403000,0x80404000,0x80405000));
  finish_match(paired,0,owner_a,100);
  assert(paired.stadium_whole_b.phase==StadiumWholeBObservation::Phase::Closed);
  assert(paired.stadium_whole_b.completed_matches==1 && paired.stadium_constructor_step==5);
  assert(!paired.StadiumConstructorWitnessStepValid(0)); // duplicate A entry still refuses
  assert(!paired.AcceptStadiumConstructorSssExit(1,3,0)); // invalid route leaves A evidence intact
  assert(paired.stadium_constructor_step==5 && paired.stadium_constructor_gobj==owner_a);
  assert(paired.AcceptStadiumConstructorSssExit(1,3,1));
  assert(paired.stadium_constructor_step==0 && paired.stadium_constructor_call==0);
  assert(paired.stadium_constructor_gobj==0 && paired.stadium_constructor_ground==0);
  assert(paired.stadium_constructor_parameter_slot==0 && paired.stadium_constructor_parameters==0);
  assert(paired.stadium_whole_b.selected_match==1 && paired.stadium_whole_b.phase==StadiumWholeBObservation::Phase::AwaitEntry);
  assert(paired.stadium_whole_b.Entry(1,4,owner_b));
  assert(paired.stadium_whole_b.Constructor(1,5,owner_b));
  assert(witness_sequence(paired,down,5,owner_b,0x80406000,0x80407000,0x80408000));
  finish_match(paired,1,owner_b,5000);
  assert(paired.stadium_whole_b.Complete());
  assert(!paired.StadiumConstructorWitnessStepValid(0)); // duplicate B entry remains rejected
  assert(!paired.AcceptStadiumConstructorSssExit(1,3,1)); // no second reset after B
  assert(paired.stadium_constructor_step==5 && paired.stadium_constructor_gobj==owner_b);

  // Legacy selected-B still has one constructor and never takes the paired reset.
  Harness legacy; legacy.stadium_whole_b.enabled=true;
  legacy.stadium_whole_b.selected_match=1;
  assert(legacy.AcceptStadiumConstructorSssExit(1,3,1));
  assert(legacy.stadium_whole_b.Entry(1,4,owner_b));
  assert(legacy.stadium_whole_b.Constructor(1,5,owner_b));
  assert(witness_sequence(legacy,up,5,owner_b,0x80406000,0x80407000,0x80408000));
  finish_match(legacy,1,owner_b,5000);
  assert(!legacy.AcceptStadiumConstructorSssExit(1,3,1));
  assert(legacy.stadium_constructor_step==5 && legacy.stadium_constructor_gobj==owner_b);
  assert(!legacy.StadiumConstructorWitnessStepValid(0));

  // A merely closed-looking/nonpaired state cannot clear stale witness data.
  Harness refused; refused.stadium_constructor_step=5; refused.stadium_constructor_call=2;
  refused.stadium_constructor_gobj=owner_a; refused.stadium_constructor_ground=0x80403000;
  refused.stadium_constructor_parameter_slot=0x80404000; refused.stadium_constructor_parameters=0x80405000;
  assert(!refused.ResetPairedConstructorWitnessForSecondMatch(1,3,1));
  assert(refused.stadium_constructor_step==5 && refused.stadium_constructor_gobj==owner_a);
}
'''
        with tempfile.TemporaryDirectory(prefix="paired-constructor-witness-") as temporary:
            source_path = Path(temporary) / "paired_constructor_witness.cpp"
            binary_path = Path(temporary) / "paired_constructor_witness"
            source_path.write_text(harness)
            subprocess.run([compiler, "-std=c++17", str(source_path), "-o", str(binary_path)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(binary_path)], check=True, capture_output=True, text=True)

    def test_paired_mode_cannot_be_used_without_its_whole_session_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "must-stay-absent"
            with self.assertRaisesRegex(ValueError, "requires the whole-session input tuple"):
                driver.run(
                    dolphin=Path("unused"), disc=Path("unused"), profile=Path("unused"),
                    menu_recipe=Path("unused"), output=output,
                    build_manifest=Path("unused"), stadium_whole_paired_idle=True,
                )
            self.assertFalse(output.exists())

            with self.assertRaisesRegex(ValueError, "excludes RNG sidecar and replay modes"):
                driver.run(
                    dolphin=Path("unused"), disc=Path("unused"), profile=Path("unused"),
                    menu_recipe=Path("unused"), output=output,
                    build_manifest=Path("unused"),
                    stadium_whole_target=Path("unused"), stadium_whole_setup=Path("unused"),
                    stadium_whole_gc=Path("unused"), stadium_whole_dol=Path("unused"),
                    stadium_whole_provenance=Path("unused"),
                    stadium_whole_paired_idle=True, stadium_whole_rng_window="1-2",
                )
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
