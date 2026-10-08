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
        authored_initialization = body.index("Stage_802251E8(")
        bind_source = body.index("melee_web_stadium_display_owner_bind_source(")
        original_ground_setup = body.index("Stage_8022524C(")
        capture_owner = body.index("melee_web_stadium_display_owner_capture(")
        capture_call = body[capture_owner:]
        self.assertIn("Ground_GetMapGObj(1)", capture_call)
        self.assertNotIn("Ground_GetStageGObj(1)", capture_call)
        collision_adoption = body.index("h->collision_map=stage_info.coll_data;")
        required_owners = body.index("for(unsigned i=0;i<definition->required_map_count;i++)")
        diagnostic_return = body.index(
            "if(on_init_diagnostic){if(retained_owner)*retained_owner=h;ok(e,n);return h;}"
        )
        camera = body.index("Stage_80225298();")
        onstart = body.index("Stage_802252E4(")
        self.assertEqual(
            [prepare_owner, authored_initialization, bind_source,
             original_ground_setup, capture_owner, collision_adoption,
             required_owners, diagnostic_return, camera, onstart],
            sorted([prepare_owner, authored_initialization, bind_source,
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

    def test_only_guarded_entry_selects_source_ordered_oninit_mode(self):
        source = (ROOT / "src/gameplay_stage_last.c").read_text(encoding="utf-8")
        declaration = source.index("#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)\nMeleeWebStageLast* melee_web_stage_begin_kind_on_init_diagnostic")
        body = function_body(source, "MeleeWebStageLast* melee_web_stage_begin_kind_on_init_diagnostic(")
        self.assertLess(declaration, source.index("#endif", declaration))
        self.assertIn("stage_kind!=St_Kind_PStadium", body)
        self.assertIn("owner_out==NULL", body)
        self.assertIn("begin_stage(definition,yaku,bank,1,1,1,owner_out", body)


if __name__ == "__main__":
    unittest.main()
