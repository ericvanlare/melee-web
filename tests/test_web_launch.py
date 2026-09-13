"""The canonical browser entry points must use the original menu player."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class WebLaunchTests(unittest.TestCase):
    def test_runtime_is_native_menu_player(self):
        runtime = (ROOT / "web" / "runtime.html").read_text(encoding="utf-8")
        development = (ROOT / "web" / "runtime-development.mjs").read_text(encoding="utf-8")
        owner = (ROOT / "web" / "melee-runtime.mjs").read_text(encoding="utf-8")
        self.assertIn('type="module" src="runtime-development.mjs"', runtime)
        self.assertIn("mountMeleeRuntime", development)
        self.assertIn("gameplay_menu_browser.js", development)
        self.assertIn("loadNativeGameDisc", owner)
        self.assertIn("_melee_web_native_menu_launch", owner)
        for text in ("perf-metrics", "audio-metrics", "runtime-cache.js",
                     "Clear render cache + reload", "Run visible action/performance sweep"):
            self.assertIn(text, runtime)
        for text in ("Scene entry profile", "clearOnLoad:clearRenderCacheOnLoad",
                     "Browser long task", "Render cache save",
                     "_melee_web_native_menu_pad_sample_full", "_melee_web_native_menu_player_state",
                     "Action performance report"):
            self.assertIn(text, development)
        self.assertIn("markRuntimeCacheDirty", owner)
        for source in (runtime, development, owner):
            self.assertNotIn("match-menu", source)
            self.assertNotIn("gameplay_browser.js", source)

    def test_legacy_native_menu_url_is_an_alias(self):
        alias = (ROOT / "web" / "native-menu.html").read_text(encoding="utf-8")
        self.assertIn("runtime.html", alias)
        self.assertNotIn("gameplay_menu_browser.js", alias)

    def test_root_and_viewer_entry_points(self):
        index = (ROOT / "web" / "index.html").read_text(encoding="utf-8")
        viewer = (ROOT / "web" / "viewer.html").read_text(encoding="utf-8")
        self.assertIn('href="runtime.html"', index)
        self.assertIn('href="viewer.html"', index)
        self.assertIn("melee_web_asset_open", viewer)

    def test_runtime_builds_native_browser_target(self):
        build = (ROOT / "scripts" / "build.py").read_text(encoding="utf-8")
        cmake = (ROOT / "cmake" / "FighterRuntime.cmake").read_text(encoding="utf-8")
        self.assertIn('"runtime": ["gameplay_menu_browser"]', build)
        self.assertIn('"all": ["gx_probe", "gameplay_checks", "gameplay_menu_browser"]', build)
        self.assertIn("HEAPU8,HEAP32,HEAPF32,UTF8ToString", cmake)
        self.assertIn("initial_pipeline_cache.db", cmake)
        self.assertIn("LINK_DEPENDS", cmake)
        self.assertIn("_melee_web_native_menu_pad_sample_full", cmake)
        self.assertIn("_melee_web_native_menu_player_state", cmake)

    def test_marth_visible_action_inventory_is_versioned_and_broad(self):
        script = """
import {actionInventory} from './web/action-sweep.mjs';
const inventory=actionInventory(18);
if(!inventory || inventory.id!=='marth-visible-actions-v1' || inventory.fighter!=='Marth')process.exit(1);
const names=new Set(inventory.cases.map(item=>item.name));
for(const name of ['jab','forward smash','down aerial','air dodge',
                   'wavedash left 10/10','wavedash right 10/10',
                   'Shield Breaker charge/release','Dancing Blade chain',
                   'Dolphin Slash','Counter'])if(!names.has(name))process.exit(2);
if(inventory.cases.length<40 || inventory.minimumStageFrames<4200)process.exit(3);
"""
        subprocess.run(
            ["node", "--input-type=module", "-e", script],
            cwd=ROOT, check=True,
        )


if __name__ == "__main__":
    unittest.main()
