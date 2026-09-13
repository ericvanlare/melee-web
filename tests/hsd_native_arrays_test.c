#include "hsd_native_arrays.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long allocation_fail_at=-1;
static size_t allocation_calls;
static long live_allocations;

void* test_malloc(size_t size)
{
    const size_t call=allocation_calls++;
    if(allocation_fail_at>=0&&(long)call==allocation_fail_at)return NULL;
    void* result=malloc(size);if(result)++live_allocations;return result;
}

void* test_calloc(size_t count,size_t size)
{
    const size_t call=allocation_calls++;
    if(allocation_fail_at>=0&&(long)call==allocation_fail_at)return NULL;
    void* result=calloc(count,size);if(result)++live_allocations;return result;
}

void test_free(void* pointer)
{
    if(pointer)--live_allocations;free(pointer);
}

static void allow_allocations(void){allocation_fail_at=-1;allocation_calls=0;}
static void fail_allocation_at(long call){allocation_fail_at=call;allocation_calls=0;}
static size_t allocation_count(void){return allocation_calls;}

static MeleeWebPObjAttribute indexed(uint32_t attr,const void* data,
                                     uint32_t stride,uint32_t bytes)
{
    return (MeleeWebPObjAttribute){attr,2,1,4,0,(uint16_t)stride,data,bytes};
}

static void expect_bound(uint32_t attr,const void* data,uint32_t stride,uint32_t expected)
{
    uint32_t bytes=0;
    assert(melee_web_native_array_bound(attr,data,stride,&bytes));
    assert(bytes==expected);
}

static void expect_miss(uint32_t attr,const void* data,uint32_t stride)
{
    uint32_t bytes=0xdeadbeef;
    assert(!melee_web_native_array_bound(attr,data,stride,&bytes));
    assert(bytes==0xdeadbeef);
}

int main(void)
{
    char error[128];
    unsigned char data[8][64]={{0}};

    /* Every registration allocation must roll back cleanly on an empty index. */
    MeleeWebPObjAttribute injected=indexed(7,data[4],8,28);
    allow_allocations();
    MeleeWebNativeArrays* allocation_probe=melee_web_native_arrays_register(&injected,1,error,sizeof(error));
    assert(allocation_probe);const size_t allocation_points=allocation_count();
    assert(allocation_points>=4);melee_web_native_arrays_remove(allocation_probe);
    assert(live_allocations==0);
    for(size_t point=0;point<allocation_points;point++){
        fail_allocation_at((long)point);
        assert(!melee_web_native_arrays_register(&injected,1,error,sizeof(error)));
        expect_miss(7,data[4],8);assert(live_allocations==0);
    }
    allow_allocations();
    allocation_probe=melee_web_native_arrays_register(&injected,1,error,sizeof(error));
    assert(allocation_probe);expect_bound(7,data[4],8,28);melee_web_native_arrays_remove(allocation_probe);
    assert(live_allocations==0);

    /* Duplicate aliases share one key and retain the largest live bound. */
    MeleeWebPObjAttribute overlap[]={
        indexed(9,data[0],12,24),indexed(9,data[0],12,48),
        indexed(9,data[0],12,16),indexed(10,data[0],12,32)};
    MeleeWebNativeArrays* first=melee_web_native_arrays_register(overlap,4,error,sizeof(error));
    assert(first);expect_bound(9,data[0],12,48);expect_bound(10,data[0],12,32);
    MeleeWebPObjAttribute larger=indexed(9,data[0],12,80);
    MeleeWebNativeArrays* second=melee_web_native_arrays_register(&larger,1,error,sizeof(error));
    assert(second);expect_bound(9,data[0],12,80);
    melee_web_native_arrays_remove(second);expect_bound(9,data[0],12,48);
    melee_web_native_arrays_remove(first);expect_miss(9,data[0],12);expect_miss(10,data[0],12);

    /* A failed registration must leave all earlier keys untouched. */
    MeleeWebPObjAttribute invalid[]={indexed(11,data[1],8,20),
        {12,2,1,4,0,8,NULL,20}};
    assert(!melee_web_native_arrays_register(invalid,2,error,sizeof(error)));
    expect_miss(11,data[1],8);
    MeleeWebPObjAttribute direct={11,1,0,0,0,0,NULL,0};
    MeleeWebNativeArrays* direct_owner=melee_web_native_arrays_register(&direct,1,error,sizeof(error));
    assert(direct_owner);expect_miss(11,data[1],8);melee_web_native_arrays_remove(direct_owner);

    /* Removal shrinks the maximum, and a recycled address gets a fresh key. */
    MeleeWebPObjAttribute small=indexed(13,data[2],16,20);
    MeleeWebNativeArrays* small_owner=melee_web_native_arrays_register(&small,1,error,sizeof(error));
    assert(small_owner);expect_bound(13,data[2],16,20);melee_web_native_arrays_remove(small_owner);
    expect_miss(13,data[2],16);
    MeleeWebPObjAttribute recycled=indexed(13,data[2],16,36);
    MeleeWebNativeArrays* recycled_owner=melee_web_native_arrays_register(&recycled,1,error,sizeof(error));
    assert(recycled_owner);expect_bound(13,data[2],16,36);melee_web_native_arrays_remove(recycled_owner);
    expect_miss(13,data[2],16);
    expect_miss(99,data[3],16);expect_miss(13,data[2],8);expect_miss(13,data[2]+1,16);

    assert(!melee_web_native_arrays_register(overlap,MELEE_WEB_POBJ_MAX_ATTRIBUTES+1,
                                            error,sizeof(error)));

    /* A later new-key allocation must roll back an earlier alias max update. */
    MeleeWebPObjAttribute base=indexed(21,data[5],8,24);
    MeleeWebNativeArrays* base_owner=melee_web_native_arrays_register(&base,1,error,sizeof(error));
    assert(base_owner);expect_bound(21,data[5],8,24);
    MeleeWebPObjAttribute mixed[]={indexed(21,data[5],8,96),indexed(22,data[6],8,32),
                                   indexed(23,data[7],8,40)};
    allow_allocations();
    MeleeWebNativeArrays* mixed_owner=melee_web_native_arrays_register(mixed,3,error,sizeof(error));
    assert(mixed_owner);const size_t mixed_points=allocation_count();
    assert(mixed_points>=4);melee_web_native_arrays_remove(mixed_owner);
    expect_bound(21,data[5],8,24);assert(live_allocations>0);
    fail_allocation_at((long)mixed_points-1);
    assert(!melee_web_native_arrays_register(mixed,3,error,sizeof(error)));
    expect_bound(21,data[5],8,24);expect_miss(22,data[6],8);expect_miss(23,data[7],8);
    assert(live_allocations>0);
    allow_allocations();
    mixed_owner=melee_web_native_arrays_register(mixed,3,error,sizeof(error));
    assert(mixed_owner);expect_bound(21,data[5],8,96);expect_bound(22,data[6],8,32);
    expect_bound(23,data[7],8,40);melee_web_native_arrays_remove(mixed_owner);
    expect_bound(21,data[5],8,24);melee_web_native_arrays_remove(base_owner);
    assert(live_allocations==0);

    /* Thousands of distinct keys exercise bounded growth and hash collisions. */
    enum { OWNER_COUNT=1024, ATTRS_PER_OWNER=4 };
    static unsigned char bulk[OWNER_COUNT][ATTRS_PER_OWNER][8];
    MeleeWebNativeArrays* owners[OWNER_COUNT]={0};
    for(unsigned owner=0;owner<OWNER_COUNT;owner++){
        MeleeWebPObjAttribute attrs[ATTRS_PER_OWNER];
        for(unsigned attr=0;attr<ATTRS_PER_OWNER;attr++){
            bulk[owner][attr][0]=(unsigned char)(owner+attr);
            attrs[attr]=indexed((owner*5+attr)%64+4,bulk[owner][attr],
                                (owner+attr)%255+1,(owner%47)+8+attr);
        }
        owners[owner]=melee_web_native_arrays_register(attrs,ATTRS_PER_OWNER,
                                                       error,sizeof(error));
        assert(owners[owner]);
    }
    for(unsigned owner=0;owner<OWNER_COUNT;owner++)for(unsigned attr=0;attr<ATTRS_PER_OWNER;attr++){
        const uint32_t key=(owner*5+attr)%64+4;
        expect_bound(key,bulk[owner][attr],(owner+attr)%255+1,(owner%47)+8+attr);
    }
    expect_miss(4,bulk[0][0],2);expect_miss(4,bulk[0][0]+1,1);
    for(unsigned owner=0;owner<OWNER_COUNT;owner+=2){
        melee_web_native_arrays_remove(owners[owner]);owners[owner]=NULL;
        expect_miss((owner*5)%64+4,bulk[owner][0],owner%255+1);
    }
    for(unsigned owner=1;owner<OWNER_COUNT;owner+=2)melee_web_native_arrays_remove(owners[owner]);
    assert(live_allocations==0);
    expect_miss(4,bulk[0][0],1);expect_miss(9,data[0],12);

    puts("Native indexed-array keyed bounds, aliases, rollback and collision checks passed");
    return 0;
}
