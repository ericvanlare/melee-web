#include "gameplay_compat.h"
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <melee/gr/granime.h>
#include <sysdolphin/baselib/aobj.h>
#include <stdio.h>
#include <stdlib.h>
int melee_web_test_stage_last_state(unsigned* objects,unsigned* state){
 *objects=0;*state=0;
 for(unsigned i=0;i<sizeof(stage_info.map_gobjs)/sizeof(stage_info.map_gobjs[0]);i++)if(stage_info.map_gobjs[i])++*objects;
 if(!stage_info.map_gobjs[3])return 0;
 Ground* ground=stage_info.map_gobjs[3]->user_data;
 if(!ground||*objects<4||*objects>10)return 0;
 *state=ground->u.map.xC4_b2_25;return *state>=1&&*state<=18;
}

static void first_animation(HSD_AObj* aobj,void* out){if(!*(HSD_AObj**)out)*(HSD_AObj**)out=aobj;}
void melee_web_test_stage_last_animation(void){
 for(unsigned i=4;i<=8;i++){
  HSD_GObj* gobj=stage_info.map_gobjs[i];
  HSD_AObj* aobj=gobj?grAnime_801C8318(gobj,1,7):NULL;
  HSD_AObj* direct=NULL;
  if(gobj)HSD_ForeachAnim(Ground_801C3FA4(gobj,1),JOBJ_TYPE,0x220|0x7484|0x100,first_animation,AOBJ_ARG_AV,&direct);
  if(aobj!=direct){fprintf(stderr,"Original FD first-animation lookup differs from traversal\n");abort();}
  if(!aobj)aobj=direct;
  if(aobj)printf("FD animation map=%u flags=%08x frame=%g end=%g speed=%g\n",i,aobj->flags,aobj->curr_frame,aobj->end_frame,aobj->framerate);
  else printf("FD animation map=%u absent\n",i);
 }
}
