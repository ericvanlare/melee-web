"""Control the rollback-staging Mario/Final Destination source-menu admission."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class OnlineMenuAdmissionTests(unittest.TestCase):
    def test_native_predicates_admit_only_mario_and_final_destination(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            self.skipTest("A native C compiler is required")
        source = r"""
enum {
    CKIND_MARIO = 8,
    CKIND_FOX = 1,
    St_Kind_Last = 0x20,
    St_Kind_PStadium = 3,
    St_Kind_Dummy = -1,
};
#include "src/slippi_profile/online_menu_admission.inc"

int main(void)
{
    return melee_web_online_menu_admission_character(CKIND_MARIO) &&
           !melee_web_online_menu_admission_character(CKIND_FOX) &&
           melee_web_online_menu_admission_stage(St_Kind_Last) &&
           !melee_web_online_menu_admission_stage(St_Kind_PStadium) &&
           !melee_web_online_menu_admission_stage(St_Kind_Dummy) ? 0 : 1;
}
"""
        with tempfile.TemporaryDirectory(prefix="online-menu-admission-") as directory:
            c_file = Path(directory) / "control.c"
            binary = Path(directory) / "control"
            c_file.write_text(source)
            built = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ROOT), str(c_file), "-o", str(binary)],
                cwd=ROOT, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run(
                [str(binary)], cwd=ROOT, capture_output=True, text=True, timeout=10,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_staging_wiring_preserves_content_and_precedes_stadium_exception(self):
        source = (ROOT / "src/gameplay_menu.c").read_text()
        character = source.split(
            "int melee_web_menu_character_available(int ckind)", 1
        )[1].split("int melee_web_menu_stage_available(int stkind)", 1)[0]
        stage = source.split(
            "int melee_web_menu_stage_available(int stkind)", 1
        )[1].split("int melee_web_menu_stage_explicit_confirm_available(int stkind)", 1)[0]
        explicit = source.split(
            "int melee_web_menu_stage_explicit_confirm_available(int stkind)", 1
        )[1].split("int melee_web_menu_active_player_count(", 1)[0]

        self.assertIn("#if defined(MELEE_WEB_ROLLBACK_STAGING)", character)
        self.assertIn("melee_web_fighter_content(ckind) == NULL", character)
        self.assertIn("melee_web_online_menu_admission_character(ckind)", character)
        self.assertIn("#if defined(MELEE_WEB_ROLLBACK_STAGING)", stage)
        self.assertIn("melee_web_stage_content(stkind) == NULL", stage)
        self.assertIn("melee_web_online_menu_admission_stage(stkind)", stage)
        self.assertLess(
            explicit.index("if (!melee_web_menu_stage_available(stkind))"),
            explicit.index("if (stkind == St_Kind_PStadium"),
        )

    def test_patched_original_menu_routes_selection_through_admission_hooks(self):
        patch = (ROOT / "patches/melee-gameplay.patch").read_text()
        self.assertIn("MELEE_WEB_MENU_CHARACTER_AVAILABLE", patch)
        self.assertIn("MELEE_WEB_MENU_STAGE_AVAILABLE", patch)
        self.assertIn("MELEE_WEB_MENU_STAGE_EXPLICIT_CONFIRM_AVAILABLE", patch)
        self.assertIn("lbAudioAx_80024030(3);\n+                return;", patch)


if __name__ == "__main__":
    unittest.main()
