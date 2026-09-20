"""Compile and run the source-backed scene asset descriptor boundary."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


STAGE_ARCHIVES = {
    "St_Kind_Last": ROOT / "assets-local/link-verification/GrNLa.dat",
    "St_Kind_Battle": ROOT / "assets-local/link-verification/GrNBa.dat",
    "St_Kind_Story": ROOT / "assets-local/link-verification/GrSt.dat",
    "St_Kind_OldPupupu": ROOT / "assets-local/link-verification/GrOp.dat",
    "St_Kind_Shrine": ROOT / "assets-local/full-game-stage-hyrule-temple/GrSh.dat",
    "St_Kind_Izumi": ROOT / "assets-local/full-game-stage-fountain/GrIz.dat",
    "St_Kind_OldYoshi": ROOT / "assets-local/full-game-stage-yoshis-island-64/GrOy.dat",
}


class GameplayAssetManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            raise RuntimeError("A C++20 compiler is required")
        cls.temp = tempfile.TemporaryDirectory(prefix="melee asset manifest ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "gameplay_asset_manifest_test"
        command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                   "-DTARGET_PC",
                   "-I", str(ROOT / "src"),
                   "-I", str(ROOT / ".deps/melee/src"),
                   "-I", str(ROOT / ".deps/aurora/include"),
                   "-include", str(ROOT / "src/hsd_probe_compat.h"),
                   str(ROOT / "src/dat_archive.cpp"),
                   str(ROOT / "src/dat_animation.cpp"),
                   str(ROOT / "src/fighter_binding.cpp"),
                   str(ROOT / "src/gameplay_asset_manifest.cpp"),
                   str(ROOT / "tests/gameplay_asset_manifest_test.cpp"),
                   "-o", str(cls.binary)]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def test_browser_menu_scope_matches_native(self):
        import json
        import sys
        sys.path.insert(0, str(ROOT / "scripts"))
        from check_gameplay import node_runtime
        native = subprocess.check_output([str(self.binary), "--menu-names"], text=True).splitlines()
        script = "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; console.log(JSON.stringify(Object.keys(NATIVE_MENU_DISC_FILES)))"
        browser = json.loads(subprocess.check_output([str(node_runtime()), "--input-type=module", "-e", script], cwd=ROOT, text=True))
        self.assertEqual(set(native), set(browser) | {"sislib_font.bin", "dsp_coef.bin"})

    def test_source_descriptor(self):
        result = subprocess.run([str(self.binary)], cwd=ROOT,
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Source menu/match asset descriptors", result.stdout)

    def _source_stage_values(self):
        source = (ROOT / ".deps/melee/src/melee/gr/forward.h").read_text()
        values = {}
        for name in STAGE_ARCHIVES:
            match = re.search(r"/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*" +
                              re.escape(name) + r"\s*[,=]", source)
            self.assertIsNotNone(match, f"Pinned source stage identity missing: {name}")
            values[name] = int(match.group(1), 16)
        return values

    def _source_hps_files(self):
        source = (ROOT / ".deps/melee/src/melee/lb/lbaudio_ax.static.h").read_text()
        block = source.split("static const char* hps_files[] = {", 1)[1].split("};", 1)[0]
        names = re.findall(r'"([^"\\]+\.hps)"', block)
        self.assertGreater(len(names), 1, "Pinned source hps_files[] table is empty")
        return names

    def _music_rows(self, archive):
        result = subprocess.run([str(self.binary), "--music-rows", str(archive)],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        rows = []
        for line in result.stdout.splitlines():
            fields = line.split()
            self.assertEqual(len(fields), 5, f"Malformed StageParam row: {line!r}")
            rows.append(tuple(int(field) for field in fields))
        self.assertTrue(rows, f"No StageParam rows decoded from {archive}")
        return rows

    def _descriptor_names(self, stage):
        result = subprocess.run([str(self.binary), "--descriptor-stage", str(stage)],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout.splitlines()

    def test_music_candidates_bind_to_owned_stage_params(self):
        missing = [path for path in STAGE_ARCHIVES.values() if not path.is_file()]
        if missing:
            self.skipTest("Owned Gr* archives unavailable: " + ", ".join(map(str, missing)))

        stage_values = self._source_stage_values()
        hps_files = self._source_hps_files()
        ground_source = (ROOT / ".deps/melee/src/melee/gr/ground.c").read_text()
        stage_source = (ROOT / ".deps/melee/src/melee/gr/stage.c").read_text()
        for field in ("x4", "x8", "xC", "x10"):
            self.assertIn(f"phi_r30->{field}", ground_source,
                          f"Ground music selector no longer consumes {field}")
        self.assertIn("Ground_801C28AC(selected_stage.stkind, r31, &spC)", stage_source)
        for branch in ("arg1 & 0x10", "arg1 & 0x20", "arg1 & 1", "arg1 & 2"):
            self.assertIn(branch, ground_source,
                          f"Ground music selector branch disappeared: {branch}")
        self.assertIn("HSD_Randi(0xC)", ground_source)
        self.assertIn("HSD_Randi(RANDI_MAX)", ground_source)
        self.assertIn("if (bgm == -2)", ground_source)
        self.assertIn("lbAudioAx_8002305C", ground_source)

        for name, archive in STAGE_ARCHIVES.items():
            stage = stage_values[name]
            rows = [row for row in self._music_rows(archive) if row[0] == stage]
            self.assertEqual(len(rows), 1,
                             f"Expected one authored {name} StageParam row in {archive}")
            words = rows[0][1:]
            self.assertNotIn(-2, words,
                             f"{name} uses dynamic BGM -2; add the source fighter bridge before admission")
            ids = {word for word in words if word >= 0}
            self.assertTrue(ids, f"{name} has no authored music candidate")
            self.assertTrue(all(identifier < len(hps_files) for identifier in ids),
                            f"{name} StageParam points past pinned hps_files[]")
            expected = {hps_files[identifier] for identifier in ids}
            descriptor_hps = {path for path in self._descriptor_names(stage)
                              if path.endswith(".hps")}
            self.assertEqual(descriptor_hps, expected,
                             f"Descriptor music set disagrees with {archive} source words")


if __name__ == "__main__":
    unittest.main()
