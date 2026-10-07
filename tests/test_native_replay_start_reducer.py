"""Keep the B4 replay-start reducer to one production CSS source tick."""

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


class NativeReplayStartReducerTests(unittest.TestCase):
    def test_uses_exact_v10_input_and_one_css_tick_without_drawing(self):
        source = (ROOT / "tests/native_menu_host_trace.cpp").read_text()
        body = function_body(source, "void run_v10_css_replay_start_prefix(")

        self.assertIn("recipe.version == melee_web::kRetailReplayFighterVersion", body)
        self.assertIn("recipe.seed == 3336171383U", body)
        self.assertIn("recipe.frames.size() == 50394", body)
        self.assertIn("bytes.size() == 2241622", body)
        self.assertIn("recipe.initial_css->css_data.data()", body)
        self.assertIn("recipe.initial_css->ko_counts.data()", body)
        self.assertIn("recipe.initial_css->game_rules.data()", body)
        self.assertIn("recipe.initial_css->save_data.data()", body)
        self.assertIn("recipe.frames[0].pads.data()", body)

        ordered_boundaries = (
            "melee_web_menu_host_apply_replay_context(",
            "melee_web_menu_host_enter(",
            "melee_web::retail_replay_session_initial(recipe)",
            "melee_web_menu_host_tick(",
            "melee_web::retail_replay_frame(recipe, 0,",
            "melee_web_audio_render(",
            "melee_web_menu_host_leave(",
        )
        positions = [body.rindex(boundary) if boundary ==
                     "melee_web_menu_host_leave(" else body.index(boundary)
                     for boundary in ordered_boundaries]
        self.assertEqual(positions, sorted(positions))
        self.assertEqual(body.count("melee_web_menu_host_tick("), 1)
        self.assertEqual(body.count("melee_web_audio_render("), 1)
        self.assertEqual(body.count("melee_web::retail_replay_frame("), 1)
        self.assertNotRegex(body, r"\b(for|while)\s*\(")
        for forbidden in ("melee_web_menu_host_draw(", "retail_replay_draw(",
                          "retail_replay_end(", "aurora_begin_frame("):
            self.assertNotIn(forbidden, body)


if __name__ == "__main__":
    unittest.main()
