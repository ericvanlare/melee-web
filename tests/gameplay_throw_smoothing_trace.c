#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
typedef unsigned char u8;
typedef struct { float x,y,z; } Vec3;
typedef struct { Vec3 translate; int dirty; } HSD_JObj;
typedef struct { struct { HSD_JObj* joint; } parts[1]; Vec3 x1A7C; float x1A6C; } Fighter;
typedef struct { void* user_data; } HSD_GObj;
static int ftParts_GetBoneIndex(Fighter* fp,int i){(void)fp;assert(i==4);return 0;}
static int HSD_JObjMtxIsDirty(HSD_JObj* j){return j->dirty;}
static void HSD_JObjGetTranslation(HSD_JObj* j,Vec3* p){*p=j->translate;}
static void HSD_JObjSetTranslate(HSD_JObj* j,Vec3* p){j->translate=*p;}
#include "throw_smoothing_source.h"
static uint32_t bits(float f){uint32_t b;memcpy(&b,&f,4);return b;}
/* Original Doc/Roy Yoshi's replay, source updates 4165,4166,4180.
 * Values and original instruction boundaries are bound in
 * docs/evidence/recorded-queue-replay-v1.json. No expected state enters runtime. */
static const struct {int tick;uint32_t previous[3],authored[3],scale,expected[3];} cases[]={
    {4165, {0x3eceadd9,0x4026e250,0xbfe0ed22}, {0x3ec8cc2b,0x40262caa,0xbfee9886}, 0x3f937070, {0x3ec7e781,0x40261114,0xbff0abf9}},
    {4166, {0x3ec7e781,0x40261114,0xbff0abf9}, {0x3eecf98c,0x40327c77,0xbffe2b7b}, 0x3f937070, {0x3ef29aca,0x40345f51,0xc0001c22}},
    {4180, {0x3f256f3b,0x40925f2e,0xc04d6227}, {0x3f12c632,0x408ff84b,0xc04c44a6}, 0x3f937070, {0x3f0ff0b6,0x408f9ae9,0xc04c194a}},
};
int main(void){
 int negative=0;
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
  HSD_JObj j={.dirty=1};Fighter fp={.parts={{&j}}};HSD_GObj g={&fp};
  memcpy(&j.translate,cases[i].authored,12);memcpy(&fp.x1A7C,cases[i].previous,12);memcpy(&fp.x1A6C,&cases[i].scale,4);
  float previous[3],authored[3];memcpy(previous,cases[i].previous,12);memcpy(authored,cases[i].authored,12);
  ftCommon_8007E3EC(&g);
  uint32_t actual[3];memcpy(actual,&j.translate,12);
  for(int c=0;c<3;++c){
   if(actual[c]!=cases[i].expected[c]){fprintf(stderr,"tick %d channel %d: %08x != %08x\n",cases[i].tick,c,actual[c],cases[i].expected[c]);return 1;}
   volatile float product=(authored[c]-previous[c])*fp.x1A6C;
   if(bits(previous[c]+product)!=cases[i].expected[c])++negative;
  }
  if(memcmp(&fp.x1A7C,&j.translate,12))return 2;
  j.dirty=0;memcpy(&j.translate,cases[i].authored,12);
  ftCommon_8007E3EC(&g);
  if(memcmp(&j.translate,cases[i].authored,12)||memcmp(&fp.x1A7C,cases[i].expected,12))return 3;
 }
 if(!negative)return 4;
 puts("throw-smoothing retail=all unfused=diff clean-joint=unchanged");return 0;
}
