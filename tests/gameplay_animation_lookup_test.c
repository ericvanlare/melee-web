#include "gameplay_compat.h"
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <melee/gr/ground.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
static struct { jmp_buf buf; } grAnime_8049EE40;
static HSD_JObj joint;
static HSD_AObj animations[2];
static unsigned count, calls;
HSD_JObj* Ground_801C3FA4(HSD_GObj* gobj,int index){return index==1?&joint:NULL;}
void HSD_ForeachAnim(void* object,HSD_Type type,HSD_TypeMask mask,void* callback,AObj_Arg_Type format,...){
    va_list ap;va_start(ap,format);void* output=va_arg(ap,void*);va_end(ap);
    if(object!=&joint||type!=JOBJ_TYPE||mask!=(0x220|0x7484|0x100)||format!=AOBJ_ARG_AV)abort();
    for(unsigned i=0;i<count;i++){++calls;((void(*)(HSD_AObj*,void*))callback)(&animations[i],output);}
}
#include "lookup.inc"
int main(void){
    HSD_GObj gobj={0};
    for(unsigned repeat=0;repeat<4;repeat++){
        count=2;calls=0;
        if(grAnime_801C8318(&gobj,1,7)!=&animations[0]||calls!=1)abort();
        count=0;
        if(grAnime_801C8318(&gobj,1,7)!=NULL)abort();
        if(grAnime_801C8318(&gobj,0,7)!=NULL)abort();
    }
    return 0;
}
