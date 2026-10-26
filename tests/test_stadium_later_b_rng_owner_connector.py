"""Actual-source private RNG connector controls with explicitly modeled source leaves."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(os.environ.get("MELEE_GATE2_RNG_SOURCE_ROOT",Path(__file__).resolve().parents[1]))


class StadiumLaterBRngOwnerConnectorTests(unittest.TestCase):
    def test_actual_native_connector_has_no_source_action_or_close_authority(self):
        source=(ROOT/"src/gameplay_menu_browser.cpp").read_text()
        start=source.index("void later_b_rng_owner_begin(")
        helpers=source[start:source.index("bool matched_setup_cf_service(){",start)]
        compiler=shutil.which("clang++") or shutil.which("g++")
        if not compiler: self.skipTest("A C++ compiler is required")
        harness=r'''
#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <vector>
int config_leaf=-1;std::vector<size_t> cursors;unsigned flushes=0;
#define EM_ASM_INT(...) config_leaf
void check(bool v,const char* error){if(!v)throw std::runtime_error(error);}
void melee_web_cpu_observation_set_rng_draw_cursor(size_t v){cursors.push_back(v);}
void melee_web_rng_draw_probe_scheduler_return(){++flushes;}
struct MatchedSetupCfState{
 bool later_b_gate2_mode=false,later_b_rng_owner_checked=false,later_b_rng_owner_enabled=false,later_b_rng_owner_open=false;
 bool later_b_gate2_go_observed=false;
 unsigned later_b_reference_go_tick=52,later_b_actual_go_tick=52,later_b_prefix_cursor=943;
 unsigned later_b_rng_owner_first=0,later_b_rng_owner_last=0,later_b_rng_owner_tick=0;
};
'''+helpers+r'''
MatchedSetupCfState valid(){MatchedSetupCfState s;s.later_b_gate2_mode=true;s.later_b_gate2_go_observed=true;return s;}
template<class F>void refuses(F f){bool failed=false;try{f();}catch(const std::runtime_error&){failed=true;}assert(failed);}
int main(){
 MatchedSetupCfState inactive;config_leaf=-2;later_b_rng_owner_begin(inactive,0);assert(!inactive.later_b_rng_owner_checked&&cursors.empty());
 auto absent=valid();config_leaf=-1;later_b_rng_owner_begin(absent,53);later_b_rng_owner_draw_return(absent,53);assert(cursors.empty()&&flushes==0);
 auto invalid=valid();config_leaf=-2;refuses([&]{later_b_rng_owner_begin(invalid,53);});assert(!invalid.later_b_rng_owner_checked&&cursors.empty());
 config_leaf=53*256+55;auto s=valid();later_b_rng_owner_begin(s,0);assert(cursors.empty());
 later_b_rng_owner_begin(s,53);assert(cursors.size()==1&&cursors[0]==996&&s.later_b_rng_owner_open&&flushes==0);
 refuses([&]{later_b_rng_owner_begin(s,54);});assert(cursors.size()==1&&flushes==0);
 refuses([&]{later_b_rng_owner_draw_return(s,54);});assert(s.later_b_rng_owner_open&&flushes==0);
 later_b_rng_owner_draw_return(s,53);assert(!s.later_b_rng_owner_open&&flushes==1);
 refuses([&]{later_b_rng_owner_draw_return(s,53);});assert(flushes==1);
 later_b_rng_owner_begin(s,54);later_b_rng_owner_draw_return(s,54);later_b_rng_owner_begin(s,55);later_b_rng_owner_draw_return(s,55);
 assert(cursors.size()==3&&cursors[1]==997&&cursors[2]==998&&flushes==3);
 later_b_rng_owner_begin(s,56);later_b_rng_owner_draw_return(s,56);assert(cursors.size()==3&&flushes==3);
 auto no_go=valid();no_go.later_b_gate2_go_observed=false;refuses([&]{later_b_rng_owner_begin(no_go,53);});
 auto wrong_go=valid();wrong_go.later_b_actual_go_tick=51;refuses([&]{later_b_rng_owner_begin(wrong_go,53);});
 auto wrong_prefix=valid();wrong_prefix.later_b_prefix_cursor=940;refuses([&]{later_b_rng_owner_begin(wrong_prefix,53);});
 assert(cursors.size()==3&&flushes==3);
}
'''
        with tempfile.TemporaryDirectory(prefix="gate2-rng-owner-") as temporary:
            cpp=Path(temporary)/"control.cpp";binary=cpp.with_suffix("");cpp.write_text(harness)
            result=subprocess.run([compiler,"-std=c++17","-Wall","-Wextra","-Werror",str(cpp),"-o",str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_actual_browser_connector_coordinates_and_initialization(self):
        node=os.environ.get("MELEE_GATE2_RNG_NODE") or shutil.which("node")
        if not node:self.skipTest("Node is required")
        result=subprocess.run([node,"--test",str(ROOT/"tests/stadium_later_b_rng_owner_connector.test.mjs")],cwd=ROOT,capture_output=True,text=True,timeout=60)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
