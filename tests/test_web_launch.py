"""The canonical browser entry points must use the original menu player."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class WebLaunchTests(unittest.TestCase):
    def test_runtime_is_native_menu_player(self):
        runtime = (ROOT / "web" / "runtime.html").read_text(encoding="utf-8")
        self.assertIn("gameplay_menu_browser.js", runtime)
        self.assertIn("loadNativeGameDisc", runtime)
        self.assertIn("_melee_web_native_menu_launch", runtime)
        self.assertIn("perf-metrics", runtime)
        self.assertIn("audio-metrics", runtime)
        self.assertIn("runtime-cache.js", runtime)
        self.assertIn("Scene entry profile", runtime)
        self.assertIn("Clear render cache + reload", runtime)
        self.assertIn("clearOnLoad:clearRenderCacheOnLoad", runtime)
        self.assertIn("markRuntimeCacheDirty", runtime)
        self.assertIn("Browser long task", runtime)
        self.assertIn("Render cache save", runtime)
        self.assertNotIn("match-menu", runtime)
        self.assertNotIn("gameplay_browser.js", runtime)

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


if __name__ == "__main__":
    unittest.main()
