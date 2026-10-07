#include "gameplay_compat.h"
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/fog.h>
#include <math.h>
int melee_web_test_native_stage_map(void* pointer,void* yaku){
 UnkStageDat* map=pointer;u32** programs=yaku;
 if(!map||map->unkC!=10||map->unk4!=1||map->unk14!=2||map->unk24!=3||map->unk2C!=1||map->unk18!=NULL||map->unk1C!=32||!programs||map->unk8[0].x28)return 0;
 for(int i=0;i<10;i++){
  struct UnkStageDat_x8_t* entry=&map->unk8[i];
  if(!entry->unk0||!entry->x10||!entry->x18||!entry->x18[0]->desc)return 0;
  if(i&&(!entry->unk4||!entry->unk8||!entry->x28))return 0;
  HSD_JObj* object=HSD_JObjLoadJoint(entry->unk0);if(!object)return 0;
  HSD_JObjRemoveAll(object);
 }
 unsigned moved=0;
 for(LightList** cursor=map->unk8[3].x18;*cursor;cursor++){
  LightList* entry=*cursor;HSD_LObj* light=HSD_LObjLoadDesc(entry->desc);if(!light)return 0;
  if(entry->anims&&entry->anims[0]){
   Vec3 start={0},end={0};HSD_LObjAddAnimAll(light,entry->anims[0]);HSD_LObjReqAnimAll(light,0);HSD_LObjAnimAll(light);
   int has_start=HSD_LObjGetPosition(light,&start);HSD_LObjReqAnimAll(light,300);HSD_LObjAnimAll(light);
   int has_end=HSD_LObjGetPosition(light,&end);
   if(has_start&&has_end){if(!isfinite(end.x)||!isfinite(end.y)||!isfinite(end.z))return 0;if(start.x!=end.x||start.y!=end.y||start.z!=end.z)++moved;}
  }
  HSD_LObjRemoveAll(light);
 }
 if(moved!=2)return 0;
 for(int i=0;i<4;i++){
  if(!programs[i]||(programs[i][0]>>26)!=18||(programs[i][2]>>26)!=19)return 0;
  GXColor* first=(GXColor*)&programs[i][1];GXColor* second=(GXColor*)&programs[i][3];
  if(i==0&&(first->r||first->a||second->r!=200||second->a!=255))return 0;
 }
 return 1;
}

int melee_web_test_native_stadium_map(void* pointer){
 UnkStageDat* map=pointer;
 static const u8 resident[10]={1,1,1,0,0,1,0,0,0,0};
 if(!map||map->unkC!=10||map->unk4!=1||map->unk14!=0||map->unk24!=0||
    map->unk2C!=44||map->unk18!=NULL||map->unk1C!=48||!map->unk28)return 0;
 for(int i=0;i<10;i++){
  struct UnkStageDat_x8_t* entry=&map->unk8[i];
  if((entry->unk0!=NULL)!=resident[i])return 0;
  if(resident[i]){
   if(!entry->x10||!entry->x18||!entry->x18[0]||!entry->x18[0]->desc)return 0;
   if(i&&(!entry->unk4||!entry->unk8))return 0;
  }else{
   if(entry->x10||entry->x18||entry->x1C)return 0;
   if(i!=9&&(entry->unk24||entry->unk20))return 0;
  }
  if(i==0){if(entry->x28)return 0;}
  else if(!entry->x28)return 0;
  if(i==9&&(entry->unk24!=1||!entry->unk20))return 0;
 }
 return 1;
}
const u8* melee_web_test_native_stadium_flags(void* pointer,int index){
 UnkStageDat* map=pointer;
 return map&&index>=0&&index<map->unkC?map->unk8[index].x28:NULL;
}
void* melee_web_test_native_stadium_flag(void* pointer,int index){
 UnkStageDat* map=pointer;
 return map&&index>=0&&index<map->unk2C?map->unk28[index]:NULL;
}

struct MarkerJointReference { HSD_Joint* joint; s16* pairs; s32 count; };
int melee_web_test_native_marker_pairs(void* pointer,const uint16_t* expected,int count){
 UnkStageDat* map=pointer;
 if(!map||!expected||count<=0||map->unk4!=1||!map->unk0||map->unkC!=1||!map->unk8)return 0;
 struct MarkerJointReference* refs=(struct MarkerJointReference*)map->unk0;
 if(refs[0].joint!=map->unk8[0].unk0||refs[0].count!=count||!refs[0].pairs)return 0;
 for(int i=0;i<count*2;i++)if((uint16_t)refs[0].pairs[i]!=expected[i])return 0;
 return 1;
}
int melee_web_test_ground_marker_last_write(void* pointer){
 UnkStageDat* map=pointer;
 if(!map||map->unk4!=1||!map->unk0)return 0;
 struct MarkerJointReference* refs=(struct MarkerJointReference*)map->unk0;
 const uint16_t* pairs=(const uint16_t*)refs[0].pairs;
 const int count=refs[0].count;
 if(!pairs||count<=0||count>261)return 0;
 HSD_JObj marker_joints[13]={{0}};
 HSD_JObj* before_135=Ground_801C2CF4(135);
 HSD_JObj* before_134=Ground_801C2CF4(134);
 for(int i=0;i<count;i++)if(pairs[2*i]>=13)return 0;
 for(int i=0;i<count;i++){
  const uint16_t index=pairs[2*i],id=pairs[2*i+1];
  if(id==135||id==134)Ground_801C2D0C((s32)id,&marker_joints[index]);
 }
 const int last_wins=Ground_801C2CF4(135)==&marker_joints[4]&&
                     Ground_801C2CF4(134)==&marker_joints[9]&&
                     Ground_801C2CF4(135)!=&marker_joints[12];
 Ground_801C2D0C(135,before_135);Ground_801C2D0C(134,before_134);
 return last_wins;
}
