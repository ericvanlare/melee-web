"""Compile actual MatchStats declarations and production stats function."""
from pathlib import Path
import shutil
import subprocess
import unittest
from owned_test_workspace import OwnedWorkspaceTests
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#define MELEE_WEB_MATCH_MAX_PLAYERS 4u
#define MELEE_WEB_MATCH_KO_SLOTS 6u
enum { Gm_Player_NumMax=6 };
'''
STUBS=r'''
typedef struct {float x,y;} V2;
typedef struct {float x,y,z;} V3;
typedef struct {int kind,motion_id,ground_or_air;V3 cur_pos;float facing_dir,cur_anim_frame;struct{int count;}x203C;struct{float x1830_percent;}dmg;float shield_health;struct{V2 lstick[1];float triggers[1];uint32_t held_buttons[1],pressed_buttons,released_buttons;}input;}Fighter;
typedef struct {Fighter*user_data;}HSD_GObj;
typedef struct {uint8_t transformed[2];HSD_GObj*player_entity[2];}StaticPlayer;
typedef struct CmSubject{struct CmSubject*next;}CmSubject;
static CmSubject*cm_804D6460;
typedef struct {uint32_t player_count,slots[4],controllers[4],seed,camera_count;uint64_t ticks;}MeleeWebMatchContext;
static StaticPlayer table[6];static int live_ok=1,getter_calls=0;static int32_t falls[6],kos[6][6];static uint32_t suicides[6];
static int fail(char*e,size_t n,const char*m){snprintf(e,n,"%s",m);return 0;}
static int ok(char*e,size_t n){if(n)*e=0;return 1;}
static int live(MeleeWebMatchContext*h,char*e,size_t n){return h&&live_ok?1:fail(e,n,"not live");}
static StaticPlayer*Player_GetPtrForSlot(uint32_t s){assert(s<6);return &table[s];}
static int32_t Player_GetFallsByIndex(uint32_t s,int i){assert(s<6&&i==0&&table[s].transformed[0]<2);getter_calls++;return falls[s];}
static uint32_t Player_GetSuicideCount(uint32_t s){assert(s<6);getter_calls++;return suicides[s];}
static int32_t Player_GetKOsByPlayerIndex(uint32_t s,uint32_t v){assert(s<6&&v<6);getter_calls++;return kos[s][v];}
static int Player_GetStocks(uint32_t s){assert(s<6);return 3;}
static HSD_GObj*Player_GetEntity(uint32_t s){assert(s<6&&table[s].transformed[0]<2);return table[s].player_entity[table[s].transformed[0]];}
static int eye_stats(Fighter*f,MeleeWebMatchStats*out,char*e,size_t n){(void)f;(void)out;return ok(e,n);}
'''
MAIN=r'''
int main(void){
 MeleeWebMatchContext h={.player_count=2,.slots={0,3},.controllers={2,1},.seed=123,.camera_count=2,.ticks=45};char err[256];MeleeWebMatchStats out;
 falls[3]=7;suicides[3]=2;kos[3][0]=8;kos[3][5]=11;
 MeleeWebMatchContext saved_h=h;StaticPlayer saved_table[6];memcpy(saved_table,table,sizeof table);
 assert(melee_web_match_player_stats(&h,1,&out,err,sizeof err));
 assert(memcmp(&saved_h,&h,sizeof h)==0&&memcmp(saved_table,table,sizeof table)==0);
 assert(out.player_slot==3&&out.primary_falls==7&&out.suicides==2&&out.kos_by_player_slot[0]==8&&out.kos_by_player_slot[5]==11);
 assert(out.live_fighters==0&&out.motion_id==-1&&out.stocks==3&&out.ticks==45&&out.random_seed==123);
 Fighter f={.kind=0,.motion_id=181,.ground_or_air=0,.facing_dir=-1,.cur_anim_frame=3.25f,.shield_health=42.5f};HSD_GObj g={&f};table[3].transformed[0]=1;table[3].player_entity[1]=&g;
 assert(melee_web_match_player_stats(&h,1,&out,err,sizeof err));assert(out.primary_falls==7&&out.shield_health==42.5f&&out.facing_direction==-1&&out.motion_id==181);
 int previous=getter_calls;table[3].transformed[0]=2;assert(!melee_web_match_player_stats(&h,1,&out,err,sizeof err));assert(getter_calls==previous);table[3].transformed[0]=1;
 h.slots[1]=6;assert(!melee_web_match_player_stats(&h,1,&out,err,sizeof err));assert(getter_calls==previous);h.slots[1]=3;
 h.controllers[1]=4;assert(!melee_web_match_player_stats(&h,1,&out,err,sizeof err));h.controllers[1]=1;
 h.player_count=5;assert(!melee_web_match_player_stats(&h,1,&out,err,sizeof err));h.player_count=2;
 assert(!melee_web_match_player_stats(&h,2,&out,err,sizeof err));assert(!melee_web_match_player_stats(&h,0,NULL,err,sizeof err));
 live_ok=0;assert(!melee_web_match_player_stats(&h,1,&out,err,sizeof err));live_ok=1;
 kos[3][5]=-1;assert(!melee_web_match_player_stats(&h,1,&out,err,sizeof err));kos[3][5]=0;
 falls[3]=-1;assert(!melee_web_match_player_stats(&h,1,&out,err,sizeof err));falls[3]=0;
 memset(kos,0,sizeof kos);suicides[3]=0;assert(melee_web_match_player_stats(&h,1,&out,err,sizeof err));assert(out.primary_falls==0&&out.suicides==0&&out.kos_by_player_slot[0]==0);
 puts("actual production stats: sparse slot/controller, no entity, transformed primary, counters, bounds and failures PASS");
}
'''
class GameplayMatchPersistentStatsTests(OwnedWorkspaceTests):
 def test_actual_production_stats(self):
  cc=shutil.which('clang') or shutil.which('cc')
  if not cc:self.skipTest('C11 compiler required')
  header=(ROOT/'src/gameplay_match_context.h').read_text();start=header.index('typedef struct MeleeWebMatchEyeStats');end=header.index('} MeleeWebMatchStats;',start)+len('} MeleeWebMatchStats;');decl=header[start:end]
  source=(ROOT/'src/gameplay_match_context.c').read_text();start=source.index('int melee_web_match_player_stats(');end=source.index('int melee_web_match_end(',start);production=source[start:end]
  w=self.new_workspace(ROOT,'persistent-match-stats-');c=w/'fixture.c';b=w/'fixture';c.write_text(FIXTURE+decl+STUBS+production+MAIN)
  r=subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(b)],capture_output=True,text=True,timeout=30);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
  r=subprocess.run([str(b)],capture_output=True,text=True,timeout=10);self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertIn('actual production stats:',r.stdout)
if __name__=='__main__':unittest.main()
