"""Compile the actual browser message function with asset-free owner controls."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BrowserMenuOwnerMessageTests(unittest.TestCase):
    def test_entered_owner_overrides_stale_construction_target(self):
        source = (ROOT / "src/gameplay_menu_browser.cpp").read_text()
        begin = source.index("const char* source_menu_message(){")
        end = source.index("\nunsigned render_frame=", begin)
        function = source[begin:end]
        header = (ROOT / "src/gameplay_menu_host.h").read_text()
        scenes = re.search(r"enum \{\s*MELEE_WEB_MENU_HOST_SCENE_CSS.*?\n\};",
                           header, re.S).group()
        forward = (ROOT / ".deps/melee/src/melee/gm/forward.h").read_text()
        training = re.search(r"/\*\s*([0-9A-Fa-f]+)\s*\*/\s*GM_TRAINING", forward).group(1)
        fixture = r'''
#include <cassert>
#include <cstring>
struct MeleeWebMenuHost { int scene, mode; };
MeleeWebMenuHost* host = nullptr;
bool host_entered = false;
int scene_reads = 0;
int melee_web_menu_host_source_scene(const MeleeWebMenuHost* h) {
    assert(h && host_entered); ++scene_reads; return h->scene;
}
int melee_web_menu_host_mode_kind(const MeleeWebMenuHost* h) {
    assert(h && host_entered); return h->mode;
}
namespace melee_web {
enum class GameplayMenuScene { Characters, Stages, Main, Title };
}
melee_web::GameplayMenuScene pending_menu_scene = melee_web::GameplayMenuScene::Stages;
'''
        controls = r'''
int main() {
    assert(std::strcmp(source_menu_message(), "Original menu") == 0);
    assert(scene_reads == 0);
    MeleeWebMenuHost owner{MELEE_WEB_MENU_HOST_SCENE_CSS, 0};
    host = &owner;
    assert(std::strcmp(source_menu_message(), "Original menu") == 0);
    assert(scene_reads == 0);
    host_entered = true;
    const auto check = [&](int scene, int mode, const char* expected) {
        owner = {scene, mode};
        const auto before = owner;
        const auto reads = scene_reads;
        assert(std::strcmp(source_menu_message(), expected) == 0);
        assert(scene_reads == reads + 1);
        assert(owner.scene == before.scene && owner.mode == before.mode);
        assert(pending_menu_scene == melee_web::GameplayMenuScene::Stages);
    };
    // Actual V8 boundary: prior SSS target, now entered returned CSS.
    check(MELEE_WEB_MENU_HOST_SCENE_CSS, 0, "Original character select");
    check(MELEE_WEB_MENU_HOST_SCENE_CSS, GM_TRAINING, "Original Training character select");
    check(MELEE_WEB_MENU_HOST_SCENE_SSS, 0, "Original stage select");
    check(MELEE_WEB_MENU_HOST_SCENE_MAIN, 0, "Original main menu");
    check(MELEE_WEB_MENU_HOST_SCENE_TITLE, 0, "Original title");
    check(MELEE_WEB_MENU_HOST_SCENE_OPENING, 0, "Original Opening movie");
    check(0, 0, "Original menu");
    // Source scenes changing at reentry/settle/resume do not inherit a target.
    check(MELEE_WEB_MENU_HOST_SCENE_CSS, 0, "Original character select");
    host_entered = false;
    const int reads = scene_reads;
    assert(std::strcmp(source_menu_message(), "Original menu") == 0);
    assert(scene_reads == reads);
}
'''
        compiler = shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler, "A C++20 compiler is required")
        scratch_root = ROOT / "work"
        scratch_root.mkdir(exist_ok=True)
        scratch = Path(tempfile.mkdtemp(prefix="browser-owner-message-", dir=scratch_root))
        passed = False
        try:
            cpp, binary = scratch / "control.cpp", scratch / "control"
            cpp.write_text(scenes + "\nenum { GM_TRAINING = 0x" + training + " };\n" +
                           fixture + function + controls)
            built = subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                                    str(cpp), "-o", str(binary)],
                                   capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr + f"\nRetained: {scratch}")
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr + f"\nRetained: {scratch}")
            passed = True
        finally:
            if passed:
                shutil.rmtree(scratch)


if __name__ == "__main__":
    unittest.main()
