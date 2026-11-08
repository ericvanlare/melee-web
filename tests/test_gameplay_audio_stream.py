"""Run the original HPS scheduler proof when built and owned audio is available."""
import json
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameplayAudioStreamTests(unittest.TestCase):
    def test_final_destination_refills_at_source_cadence(self):
        target = ROOT / "build/browser-rollback-staging-release/gameplay_audio_stream_trace.js"
        nodes = list((ROOT / ".deps/emsdk/node").glob("*/bin/node"))
        asset_root = Path(os.environ.get("MELEE_HPS_TRACE_ASSET_ROOT", ROOT / "assets-local/next-gate"))
        assets = [asset_root / name for name in
                  ("main.ssm", "mario.ssm", "smash2.sem", "dsp_coef.bin", "sp_end.hps")]
        if not target.is_file() or not nodes or not all(path.is_file() for path in assets):
            self.skipTest("Build the single-threaded staging HPS trace and provide the owned Final Destination audio")
        result = subprocess.run([str(nodes[0]), str(target), "--source-cadence-leaf", *map(str, assets)],
                                text=True, capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        records = {}
        for line in result.stdout.splitlines():
            if line.startswith("HPS_SOURCE_"):
                label, payload = line.split(" ", 1)
                records.setdefault(label, []).append(json.loads(payload))
        scan, = records["HPS_SOURCE_SCAN"]
        span, = records["HPS_SOURCE_SPAN7"]
        pending = records["HPS_SOURCE_PENDING"]
        completions = records["HPS_SOURCE_COMPLETION"]
        scanned = pending[:scan["pending_ticks"]]
        gaps = [b["frame"] - a["frame"] for a, b in zip(scanned, scanned[1:])]
        self.assertEqual(scan["minimum_pending_gap"], min(gaps))
        self.assertEqual(scan["adjacent_pending_pairs"], gaps.count(1))
        self.assertEqual(scan["adjacent_pending_pairs"], 0)
        self.assertFalse(scan["last_tick_pending"])
        self.assertEqual(len(completions), len(pending))
        for request, completion in zip(pending, completions):
            self.assertEqual(completion["frame"], request["frame"] + 1)
            self.assertEqual(completion["header_id"], request["header_id"])
            self.assertEqual(completion["source_loading"], 0)
        self.assertEqual(scan["samples"], scan["frames"] * 32000 // 60)
        self.assertGreater(scan["revisited"], 0)
        self.assertEqual(span["endpoint_frame"] - span["pre_frame"], 7)
        self.assertEqual(pending[-1], scanned[0])
        self.assertEqual(completions[-1]["frame"], span["next_frame"])
        self.assertEqual(pending[-1]["type"], 0x21)
        self.assertTrue(pending[-1]["owner_token_valid"])
        self.assertTrue(pending[-1]["strict_snapshot_refused"])

    def test_original_menu_bgm_ids_select_owned_hps_candidates(self):
        target = ROOT / "build/browser/gameplay_audio_stream_trace.js"
        nodes = list((ROOT / ".deps/emsdk/node").glob("*/bin/node"))
        menu = ROOT / "assets-local/native-menus"
        audio = ROOT / "assets-local/next-gate"
        assets = [audio / name for name in
                  ("main.ssm", "mario.ssm", "smash2.sem", "dsp_coef.bin")]
        assets += [menu / name for name in ("menu01.hps", "menu3.hps")]
        if not target.is_file() or not nodes or not all(path.is_file() for path in assets):
            self.skipTest("Build the HPS trace and provide the original menu music candidates")
        result = subprocess.run([str(nodes[0]), str(target), *map(str, assets)],
                                text=True, capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Original CSS/SSS BGM IDs selected and streamed menu01.hps/menu3.hps through one owner",
                      result.stdout)

    def test_original_music_id_stream_loop_and_restart(self):
        target = ROOT / "build/browser/gameplay_audio_stream_trace.js"
        nodes = list((ROOT / ".deps/emsdk/node").glob("*/bin/node"))
        assets = [ROOT / "assets-local/next-gate" / name for name in
                  ("main.ssm", "mario.ssm", "smash2.sem", "dsp_coef.bin", "sp_end.hps")]
        if not target.is_file() or not nodes or not all(p.is_file() for p in assets):
            self.skipTest("Build fighter/audio stream target and supply optional owned HPS/SSM/SEM/coefficient inputs")
        result = subprocess.run([str(nodes[0]), str(target), *map(str, assets)],
                                text=True, capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count("HPS payloads58 revisited8"), 2)
        self.assertIn("Original HPS three-slot scheduler, native PCM loop, and restart passed", result.stdout)

    def test_original_music_registry_switches_authored_hps_files(self):
        target = ROOT / "build/browser/gameplay_audio_stream_trace.js"
        nodes = list((ROOT / ".deps/emsdk/node").glob("*/bin/node"))
        next_gate = ROOT / "assets-local/next-gate"
        prize = ROOT / "assets-local/results-mario"
        assets = [next_gate / name for name in
                  ("main.ssm", "mario.ssm", "smash2.sem", "dsp_coef.bin")]
        info = [prize / ("s_info%d.hps" % index) for index in (1, 2, 3)]
        if not target.is_file() or not nodes or not all(p.is_file() for p in assets + info):
            self.skipTest("Build the HPS trace and supply the owned Prize music fixtures")
        result = subprocess.run([str(nodes[0]), str(target), *(str(p) for p in
            assets + [info[0]] + info)], text=True, capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Original HPS registry selected three authored files and preserved source changes", result.stdout)


if __name__ == "__main__":
    unittest.main()
