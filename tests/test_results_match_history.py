"""Opt-in Results ownership/input discriminators, not CPU9/reference replay.

Enable with MELEE_WEB_RESULTS_MATCH_HISTORY=1 after building the Results trace.
The ordinary suite does not implicitly start this bounded multi-stock match.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


@unittest.skipUnless(os.environ.get("MELEE_WEB_RESULTS_MATCH_HISTORY") == "1",
                     "Explicit opt-in for bounded Results traces required")
class ResultsMatchHistoryTests(unittest.TestCase):
    def run_trace(self, flag):
        target = Path(os.environ.get(
            "MELEE_WEB_RESULTS_TRACE",
            ROOT / "build/browser-release/gameplay_results_scene_trace.js")).resolve()
        roots = [ROOT / "assets-local" / name for name in
                 ("native-menus", "repro-results-v1", "next-gate")]
        required = [target, roots[0] / "MnSlChr.usd", roots[0] / "menu01.hps",
                    roots[1] / "GmRst.usd", roots[1] / "SdRst.usd",
                    roots[1] / "TyDatai.usd", roots[2] / "GrNLa.dat"]
        required += [roots[2] / f"Pl{fighter}{suffix}.dat"
                     for fighter in ("Ss", "Ys", "Zd", "Sk", "Fc")
                     for suffix in ("", "AJ")]
        missing = [str(path.relative_to(ROOT)) for path in required if not path.is_file()]
        self.assertFalse(missing, "Opt-in requires built trace and owned inputs: " + ", ".join(missing))
        command = [str(node_runtime(ROOT)), str(target), flag, *map(str, roots)]
        evidence_root = ROOT / "work/results-match-history-tests"
        evidence_root.mkdir(parents=True, exist_ok=True)
        prefix = "p1-statistics-" if "p1-statistics" in flag else "native-"
        evidence = Path(tempfile.mkdtemp(prefix=prefix, dir=evidence_root))
        try:
            result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                    text=True, timeout=180)
        except subprocess.TimeoutExpired as error:
            for name, data in (("stdout.log", error.stdout), ("stderr.log", error.stderr)):
                (evidence / name).write_bytes(data.encode() if isinstance(data, str) else data or b"")
            (evidence / "command.json").write_text(json.dumps(
                {"command": command, "result": "timeout", "timeout_seconds": 180}, indent=2))
            self.fail(f"Native Results timeout; retained {evidence}")
        (evidence / "stdout.log").write_text(result.stdout)
        (evidence / "stderr.log").write_text(result.stderr)
        scenario_scope = (
            "natural source four-CPU9 Stock MatchExitInfo with CPU-typed standings; "
            "P1-only 180/360/600 ten-tick Start pulses; connected ports 0/1 and "
            "disconnected CPU ports 2/3; source CPU auto-page before confirmation; "
            "no forced seed, form, terminal data, or winner; native state-only, "
            "not rendered browser/reference evidence"
            if "cpu9-match-history" in flag else
            "synthetic Results CPU standings; P1-only 180/360/539 ten-tick Start pulses, "
            "connected ports 0/1, disconnected CPU ports 2/3; source ticks bracket the "
            "fresh browser Enter dispatches, not consumed historical PAD or a historical replay"
            if "p1-statistics-browser-cadence" in flag else
            "synthetic Results standings; P1-only 180/360/600 held-Start pulses, "
            "connected ports 0/1, disconnected CPU ports 2/3, automatic statistics "
            "page advance before confirmation; chosen source ticks, not historical replay"
            if "p1-statistics" in flag else
            "actual source MatchExitInfo and source-generated Sheik winner; four human PAD "
            "ports active during Match, then all four receive one-tick delayed Start pulses "
            "at chosen Results ticks; not CPU9 or historical replay"
            if "match-history" in flag else
            "synthetic Results standings / default-CSS profile subset"
        )
        (evidence / "command.json").write_text(json.dumps(
            {"command": command, "exit_code": result.returncode,
             "scope": f"native-state-only, {scenario_scope}, no GPU or retail claim"}, indent=2))
        brief = "\n".join(line[:500] for line in
                          (result.stdout + result.stderr).splitlines())[-12000:]
        self.assertEqual(result.returncode, 0, f"Retained {evidence}\n{brief}")
        print(f"Native Results evidence: {evidence}")
        return result.stdout, evidence, brief

    def test_source_pad_b_match_to_sheik_results(self):
        stdout, evidence, brief = self.run_trace("--lineup-b-match-history-host-state")
        scope = json.loads((evidence / "command.json").read_text())["scope"]
        self.assertIn("four human PAD ports active during Match", scope)
        self.assertIn("all four receive one-tick delayed Start pulses", scope)
        self.assertNotIn("P1-only", scope)
        for marker in ("host_profile=default-CSS-subset", "host_hud_layout=4",
                       "draw_scope=unrun native-state-only",
                       "P3 (source slot 2) down-B Zelda->Sheik", "source-terminal winner=2",
                       "winner_ftkind=7 winner_slot_type=0 winner_is_big_loser=0",
                       "losses=4,4,0,4", "match_draw_api_calls=0",
                       "arena_reused=1", "results_draw_api_calls=0",
                       "actual Match->Sheik Results host handoff and close passed"):
            self.assertIn(marker, stdout, f"Retained {evidence}\n{brief}")
        losses = re.findall(r"stock-loss slot=(\d) stocks=(\d)", stdout)
        for slot in ("0", "1", "3"):
            self.assertEqual([stocks for player, stocks in losses if player == slot],
                             ["3", "2", "1", "0"])
        self.assertEqual(len(losses), 12)
        handoff = re.search(r"host OnExit tick=(\d+) initial_pool=(0x[0-9a-f]+) "
                            r"source_pool=(0x[0-9a-f]+) results_draw_api_calls=0 "
                            r"destination=(\d+) route_commit=(\d) continuation=(\S+)", stdout)
        self.assertIsNotNone(handoff, f"Retained {evidence}\n{brief}")
        self.assertGreater(int(handoff[1]), 600)
        self.assertLessEqual(int(handoff[1]), 1200)
        self.assertEqual(handoff[2], handoff[3])
        # The default profile's actual human-match stats select Prize. This
        # lane checks Results close/host unload, not Prize or a CSS return.
        self.assertEqual(handoff.groups()[3:], ("192", "0", "Prize-unrun"))

    def test_natural_cpu9_terminal_flows_through_p1_cpu_auto_pages(self):
        stdout, evidence, brief = self.run_trace(
            "--lineup-b-cpu9-match-history-host-state")
        scope = json.loads((evidence / "command.json").read_text())["scope"]
        self.assertIn("natural source four-CPU9 Stock MatchExitInfo", scope)
        self.assertIn("P1-only 180/360/600 ten-tick Start pulses", scope)
        self.assertIn("connected ports 0/1 and disconnected CPU ports 2/3", scope)
        self.assertIn("no forced seed, form, terminal data, or winner", scope)
        self.assertIn("not rendered browser/reference evidence", scope)
        for marker in (
                "host_hud_layout=4", "match_mode=four-CPU9",
                "before-confirm tick=600 auto_pages=1,1",
                "p1-statistics auto-page slot=2 from=0 to=1 tick=",
                "p1-statistics auto-page slot=3 from=0 to=1 tick=",
                "p1-statistics coverage frames=", "natural_cpu9=1",
                "natural four-CPU9 Match->Results host handoff and close passed"):
            self.assertIn(marker, stdout, f"Retained {evidence}\n{brief}")
        terminal = re.search(
            r"match-history source-terminal winner=(\d) winner_ckind=(\d+) "
            r"winner_ftkind=(\d+) winner_slot_type=(\d+) "
            r"winner_is_big_loser=0 source_frame=(\d+) raw_ticks=(\d+) "
            r"match_draw_api_calls=0 match_exit_seed=(\d+) natural_cpu9=1",
            stdout)
        self.assertIsNotNone(terminal, f"Retained {evidence}\n{brief}")
        winner_slot, winner_ckind, winner_ftkind = map(int, terminal.groups()[:3])
        self.assertLess(winner_slot, 4)
        self.assertGreater(int(terminal.group(5)), 0)
        self.assertGreater(int(terminal.group(6)), 0)
        edges = re.findall(
            r"p1-statistics input tick=(\d+) port=0 held=1 trigger=1 release=0", stdout)
        releases = re.findall(
            r"p1-statistics input tick=(\d+) port=0 held=0 trigger=0 release=1", stdout)
        self.assertEqual(edges, ["180", "360", "600"])
        self.assertEqual(releases, ["190", "370", "610"])
        auto_pages = re.findall(
            r"p1-statistics auto-page slot=(\d) from=0 to=1 tick=(\d+)", stdout)
        self.assertEqual([row[0] for row in auto_pages], ["2", "3"])
        for _, tick in auto_pages:
            self.assertGreater(int(tick), 360)
            self.assertLess(int(tick), 600)
        before_confirm = re.search(
            r"p1-statistics before-confirm tick=600 auto_pages=1,1 "
            r"auto_page_ticks=(\d+),(\d+)", stdout)
        self.assertIsNotNone(before_confirm, f"Retained {evidence}\n{brief}")
        self.assertEqual(before_confirm.groups(), tuple(row[1] for row in auto_pages))
        target = winner_slot == 2 and winner_ckind == 18 and winner_ftkind == 7
        print(f"Natural CPU9 terminal winner={winner_slot} ckind={winner_ckind} "
              f"ftkind={winner_ftkind}; target Sheik winner observed={target}")

    def test_p1_held_start_disconnected_cpu_statistics(self):
        stdout, evidence, brief = self.run_trace(
            "--lineup-b-zelda-sheik-stock-p1-statistics-host-state")
        scope = json.loads((evidence / "command.json").read_text())["scope"]
        self.assertIn("P1-only 180/360/600 held-Start pulses", scope)
        self.assertIn("disconnected CPU ports 2/3", scope)
        for marker in ("match_kind=1", "winner_ckind=18 winner_ftkind=7",
                       "seed=324508639 connected=0,1 disconnected=2,3",
                       "pulse_ticks=180,360,600 hold_ticks=10 cap=900",
                       "scope=chosen-source-ticks-not-historical-replay",
                       "host_profile=default-CSS-subset", "draw_scope=unrun native-state-only",
                       "before-confirm tick=600 auto_pages=1,1",
                       "ok; all four participant demo owners constructed and closed"):
            self.assertIn(marker, stdout, f"Retained {evidence}\n{brief}")
        edges = re.findall(r"p1-statistics input tick=(\d+) port=0 held=1 trigger=1 release=0", stdout)
        releases = re.findall(r"p1-statistics input tick=(\d+) port=0 held=0 trigger=0 release=1", stdout)
        self.assertEqual(edges, ["180", "360", "600"])
        self.assertEqual(releases, ["190", "370", "610"])
        auto_pages = re.findall(r"p1-statistics auto-page slot=(\d) from=0 to=1 tick=(\d+)", stdout)
        self.assertEqual([row[0] for row in auto_pages], ["2", "3"])
        for _, tick in auto_pages:
            self.assertGreater(int(tick), 360)
            self.assertLess(int(tick), 600)
        phases = re.findall(r"p1-statistics state tick=\d+ phase=(\d)", stdout)
        self.assertEqual(set(phases), set("01234"))
        coverage = re.search(r"p1-statistics coverage frames=(\d+) trigger_edges=3 releases=3 "
                             r"held_ticks=30 auto_page_ticks=(\d+),(\d+) source_draw_api_calls=0", stdout)
        self.assertIsNotNone(coverage, f"Retained {evidence}\n{brief}")
        self.assertGreater(int(coverage[1]), 610)
        self.assertLessEqual(int(coverage[1]), 900)
        self.assertEqual(coverage.groups()[1:], tuple(row[1] for row in auto_pages))
        handoff = re.search(r"host OnExit\+commit tick=(\d+) initial_pool=(0x[0-9a-f]+) "
                            r"source_pool=(0x[0-9a-f]+)", stdout)
        self.assertIsNotNone(handoff, f"Retained {evidence}\n{brief}")
        self.assertEqual(handoff[1], coverage[1])
        self.assertEqual(handoff[2], handoff[3])

    def test_p1_browser_observed_cadence_before_cpu_auto_page(self):
        stdout, evidence, brief = self.run_trace(
            "--lineup-b-zelda-sheik-stock-p1-statistics-browser-cadence-host-state")
        scope = json.loads((evidence / "command.json").read_text())["scope"]
        self.assertIn("source ticks bracket the fresh browser Enter dispatches", scope)
        self.assertIn("not consumed historical PAD or a historical replay", scope)
        for marker in ("connected=0,1 disconnected=2,3 pulse_ticks=180,360,539 hold_ticks=10",
                       "scope=browser-observed-dispatch-brackets-not-historical",
                       "before-confirm tick=539", "host_profile=default-CSS-subset",
                       "source_draw_api_calls=0", "host OnExit+commit",
                       "source_pool="):
            self.assertIn(marker, stdout, f"Retained {evidence}\n{brief}")
        edges = re.findall(r"p1-statistics input tick=(\d+) port=0 held=1 trigger=1 release=0", stdout)
        releases = re.findall(r"p1-statistics input tick=(\d+) port=0 held=0 trigger=0 release=1", stdout)
        self.assertEqual(edges, ["180", "360", "539"])
        self.assertEqual(releases, ["190", "370", "549"])
        before = re.search(r"before-confirm tick=539 phase=(\d) pages=([0-3]),([0-3]),([0-3]),([0-3]) "
                           r"auto_page_ticks=(\d+),(\d+)", stdout)
        self.assertIsNotNone(before, f"Retained {evidence}\n{brief}")
        self.assertEqual(before.groups()[1:5], ("0", "0", "0", "0"))
        self.assertEqual(before.groups()[5:], ("0", "0"))
        coverage = re.search(r"p1-statistics coverage frames=(\d+) trigger_edges=3 releases=3 "
                             r"held_ticks=30 auto_page_ticks=(\d+),(\d+) source_draw_api_calls=0",
                             stdout)
        self.assertIsNotNone(coverage, f"Retained {evidence}\n{brief}")
        self.assertGreater(int(coverage[1]), 549)
        self.assertLessEqual(int(coverage[1]), 570)
        self.assertEqual(coverage.groups()[1:], ("0", "0"))
        handoff = re.search(r"host OnExit\+commit tick=(\d+) initial_pool=(0x[0-9a-f]+) "
                            r"source_pool=(0x[0-9a-f]+)", stdout)
        self.assertIsNotNone(handoff, f"Retained {evidence}\n{brief}")
        self.assertEqual(handoff[1], coverage[1])
        self.assertEqual(handoff[2], handoff[3])

if __name__ == "__main__":
    unittest.main()
