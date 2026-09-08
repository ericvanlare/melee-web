#include "hsd_native_arrays.h"
#include <stdio.h>
#include <stdlib.h>
struct MeleeWebNativeArrays {
    struct MeleeWebNativeArrays* next;
    uint32_t count;
    MeleeWebPObjAttribute entries[];
};
static MeleeWebNativeArrays* owners;
static uint32_t total;
MeleeWebNativeArrays* melee_web_native_arrays_register(const MeleeWebPObjAttribute* input,uint32_t count,char* e,size_t n)
{
    if(!input||!count||count>MELEE_WEB_POBJ_MAX_ATTRIBUTES||count>262144-total){
        if(e&&n)snprintf(e,n,"Native indexed array registry budget/metadata invalid");return NULL;
    }
    for(uint32_t i=0;i<count;i++)if(input[i].attr_type!=1 &&
        (input[i].attr_type<2||input[i].attr_type>3||!input[i].data||!input[i].byte_size||!input[i].stride||input[i].stride>255)){
        if(e&&n)snprintf(e,n,"Native indexed array requires proven bytes and stride");return NULL;
    }
    MeleeWebNativeArrays* h=malloc(sizeof(*h)+count*sizeof(*input));
    if(!h){if(e&&n)snprintf(e,n,"Cannot allocate native array registration");return NULL;}
    h->count=count;for(uint32_t i=0;i<count;i++)h->entries[i]=input[i];
    h->next=owners;owners=h;total+=count;if(e&&n)*e=0;return h;
}
void melee_web_native_arrays_remove(MeleeWebNativeArrays* h)
{
    if(!h)return;MeleeWebNativeArrays** link=&owners;
    while(*link&&*link!=h)link=&(*link)->next;
    if(!*link){fputs("Native array registration ownership was lost\n",stderr);abort();}
    *link=h->next;total-=h->count;free(h);
}
int melee_web_native_array_bound(uint32_t attr,const void* data,uint32_t stride,uint32_t* bytes)
{
    if(!data||!bytes)return 0;uint32_t bound=0;
    for(MeleeWebNativeArrays* h=owners;h;h=h->next)for(uint32_t i=0;i<h->count;i++){
        const MeleeWebPObjAttribute* a=&h->entries[i];
        if(a->attr_type!=1&&a->attr==attr&&a->data==data&&a->stride==stride&&a->byte_size>bound)bound=a->byte_size;
    }
    if(!bound)return 0;*bytes=bound;return 1;
}
