"""Keep the diagnostic Stadium boundary before camera and scheduled startup."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"Unclosed function body: {signature}")


class StadiumOnInitBoundaryTests(unittest.TestCase):
    def test_diagnostic_return_keeps_live_owner_before_camera_and_onstart(self):
        source = (ROOT / "src/gameplay_stage_last.c").read_text(encoding="utf-8")
        body = function_body(source, "static MeleeWebStageLast* begin_stage(")

        prepare_owner = body.index("melee_web_stadium_display_owner_prepare(")
        arm_source_journal = body.index(
            "melee_web_stadium_display_owner_arm_source_journal(")
        authored_initialization = body.index("Stage_802251E8(")
        bind_source = body.index("melee_web_stadium_display_owner_bind_source(")
        original_ground_setup = body.index("Stage_8022524C(")
        capture_owner = body.index("melee_web_stadium_display_owner_capture(")
        capture_call = body[capture_owner:]
        self.assertIn("Ground_GetMapGObj(1)", capture_call)
        self.assertNotIn("Ground_GetStageGObj(1)", capture_call)
        self.assertIn("&h->stadium_map2_buffer_owner", capture_call)
        collision_adoption = body.index("h->collision_map=stage_info.coll_data;")
        required_owners = body.index("for(unsigned i=0;i<definition->required_map_count;i++)")
        diagnostic_return = body.index(
            "if(on_init_diagnostic){if(retained_owner)*retained_owner=h;ok(e,n);return h;}"
        )
        camera = body.index("Stage_80225298();")
        onstart = body.index("Stage_802252E4(")
        self.assertEqual(
            [prepare_owner, arm_source_journal, authored_initialization, bind_source,
             original_ground_setup, capture_owner, collision_adoption,
             required_owners, diagnostic_return, camera, onstart],
            sorted([prepare_owner, arm_source_journal, authored_initialization, bind_source,
                    original_ground_setup, capture_owner, collision_adoption,
                    required_owners, diagnostic_return, camera, onstart]),
        )

        branch = body[diagnostic_return:camera]
        self.assertNotIn("melee_web_stage_last_end", branch)
        self.assertNotIn("free(", branch)
        self.assertIn("return h;", branch)
        self.assertIn("if(!defer_start)Stage_802252E4(", body)

        capture_failure = body[body.index("melee_web_stadium_display_owner_capture("):collision_adoption]
        self.assertIn("if(retained_owner)*retained_owner=h;", capture_failure)
        self.assertNotIn("melee_web_stage_last_end", capture_failure)

        end = function_body(source, "int melee_web_stage_last_end(")
        owner_end = end.index("melee_web_stadium_display_owner_end(")
        first_general_removal = end.index("HSD_GObjPLink_80390228(h->manager)")
        self.assertLess(owner_end, first_general_removal)
        ground_removal = end.index("Ground_801C4A08(found)")
        buffer_retire = end.index("melee_web_stadium_map2_buffer_owner_end(")
        self.assertGreater(buffer_retire, ground_removal)
        self.assertGreater(buffer_retire, end.index("melee_web_ground_remove_unmapped(found)"))
        self.assertLess(buffer_retire, end.index("melee_web_ground_remove_camera("))

    def test_source_journal_const_preflight_and_mutable_capture_failure(self):
        generated = ROOT / "build/gameplay-source/src/melee/gr/grpstadium.c"
        if not generated.is_file():
            self.skipTest("The checked generated gameplay source tree is not prepared")
        source = generated.read_text(encoding="utf-8")
        signature = source[source.index("static int stadium_owner_capture_matches("):]
        self.assertIn(
            "static int stadium_owner_capture_matches(\n    const MeleeWebStadiumDisplayOwner* owner",
            signature,
        )
        capture_matches = function_body(
            source, "static int stadium_owner_capture_matches("
        )
        self.assertIn("stadium_owner_source_journal_matches(", capture_matches)
        self.assertNotIn("stadium_owner_source_journal_validate(", capture_matches)
        capture = function_body(
            source, "int melee_web_stadium_display_owner_capture("
        )
        self.assertLess(
            capture.index("stadium_owner_source_journal_validate("),
            capture.index("stadium_owner_capture_matches("),
        )
        controls = function_body(
            source, "int melee_web_stadium_source_journal_controls("
        )
        self.assertIn("stadium_owner_source_journal_matches(", controls)
        self.assertIn("melee_web_stadium_display_owner_capture(", controls)
        self.assertIn("owner.source_journal_failed", controls)

    def test_only_guarded_entry_selects_source_ordered_oninit_mode(self):
        source = (ROOT / "src/gameplay_stage_last.c").read_text(encoding="utf-8")
        declaration = source.index("#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)\nMeleeWebStageLast* melee_web_stage_begin_kind_on_init_diagnostic")
        body = function_body(source, "MeleeWebStageLast* melee_web_stage_begin_kind_on_init_diagnostic(")
        self.assertLess(declaration, source.index("#endif", declaration))
        self.assertIn("stage_kind!=St_Kind_PStadium", body)
        self.assertIn("owner_out==NULL", body)
        self.assertIn("begin_stage(definition,yaku,bank,1,1,1,owner_out", body)

    def test_bind_refusal_preserves_cause_and_real_cancel_error(self):
        source = (ROOT / "src/gameplay_stage_last.c").read_text(encoding="utf-8")
        helper = function_body(source, "static int stage_report_on_init_bind_failure(")
        self.assertIn("melee_web_stage_last_end(h,cleanup_error", helper)
        self.assertIn("bind failed:", helper)
        self.assertIn("cleanup refused:", helper)
        self.assertIn("if(retained_owner)*retained_owner=h", helper)

        control = function_body(
            source, "int melee_web_stage_last_on_init_bind_refusal_controls("
        )
        self.assertIn("melee_web_stadium_display_owner_arm_source_journal(", control)
        self.assertIn("MELEE_WEB_STADIUM_SOURCE_EVENT_STAGE_E8", control)
        self.assertIn("stage_report_on_init_bind_failure(", control)
        self.assertIn("Partial Stadium display ownership must remain reachable", control)
        self.assertIn("active=h", control)

        host = (ROOT / "tests/native_menu_host_trace.cpp").read_text(encoding="utf-8")
        self.assertIn("--stadium-bind-refusal-controls", host)
        self.assertIn("no archive lookup or ", host)
        self.assertIn("original Stage routine/OnInit call", host)
        self.assertIn("std::_Exit(0)", host)

    def test_one_shot_driver_uses_distinct_guarded_recipe_and_existing_owner(self):
        source = (ROOT / "tests/native_menu_host_trace.cpp").read_text(encoding="utf-8")
        helper = function_body(source, "void run_stadium_e8_request(")
        self.assertIn("stadium-source-oninit-v1", source)
        self.assertIn("MELEE_RUN_STADIUM_SOURCE_ONINIT", (ROOT / "tests/test_gameplay_native_menus.py").read_text(encoding="utf-8"))
        self.assertIn("melee_web_stage_begin_kind_on_init_diagnostic(", helper)
        self.assertIn("melee_web_stage_last_stadium_map2_buffer_snapshot(", helper)
        self.assertIn("melee_web_stage_last_stadium_source_journal_snapshot(", helper)
        self.assertIn("melee_web_stage_last_end(retained_stage_owner", helper)
        self.assertIn("std::_Exit(1)", helper)
        failure_start = helper.index(
            "if (retained_stage_owner != nullptr || returned_stage_owner != nullptr ||"
        )
        failure_report = helper[failure_start:]
        failure_exit = failure_report.index("std::_Exit(1)")
        self.assertLess(
            failure_report.index("melee_web_stage_last_stadium_source_journal_snapshot("),
            failure_report.index("failure_source_journal={"),
        )
        self.assertLess(failure_report.index("failure_source_journal={"), failure_exit)
        self.assertIn("failure_journal.count", failure_report)
        self.assertIn("failure_journal.failed", failure_report)
        self.assertIn("failure_journal.overflowed", failure_report)
        self.assertIn("stadium_source_event_kind_name(event.kind)", failure_report)
        self.assertIn("!on_init_stage_end_succeeded", failure_report)
        self.assertIn("runtime_map_call_order_observed\\\":true", helper)
        self.assertIn("MELEE_WEB_STADIUM_SOURCE_EVENT_MAP_GOBJ", helper)
        self.assertNotIn("Stage_802251E8(St_Kind_PStadium, NULL);\n            check(melee_web_stage_last", helper)
        api = (ROOT / "src/gameplay_stage_last.c").read_text(encoding="utf-8")
        accessor = function_body(api, "int melee_web_stage_last_stadium_map2_buffer_snapshot(")
        self.assertIn("h!=active", accessor)
        self.assertIn("h->generation!=melee_web_gameplay_stats().generation", accessor)
        self.assertIn("*out=h->stadium_map2_buffer_owner", accessor)


if __name__ == "__main__":
    unittest.main()
