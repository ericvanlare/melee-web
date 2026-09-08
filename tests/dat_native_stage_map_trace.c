#include "gameplay_compat.h"
#include <melee/gr/types.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/fog.h>
#include <math.h>
int melee_web_test_native_stage_map(void* pointer,void* yaku){
 UnkStageDat* map=pointer;u32** programs=yaku;
 if(!map||map->unkC!=10||map->unk4!=1||map->unk14!=2||map->unk24!=3||map->unk2C!=1||map->unk18!=NULL||map->unk1C!=32||!programs)return 0;
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
