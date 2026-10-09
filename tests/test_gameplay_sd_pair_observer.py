"""Compile the actual read-only pair observer with distinct live/source values."""
import json
from pathlib import Path
import shutil
import subprocess
import unittest

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]


def observer_source():
    source = (ROOT / "src/gameplay_menu_browser_observers.cpp").read_text()
    start = source.index("const char* melee_web_native_menu_match_observe(){")
    end = source.index("const char* melee_web_native_menu_source_observe(){", start)
    return source[start:end]


FIXTURE = r'''
#include "fixed_format_writer.hpp"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
constexpr int Gm_PKind_Human=0, GM_MAX_PLAYERS=6;
struct CopiedPad { signed char err; };
CopiedPad HSD_PadCopyStatus[4]={{0},{-1},{-2},{-3}};
struct Rules {
 int match_kind=1, stkind=32, timer_enabled=0, time_limit=60;
 int is_stock=0, is_vs=0, x6=1, xB=-1, is_teams=0, friendly_fire=1;
 unsigned long long x20=~0ULL;
};
struct Player { int team=0, stocks=0, slot_type=3, slot=0, ckind=-1, color=0, x12=0; };
struct StartMeleeData { Rules rules; Player players[6]; };
struct Stats {
 unsigned player_slot=0;
 int fighter_kind=0, stocks=1, motion_id=14, ground_or_air=0;
 float damage_percent=300, position[3]={0,0,0};
};
struct Match {
 StartMeleeData start;
 Stats stats[2];
 bool constructed=true, throw_stats=false;
 bool construction_complete() const { return constructed; }
 Stats player_stats(int index) const {
  if(throw_stats) throw std::runtime_error("owned stats unavailable");
  return stats[index];
 }
 const StartMeleeData& start_data() const { return start; }
 bool sudden_death() const { return true; }
 bool ready() const { return true; }
 bool paused() const { return false; }
 bool ending() const { return false; }
 bool complete() const { return false; }
 unsigned source_frames() const { return 1; }
 unsigned random_seed() const { return 123; }
 int outcome(int& winner) const { winner=-1; return 0; }
};
Match storage, *match=&storage;
unsigned prior_vs_source_frames=3600;
struct End { int outcome=1,n_winners=2,winners[6]={0,3}; };
struct Terminal { End match_end; } prior_vs_terminal;
std::string match_observer_error,terminal_match_observation;
'''

SUFFIX = r'''
int main(int argc,char**) {
 storage.stats[0].player_slot=0;
 storage.stats[1].player_slot=3;
 storage.stats[1].damage_percent=301.5f;
 for(int index : {0,3}) {
  auto& player=storage.start.players[index];
  player.slot_type=0; player.stocks=1; player.ckind=8; player.x12=300;
 }
 storage.start.players[3].slot=4;
 storage.start.players[3].color=1;
 const auto before=storage.start;
 CopiedPad pads_before[4];
 std::memcpy(pads_before,HSD_PadCopyStatus,sizeof(pads_before));
 std::cout<<melee_web_native_menu_match_observe()<<'\n';
 if(std::memcmp(&before,&storage.start,sizeof(before))!=0||storage.random_seed()!=123||
    std::memcmp(pads_before,HSD_PadCopyStatus,sizeof(pads_before))!=0)
  return 2;
 if(argc==1) {
  storage.throw_stats=true;
  std::cout<<melee_web_native_menu_match_observe()<<'\n';
  storage.throw_stats=false; storage.constructed=false;
  std::cout<<melee_web_native_menu_match_observe()<<'\n';
  match=nullptr; terminal_match_observation="{\"terminal\":true}";
  std::cout<<melee_web_native_menu_match_observe()<<'\n';
 }
}
'''


class GameplaySdPairObserverTests(OwnedWorkspaceTests):
    def compile_case(self, *, reduced_capacity=False):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            self.skipTest("a C++20 compiler is required")
        work = self.new_workspace(ROOT, "sd-pair-observer-")
        source, binary = work / "observer.cpp", work / "observer"
        production = observer_source()
        if reduced_capacity:
            # Exercise the same production overflow branch with a deliberately
            # small buffer still large enough for its complete error packet.
            self.assertIn("text[2048]", production)
            production = production.replace("text[2048]", "text[160]", 1)
        source.write_text(FIXTURE + production + SUFFIX)
        compile_result = subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra",
            "-Werror", "-I", str(ROOT / "src"), str(source), "-o", str(binary)],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(compile_result.returncode, 0, compile_result.stdout+compile_result.stderr)
        result = subprocess.run([str(binary), *(["capacity"] if reduced_capacity else [])],
            capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        return [json.loads(line) for line in result.stdout.splitlines()]

    def test_actual_live_damage_and_sparse_original_payload_are_distinct_readonly(self):
        live, unavailable, preparing, retained = self.compile_case()
        self.assertEqual(live["source_pad_errors"], [0, -1, -2, -3])
        self.assertEqual(live["observed_player_source_slots"], [0, 3])
        self.assertEqual(live["prior_vs_terminal"], {"outcome": 1, "winners": [0, 3]})
        self.assertEqual([p["damage_percent"] for p in live["players"]], [300, 301.5])
        self.assertEqual([p["source_initial_damage"] for p in live["players"]], [300, 300])
        self.assertEqual([p["source_character"] for p in live["players"]], [8, 8])
        self.assertEqual([p["source_player_index"] for p in live["players"]], [0, 3])
        self.assertEqual([p["source_slot"] for p in live["players"]], [0, 4])
        self.assertEqual([p["source_port"] for p in live["players"]], [0, 3])
        self.assertEqual([p["source_color"] for p in live["players"]], [0, 1])
        self.assertEqual([p["stocks"] for p in live["players"]], [1, 1])
        self.assertEqual([p["source_stocks"] for p in live["players"]], [1, 1])
        self.assertEqual([p["slot_type"] for p in live["players"]], [0, 0])
        self.assertEqual([p["human"] for p in live["players"]], [True, True])
        self.assertEqual(live["rules"]["is_stock"], 0)
        self.assertEqual(live["rules"]["is_vs"], 0)
        self.assertEqual(live["rules"]["source_sudden_death_flag"], 1)
        self.assertEqual(unavailable, {"ready": False, "observer_error": True})
        self.assertEqual(preparing, {})
        self.assertEqual(retained, {"terminal": True})

    def test_overflow_returns_checked_error_packet(self):
        self.assertEqual(self.compile_case(reduced_capacity=True), [{"ready": False,
            "observer_error": True, "observer_error_reason": "match observation overflow"}])


if __name__ == "__main__":
    unittest.main()
