"""Asset-free actual VS finish and browser continuation owner controls."""
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests
from test_gameplay_sd_handoff_order import function


class BrowserSuddenDeathContinuationTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, "browser-sd-continuation-")

    def compile_run(self, name, source):
        compiler = shutil.which("clang++")
        if not compiler:
            self.skipTest("Native clang++ unavailable")
        cpp = self.scratch / (name + ".cpp")
        binary = self.scratch / name
        cpp.write_text(source)
        built = subprocess.run([compiler, "-std=c++20", str(cpp), "-o", str(binary)],
                               capture_output=True, text=True, timeout=30)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_actual_finish_vs_publishes_before_capture_and_rejects_other_owners(self):
        source = (ROOT / "src/gameplay_match_session.cpp").read_text()
        finish = function(source, "void GameplayMatchSession::finish_vs(")
        end_flow = function(source, "    void end_flow()")
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
constexpr unsigned MELEE_WEB_PAD_STATE_BYTES=822;
static std::vector<std::string> events;
static uint32_t host_seed=777,*seed_ptr=&host_seed;
static uint8_t live_pad=0;static bool published=false,publication_ok=true;
static void check(bool v,const char* e){if(!v)throw std::runtime_error(e);}
static int melee_web_match_flow_end(int*,char*,size_t){events.push_back("end");*seed_ptr+=2;return 1;}
static int melee_web_match_rules_publish_result(){
 if(!publication_ok){events.push_back("publish_failed");return 0;}
 if(published){events.push_back("publish_idempotent");return 1;}
 published=true;events.push_back("publish");*seed_ptr+=100;live_pad=42;return 1;
}
struct MeleeWebMatchStats{uint32_t random_seed;};
static int melee_web_match_stats(int*,MeleeWebMatchStats* s,char*,size_t){
 events.push_back("capture_seed");s->random_seed=*seed_ptr;return 1;
}
static void melee_web_pad_state_capture(uint8_t* p){events.push_back("capture_pad");std::memset(p,live_pad,822);}
struct Storage{
 struct{bool opening_demo=false,sudden_death=false;}selected;
 bool sudden_death_claimed=false;int value=1,*match=&value,*flow=&value;uint32_t seed=17;
 Storage(){seed_ptr=&seed;live_pad=1;}
''' + end_flow + r'''
 void close(){end_flow();melee_web_match_rules_publish_result();events.push_back("close");seed_ptr=&host_seed;}
 ~Storage(){seed_ptr=&host_seed;}
};
class GameplayMatchSession{
public:
 std::unique_ptr<Storage> storage_=std::make_unique<Storage>();bool completed=true;
 bool complete()const{return completed;}
 void finish_vs(uint32_t&,uint8_t*);
};
''' + finish + r'''
int main(){
 {GameplayMatchSession s;uint32_t seed=0;uint8_t pad[822]{};s.finish_vs(seed,pad);
 assert(seed==119&&pad[0]==42&&pad[821]==42&&!s.storage_&&seed_ptr==&host_seed);
 assert((events==std::vector<std::string>{"end","publish","capture_seed","capture_pad","publish_idempotent","close"}));}
 for(unsigned bad=0;bad<6;++bad){events.clear();published=false;GameplayMatchSession s;
 if(bad==0)s.completed=false;if(bad==1)s.storage_->selected.opening_demo=true;
 if(bad==2)s.storage_->selected.sudden_death=true;if(bad==3)s.storage_->sudden_death_claimed=true;
 if(bad==4)s.storage_->match=nullptr;
 uint32_t seed=999;uint8_t pad[822];std::memset(pad,7,822);bool rejected=false;
 try{s.finish_vs(seed,bad==5?nullptr:pad);}catch(const std::exception&){rejected=true;}
 assert(rejected&&events.empty()&&seed==999&&pad[0]==7&&s.storage_->flow);}
 events.clear();published=false;publication_ok=false;
 {GameplayMatchSession s;uint32_t seed=999;uint8_t pad[822]{};bool rejected=false;
 try{s.finish_vs(seed,pad);}catch(const std::exception&){rejected=true;}
 assert(rejected&&seed==999&&pad[0]==0&&s.storage_&&s.storage_->flow==nullptr);
 assert((events==std::vector<std::string>{"end","publish_failed"}));}
}
'''
        self.compile_run("vs-finish", harness)

    def test_actual_browser_dispatch_retains_input_and_refuses_unsupported_timeline(self):
        source = (ROOT / "src/gameplay_menu_browser.cpp").read_text()
        methods = "\n".join(function(source, name) for name in (
            "void enter_typed_results_world()", "void enter_typed_sudden_death_world()",
            "void begin_typed_results(",
            "void begin_typed_sudden_death(", "void dispatch_vs_continuation(",
            "void abort_sudden_death_after_failure(",
            "bool advance_match_construction()"))
        harness = r'''
#define MELEE_WEB_PUBLIC_RUNTIME 1
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
constexpr unsigned MELEE_WEB_PAD_STATE_BYTES=822;
constexpr int MELEE_WEB_MENU_MATCH_CONTINUATION_RESULTS=1;
constexpr int MELEE_WEB_MENU_MATCH_CONTINUATION_SUDDEN_DEATH=2;
struct MatchExitInfo{};struct ResultsMatchInfo{int marker=0;};
struct MeleeWebMenuMatchContinuation{int kind=0;struct{ResultsMatchInfo results;}payload;};
struct MeleeWebPadState{int marker=0;};struct Host{};static Host owned_host,*host=&owned_host;
static bool fail_assets=false,network=false,fail_close=false,construct_complete=false;
static int assets=0,claims=0,closed=0,pads=0,results_constructions=0;
static int delivered_pad=0;static uint32_t delivered_seed=0;
static void check(bool v,const char* e){if(!v)throw std::runtime_error(e);}
static MeleeWebPadState* melee_web_pad_state_decode(const uint8_t* p,size_t,char*,size_t){++pads;return new MeleeWebPadState{p[0]};}
static void melee_web_pad_state_free(MeleeWebPadState* p){if(p){--pads;delete p;}}
static MeleeWebPadState host_pad{42};
static const MeleeWebPadState* melee_web_menu_host_input(Host*){return &host_pad;}
static bool melee_web_net_active(){return network;}
static uint32_t host_seed=0,*seed_ptr=&host_seed;static int route_kind=1,route_calls=0;
static int melee_web_menu_host_match_continuation_begin(Host*,const MatchExitInfo*,uint32_t seed,
 MeleeWebMenuMatchContinuation* out,char*,size_t){++route_calls;host_seed=seed+11;
 out->kind=route_kind;out->payload.results.marker=123;return 1;}
struct AuroraStats{};static AuroraStats aurora_stats_snapshot(){return {};}
static double emscripten_get_now(){return 0;}
static void report_construction(const char*,double,double,double,AuroraStats,AuroraStats){}
struct Clock{void reset(){}};static Clock menu_clock,audio_clock;
namespace melee_web{
struct RuntimeFiles{};struct RuntimeArchiveCache{};
enum class GameplayMatchConstruction{Deferred};
struct GameplayMatchSession{
 const MeleeWebPadState* retained=nullptr;
 GameplayMatchSession(RuntimeFiles&,Host*,const MeleeWebMenuMatchContinuation&,
 RuntimeArchiveCache&,GameplayMatchConstruction,const MeleeWebPadState& p){++claims;retained=&p;}
 bool advance_construction(){assert(retained&&retained->marker==19&&pads==1);return construct_complete;}
 bool sudden_death(){return true;}
 const int& start_data(){static int i=0;return i;}
};
struct GameplayResultsSession{
 GameplayResultsSession(RuntimeFiles&,ResultsMatchInfo r,uint32_t seed,const MeleeWebPadState& p){
 assert(r.marker==123);++results_constructions;delivered_pad=p.marker;delivered_seed=seed;}
};
template<class T> static void retail_replay_initial(const T&,bool,int){}
}
static melee_web::RuntimeFiles files;static std::unique_ptr<melee_web::RuntimeArchiveCache> archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>();
static std::unique_ptr<melee_web::GameplayMatchSession> match;
static std::unique_ptr<melee_web::GameplayResultsSession> results;
static ResultsMatchInfo results_info;static uint32_t results_seed=0;
static std::unique_ptr<MeleeWebPadState,decltype(&melee_web_pad_state_free)> results_input{nullptr,melee_web_pad_state_free},sudden_death_input{nullptr,melee_web_pad_state_free};
static const MeleeWebPadState* results_borrowed_input=nullptr;
static bool sudden_death_route_active=false,results_route_active=false,scoped_assets=true;
static MeleeWebMenuMatchContinuation pending_match_continuation{};
static bool running=false,pending=false,first_use_draw_pending=false,replay_trace=false;
static int audio_phase=0,completed_matches=0;static std::string message,match_message;
struct Replay{};static std::unique_ptr<Replay> replay;
static std::unique_ptr<int> dummy;enum class AssetDestination{Results,SuddenDeath};
static void request_assets(AssetDestination){++assets;if(fail_assets)throw std::runtime_error("primary asset error");}
static void close(){++closed;if(fail_close)throw std::runtime_error("secondary cleanup error");
 match.reset();sudden_death_input.reset();pending_match_continuation={};sudden_death_route_active=false;
 results.reset();results_input.reset();results_borrowed_input=nullptr;}
''' + methods + r'''
int main(){
 MeleeWebMenuMatchContinuation sd{};sd.kind=2;uint8_t input[822]{};input[0]=19;
 for(int unsupported=0;unsupported<2;++unsupported){
 network=unsupported==1;if(unsupported==0)replay=std::make_unique<Replay>();
 bool rejected=false;try{begin_typed_sudden_death(sd,input);}catch(const std::exception&){rejected=true;}
 assert(rejected&&assets==0&&claims==0&&pads==0&&sudden_death_route_active);
 abort_sudden_death_after_failure("unsupported timeline");assert(!sudden_death_route_active);
 replay.reset();network=false;}
 fail_assets=true;std::string primary;
 try{begin_typed_sudden_death(sd,input);}catch(const std::exception& e){primary=e.what();}
 assert(primary=="primary asset error"&&pads==1&&claims==0);
 fail_close=true;abort_sudden_death_after_failure(primary);
 assert(primary=="primary asset error"&&sudden_death_route_active&&pads==1);
 fail_close=false;abort_sudden_death_after_failure(primary);assert(pads==0);fail_assets=false;
 scoped_assets=false;begin_typed_sudden_death(sd,input);assert(claims==1&&pads==1&&completed_matches==0);
 assert(!advance_match_construction()&&pads==1);
 construct_complete=true;assert(advance_match_construction()&&pads==0&&match&&sudden_death_route_active);
 close();
 MeleeWebMenuMatchContinuation typed{};typed.kind=1;typed.payload.results.marker=123;
 MatchExitInfo terminal{};scoped_assets=true;sudden_death_route_active=true;
 begin_typed_results(typed,terminal,654,nullptr);
 assert(results_borrowed_input==&host_pad&&completed_matches==1&&sudden_death_route_active);
 enter_typed_results_world();assert(delivered_pad==42&&delivered_seed==654&&results_constructions==1&&!sudden_death_route_active);
 close();scoped_assets=false;
 dispatch_vs_continuation(terminal,321,input);
 assert(delivered_pad==19&&delivered_seed==332&&completed_matches==2&&pads==0&&route_calls==1);
 close();route_kind=2;scoped_assets=true;
 dispatch_vs_continuation(terminal,1000,input);
 assert(route_calls==2&&claims==1&&pads==1&&sudden_death_route_active&&completed_matches==2);
 close();
}
'''
        self.compile_run("browser-dispatch", harness)
