#include "gameplay_bootstrap.h"
#include "hsd_native_joint.h"
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/fobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned callbacks;
static void update(void* obj,enum_t type,HSD_ObjData* value){(void)obj;(void)type;(void)value;callbacks++;}
static void check(int c,const char* e){if(!c){fprintf(stderr,"%s\n",e);exit(1);}}
int main(int argc,char** argv){char error[256];
 check(melee_web_gameplay_startup(8*1024*1024,error,sizeof(error)),error);
 check(melee_web_native_world_enable(error,sizeof(error)),error);
 u8 constant[]={1,128,0};HSD_FObjDesc track={0};track.ad=constant;track.length=sizeof(constant);
 track.startframe=-3;track.type=12;track.frac_value=0x87;track.frac_slope=0x87;
 HSD_AObjDesc desc={0};desc.flags=AOBJ_LOOP;desc.end_frame=3;desc.fobjdesc=&track;
 HSD_AObj* a=HSD_AObjLoadDesc(&desc);HSD_AObjReqAnim(a,0);
 if(argc==2&&!strcmp(argv[1],"--undefined-state")){
  HSD_FObjReqAnimAll(a->fobj,3);HSD_FObjInterpretAnimAll(a->fobj,NULL,update,0);
  check(0,"undefined source FObj state must fail before callback");
 }
 for(unsigned i=0;i<100;i++)HSD_AObjInterpretAnim(a,NULL,update);
 check(callbacks==0,"source LOOP rewinds dormant visibility track before its boundary");
 HSD_AObjRemove(a);
 u8 key[]={6,0,0,0,64}; // Exact little-endian float2 for an invalid u8 conversion.
 track.ad=key;track.length=sizeof(key);track.startframe=0;track.frac_value=track.frac_slope=0;track.type=1;
 desc.flags=0;desc.end_frame=1;
 if(argc==2&&!strcmp(argv[1],"--material-color")){
  HSD_MObj material={0};HSD_Material colors={0};material.mat=&colors;material.aobj=HSD_AObjLoadDesc(&desc);
  HSD_AObjReqAnim(material.aobj,0);HSD_MObjAnim(&material);check(0,"material color conversion must reject");
 }
 if(argc==2&&!strcmp(argv[1],"--texture-color")){
  HSD_TObj texture={0};HSD_TObjTev tev={0};texture.tev=&tev;track.type=HSD_A_T_KONST_R;texture.aobj=HSD_AObjLoadDesc(&desc);
  HSD_AObjReqAnim(texture.aobj,0);HSD_TObjAnim(&texture);check(0,"texture color conversion must reject");
 }
 check(melee_web_gameplay_shutdown(error,sizeof(error)),error);
 puts("Original dormant native FObj loop timing passed");return 0;
}
