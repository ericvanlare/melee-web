#include "gameplay_bonus_data.h"
#include <melee/pl/player.h>
#include <melee/pl/types.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(pl_804D6470_t)==0x184,"Original bonus threshold ABI");
_Static_assert(offsetof(pl_804D6470_t,x180)==0x180,"Final bonus threshold offset");
struct MeleeWebBonusData {
    pl_804D6470_t values;
    pl_804D6470_t* public_root;
    pl_804D6470_t* previous;
};
static MeleeWebBonusData* active;
MeleeWebBonusData* melee_web_bonus_data_decode(const MeleeWebNativeDat* r,uint32_t root){
    if(!r)return NULL;r->region(r->context,root,4);
    uint32_t at=r->pointer(r->context,root,sizeof(pl_804D6470_t));
    if(at==UINT32_MAX)r->reject(r->context,"Missing PdPm bonus threshold pointer");
    r->region(r->context,at,sizeof(pl_804D6470_t));
    MeleeWebBonusData* data=r->allocate(r->context,1,sizeof(*data));
    /* Every known scalar is a four-byte integer or IEEE float. Copy host-endian
     * bits into its typed field; preserve unknown xC0 as actual uninterpreted
     * bytes. Reader rejects pointer relocations in every scalar slot. */
    for(unsigned i=0;i<sizeof(data->values);i+=4){
        if(i==0xc0){for(unsigned j=0;j<4;j++)data->values.xC0[j]=r->byte(r->context,at+i+j);}
        else {uint32_t word=r->word(r->context,at+i);memcpy((char*)&data->values+i,&word,4);}
    }
    /* x8C is declared unsigned but fn_8003F294 explicitly consumes its bits as
     * float, so it receives the same finite-value validation as typed floats. */
    static const uint16_t floats[]={0,8,0x10,0x14,0x20,0x28,0x34,0x38,0x44,0x48,0x4c,0x58,0x5c,0x60,0x64,0x68,0x6c,0x78,0x84,0x8c,0x98,0x9c,0xa0,0xcc,0xd0,0xd4,0xd8,0xe8,0xf4,0xf8,0xfc,0x100,0x104,0x10c,0x114,0x11c,0x160,0x180};
    for(unsigned i=0;i<sizeof(floats)/sizeof(*floats);i++){float value;memcpy(&value,(char*)&data->values+floats[i],4);if(!isfinite(value))r->reject(r->context,"Nonfinite PdPm bonus threshold");}
    data->public_root=&data->values;
    return data;
}
static int fail(char* e,size_t n,const char* s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
void* melee_web_bonus_data_public_data(MeleeWebBonusData* data){return data?&data->public_root:NULL;}
int melee_web_bonus_data_begin(MeleeWebBonusData* data,char* e,size_t n){
    if(!data||active)return fail(e,n,"Bonus threshold scope missing or already active");
    data->previous=pl_804D6470;pl_804D6470=&data->values;active=data;if(e&&n)*e=0;return 1;
}
int melee_web_bonus_data_ready(const MeleeWebBonusData* data){return data&&active==data&&pl_804D6470==&data->values;}
int melee_web_bonus_data_end(MeleeWebBonusData* data,char* e,size_t n){
    if(!data||active!=data)return fail(e,n,"Bonus threshold scope owner changed before release");
    if(pl_804D6470!=&data->values){
        if(e&&n)snprintf(e,n,"Source replaced PdPm owner (active=%p expected=%p published=%p)",
            (void*)active,(void*)&data->values,(void*)pl_804D6470);
        return 0;
    }
    pl_804D6470=data->previous;data->previous=NULL;active=NULL;if(e&&n)*e=0;return 1;
}
