#include "hsd_native_arrays.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct NativeArrayIndexKey NativeArrayIndexKey;
typedef struct NativeArrayIndexAlias NativeArrayIndexAlias;

struct NativeArrayIndexAlias {
    NativeArrayIndexAlias* next;
    NativeArrayIndexKey* key;
    uint32_t byte_size;
};

struct NativeArrayIndexKey {
    NativeArrayIndexKey* next;
    NativeArrayIndexAlias* aliases;
    uint32_t attr;
    uint32_t stride;
    const void* data;
    uint32_t max_byte_size;
};

struct MeleeWebNativeArrays {
    struct MeleeWebNativeArrays* next;
    uint32_t count;
    uint32_t alias_count;
    NativeArrayIndexAlias* aliases;
    MeleeWebPObjAttribute entries[];
};

static MeleeWebNativeArrays* owners;
static uint32_t total;
static NativeArrayIndexKey** index_buckets;
static size_t index_capacity;
static uint32_t index_key_count;

static void error_text(char* error,size_t error_size,const char* text)
{
    if(error&&error_size)snprintf(error,error_size,"%s",text);
}

static uint64_t index_hash(uint32_t attr,const void* data,uint32_t stride)
{
    uint64_t value=(uint64_t)(uintptr_t)data;
    value^=(uint64_t)attr*UINT64_C(0x9e3779b97f4a7c15);
    value^=(uint64_t)stride*UINT64_C(0xbf58476d1ce4e5b9);
    value^=value>>30;value*=UINT64_C(0xbf58476d1ce4e5b9);
    value^=value>>27;value*=UINT64_C(0x94d049bb133111eb);
    return value^(value>>31);
}

static size_t index_bucket(uint32_t attr,const void* data,uint32_t stride)
{
    return (size_t)index_hash(attr,data,stride)&(index_capacity-1);
}

static int key_matches(const NativeArrayIndexKey* key,uint32_t attr,
                       const void* data,uint32_t stride)
{
    return key->attr==attr&&key->data==data&&key->stride==stride;
}

static NativeArrayIndexKey* find_key(uint32_t attr,const void* data,uint32_t stride)
{
    if(!index_buckets||!index_capacity)return NULL;
    for(NativeArrayIndexKey* key=index_buckets[index_bucket(attr,data,stride)];
        key;key=key->next)
        if(key_matches(key,attr,data,stride))return key;
    return NULL;
}

static uint32_t count_indexed(const MeleeWebPObjAttribute* input,uint32_t count)
{
    uint32_t indexed=0;
    for(uint32_t i=0;i<count;i++)if(input[i].attr_type!=1)++indexed;
    return indexed;
}

static uint32_t count_new_keys(const MeleeWebPObjAttribute* input,uint32_t count)
{
    uint32_t result=0;
    for(uint32_t i=0;i<count;i++){
        if(input[i].attr_type==1||find_key(input[i].attr,input[i].data,input[i].stride))continue;
        int duplicate=0;
        for(uint32_t j=0;j<i;j++)if(input[j].attr_type!=1&&
            input[j].attr==input[i].attr&&input[j].data==input[i].data&&
            input[j].stride==input[i].stride){
            duplicate=1;break;
        }
        if(!duplicate)++result;
    }
    return result;
}

static int ensure_index_capacity(uint32_t additional,char* error,size_t error_size)
{
    if(!additional)return 1;
    const size_t needed=(size_t)index_key_count+additional;
    size_t capacity=index_capacity?index_capacity:16;
    while(needed>capacity-(capacity/4)){
        if(capacity>SIZE_MAX/2){error_text(error,error_size,"Native indexed array index is too large");return 0;}
        capacity*=2;
    }
    if(capacity==index_capacity)return 1;
    if(capacity>SIZE_MAX/sizeof(*index_buckets)){
        error_text(error,error_size,"Native indexed array index is too large");return 0;
    }
    NativeArrayIndexKey** buckets=calloc(capacity,sizeof(*buckets));
    if(!buckets){error_text(error,error_size,"Cannot allocate native array index");return 0;}
    if(index_buckets){
        for(size_t i=0;i<index_capacity;i++){
            NativeArrayIndexKey* key=index_buckets[i];
            while(key){
                NativeArrayIndexKey* next=key->next;
                const size_t bucket=(size_t)index_hash(key->attr,key->data,key->stride)&(capacity-1);
                key->next=buckets[bucket];buckets[bucket]=key;key=next;
            }
        }
        free(index_buckets);
    }
    index_buckets=buckets;index_capacity=capacity;return 1;
}

static void unlink_key(NativeArrayIndexKey* key)
{
    NativeArrayIndexKey** link=&index_buckets[index_bucket(key->attr,key->data,key->stride)];
    while(*link&&*link!=key)link=&(*link)->next;
    if(!*link){fputs("Native array index ownership was lost\n",stderr);abort();}
    *link=key->next;--index_key_count;free(key);
}

static void unlink_alias(NativeArrayIndexAlias* alias)
{
    NativeArrayIndexKey* key=alias->key;
    NativeArrayIndexAlias** link=&key->aliases;
    while(*link&&*link!=alias)link=&(*link)->next;
    if(!*link){fputs("Native array alias ownership was lost\n",stderr);abort();}
    *link=alias->next;
    if(key->max_byte_size==alias->byte_size){
        key->max_byte_size=0;
        for(NativeArrayIndexAlias* other=key->aliases;other;other=other->next)
            if(other->byte_size>key->max_byte_size)key->max_byte_size=other->byte_size;
    }
    if(!key->aliases)unlink_key(key);
}

static void rollback_aliases(MeleeWebNativeArrays* owner)
{
    for(uint32_t i=0;i<owner->alias_count;i++){
        if(owner->aliases[i].key)unlink_alias(&owner->aliases[i]);
    }
}

static void discard_empty_index(void)
{
    if(!index_key_count){free(index_buckets);index_buckets=NULL;index_capacity=0;}
}

MeleeWebNativeArrays* melee_web_native_arrays_register(const MeleeWebPObjAttribute* input,uint32_t count,char* error,size_t error_size)
{
    if(!input||!count||count>MELEE_WEB_POBJ_MAX_ATTRIBUTES||count>262144-total){
        error_text(error,error_size,"Native indexed array registry budget/metadata invalid");return NULL;
    }
    for(uint32_t i=0;i<count;i++)if(input[i].attr_type!=1&&
        (input[i].attr_type<2||input[i].attr_type>3||!input[i].data||!input[i].byte_size||
         !input[i].stride||input[i].stride>255)){
        error_text(error,error_size,"Native indexed array requires proven bytes and stride");return NULL;
    }
    const size_t owner_size=sizeof(MeleeWebNativeArrays)+
                            (size_t)count*sizeof(*input);
    if(owner_size<sizeof(MeleeWebNativeArrays)){
        error_text(error,error_size,"Native indexed array registration is too large");return NULL;
    }
    MeleeWebNativeArrays* h=malloc(owner_size);
    if(!h){error_text(error,error_size,"Cannot allocate native array registration");return NULL;}
    h->count=count;h->alias_count=count_indexed(input,count);h->aliases=NULL;
    for(uint32_t i=0;i<count;i++)h->entries[i]=input[i];
    if(h->alias_count){
        h->aliases=calloc(h->alias_count,sizeof(*h->aliases));
        if(!h->aliases){free(h);error_text(error,error_size,"Cannot allocate native array index aliases");return NULL;}
        if(!ensure_index_capacity(count_new_keys(input,count),error,error_size)){
            free(h->aliases);free(h);return NULL;
        }
    }
    uint32_t alias=0;
    for(uint32_t i=0;i<count;i++)if(h->entries[i].attr_type!=1){
        const MeleeWebPObjAttribute* entry=&h->entries[i];
        NativeArrayIndexKey* key=find_key(entry->attr,entry->data,entry->stride);
        if(!key){
            key=malloc(sizeof(*key));
            if(!key){rollback_aliases(h);free(h->aliases);free(h);
                discard_empty_index();
                error_text(error,error_size,"Cannot allocate native array index key");return NULL;}
            *key=(NativeArrayIndexKey){.next=NULL,.aliases=NULL,.attr=entry->attr,
                                      .stride=entry->stride,.data=entry->data,.max_byte_size=0};
            const size_t bucket=index_bucket(key->attr,key->data,key->stride);
            key->next=index_buckets[bucket];index_buckets[bucket]=key;++index_key_count;
        }
        NativeArrayIndexAlias* link=&h->aliases[alias++];
        link->key=key;link->byte_size=entry->byte_size;link->next=key->aliases;key->aliases=link;
        if(link->byte_size>key->max_byte_size)key->max_byte_size=link->byte_size;
    }
    h->next=owners;owners=h;total+=count;if(error&&error_size)*error=0;return h;
}

void melee_web_native_arrays_remove(MeleeWebNativeArrays* h)
{
    if(!h)return;MeleeWebNativeArrays** link=&owners;
    while(*link&&*link!=h)link=&(*link)->next;
    if(!*link){fputs("Native array registration ownership was lost\n",stderr);abort();}
    for(uint32_t i=0;i<h->alias_count;i++)unlink_alias(&h->aliases[i]);
    *link=h->next;total-=h->count;free(h->aliases);free(h);
    discard_empty_index();
}

int melee_web_native_array_bound(uint32_t attr,const void* data,uint32_t stride,uint32_t* bytes)
{
    if(!data||!bytes)return 0;NativeArrayIndexKey* key=find_key(attr,data,stride);
    if(!key||!key->max_byte_size)return 0;*bytes=key->max_byte_size;return 1;
}
