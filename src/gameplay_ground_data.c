#include "gameplay_ground_data.h"
#include <melee/gr/types.h>
#include <melee/gr/ground.h>
#include <math.h>
#include <string.h>
_Static_assert(sizeof(GroundParam)==0xdc && offsetof(GroundParam,stage_params)==0xb0,"Ground parameter ABI");
_Static_assert(sizeof(StageParam)==0x64,"Stage parameter row ABI");
#define WORD(at) r->word(r->context,(at))
#define HALF(at) r->half(r->context,(at))
#define REQUIRE(c,m) do { if(!(c))r->reject(r->context,(m)); } while(0)
void* melee_web_ground_data_decode(const MeleeWebNativeDat* r,uint32_t root) {
    if(!r)return NULL;
    r->region(r->context,root,0xdc);
    GroundParam* p=r->allocate(r->context,1,sizeof(*p));
    /* These source fields are individual aligned words; pointer and color words
     * are decoded separately below. Preserve signed integers by their bit pattern. */
    static const uint8_t floats[]={0,0x18,0x1c,0x20,0x24,0x28,0x3c,0x40,0x44,0x48,0x50,0x54,0x58,0x5c,0x60,0x64};
    static const uint8_t words[]={0xc,0x10,0x14,0x30,0x34,0x38,0x4c};
    static const uint8_t halves[]={4,8,0xa,0x2e};
    for(unsigned i=0;i<sizeof(floats);i++) {
        uint32_t bits=WORD(root+floats[i]);float value;memcpy(&value,&bits,4);
        REQUIRE(isfinite(value),"Nonfinite ground parameter");memcpy((char*)p+floats[i],&value,4);
    }
    REQUIRE(p->y>0,"Ground scale must be positive");
    for(unsigned i=0;i<sizeof(words);i++){uint32_t value=WORD(root+words[i]);memcpy((char*)p+words[i],&value,4);}
    for(unsigned i=0;i<sizeof(halves);i++){uint16_t value=HALF(root+halves[i]);memcpy((char*)p+halves[i],&value,2);}
    for(unsigned i=0x68;i<0xb0;i+=2){uint16_t value=HALF(root+i);memcpy((char*)p+i,&value,2);}
    p->stage_param_count=(int32_t)WORD(root+0xb4);
    REQUIRE(p->stage_param_count>=0&&p->stage_param_count<=256,"Invalid ground stage row count");
    uint32_t rows=r->pointer(r->context,root+0xb0,p->stage_param_count?p->stage_param_count*0x64:1);
    REQUIRE(!p->stage_param_count||rows!=UINT32_MAX,"Missing ground stage rows");
    if(p->stage_param_count) {
        r->region(r->context,rows,p->stage_param_count*0x64);
        p->stage_params=r->allocate(r->context,p->stage_param_count,sizeof(StageParam));
        for(int row=0;row<p->stage_param_count;row++) {
            char* out=(char*)&p->stage_params[row];uint32_t at=rows+row*0x64;
            for(unsigned i=0;i<20;i+=4){uint32_t value=WORD(at+i);memcpy(out+i,&value,4);}
            for(unsigned i=20;i<0x64;i+=2){uint16_t value=HALF(at+i);memcpy(out+i,&value,2);}
        }
    }
    for(unsigned i=0xb8;i<0xdc;i++)((uint8_t*)p)[i]=r->byte(r->context,root+i);
    return p;
}
void* melee_web_ground_data_publish(void* decoded) {
    GroundParam* previous=stage_info.param;stage_info.param=decoded;return previous;
}
