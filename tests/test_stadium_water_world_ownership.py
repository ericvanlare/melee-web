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
