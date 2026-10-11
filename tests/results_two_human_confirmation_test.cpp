// Asset-free original confirmation control only. The original function is
// inserted unchanged by the wrapper. Graphics/audio collaborators are mocked;
// this test does not create a source world or claim rendered Results behavior.
#include <cassert>
#include <cstdint>
#include <iostream>
using s32=int32_t;using u32=uint32_t;using u8=uint8_t;
constexpr u32 PAD_BUTTON_START=0x1000,JOBJ_HIDDEN=1;
#define PAD_STACK(n)
struct HSD_JObj{float frame=0;void* aobj=nullptr;};
struct HSD_GObj{HSD_JObj* hsd_obj;};
struct PlayerData{int x0_0=0,x0_1=0,x0_2=0,x0_3=0,x0_4=0;HSD_JObj* jobjs[12]{};};
struct ResultsData{u32 x0_23=0;int x1=1,x3=0;PlayerData player_data[4];};
struct MatchEnd{struct Standing{u8 slot_type=3;}player_standings[4];};
struct Pad{int err=0;u32 trigger=0;}HSD_PadCopyStatus[4];
ResultsData lbl_8046DBE8;MatchEnd terminal;static s32 lbl_804D3FC8=1;
HSD_JObj objects[4][12],scene;HSD_GObj gobj{&scene};
int initialized[4]{},confirmed_calls=0,unconfirmed_calls=0;
MatchEnd* fn_80174274(){return &terminal;}
float lbGetJObjCurrFrame(HSD_JObj* j){return j->frame;}
void lb_8000BA0C(HSD_JObj*,float){}
void fn_80175D34(){}
void fn_80174B4C(ResultsData*,int k){initialized[k]++;}
int fn_80177B7C(int){return 0;}int fn_80177DD0(int){return 0;}
void fn_80174920(PlayerData*){}
void* HSD_JObjGetDObj(HSD_JObj*){return nullptr;}
void lbDObjSetRateAll(void*,float){}void lbDObjReqAnimAll(void*,float){}
void HSD_AObjSetRate(void*,float){}void HSD_AObjReqAnim(void*,float){}
void HSD_JObjAnimAll(HSD_JObj*){}void HSD_JObjSetFlagsAll(HSD_JObj*,u32){}
void HSD_JObjClearFlagsAll(HSD_JObj*,u32){}
void fn_80174338(){confirmed_calls++;}void fn_8017435C(){unconfirmed_calls++;}
// ORIGINAL_FUNCTION
void reset(u8 p0=0,u8 p1=0){
 lbl_8046DBE8={};terminal={};scene={};lbl_804D3FC8=1;
 confirmed_calls=unconfirmed_calls=0;
 for(int k=0;k<4;k++){
  initialized[k]=0;HSD_PadCopyStatus[k]={};
  terminal.player_standings[k].slot_type=k==0?p0:k==1?p1:3;
  for(int j=0;j<12;j++)lbl_8046DBE8.player_data[k].jobjs[j]=&objects[k][j];
 }
}
void tick(u32 p0=0,u32 p1=0){
 HSD_PadCopyStatus[0].trigger=p0;HSD_PadCopyStatus[1].trigger=p1;
 fn_80178050(&gobj);
}
int main(){
 reset();scene.frame=49;tick(PAD_BUTTON_START,PAD_BUTTON_START);
 assert(lbl_8046DBE8.x0_23==0&&lbl_8046DBE8.x1!=4);
 assert(!lbl_8046DBE8.player_data[0].x0_0&&!lbl_8046DBE8.player_data[1].x0_0);
 scene.frame=50;tick();
 assert(lbl_8046DBE8.x0_23==2&&lbl_8046DBE8.x3==10);
 assert(initialized[0]==1&&initialized[1]==1);
 assert(!lbl_8046DBE8.player_data[0].x0_0&&!lbl_8046DBE8.player_data[1].x0_0);
 assert(lbl_8046DBE8.player_data[2].x0_0&&lbl_8046DBE8.player_data[3].x0_0);
 tick(PAD_BUTTON_START);assert(lbl_8046DBE8.player_data[0].x0_0==1);
 assert(lbl_8046DBE8.player_data[1].x0_0==0&&lbl_8046DBE8.x1!=4);
 for(int n=0;n<90;n++)tick(); // Neutral/held-without-trigger cannot acknowledge P2.
 assert(lbl_8046DBE8.player_data[0].x0_0==1&&lbl_8046DBE8.x1!=4);
 tick(PAD_BUTTON_START);assert(lbl_8046DBE8.player_data[0].x0_0==0&&unconfirmed_calls==1);
 tick();tick(PAD_BUTTON_START);assert(lbl_8046DBE8.player_data[0].x0_0==1);
 tick();tick(0,PAD_BUTTON_START);
 assert(lbl_8046DBE8.player_data[1].x0_0==1&&lbl_8046DBE8.x1==4);
 std::cout<<"two connected humans: pre-readiness rejected; P1-only no exit; repeated P1 toggles; P2 Start completes all ready\n";
 reset();scene.frame=50;tick();tick(PAD_BUTTON_START,PAD_BUTTON_START);
 assert(lbl_8046DBE8.x1==4);std::cout<<"simultaneous per-port Start acknowledges both humans\n";
 reset();scene.frame=50;HSD_PadCopyStatus[1].err=-1;tick();
 assert(lbl_8046DBE8.player_data[1].x0_0==1&&lbl_8046DBE8.x1!=4);
 tick(PAD_BUTTON_START);assert(lbl_8046DBE8.x1==4);
 std::cout<<"disconnected human source auto-confirm differs from connected P2\n";
 reset(0,1);scene.frame=50;tick();
 assert(lbl_8046DBE8.player_data[1].x0_0==1&&lbl_804D3FC8==0);
 tick(PAD_BUTTON_START);assert(lbl_8046DBE8.x1==4);
 std::cout<<"ordinary human/CPU plus NA retains source auto-confirm behavior\n";
 reset(1,1);scene.frame=50;tick();assert(lbl_804D3FC8==1&&lbl_8046DBE8.x3==20);
 assert(lbl_8046DBE8.x1!=4);tick(PAD_BUTTON_START);assert(lbl_8046DBE8.x1==4);
 std::cout<<"all CPU source path still needs its distinct connected Start gate\n";
}
