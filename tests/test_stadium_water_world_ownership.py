"""Actual World/Session receipt routing reducer; no source graph parity claim."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get('MELEE_WEB_CANDIDATE_SOURCE_ROOT',ROOT))
REFERENCE=Path(os.environ.get('MELEE_WEB_REFERENCE_SOURCE_ROOT',ROOT))
OWNER=Path(os.environ.get('MELEE_WEB_FIRST_TRANSFORM_OWNER_ROOT',SOURCE))
sys.path.insert(0,str(REFERENCE/'tests'))
from owned_test_workspace import OwnedWorkspaceTests

def body(source,signature):
    a=source.index(signature);o=source.index('{',a);n=1;b=o+1
    while n:n+=(source[b]=='{')-(source[b]=='}');b+=1
    return source[a:b]

class StadiumWaterWorldOwnershipTests(OwnedWorkspaceTests):
    def test_actual_session_water_query_is_read_only_and_water_scoped(self):
        cc=shutil.which('clang++') or shutil.which('c++')
        if not cc:self.skipTest('Native C++ compiler required')
        scratch=self.new_workspace(REFERENCE,'stadium-water-query-session-')
        header=(SOURCE/'src/gameplay_match_session.hpp').read_text()
        session=(SOURCE/'src/gameplay_match_session.cpp').read_text()
        probe_start=header.index('struct GameplayStadiumWaterCollisionProbe {')
        probe_open=header.index('{',probe_start);depth=1;probe_end=probe_open+1
        while depth:
            depth+=(header[probe_end]=='{')-(header[probe_end]=='}');probe_end+=1
        probe_type=header[probe_start:probe_end]+';'
        implementation=body(session,'bool GameplayMatchSession::diagnostic_stadium_water_collision_probe(')
        collision_source=(REFERENCE/'.deps/melee/src/melee/mp/forward.h').read_text()
        enum_start=collision_source.index('typedef enum CollLineKind {')
        enum_end=collision_source.index('} CollLineKind;',enum_start)+len('} CollLineKind;')
        collision_declarations=collision_source[enum_start:enum_end]+'\n'
        for flag in ('LINE_FLAG_ENABLED','LINE_FLAG_EMPTY'):
            declarations=[line for line in collision_source.splitlines() if line.startswith('#define '+flag+' ')]
            self.assertEqual(len(declarations),1)
            collision_declarations+=declarations[0]+'\n'
        outcome_source=(REFERENCE/'.deps/melee/src/melee/gm/forward.h').read_text()
        outcome_start=outcome_source.index('typedef enum MatchOutcome {')
        outcome_end=outcome_source.index('} MatchOutcome;',outcome_start)+len('} MatchOutcome;')
        collision_declarations+=outcome_source[outcome_start:outcome_end]+'\n'
        code=r"""
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
#include "gameplay_collision.h"
#include "gameplay_stadium_display_owner.h"
#include "gameplay_match_flow.h"
"""+collision_declarations+r"""
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
struct MeleeWebMatchFlow {uint64_t generation=7;uint32_t frame=12;int paused=0,complete=0;};
static MeleeWebMatchFlow* mutated_flow=nullptr;static uint32_t* mutated_seed=nullptr;
struct FakeCollision {MeleeWebCollisionReadiness readiness{};MeleeWebCollisionLineResult lines[4]{};int fail_readiness=0,wrong_right_miss=0,mutate_readiness=0,mutate_clock=0,mutate_seed=0;unsigned line_calls=0,floor_calls=0;};
struct MeleeWebCollision {FakeCollision fake;};
extern "C" int melee_web_match_flow_pause_observation(const MeleeWebMatchFlow* f,MeleeWebMatchPauseObservation* out){if(!f||!out)return 0;*out={f->generation,f->frame,0,f->paused,OUTCOME_NONE,f->complete};return 1;}
extern "C" uint32_t melee_web_match_flow_frames(const MeleeWebMatchFlow* f){return f?f->frame:0;}
extern "C" int melee_web_collision_readiness(MeleeWebCollision* c,MeleeWebCollisionReadiness* out,char* e,size_t n){if(!c||!out||c->fake.fail_readiness)return 0;*out=c->fake.readiness;if(e&&n)*e='\0';return 1;}
extern "C" int melee_web_collision_line(MeleeWebCollision* c,int32_t i,MeleeWebCollisionLineResult* out,char* e,size_t n){if(!c||!out||i<0||i>=4)return 0;++c->fake.line_calls;*out=c->fake.lines[i];if(e&&n)*e='\0';return 1;}
extern "C" int melee_web_collision_floor(MeleeWebCollision* c,int32_t start,float x,float y,MeleeWebCollisionFloorResult* out,char* e,size_t n){if(!c||!out||start!=2)return 0;++c->fake.floor_calls;if(c->fake.floor_calls==1){if(c->fake.mutate_readiness)++c->fake.readiness.lines;if(c->fake.mutate_clock&&mutated_flow)++mutated_flow->frame;if(c->fake.mutate_seed&&mutated_seed)++*mutated_seed;}*out={};if(x < -1.0F || x > 2.0F)out->line=c->fake.wrong_right_miss&&x>2.0F?3:-1;else out->line=2;out->displacement_y=0.0001F;out->normal[1]=1.0F;(void)y;if(e&&n)*e='\0';return 1;}
namespace melee_web {
"""+probe_type+r"""
struct GameplayWorld {MeleeWebCollision* owner=nullptr;MeleeWebStadiumFirstTransformJournal journal{};bool journal_ok=true;MeleeWebCollision* collision()const{return owner;}bool diagnostic_stadium_first_transform_snapshot(MeleeWebStadiumFirstTransformJournal* out,char* e,size_t n)const{if(!journal_ok||!out)return 0;*out=journal;if(e&&n)*e='\0';return 1;}};
struct GameplayMatchSession {struct Storage;Storage* storage_=nullptr;bool construction_complete()const;bool paused()const;bool ending()const;bool complete()const;uint32_t random_seed()const;bool diagnostic_stadium_water_collision_probe(uint64_t,GameplayStadiumWaterCollisionProbe*,char*,size_t)const;};
struct GameplayMatchSession::Storage {bool stadium_diagnostic=true,first_water_diagnostic=true,first_fire_diagnostic=false,constructed=true,paused=false,ending=false,complete=false;uint32_t seed=1234;GameplayWorld* world=nullptr;MeleeWebMatchFlow* flow=nullptr;void* match=reinterpret_cast<void*>(1);};
bool GameplayMatchSession::construction_complete()const{return storage_&&storage_->constructed;}
bool GameplayMatchSession::paused()const{return storage_&&storage_->paused;}
bool GameplayMatchSession::ending()const{return storage_&&storage_->ending;}
bool GameplayMatchSession::complete()const{return storage_&&storage_->complete;}
uint32_t GameplayMatchSession::random_seed()const{return storage_?storage_->seed:0;}
"""+implementation+r"""
} // namespace melee_web
int main(){
 char error[256]{};MeleeWebCollision collision{};MeleeWebMatchFlow flow{};melee_web::GameplayWorld world;melee_web::GameplayMatchSession session;melee_web::GameplayMatchSession::Storage storage{};
 collision.fake.readiness={5,4,3,1,0,0,1,1,1,1};mutated_flow=&flow;
 auto line=[](float x0,float x1,int joint,int previous,int next){MeleeWebCollisionLineResult r{};r.v0[0]=x0;r.v1[0]=x1;r.v0[1]=r.v1[1]=0;r.normal[1]=1;r.kind=CollLine_Floor;r.runtime_flags=LINE_FLAG_ENABLED|CollLine_Floor;r.previous=previous;r.next=next;r.joint=joint;r.has_normal=1;return r;};
 collision.fake.lines[0]=line(-2,-1,1,-1,-1);collision.fake.lines[0].kind=CollLine_Ceiling;
 collision.fake.lines[1]=line(-1,0,1,-1,2);collision.fake.lines[2]=line(0,1,0,1,3);collision.fake.lines[3]=line(1,2,1,2,-1);
 world.owner=&collision;world.journal.armed=1;world.journal.selected_form=9;world.journal.phase=0;world.journal.form=9;world.journal.pending=0;world.journal.count=5;world.journal.buffer_uses=2;world.journal.scalar_sample_valid=1;world.journal.scalar_world_ticks=50;world.journal.world_generation=7;
 storage.world=&world;storage.flow=&flow;session.storage_=&storage;mutated_seed=&storage.seed;
 melee_web::GameplayStadiumWaterCollisionProbe out{};assert(session.diagnostic_stadium_water_collision_probe(49,&out,error,sizeof(error)));
 assert(out.world_generation==7&&out.source_tick==49&&out.line_index==2&&out.chain_min_x==-1&&out.chain_max_x==2);
 assert(out.hit.line==2&&out.left_miss.line==-1&&out.right_miss.line==-1&&collision.fake.floor_calls==3);
 auto refuses=[&](){melee_web::GameplayStadiumWaterCollisionProbe sentinel{};sentinel.world_generation=99;assert(!session.diagnostic_stadium_water_collision_probe(49,&sentinel,error,sizeof(error)));assert(sentinel.world_generation==99);};
 storage.first_water_diagnostic=false;refuses();storage.first_water_diagnostic=true;
 storage.constructed=false;refuses();storage.constructed=true;
 storage.paused=true;refuses();storage.paused=false;
 world.journal.phase=1;refuses();world.journal.phase=0;
 world.journal.scalar_world_ticks=51;refuses();world.journal.scalar_world_ticks=50;
 collision.fake.readiness.stage_joint_bindings_ready=0;refuses();collision.fake.readiness.stage_joint_bindings_ready=1;
 world.journal.armed=2;refuses();world.journal.armed=1;
 collision.fake.lines[2].joint=1;refuses();collision.fake.lines[2].joint=0;
 collision.fake.wrong_right_miss=1;refuses();collision.fake.wrong_right_miss=0;
 collision.fake.floor_calls=0;collision.fake.mutate_readiness=1;refuses();collision.fake.mutate_readiness=0;collision.fake.readiness.lines=4;
 collision.fake.floor_calls=0;collision.fake.mutate_clock=1;refuses();collision.fake.mutate_clock=0;flow.frame=12;
 collision.fake.floor_calls=0;collision.fake.mutate_seed=1;refuses();collision.fake.mutate_seed=0;storage.seed=1234;
 assert(collision.fake.line_calls>0&&collision.fake.floor_calls==3);
 return 0;
}
"""
        src,binary=scratch/'control.cpp',scratch/'control';src.write_text(code)
        command=[cc,'-std=c++20','-Wall','-Wextra','-Werror','-I',str(OWNER/'src'),'-I',str(SOURCE/'src'),'-I',str(SOURCE/'.deps/melee/src'),'-I',str(REFERENCE/'src'),'-I',str(REFERENCE/'.deps/melee/src'),str(src),'-o',str(binary)]
        (scratch/'command.json').write_text(json.dumps(command))
        result=subprocess.run(command,capture_output=True,text=True,timeout=30)
        (scratch/'compile.stdout').write_text(result.stdout);(scratch/'compile.stderr').write_text(result.stderr)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
        (scratch/'run.stdout').write_text(result.stdout);(scratch/'run.stderr').write_text(result.stderr)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_actual_live_and_retained_form_routes_do_not_cross_accept(self):
        cc=shutil.which('clang++') or shutil.which('c++')
        if not cc:self.skipTest('Native C++ compiler required')
        scratch=self.new_workspace(REFERENCE,'stadium-water-world-ownership-')
        world=(SOURCE/'src/gameplay_world.cpp').read_text()
        session=(SOURCE/'src/gameplay_match_session.cpp').read_text()
        selector=body(world,'    unsigned first_transform_selected_form()const')
        world_snapshot=body(world,'bool GameplayWorld::diagnostic_stadium_first_transform_snapshot(')
        session_snapshot=body(session,'bool GameplayMatchSession::diagnostic_stadium_first_transform_snapshot(')
        code=r"""
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
#include "gameplay_stadium_display_owner.h"
#include <cassert>
#include <cstdio>
#include <cstddef>
// Fail-closed native leaves. This fixture never claims an actual StageLast graph.
static int live_calls;
static int melee_web_stage_last_stadium_water_graph_status(const void*,MeleeWebStadiumFirstTransformJournal*,char*,size_t){++live_calls;return 0;}
static int melee_web_stage_last_stadium_first_transform_status(const void*,MeleeWebStadiumFirstTransformJournal*,char*,size_t){++live_calls;return 0;}
struct GameplayWorld {
 struct Storage {bool stadium_first_transform_diagnostic=false,stadium_first_water_diagnostic=false;
 void* stage_last=nullptr;MeleeWebStadiumFirstTransformJournal stadium_first_transform_terminal{};
"""+selector+r"""
 };Storage* storage_=nullptr;
 bool diagnostic_stadium_first_transform_snapshot(MeleeWebStadiumFirstTransformJournal*,char*,size_t)const;
};
"""+world_snapshot+r"""
struct GameplayMatchSession {
 struct Storage {bool first_fire_diagnostic=false,first_water_diagnostic=false;GameplayWorld* world=nullptr;};
 Storage* storage_=nullptr;unsigned first_transform_selected_form_=0;
 MeleeWebStadiumFirstTransformJournal first_transform_retained_{};
 bool diagnostic_stadium_first_transform_snapshot(MeleeWebStadiumFirstTransformJournal*,char*,size_t)const;
};
"""+session_snapshot+r"""
int main(){
 char error[128]{};MeleeWebStadiumFirstTransformJournal out{},j{};
 j.armed=2;j.count=11;j.selected_form=9;j.world_generation=7;
 GameplayWorld::Storage ws;ws.stadium_first_water_diagnostic=true;ws.stadium_first_transform_terminal=j;
 GameplayWorld w;w.storage_=&ws;
 assert(w.diagnostic_stadium_first_transform_snapshot(&out,error,sizeof(error))&&out.selected_form==9&&out.count==11);
 ws.stadium_first_transform_diagnostic=true;assert(!w.diagnostic_stadium_first_transform_snapshot(&out,error,sizeof(error)));
 ws.stadium_first_water_diagnostic=false;assert(!w.diagnostic_stadium_first_transform_snapshot(&out,error,sizeof(error)));
 ws.stadium_first_transform_terminal.selected_form=3;ws.stadium_first_transform_terminal.count=5;
 assert(w.diagnostic_stadium_first_transform_snapshot(&out,error,sizeof(error))&&out.selected_form==3);
 ws.stadium_first_transform_terminal.count=11;assert(!w.diagnostic_stadium_first_transform_snapshot(&out,error,sizeof(error)));
 ws.stadium_first_transform_diagnostic=false;ws.stadium_first_water_diagnostic=true;ws.stadium_first_transform_terminal=j;
 for(unsigned k=0;k<4;k++){ws.stadium_first_transform_terminal=j;
  if(k==0)ws.stadium_first_transform_terminal.failed=1;
  if(k==1)ws.stadium_first_transform_terminal.armed=1;
  if(k==2)ws.stadium_first_transform_terminal.count=5;
  if(k==3)ws.stadium_first_transform_terminal.world_generation=0;
  assert(!w.diagnostic_stadium_first_transform_snapshot(&out,error,sizeof(error)));}
 ws.stadium_first_transform_terminal=j;ws.stage_last=&ws;
 assert(!w.diagnostic_stadium_first_transform_snapshot(&out,error,sizeof(error))&&live_calls==1);ws.stage_last=nullptr;
 GameplayMatchSession s;s.first_transform_selected_form_=9;s.first_transform_retained_=j;
 assert(s.diagnostic_stadium_first_transform_snapshot(&out,error,sizeof(error))&&out.selected_form==9);
 s.first_transform_retained_.selected_form=3;s.first_transform_retained_.count=5;
 assert(!s.diagnostic_stadium_first_transform_snapshot(&out,error,sizeof(error)));
 s.first_transform_selected_form_=3;assert(s.diagnostic_stadium_first_transform_snapshot(&out,error,sizeof(error)));
 s.first_transform_retained_.count=11;assert(!s.diagnostic_stadium_first_transform_snapshot(&out,error,sizeof(error)));
 s.first_transform_selected_form_=0;s.first_transform_retained_.selected_form=0;s.first_transform_retained_.count=0;
 assert(!s.diagnostic_stadium_first_transform_snapshot(&out,error,sizeof(error)));
 GameplayMatchSession::Storage ss;ss.first_water_diagnostic=true;ss.world=&w;s.storage_=&ss;
 assert(s.diagnostic_stadium_first_transform_snapshot(&out,error,sizeof(error))&&out.selected_form==9);
 return 0;
}
"""
        src,binary=scratch/'control.cpp',scratch/'control';src.write_text(code)
        commands=[('compile',[cc,'-std=c++20','-Wall','-Wextra','-Werror','-I',str(OWNER/'src'),'-I',str(SOURCE/'src'),'-I',str(REFERENCE/'src'),str(src),'-o',str(binary)]),('run',[str(binary)])]
        (scratch/'commands.json').write_text(json.dumps(commands))
        for name,argv in commands:
            r=subprocess.run(argv,capture_output=True,text=True,timeout=30)
            (scratch/(name+'.stdout')).write_text(r.stdout);(scratch/(name+'.stderr')).write_text(r.stderr)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_actual_route_and_destructive_order_keep_existing_contract(self):
        w=(SOURCE/'src/gameplay_world.cpp').read_text();s=(SOURCE/'src/gameplay_match_session.cpp').read_text()
        self.assertIn('StadiumFirstWaterReady',(SOURCE/'src/gameplay_match_session.hpp').read_text())
        self.assertIn('stadium_first_water_diagnostic?"GrPs3.dat":"GrPs1.dat"',w)
        self.assertIn('melee_web_stadium_water_graph_callback_matches',w)
        self.assertIn('stadium_first_transform_diagnostic&&stadium_first_water_diagnostic',w)
        stage=body(w,'    void end_stage()')
        self.assertLess(stage.index('first_fire_completion_can_end'),stage.index('melee_web_stage_numeric_clear_quakes'))
        self.assertLess(stage.index('melee_web_stage_last_stadium_water_graph_can_end'),stage.index('melee_web_stage_numeric_clear_quakes'))
        close=body(s,'    void close()')
        self.assertLess(close.index('diagnostic_stadium_first_transform_can_end'),close.index('end_flow()'))
        self.assertLess(close.index('diagnostic_stadium_first_transform_snapshot'),close.index('world->close('))
        self.assertIn('first_transform_retired.selected_form==(first_water_diagnostic?9U:3U)',close)
        for name in ['void GameplayMatchSession::close()','void GameplayMatchSession::finish_vs(']:
            f=body(s,name);self.assertLess(f.index('first_transform_retained_='),f.index('storage_.reset()'))

if __name__=='__main__':unittest.main()
