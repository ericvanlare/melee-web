#include "hsd_texture_bounds.h"
#include <math.h>
#include <inttypes.h>
#include <string.h>
_Static_assert(sizeof(float)==sizeof(uint32_t),"texture diagnostic requires 32-bit float");
#include <stdio.h>
#include <stdlib.h>

typedef struct Binding {
    struct Binding* next;
    const void* object;
    uint32_t images, palettes;
} Binding;
static Binding* buckets[1024];
static uint32_t live;
static Binding** slot(const void* object) {
    uintptr_t key=(uintptr_t)object;
    Binding** entry=&buckets[((key>>4)^(key>>14))&1023];
    while(*entry && (*entry)->object!=object)entry=&(*entry)->next;
    return entry;
}
void melee_web_texture_bounds_bind(const void* object,uint32_t images,uint32_t palettes) {
    if(!object || images>256 || palettes>256){
        fputs("Invalid native texture animation bounds\n",stderr);abort();
    }
    Binding** entry=slot(object);
    if(!*entry){
        if(live>=65536){fputs("Native texture animation owner budget exceeded\n",stderr);abort();}
        *entry=calloc(1,sizeof(**entry));
        if(!*entry){fputs("Native texture animation bound allocation failed\n",stderr);abort();}
        (*entry)->object=object;++live;
    }
    (*entry)->images=images;(*entry)->palettes=palettes;
}
void melee_web_texture_bounds_remove(const void* object) {
    Binding** entry=slot(object);
    if(*entry){Binding* old=*entry;*entry=old->next;free(old);--live;}
}
static void report_refusal(const void* object,uint32_t channel,float value,
                           int binding_observed,int binding_present,
                           int count_observed,uint32_t count,const char* reason) {
    uint32_t bits;
    memcpy(&bits,&value,sizeof(bits));
    fprintf(stderr,"TObj index refusal object=%p channel=%" PRIu32
            " value_bits=0x%08" PRIx32 " binding_observed=%u binding=%u"
            " count_observed=%u count=%" PRIu32 " live=%" PRIu32
            " reason=%s\n",
            object,channel,bits,binding_observed?1u:0u,
            binding_present?1u:0u,count_observed?1u:0u,count,live,reason);
}
int melee_web_texture_index_valid(const void* object,uint32_t channel,float value) {
    if(channel!=1 && channel!=10){
        report_refusal(object,channel,value,0,0,0,0,"invalid-channel");
        return 0;
    }
    const Binding* entry=*slot(object);
    if(!entry){report_refusal(object,channel,value,1,0,0,0,"binding-missing");return 0;}
    uint32_t count=channel==1?entry->images:entry->palettes;
    // HSD_TObjUpdateFunc casts TIMG to signed int before indexing, so its
    // negative fractions in (-1, 0) truncate to source image zero. TCLT's
    // byte conversion has a distinct unsigned domain and stays nonnegative.
    const int lower_in_range=channel==1?value>-1.0f:value>=0.0f;
    if(!isfinite(value)){report_refusal(object,channel,value,1,1,1,count,"non-finite");return 0;}
    if(!lower_in_range){report_refusal(object,channel,value,1,1,1,count,"below-source-domain");return 0;}
    if(!(value<(float)count)){report_refusal(object,channel,value,1,1,1,count,"index-at-or-above-count");return 0;}
    return 1;
}
uint32_t melee_web_texture_bounds_live(void){return live;}
