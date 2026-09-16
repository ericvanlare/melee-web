"""The canonical browser entry points must use the original menu player."""
from pathlib import Path
import json
import re
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

    def test_native_game_manifest_matches_browser_allowlist(self):
        browser = (ROOT / "src" / "gameplay_menu_browser.cpp").read_text(encoding="utf-8")
        manifest = subprocess.run(
            ["node", "--input-type=module", "-e",
             "import {NATIVE_GAME_DISC_FILES} from './web/runtime-assets.mjs';"
             "process.stdout.write(JSON.stringify(Object.keys(NATIVE_GAME_DISC_FILES)));"],
            cwd=ROOT, check=True, capture_output=True, text=True,
        )
        native_block = re.search(
            r"constexpr std::array<std::string_view,\d+> keys=\{(.*?)\};",
            browser, re.S,
        )
        self.assertIsNotNone(native_block)
        manifest_keys = set(json.loads(manifest.stdout))
        native_keys = set(re.findall(r'"([^"]+)"', native_block.group(1)))
        self.assertEqual(native_keys, manifest_keys | {"dsp_coef.bin", "sislib_font.bin"},
                         "native upload allowlist must match the disc manifest plus generated inputs")

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

    def test_roy_and_dr_mario_visible_action_inventories_use_source_ids(self):
        script = """
import {actionInventory} from './web/action-sweep.mjs';
const hasExpectation=(inventory,first,last)=>inventory.cases.some(item=>
  item.expect.some(([actualFirst,actualLast])=>actualFirst===first&&actualLast===last));
const commonNames=new Set(['jab','forward smash','down aerial','air dodge',
                           'wavedash left 10/10','wavedash right 10/10']);
const dr=actionInventory(21), roy=actionInventory(26);
if(!dr || dr.id!=='dr-mario-visible-actions-v1' || dr.fighter!=='Dr. Mario')process.exit(1);
if(!roy || roy.id!=='roy-visible-actions-v1' || roy.fighter!=='Roy')process.exit(2);
for(const inventory of [dr,roy]) {
  const names=new Set(inventory.cases.map(item=>item.name));
  for(const name of commonNames)if(!names.has(name))process.exit(3);
  if(inventory.cases.length<45 || inventory.minimumStageFrames<4200)process.exit(4);
}
const drNames=new Set(dr.cases.map(item=>item.name));
for(const name of ['Dr. Mario taunt','Megavitamin','Megavitamin (air)','Super Sheet',
                   'Super Sheet (air)','Super Jump Punch','Super Jump Punch (air)',
                   'Dr. Tornado','Dr. Tornado (air)'])if(!drNames.has(name))process.exit(7);
const royNames=new Set(roy.cases.map(item=>item.name));
for(const name of ['Flare Blade charge/release','Double-Edge Dance chain (representative)',
                   'Blazer','Blazer (air)','Counter stance','Counter stance (air)'])
  if(!royNames.has(name))process.exit(8);
if(!hasExpectation(dr,341,342))process.exit(5);
for(const id of [343,344,345,346,347,348,349,350])
  if(!hasExpectation(dr,id,id))process.exit(5);
for(const id of [341,342,343])if(!hasExpectation(roy,id,id))process.exit(6);
if(!hasExpectation(roy,349,349) ||
   !hasExpectation(roy,350,357))process.exit(6);
for(const id of [367,368,369,371])if(!hasExpectation(roy,id,id))process.exit(6);
"""
        subprocess.run(
            ["node", "--input-type=module", "-e", script],
            cwd=ROOT, check=True,
        )


if __name__ == "__main__":
    unittest.main()
