#include "hsd_texture_bounds.h"
#include <math.h>
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
int melee_web_texture_index_valid(const void* object,uint32_t channel,float value) {
    if(channel!=1 && channel!=10)return 0;
    const Binding* entry=*slot(object);
    if(!entry)return 0;
    uint32_t count=channel==1?entry->images:entry->palettes;
    // Prove the original conversion and ensuing lookup are in range before
    // either operation. Valid source values are passed through unchanged.
    return isfinite(value) && value>=0 && value<(float)count;
}
uint32_t melee_web_texture_bounds_live(void){return live;}
