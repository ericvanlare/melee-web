#include "gameplay_compat.h"
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/debug.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef DISPATCH_NEGATIVE_CONTROL
#undef TARGET_PC
#endif
#include "stage_dispatch.inc"
void __assert(char* file,u32 line,char* message){fprintf(stderr,"%s:%u %s\n",file,line,message);abort();}
static HSD_AObj animation;
static int object,token;
static unsigned visited;
static void check(int c){if(!c)abort();}
static void base(HSD_AObj* a,unsigned bit){check(a==&animation);visited|=1u<<bit;}
static void a(HSD_AObj* p){base(p,0);}
static void af(HSD_AObj* p,float f){base(p,1);check(f==1.25f);}
static void av(HSD_AObj* p,void* v){base(p,2);check(v==&token);}
static void au(HSD_AObj* p,u32 u){base(p,3);check(u==0x80000001u);}
static void ao(HSD_AObj* p,void* o){base(p,4);check(o==&object);}
static void aof(HSD_AObj* p,void* o,float f){base(p,5);check(o==&object&&f==1.25f);}
static void aov(HSD_AObj* p,void* o,void* v){base(p,6);check(o==&object&&v==&token);}
static void aou(HSD_AObj* p,void* o,u32 u){base(p,7);check(o==&object&&u==0x80000001u);}
static void aot(HSD_AObj* p,void* o,HSD_Type t){base(p,8);check(o==&object&&t==JOBJ_TYPE);}
static void aotf(HSD_AObj* p,void* o,HSD_Type t,float f){base(p,9);check(o==&object&&t==JOBJ_TYPE&&f==1.25f);}
static void aotv(HSD_AObj* p,void* o,HSD_Type t,void* v){base(p,10);check(o==&object&&t==JOBJ_TYPE&&v==&token);}
static void aotu(HSD_AObj* p,void* o,HSD_Type t,u32 u){base(p,11);check(o==&object&&t==JOBJ_TYPE&&u==0x80000001u);}
int main(void){
 void* functions[]={a,af,av,au,ao,aof,aov,aou,aot,aotf,aotv,aotu};
 for(unsigned type=0;type<12;type++){
  callbackArg arg={0};
  switch(type%4){case 1:arg.f=1.25f;break;case 2:arg.v=&token;break;case 3:arg.d=0x80000001u;break;}
  grAnime_801C6F50(&animation,&object,JOBJ_TYPE,functions[type],type,&arg);
 }
 check(visited==0xfff);puts("Original stage animation twelve typed callbacks passed");return 0;
}
