"""Compile the actual public SD finish against an RNG/PAD-mutating publisher."""
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    cursor = opening + 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[start:cursor]


class SuddenDeathHandoffOrderTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, "sd-handoff-order-")

    def test_actual_public_finish_captures_after_publication_before_teardown(self):
        compiler = shutil.which("clang++")
        if not compiler:
            self.skipTest("Native clang++ unavailable")
        source = (ROOT / "src/gameplay_match_session.cpp").read_text()
        end_flow = function(source, "    void end_flow()")
        finish = function(source, "void GameplayMatchSession::finish_sudden_death(")
        harness = r'''
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <cassert>
constexpr int MELEE_WEB_PAD_STATE_BYTES=822;
constexpr int MELEE_WEB_MENU_MATCH_CONTINUATION_RESULTS=1;
struct MeleeWebMenuMatchContinuation {int kind=0;};
struct MeleeWebMenuHost {};
struct MatchExitInfo {int marker=0;};
static std::vector<std::string> events;
static uint32_t host_seed=777,*seed_ptr=&host_seed;
static uint8_t live_pad=0;
static bool published=false,publication_ok=true;
static uint32_t delivered_seed=0;static uint8_t delivered_pad=0;
static void check(bool value,const char* error){if(!value)throw std::runtime_error(error);}
static int melee_web_match_flow_end(int*,char*,std::size_t){events.push_back("flow_end");*seed_ptr+=2;live_pad=2;return 1;}
static int melee_web_match_rules_publish_result(){
 if(!publication_ok){events.push_back("publish_failed");return 0;}
 if(published){events.push_back("publish_idempotent");return 1;}
 events.push_back("publish");*seed_ptr+=100;live_pad=42;published=true;return 1;
}
static int melee_web_match_rules_terminal_data(MatchExitInfo* out){out->marker=published?1:0;return published;}
static int melee_web_menu_host_sudden_death_finish(MeleeWebMenuHost*,uint64_t,
 const MatchExitInfo* info,uint32_t seed,const uint8_t* input,
 MeleeWebMenuMatchContinuation* result,char*,std::size_t){
 assert(seed_ptr==&host_seed&&info->marker==1);events.push_back("host_finish");
 delivered_seed=seed;delivered_pad=input[0];result->kind=1;return 1;
}
struct Storage {
 struct {bool sudden_death=true;} selected;
 MeleeWebMenuHost host;MeleeWebMenuHost* sudden_death_host=&host;
 bool sudden_death_claimed=true;uint64_t sudden_death_owner_id=1;
 uint32_t seed=17;int flow_value=1;int* flow=&flow_value;
 Storage(){seed_ptr=&seed;live_pad=1;}
''' + end_flow + r'''
 void close(){end_flow();melee_web_match_rules_publish_result();events.push_back("teardown");seed_ptr=&host_seed;live_pad=0;}
 ~Storage(){seed_ptr=&host_seed;}
};
class GameplayMatchSession {
public:
 std::unique_ptr<Storage> storage_=std::make_unique<Storage>();bool completed=true;
 bool complete()const{return completed;}
 void capture_handoff(uint32_t& seed,uint8_t* input)const{events.push_back("capture");seed=*seed_ptr;std::memset(input,live_pad,822);}
 void finish_sudden_death(MeleeWebMenuMatchContinuation&);
};
''' + finish + r'''
int main(){
 {
 GameplayMatchSession session;MeleeWebMenuMatchContinuation result;
 session.finish_sudden_death(result);
 assert(result.kind==1&&!session.storage_&&delivered_seed==119&&delivered_pad==42);
 assert((events==std::vector<std::string>{"flow_end","publish","capture","publish_idempotent","teardown","host_finish"}));
 }
 events.clear();published=false;
 {
 GameplayMatchSession session;session.completed=false;MeleeWebMenuMatchContinuation result;result.kind=99;
 bool refused=false;try{session.finish_sudden_death(result);}catch(const std::runtime_error&){refused=true;}
 assert(refused&&result.kind==0&&events.empty()&&session.storage_->flow!=nullptr);
 }
 events.clear();publication_ok=false;
 {
 GameplayMatchSession session;MeleeWebMenuMatchContinuation result;
 bool refused=false;try{session.finish_sudden_death(result);}catch(const std::runtime_error&){refused=true;}
 assert(refused&&result.kind==0&&(events==std::vector<std::string>{"flow_end","publish_failed"}));
 }
}
'''
        cpp = self.scratch / "handoff.cpp"
        binary = self.scratch / "handoff"
        cpp.write_text(harness)
        build = subprocess.run([compiler, "-std=c++20", str(cpp), "-o", str(binary)],
                               capture_output=True, text=True, timeout=30)
        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
