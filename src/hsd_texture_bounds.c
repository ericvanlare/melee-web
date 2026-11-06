#include "hsd_texture_bounds.h"
#include <math.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

_Static_assert(sizeof(float)==sizeof(uint32_t),"texture diagnostic requires 32-bit float");

enum { TEXTURE_BOUNDS_BUCKETS=1024, TEXTURE_BOUNDS_MAX_OWNERS=65536 };

typedef struct Binding {
    struct Binding* next;
    const void* object;
    uint32_t images, palettes;
} Binding;
typedef struct SnapshotEntry {
    const void* object;
    uint32_t images, palettes;
} SnapshotEntry;
struct MeleeWebTextureBoundsSnapshot {
    uint64_t world;
    uint32_t count;
    SnapshotEntry* entries;
};
struct MeleeWebTextureBoundsPrepared {
    const MeleeWebTextureBoundsSnapshot* snapshot;
    uint64_t world, revision;
    uint32_t count;
    Binding* replacement[TEXTURE_BOUNDS_BUCKETS];
    int admitted, consumed;
};

static Binding* buckets[TEXTURE_BOUNDS_BUCKETS];
static uint32_t live;
static uint64_t mutation_revision;

static uint32_t bucket_index(const void* object) {
    uintptr_t key=(uintptr_t)object;
    return (uint32_t)(((key>>4)^(key>>14))&(TEXTURE_BOUNDS_BUCKETS-1));
}
static Binding** slot_in(Binding** table,const void* object) {
    Binding** entry=&table[bucket_index(object)];
    while(*entry && (*entry)->object!=object)entry=&(*entry)->next;
    return entry;
}
static Binding** slot(const void* object) { return slot_in(buckets,object); }
static void mutation_revision_require_room(void) {
    if(mutation_revision==UINT64_MAX){
        fputs("Native texture animation mutation revision exhausted\n",stderr);
        abort();
    }
}
/* Bound every traversal by the authoritative count before checking uniqueness.
 * Keys are opaque pointer values: no source object is read or dereferenced. */
static int table_valid(Binding** table,uint32_t expected_count) {
    if(expected_count>TEXTURE_BOUNDS_MAX_OWNERS)return 0;
    uint32_t seen=0;
    for(uint32_t bucket=0;bucket<TEXTURE_BOUNDS_BUCKETS;bucket++){
        for(Binding* node=table[bucket];node;node=node->next){
            if(seen>=expected_count)return 0;
            ++seen;
            if(!node->object || node->images>256 || node->palettes>256 ||
               bucket_index(node->object)!=bucket)return 0;
            if(*slot_in(table,node->object)!=node)return 0;
        }
    }
    return seen==expected_count;
}
static int snapshot_entry_compare(const void* left,const void* right) {
    uintptr_t a=(uintptr_t)((const SnapshotEntry*)left)->object;
    uintptr_t b=(uintptr_t)((const SnapshotEntry*)right)->object;
    return (a>b)-(a<b);
}
static int snapshot_valid(const MeleeWebTextureBoundsSnapshot* snapshot) {
    if(!snapshot || !snapshot->world || snapshot->count>TEXTURE_BOUNDS_MAX_OWNERS ||
       (snapshot->count && !snapshot->entries) || (!snapshot->count && snapshot->entries))return 0;
    for(uint32_t i=0;i<snapshot->count;i++){
        const SnapshotEntry* row=&snapshot->entries[i];
        if(!row->object || row->images>256 || row->palettes>256)return 0;
        if(i && (uintptr_t)snapshot->entries[i-1].object >= (uintptr_t)row->object)return 0;
    }
    return 1;
}
static int table_matches_snapshot(Binding** table,uint32_t count,
                                  const MeleeWebTextureBoundsSnapshot* snapshot) {
    if(!snapshot_valid(snapshot) || count!=snapshot->count || !table_valid(table,count))return 0;
    for(uint32_t i=0;i<snapshot->count;i++){
        const SnapshotEntry* row=&snapshot->entries[i];
        Binding** found=slot_in(table,row->object);
        if(!*found || (*found)->images!=row->images || (*found)->palettes!=row->palettes)return 0;
    }
    return 1;
}
static void table_free(Binding** table) {
    for(uint32_t i=0;i<TEXTURE_BOUNDS_BUCKETS;i++){
        Binding* node=table[i];
        table[i]=NULL;
        while(node){Binding* next=node->next;free(node);node=next;}
    }
}

void melee_web_texture_bounds_bind(const void* object,uint32_t images,uint32_t palettes) {
    if(!object || images>256 || palettes>256){
        fputs("Invalid native texture animation bounds\n",stderr);abort();
    }
    Binding** entry=slot(object);
    if(!*entry){
        if(live>=TEXTURE_BOUNDS_MAX_OWNERS){fputs("Native texture animation owner budget exceeded\n",stderr);abort();}
        mutation_revision_require_room();
        *entry=calloc(1,sizeof(**entry));
        if(!*entry){fputs("Native texture animation bound allocation failed\n",stderr);abort();}
        (*entry)->object=object;
        (*entry)->images=images;
        (*entry)->palettes=palettes;
        ++mutation_revision;
        ++live;
        return;
    }
    if((*entry)->images==images && (*entry)->palettes==palettes)return;
    mutation_revision_require_room();
    ++mutation_revision;
    (*entry)->images=images;
    (*entry)->palettes=palettes;
}
void melee_web_texture_bounds_remove(const void* object) {
    Binding** entry=slot(object);
    if(*entry){
        mutation_revision_require_room();
        Binding* old=*entry;
        ++mutation_revision;
        *entry=old->next;
        --live;
        free(old);
    }
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

MeleeWebTextureBoundsSnapshot* melee_web_texture_bounds_capture(uint64_t world) {
    if(!world || !table_valid(buckets,live))return NULL;
    MeleeWebTextureBoundsSnapshot* snapshot=calloc(1,sizeof(*snapshot));
    if(!snapshot)return NULL;
    snapshot->world=world;
    snapshot->count=live;
    if(live){
        snapshot->entries=malloc((size_t)live*sizeof(*snapshot->entries));
        if(!snapshot->entries){free(snapshot);return NULL;}
        uint32_t next=0;
        for(uint32_t bucket=0;bucket<TEXTURE_BOUNDS_BUCKETS;bucket++)
            for(Binding* node=buckets[bucket];node;node=node->next)
                snapshot->entries[next++]=(SnapshotEntry){node->object,node->images,node->palettes};
        if(next!=live){free(snapshot->entries);free(snapshot);return NULL;}
        qsort(snapshot->entries,live,sizeof(*snapshot->entries),snapshot_entry_compare);
    }
    if(!snapshot_valid(snapshot)){free(snapshot->entries);free(snapshot);return NULL;}
    return snapshot;
}
MeleeWebTextureBoundsPrepared* melee_web_texture_bounds_prepare(
    const MeleeWebTextureBoundsSnapshot* snapshot,uint64_t world) {
    if(!snapshot_valid(snapshot) || !world || snapshot->world!=world ||
       mutation_revision==UINT64_MAX || !table_valid(buckets,live))return NULL;
    const uint64_t revision=mutation_revision;
    MeleeWebTextureBoundsPrepared* prepared=calloc(1,sizeof(*prepared));
    if(!prepared)return NULL;
    prepared->snapshot=snapshot;
    prepared->world=world;
    prepared->revision=revision;
    for(uint32_t i=0;i<snapshot->count;i++){
        const SnapshotEntry* row=&snapshot->entries[i];
        Binding* node=malloc(sizeof(*node));
        if(!node){melee_web_texture_bounds_prepared_destroy(prepared);return NULL;}
        const uint32_t bucket=bucket_index(row->object);
        node->object=row->object;
        node->images=row->images;
        node->palettes=row->palettes;
        node->next=prepared->replacement[bucket];
        prepared->replacement[bucket]=node;
        ++prepared->count;
    }
    if(revision!=mutation_revision || !table_valid(buckets,live) ||
       !table_matches_snapshot(prepared->replacement,prepared->count,snapshot)){
        melee_web_texture_bounds_prepared_destroy(prepared);
        return NULL;
    }
    return prepared;
}
int melee_web_texture_bounds_admit(MeleeWebTextureBoundsPrepared* prepared,uint64_t world) {
    if(prepared)prepared->admitted=0;
    if(!prepared || !prepared->snapshot || !world || prepared->world!=world ||
       prepared->snapshot->world!=world || prepared->consumed ||
       mutation_revision==UINT64_MAX || prepared->revision!=mutation_revision ||
       !table_valid(buckets,live) ||
       !table_matches_snapshot(prepared->replacement,prepared->count,prepared->snapshot))return 0;
    prepared->admitted=1;
    return 1;
}
int melee_web_texture_bounds_matches_current(
    const MeleeWebTextureBoundsSnapshot* snapshot,uint64_t world) {
    return world && snapshot_valid(snapshot) && snapshot->world==world &&
        table_matches_snapshot(buckets,live,snapshot);
}
void melee_web_texture_bounds_commit_prevalidated(MeleeWebTextureBoundsPrepared* prepared) {
    if(!prepared || !prepared->admitted || prepared->consumed || !prepared->snapshot ||
       !prepared->world || prepared->snapshot->world!=prepared->world ||
       mutation_revision==UINT64_MAX || prepared->revision!=mutation_revision){
        abort();
    }
    const uint32_t displaced_count=live;
    ++mutation_revision;
    for(uint32_t i=0;i<TEXTURE_BOUNDS_BUCKETS;i++){
        Binding* displaced=buckets[i];
        buckets[i]=prepared->replacement[i];
        prepared->replacement[i]=displaced;
    }
    live=prepared->count;
    prepared->count=displaced_count;
    prepared->admitted=0;
    prepared->consumed=1;
}
void melee_web_texture_bounds_prepared_destroy(MeleeWebTextureBoundsPrepared* prepared) {
    if(!prepared)return;
    table_free(prepared->replacement);
    free(prepared);
}
void melee_web_texture_bounds_snapshot_destroy(MeleeWebTextureBoundsSnapshot* snapshot) {
    if(!snapshot)return;
    free(snapshot->entries);
    free(snapshot);
}
