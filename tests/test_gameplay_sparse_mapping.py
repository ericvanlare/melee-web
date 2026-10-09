"""Compile actual host/session/world/Results mapping blocks with synthetic owners.

Asset-free mapping controls only: no gameplay, input, graphics or retail claim.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests


def between(text, begin, end):
    start = text.index(begin)
    return text[start:text.index(end, start)]


class SparseMappingTests(OwnedWorkspaceTests):
    def test_actual_mapping_blocks_preserve_original_rows_and_reject_foreign_ports(self):
        compiler = shutil.which("clang++")
        if not compiler:
            self.skipTest("clang++ required")
        menu = (ROOT / "src/gameplay_menu.c").read_text()
        host = (ROOT / "src/gameplay_menu_host.c").read_text()
        session = (ROOT / "src/gameplay_match_session.cpp").read_text()
        world = (ROOT / "src/gameplay_world.cpp").read_text()
        results = (ROOT / "src/gameplay_results_session.cpp").read_text()
        count = between(menu, 'int melee_web_menu_active_player_count(', '\nstatic int stage_selection_valid')
        host_selection = between(host, 'static int host_selection_from_vs(', '\nconst VsModeData* melee_web_menu_host_post_vs_mode')
        session_loop = between(session, '        content.player_count=player_count;', '        content.begin_source_match=true;')
        world_loop = between(world, '            for(unsigned i=0;i<selection.player_count;i++){\n                const auto& player=selection.source_players[i];', '            match_context=melee_web_match_begin_players')
        results_loop = between(results, '        selection.player_count = 0;', '\n        world = std::make_unique<GameplayWorld>')
        source = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
enum {MELEE_WEB_MENU_MAX_PLAYERS=4,MELEE_WEB_MENU_MIN_PLAYERS=2,GM_MAX_PLAYERS=6};
constexpr int Gm_PKind_NA=3,Gm_PKind_Cpu=1;
struct PlayerInitData {int slot_type=Gm_PKind_NA,ckind=0,cpu_kind=0,cpu_level=0,team=0;unsigned slot=0,stocks=4,color=0,sub_color=0;};
struct StartMeleeData {struct {unsigned x0_3=1;} rules;PlayerInitData players[6];};
struct VsModeData {StartMeleeData start;};
struct Compatibility {unsigned controller,stocks,costume,sub_color;};
struct MeleeWebMenuMatchSelection {StartMeleeData start;Compatibility players[4];unsigned player_count=0,hud_layout=0,random_seed=0,unlocked_characters=0,unlocked_stages=0,save_profile_present=0;};
struct MeleeWebMenuHost {unsigned seed=17,selected_characters=3,selected_stages=4;};
struct MeleeWebFighterContent {unsigned fighter_kind,costumes;};
static const MeleeWebFighterContent* melee_web_fighter_content(int ckind){static const MeleeWebFighterContent values[]={{10,4},{20,4}};return ckind>=0&&ckind<2?&values[ckind]:nullptr;}
static unsigned melee_web_fighter_kind_count(int){return 1;}
static int melee_web_fighter_kind_at(int ckind,unsigned){return int(melee_web_fighter_content(ckind)->fighter_kind);}
struct FighterCostume {unsigned fighter_kind,costume_index;};
static const std::vector<FighterCostume>& fighter_costumes(){static const std::vector<FighterCostume> values{{10,0},{20,0}};return values;}
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static int fail(char*,size_t,const char*){return 0;}
static int ok(char*,size_t){return 1;}
struct SourceSettings {unsigned slot=0,controller=0,stocks=0;float position[3]{},facing=0;unsigned costume=0,sub_color=0,fighter_kind=0;};
struct GameplayWorldSelection {unsigned player_count=0;std::array<unsigned,4> fighter_kinds{},costume_indices{};std::array<SourceSettings,4> source_players{};bool opening_demo=false;const StartMeleeData* source_start_data=nullptr;};
using DatError=std::runtime_error;
''' + count + host_selection + r'''
static GameplayWorldSelection make_world(const MeleeWebMenuMatchSelection& selected_input){
 GameplayWorldSelection content;
 const unsigned player_count=selected_input.player_count;
 const bool opening_demo=false;
''' + session_loop + r'''
 return content;
}
static void validate_world(const GameplayWorldSelection& selection){
''' + world_loop + r'''
}
struct Standing {int slot_type=Gm_PKind_NA,ckind=0;unsigned x3=0;};
struct Results {struct {Standing player_standings[4];} match_end;};
static GameplayWorldSelection make_results(const Results& result){
 GameplayWorldSelection selection;
''' + results_loop + r'''
 return selection;
}
int main(){
 for(unsigned second:{1u,2u}){
  VsModeData vs;
  vs.start.players[0].slot_type=0;vs.start.players[0].slot=1;
  vs.start.players[second].slot_type=0;vs.start.players[second].ckind=1;
  vs.start.players[second].slot=second+1;vs.start.players[second].color=2;
  const auto before=vs;
  MeleeWebMenuHost host;MeleeWebMenuMatchSelection selected;char error[256]{};
  assert(host_selection_from_vs(&host,&vs,&selected,error,sizeof(error)));
  assert(selected.player_count==2&&selected.players[second].controller==second);
  assert(std::memcmp(&vs,&before,sizeof(vs))==0);
  assert(std::memcmp(&selected.start,&vs.start,sizeof(vs.start))==0);
  auto world=make_world(selected);world.source_start_data=&selected.start;
  assert(world.source_players[0].slot==0&&world.source_players[1].slot==second);
  assert(world.source_players[1].controller==second&&world.fighter_kinds[1]==20);
  validate_world(world);
  Results result;result.match_end.player_standings[0]={0,0,0};
  result.match_end.player_standings[second]={0,1,2};const auto retained=result;
  const auto display=make_results(result);
  assert(display.player_count==2&&display.fighter_kinds[1]==20&&display.costume_indices[1]==2);
  assert(std::memcmp(&result,&retained,sizeof(result))==0);
  if(second==2){assert(selected.start.players[1].slot_type==Gm_PKind_NA);
   assert(selected.start.players[3].slot_type==Gm_PKind_NA);
   auto wrong=vs;wrong.start.players[2].slot=2;
   assert(!host_selection_from_vs(&host,&wrong,&selected,error,sizeof(error)));
   wrong=vs;wrong.start.players[4]=wrong.start.players[2];
   assert(!host_selection_from_vs(&host,&wrong,&selected,error,sizeof(error)));
   world.source_players[1].controller=1;
   bool refused=false;try{validate_world(world);}catch(const DatError&){refused=true;}assert(refused);
  }
 }
}
'''
        scratch = self.new_workspace(ROOT, "sparse-mapping-control-")
        file = Path(scratch) / "control.cpp"
        file.write_text(source)
        binary = Path(scratch) / "control"
        compile_result = subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra",
                                        "-Werror", str(file), "-o", str(binary)],
                                       capture_output=True, text=True, timeout=30)
        self.assertEqual(compile_result.returncode, 0, compile_result.stdout + compile_result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
