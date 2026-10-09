#include "stadium_c1_stage_state_probe.h"
#include "gameplay_bootstrap.h"
#include "gameplay_stage_context.h"
#include "gameplay_stage_map.h"
#include "gameplay_source_memory_runtime.h"
#include "hsd_native_joint.h"
#include "gameplay_stadium_start.h"
#include <melee/gr/grzakogenerator.h>

#include <melee/gr/grdatfiles.h>
#include <melee/gr/grpstadium.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/ft/ftdevice.h>
#include <melee/it/it_3F14.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/objalloc.h>
#include <sysdolphin/baselib/jobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MeleeWebStadiumC1StageInfoSnapshot {
    struct StageInfo saved;
    int restored;
};

static struct MeleeWebStadiumC1StageInfoSnapshot* active_snapshot;

int melee_web_stadium_c1_item_runtime_globals_view(
    MeleeWebStadiumC1ItemRuntimeGlobalsView* view)
{
    if (!view) return 0;
    view->public_data = it_804D6D20;
    view->common_articles = it_804D6D24;
    view->common_data = it_804D6D28;
    view->pokemon_articles = it_804D6D30;
    view->character_articles = it_804D6D38;
    view->bounce_data = it_804D6D40;
    view->color_rows = it_804D6D04;
    return 1;
}

int melee_web_stadium_c1_item_public_data_view(
    void* public_data, MeleeWebStadiumC1ItemPublicDataView* view)
{
    if (!public_data || !view) return 0;
    const it_804D6D20_t* source = public_data;
    view->common_data = source->x0;
    view->common_articles = source->x4;
    view->character_articles = source->x8;
    view->pokemon_articles = source->xC;
    view->bounce_data = source->x10;
    view->color_rows = source->x14;
    return 1;
}

int melee_web_stadium_c1_random_article_state_row(
    void* value, uint32_t row, void** state_table, void** script)
{
    if (!value || !state_table || !script || row >= 8) return 0;
    Article* article = value;
    if (!article->xC_itemStates) return 0;
    *state_table = article->xC_itemStates;
    *script = article->xC_itemStates->x0_itemStateDesc[row].xC_script;
    return 1;
}

static int snapshot_fail(char* error, size_t size, const char* message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

static void copy_stage_info_view(const struct StageInfo* source,
                                 MeleeWebStadiumC1StageInfoView* view)
{
    view->grkind = (int32_t) source->grkind;
    view->xA0 = source->xA0;
    view->x6E4[0] = source->x6E4[0];
    view->x6E4[1] = source->x6E4[1];
    view->itemdata = source->itemdata;
    view->coll_data = source->coll_data;
    view->param = source->param;
    view->ald_yaku_all = source->ald_yaku_all;
    view->map_ptcl = source->map_ptcl;
    view->map_texg = source->map_texg;
    view->yakumono_param = source->yakumono_param;
    view->map_plit = source->map_plit;
    view->quake_model_set = source->quake_model_set;
}

/* Source grDatFiles_8049EE10 is authored as UnkArchiveStruct[4]. */
enum {
    SOURCE_GRDATFILES_SLOT_COUNT = 4,
    /* The stage registry link walked by gameplay_stage_map.c. */
    SOURCE_STAGE_GOBJ_ENTITY_LINK = 5,
};

uint32_t melee_web_stadium_c1_stage_state_failures(void)
{
    uint32_t failures = melee_web_stadium_c1_stage_object_failures();
    UnkArchiveStruct* const ordinary = grDatFiles_GetArchive();
    if (ordinary == NULL) {
        failures |= MELEE_WEB_STADIUM_C1_ORDINARY_GRDAT_SLOT;
    } else {
        for (size_t i = 0; i < SOURCE_GRDATFILES_SLOT_COUNT; ++i) {
            if (ordinary[i].unk0 != NULL || ordinary[i].unk4 != NULL ||
                ordinary[i].unk8 != 0) {
                failures |= MELEE_WEB_STADIUM_C1_ORDINARY_GRDAT_SLOT;
                break;
            }
        }
    }
    return failures;
}

uint32_t melee_web_stadium_c1_stage_object_failures(void)
{
    uint32_t failures = 0;
    HSD_GObj** const entities = (HSD_GObj**) HSD_GObj_Entities;

    if (entities == NULL) {
        failures |= MELEE_WEB_STADIUM_C1_STAGE_LIST_UNAVAILABLE;
    } else {
        for (size_t i = 0; i < sizeof(stage_info.map_gobjs) /
                                    sizeof(stage_info.map_gobjs[0]); ++i) {
            if (stage_info.map_gobjs[i] != NULL) {
                failures |= MELEE_WEB_STADIUM_C1_STAGE_MAP_GOBJ;
                break;
            }
        }
        for (HSD_GObj* object = entities[SOURCE_STAGE_GOBJ_ENTITY_LINK]; object;
             object = object->next) {
            if (object->classifier == HSD_GOBJ_CLASS_STAGE) {
                failures |= MELEE_WEB_STADIUM_C1_STAGE_INSTANCE;
                break;
            }
        }
        if (Ground_801C498C() != NULL)
            failures |= MELEE_WEB_STADIUM_C1_GROUND_GOBJ;
    }

    if (stage_info.itemdata != NULL)
        failures |= MELEE_WEB_STADIUM_C1_STAGE_ITEMS;
    if (stage_info.map_plit != NULL)
        failures |= MELEE_WEB_STADIUM_C1_STAGE_LIGHTS;
    return failures;
}

int melee_web_stadium_c1_yakumono_exchange_baseline_empty(void)
{
    if (stage_info.yakumono_param != NULL || HSD_GObj_Entities != NULL)
        return 0;
    for (size_t i = 0; i < sizeof(stage_info.map_gobjs) /
                                sizeof(stage_info.map_gobjs[0]); ++i) {
        if (stage_info.map_gobjs[i] != NULL) return 0;
    }
    for (size_t i = 0; i < sizeof(stage_info.x280) /
                                sizeof(stage_info.x280[0]); ++i) {
        if (stage_info.x280[i] != NULL) return 0;
    }
    return 1;
}

MeleeWebStadiumC1StageInfoSnapshot*
melee_web_stadium_c1_stage_info_snapshot_begin(char* error,
                                                size_t error_size)
{
    if (active_snapshot) {
        snapshot_fail(error, error_size,
                      "A test StageInfo snapshot is already active");
        return NULL;
    }
    struct MeleeWebStadiumC1StageInfoSnapshot* snapshot =
        malloc(sizeof(*snapshot));
    if (!snapshot) {
        snapshot_fail(error, error_size,
                      "Cannot allocate the test StageInfo snapshot");
        return NULL;
    }
    memcpy(&snapshot->saved, &stage_info, sizeof(snapshot->saved));
    snapshot->restored = 0;
    active_snapshot = snapshot;
    if (error && error_size) error[0] = '\0';
    return snapshot;
}

int melee_web_stadium_c1_stage_info_snapshot_view(
    const MeleeWebStadiumC1StageInfoSnapshot* snapshot,
    MeleeWebStadiumC1StageInfoView* view)
{
    if (!snapshot || snapshot != active_snapshot || !view) return 0;
    copy_stage_info_view(&snapshot->saved, view);
    return 1;
}

int melee_web_stadium_c1_stage_info_current_view(
    MeleeWebStadiumC1StageInfoView* view)
{
    if (!view) return 0;
    copy_stage_info_view(&stage_info, view);
    return 1;
}

void* melee_web_stadium_c1_stage_info_x6A4_root(void)
{
    return stage_info.x6A4;
}

int melee_web_stadium_c1_stage_info_snapshot_restore(
    MeleeWebStadiumC1StageInfoSnapshot* snapshot, char* error,
    size_t error_size)
{
    if (!snapshot || snapshot != active_snapshot || snapshot->restored)
        return snapshot_fail(error, error_size,
                             "The test StageInfo snapshot is not restorable");
    memcpy(&stage_info, &snapshot->saved, sizeof(snapshot->saved));
    if (memcmp(&stage_info, &snapshot->saved, sizeof(snapshot->saved)) != 0)
        return snapshot_fail(error, error_size,
                             "Full source StageInfo restoration did not match its snapshot");
    snapshot->restored = 1;
    if (error && error_size) error[0] = '\0';
    return 1;
}

int melee_web_stadium_c1_stage_info_snapshot_matches(
    const MeleeWebStadiumC1StageInfoSnapshot* snapshot)
{
    return snapshot != NULL && snapshot == active_snapshot &&
           memcmp(&stage_info, &snapshot->saved, sizeof(snapshot->saved)) == 0;
}

int melee_web_stadium_c1_stage_info_snapshot_release_unchanged(
    MeleeWebStadiumC1StageInfoSnapshot* snapshot, char* error,
    size_t error_size)
{
    if (!snapshot || snapshot != active_snapshot || snapshot->restored ||
        memcmp(&stage_info, &snapshot->saved, sizeof(snapshot->saved)) != 0)
        return snapshot_fail(error, error_size,
                             "The active StageInfo snapshot changed before read-only release");
    active_snapshot = NULL;
    free(snapshot);
    if (error && error_size) error[0] = '\0';
    return 1;
}

int melee_web_stadium_c1_stage_info_snapshot_release(
    MeleeWebStadiumC1StageInfoSnapshot* snapshot, char* error,
    size_t error_size)
{
    if (!snapshot || snapshot != active_snapshot || !snapshot->restored)
        return snapshot_fail(error, error_size,
                             "The test StageInfo snapshot must be restored before release");
    active_snapshot = NULL;
    free(snapshot);
    if (error && error_size) error[0] = '\0';
    return 1;
}

struct MeleeWebStadiumC1FtDeviceSnapshot {
    struct ftDeviceUnk3 first[1];
    struct ftDeviceUnk5 bury_things[2];
    struct ftDeviceUnk3 third[1];
    struct ftDeviceUnk4 fourth;
    int first_count;
    int bury_thing_count;
    const void* addresses[6];
};

size_t melee_web_stadium_c1_ground_map_slot_count(void)
{
    return sizeof(stage_info.map_gobjs) / sizeof(stage_info.map_gobjs[0]);
}

void* melee_web_stadium_c1_ground_map_slot(size_t index)
{
    if (index >= melee_web_stadium_c1_ground_map_slot_count()) return NULL;
    return stage_info.map_gobjs[index];
}

size_t melee_web_stadium_c1_ground_marker_slot_count(void)
{
    return sizeof(stage_info.x280) / sizeof(stage_info.x280[0]);
}

void* melee_web_stadium_c1_ground_marker_slot(size_t index)
{
    if (index >= melee_web_stadium_c1_ground_marker_slot_count()) return NULL;
    return stage_info.x280[index];
}

int melee_web_stadium_c1_ground_map_profile(
    int map_id, MeleeWebStadiumC1GroundStageProfile* profile)
{
    if (profile == NULL || map_id != 1 ||
        (size_t) map_id >= melee_web_stadium_c1_ground_map_slot_count())
        return 0;
    profile->grkind = (int32_t) grPs_StageData.grkind;
    profile->callback_row_present = grPs_StageData.callbacks != NULL;
    profile->callback_flags_b2 = profile->callback_row_present
                                     ? grPs_StageData.callbacks[map_id].flags_b2
                                     : 0;
    profile->joint_count = grPs_StageData.joint_count;
    profile->joint_table_present = profile->joint_count == 0 ||
                                   grPs_StageData.joints != NULL;
    profile->collision_row_present = 0;
    if (grPs_StageData.joints != NULL) {
        for (size_t i = 0; i < grPs_StageData.joint_count; ++i) {
            if (grPs_StageData.joints[i].y == map_id) {
                profile->collision_row_present = 1;
                break;
            }
        }
    }
    return 1;
}

void* melee_web_stadium_c1_ground_map_lookup(int map_id)
{
    return Ground_GetMapGObj(map_id);
}

void* melee_web_stadium_c1_ground_map_create(int map_id)
{
    return Ground_GetStageGObj(map_id);
}

int melee_web_stadium_c1_ground_map_remove(void* object)
{
    if (object == NULL) return 0;
    Ground_801C4A08((HSD_GObj*) object);
    return 1;
}

void* melee_web_stadium_c1_ground_map_joint(void* object, int depth)
{
    if (object == NULL) return NULL;
    return Ground_801C3FA4((HSD_GObj*) object, depth);
}

int melee_web_stadium_c1_ground_map_object_view(
    void* user_data, MeleeWebStadiumC1GroundMapObjectView* view)
{
    if (user_data == NULL || view == NULL) return 0;
    Ground* ground = (Ground*) user_data;
    view->map_id = ground->map_id;
    view->gobj = ground->gobj;
    view->camera = ground->x18;
    return 1;
}

MeleeWebStadiumC1FtDeviceSnapshot*
melee_web_stadium_c1_ft_device_snapshot_create(void)
{
    MeleeWebStadiumC1FtDeviceSnapshot* snapshot =
        malloc(sizeof(*snapshot));
    if (snapshot == NULL) return NULL;
    memcpy(snapshot->first, ft_80459A68, sizeof(snapshot->first));
    memcpy(snapshot->bury_things, ftDevice_BuryThings,
           sizeof(snapshot->bury_things));
    memcpy(snapshot->third, ft_80459A8C, sizeof(snapshot->third));
    memcpy(&snapshot->fourth, &ft_804D6578, sizeof(snapshot->fourth));
    snapshot->first_count = ft_804D6570;
    snapshot->bury_thing_count = ftDevice_BuryThingCount;
    snapshot->addresses[0] = ft_80459A68;
    snapshot->addresses[1] = ftDevice_BuryThings;
    snapshot->addresses[2] = ft_80459A8C;
    snapshot->addresses[3] = &ft_804D6578;
    snapshot->addresses[4] = &ft_804D6570;
    snapshot->addresses[5] = &ftDevice_BuryThingCount;
    return snapshot;
}

int melee_web_stadium_c1_ft_device_snapshot_restore(
    const MeleeWebStadiumC1FtDeviceSnapshot* snapshot)
{
    if (snapshot == NULL) return 0;
    memcpy(ft_80459A68, snapshot->first, sizeof(snapshot->first));
    memcpy(ftDevice_BuryThings, snapshot->bury_things,
           sizeof(snapshot->bury_things));
    memcpy(ft_80459A8C, snapshot->third, sizeof(snapshot->third));
    memcpy(&ft_804D6578, &snapshot->fourth, sizeof(snapshot->fourth));
    ft_804D6570 = snapshot->first_count;
    ftDevice_BuryThingCount = snapshot->bury_thing_count;
    return melee_web_stadium_c1_ft_device_snapshot_matches(snapshot);
}

int melee_web_stadium_c1_ft_device_snapshot_matches(
    const MeleeWebStadiumC1FtDeviceSnapshot* snapshot)
{
    return snapshot != NULL &&
           memcmp(ft_80459A68, snapshot->first,
                  sizeof(snapshot->first)) == 0 &&
           memcmp(ftDevice_BuryThings, snapshot->bury_things,
                  sizeof(snapshot->bury_things)) == 0 &&
           memcmp(ft_80459A8C, snapshot->third,
                  sizeof(snapshot->third)) == 0 &&
           memcmp(&ft_804D6578, &snapshot->fourth,
                  sizeof(snapshot->fourth)) == 0 &&
           ft_804D6570 == snapshot->first_count &&
           ftDevice_BuryThingCount == snapshot->bury_thing_count;
}

int melee_web_stadium_c1_ft_device_snapshot_release(
    MeleeWebStadiumC1FtDeviceSnapshot* snapshot)
{
    if (snapshot == NULL) return 0;
    free(snapshot);
    return 1;
}

size_t melee_web_stadium_c1_ft_device_snapshot_addresses(
    const MeleeWebStadiumC1FtDeviceSnapshot* snapshot,
    const void** addresses, size_t capacity)
{
    if (snapshot == NULL || addresses == NULL ||
        capacity < sizeof(snapshot->addresses) / sizeof(snapshot->addresses[0]))
        return 0;
    memcpy(addresses, snapshot->addresses, sizeof(snapshot->addresses));
    return sizeof(snapshot->addresses) / sizeof(snapshot->addresses[0]);
}

#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
extern HSD_ObjAllocData gobj_alloc_data;
extern HSD_ObjAllocData gobjproc_alloc_data;
static int map_light_control_fail(char* error,size_t size,const char* message)
{
    if(error&&size&&message!=error)snprintf(error,size,"%s",message);
    return 0;
}
#define LIGHT_CONTROL_CHECK(condition,message) \
    do { if(!(condition))return map_light_control_fail(error,size,message); } while(0)
static int map_light_adoption_control(
    MeleeWebStadiumC1CacheLiveObserver observer,void* user,char* error,size_t size)
{
    if(!error||!size)return 0;
    /* Keep borrowed synthetic map/row storage alive even after a failed check.
     * No failed ownership graph is drained or silently detached by this control. */
    static struct StageInfo saved;
    static struct UnkStageDat_x8_t entry;
    static UnkStageDat map;
    static HSD_GObj foreign;
    static MeleeWebStageLights* context;
    static MeleeWebStageMap* publication;
    const MeleeWebGameplayStats before=melee_web_gameplay_stats();
    const int scheduler_before=HSD_GObj_804D783C;
    const uint32_t gobj_before=HSD_ObjAllocGetUsing(&gobj_alloc_data);
    const uint32_t proc_before=HSD_ObjAllocGetUsing(&gobjproc_alloc_data);
    saved=stage_info;
    LIGHT_CONTROL_CHECK(!Ground_801C498C()&&!stage_info.map_plit,
                        "Map-light reducer requires an unowned source baseline");
    LIGHT_CONTROL_CHECK(!melee_web_stage_lights_adopt_source(NULL,error,size)&&
        strcmp(error,"Source map-light adoption has no published descriptor context")==0,
        "Missing publication did not report its exact adoption condition");
    for(unsigned cycle=0;cycle<2;++cycle){
        LIGHT_CONTROL_CHECK(!observer||observer("ground-light",cycle?"warm":"cold",cycle,NULL,user),
                            "Map-light cold/warm snapshot refused");
        MeleeWebStageLightDesc descriptor={0};descriptor.flags=0x20;
        memset(descriptor.color,255,sizeof(descriptor.color));
        context=melee_web_stage_lights_create(&descriptor,1,error,size);
        LIGHT_CONTROL_CHECK(context!=NULL,error);
        LIGHT_CONTROL_CHECK(melee_web_stage_lights_set_override(context,0,0,0,error,size),error);
        LIGHT_CONTROL_CHECK(melee_web_stage_lights_attach(context,error,size),error);
        /* Synthetic callback row0 has no light flag: original Ground selects
         * its authored two-row static list, not a fabricated source owner. */
        memset(&entry,0,sizeof(entry));memset(&map,0,sizeof(map));
        map.unk8=&entry;map.unkC=1;
        publication=melee_web_stage_map_publish(&map,error,size);
        LIGHT_CONTROL_CHECK(publication!=NULL,error);
        const uint32_t row_count=1;
        LIGHT_CONTROL_CHECK(melee_web_stage_lights_set_source_counts(context,&row_count,1,error,size),error);
        stage_info.grkind=Gr_Kind_PStadium;stage_info.param=NULL;
        extern void melee_web_ground_load_map_lights(void);
        melee_web_ground_load_map_lights();
        HSD_GObj* const owner=Ground_801C498C();
        LIGHT_CONTROL_CHECK(owner&&owner->classifier==HSD_GOBJ_CLASS_GROUND&&owner->hsd_obj,
                            "Original Ground did not create its own source light owner");
        memset(&foreign,0,sizeof(foreign));foreign.classifier=HSD_GOBJ_CLASS_GROUND;
        LIGHT_CONTROL_CHECK(!melee_web_stage_lights_adopt_source(&foreign,error,size)&&
            strcmp(error,"Source map-light adoption requires Ground's current original owner")==0,
            "Foreign Ground-class owner was accepted");
        LightList** const published_list=stage_info.map_plit;
        stage_info.map_plit=NULL;
        LIGHT_CONTROL_CHECK(!melee_web_stage_lights_adopt_source(owner,error,size)&&
            strcmp(error,"Source map-light adoption descriptor publication was replaced")==0,
            "Replaced descriptor publication was accepted");
        LIGHT_CONTROL_CHECK(!melee_web_stage_lights_detach(context,error,size),
                            "Detach overwrote a replaced descriptor publication");
        stage_info.map_plit=published_list;
        LIGHT_CONTROL_CHECK(melee_web_stage_lights_select_source_entry(0),"Synthetic row selection failed");
        LIGHT_CONTROL_CHECK(!melee_web_stage_lights_adopt_source(owner,error,size)&&
            strcmp(error,"Original Ground light chain exceeds its selected DAT entry count")==0,
            "Wrong authored row bound did not refuse the actual chain");
        LIGHT_CONTROL_CHECK(melee_web_stage_lights_select_source_entry(-1),"Original static-list bound selection failed");
        LIGHT_CONTROL_CHECK(melee_web_stage_lights_adopt_source(owner,error,size),error);
        uint32_t count=0;uint16_t flags[2]={0};uint8_t colors[8]={0};
        LIGHT_CONTROL_CHECK(melee_web_stage_lights_stats(context,&count,flags,colors,2,error,size)&&count==2,
                            "Adopted original static-list chain differs from its authored bound");
        LIGHT_CONTROL_CHECK(!melee_web_stage_lights_adopt_source(owner,error,size),"Duplicate adoption succeeded");
        LIGHT_CONTROL_CHECK(!melee_web_stage_lights_detach(context,error,size),"Live source context detached before retirement");
        LIGHT_CONTROL_CHECK(!melee_web_stage_lights_destroy(context,error,size),"Live source context destroyed before retirement");
        LIGHT_CONTROL_CHECK(!melee_web_stage_lights_retire_source(&foreign,error,size),"Foreign owner retirement succeeded");
        LIGHT_CONTROL_CHECK(!observer||observer("ground-light","live",cycle,owner,user),
                            "Map-light live snapshot refused");
        LIGHT_CONTROL_CHECK(melee_web_stage_lights_retire_source(owner,error,size),error);
        HSD_GObjPLink_80390228(owner);
        LIGHT_CONTROL_CHECK(!Ground_801C498C(),"Original Ground owner survived its exact teardown");
        LIGHT_CONTROL_CHECK(melee_web_stage_lights_destroy(context,error,size),error);context=NULL;
        LIGHT_CONTROL_CHECK(melee_web_stage_map_close(publication,error,size),error);publication=NULL;
        stage_info=saved;
        LIGHT_CONTROL_CHECK(melee_web_gameplay_stats().generation==before.generation&&
            melee_web_gameplay_stats().ticks==before.ticks&&HSD_GObj_804D783C==scheduler_before&&
            HSD_ObjAllocGetUsing(&gobj_alloc_data)==gobj_before&&
            HSD_ObjAllocGetUsing(&gobjproc_alloc_data)==proc_before,
            "Map-light reducer advanced scheduling or retained original GObj/proc owners");
        LIGHT_CONTROL_CHECK(!observer||observer("ground-light","removed",cycle,NULL,user),
                            "Map-light removed snapshot refused");
    }
    if(error&&size)*error=0;
    return 1;
}

int melee_web_stadium_c1_map_light_adoption_control(char* error,size_t size)
{
    return map_light_adoption_control(NULL,NULL,error,size);
}
int melee_web_stadium_c1_cache_live_control(
    MeleeWebStadiumC1CacheLiveObserver observer,void* user,char* error,size_t size)
{
    /* Do not drain an unexpected graph. Retain its actual pointer on failure. */
    static HSD_JObj* owned;
    LIGHT_CONTROL_CHECK(observer&&error&&size&&!owned,
                        "Cache/live reducer requires a fresh owned control");
    for(unsigned cycle=0;cycle<2;++cycle){
        LIGHT_CONTROL_CHECK(observer("jobj",cycle?"warm":"cold",cycle,NULL,user),
                            "JObj cold/warm snapshot refused");
        owned=HSD_JObjAlloc();
        LIGHT_CONTROL_CHECK(owned!=NULL,"Original JObj allocation failed");
        LIGHT_CONTROL_CHECK(observer("jobj","live",cycle,owned,user),
                            "JObj live snapshot refused");
        HSD_JObjRemoveAll(owned);owned=NULL;
        LIGHT_CONTROL_CHECK(observer("jobj","removed",cycle,NULL,user),
                            "JObj removed snapshot refused");
    }
    return map_light_adoption_control(observer,user,error,size);
}
#undef LIGHT_CONTROL_CHECK
#endif

#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
/* A source-bound restoration fragment, not the full StageLast function.  The
 * focused test checks this assignment against the production restoration. */
static void queue_restore_stage_last_fragment(
    struct MeleeWebStadiumC1StageInfoSnapshot* h)
{
    stage_info=h->saved;
}

static unsigned queue_control_callback_count;
static void queue_control_callback(HSD_GObj* borrowed)
{
    (void) borrowed;
    ++queue_control_callback_count;
}

static void queue_control_require(int condition, const char* message)
{
    if (!condition) {
        fprintf(stderr, "GROUND_QUEUE_CONTROL failure=%s\n", message);
        fflush(stderr);
        _Exit(1); /* Never raw-shutdown a partially owned source graph. */
    }
}

int melee_web_stadium_c1_generator_lifetime_control(void)
{
 char error[256]={0};
 queue_control_require(melee_web_gameplay_startup(8U*1024U*1024U,error,sizeof(error)),error);
 queue_control_require(melee_web_native_world_enable(error,sizeof(error)),error);
 MeleeWebGameplayStats baseline=melee_web_gameplay_stats();
 size_t size=melee_web_stadium_zako_snapshot_size();
 void* before=malloc(size);void* after=malloc(size);
 queue_control_require(before&&after,"generator snapshot witness allocation");
 queue_control_require(melee_web_stadium_zako_snapshot_read(before,size),"initial private source root snapshot");
 for(unsigned lifetime=0;lifetime<2;++lifetime){
  MeleeWebStadiumGenerator* owner=melee_web_stadium_generator_prepare(error,sizeof(error));
  queue_control_require(owner!=NULL,error);
  /* These are actual original stage callbacks. Ground/Stage dispatch and map
   * callbacks remain outside this asset-free generator-owner control. */
  grStadium_OnLoad();grStadium_OnStart();
  queue_control_require(melee_web_stadium_generator_capture(owner,error,sizeof(error)),error);
  void *descs,*data;
  queue_control_require(melee_web_stadium_zako_view(&descs,&data)&&!descs&&data,"original NULL generator root");
  MeleeWebSourceMemoryAllocation lease;
  queue_control_require(melee_web_source_memory_allocation_read(data,&lease)==MELEE_WEB_SOURCE_MEMORY_READ_OK&&lease.live,"generator SDK lease");
  grZakoGenerator_Data* original=data;
  /* Borrowed item witness is never dereferenced or dispatched. The control
   * must refuse before freeing any source data/scheduler; restore afterward. */
  HSD_GObj* borrowed=GObj_Create(HSD_GOBJ_CLASS_STAGE,5,0);
  queue_control_require(borrowed!=NULL,"borrowed witness GObj");
  original->entries[ARRAY_SIZE(original->entries)-1].x4=(Item_GObj*)borrowed;
  MeleeWebGameplayStats refusal_before=melee_web_gameplay_stats();
  queue_control_require(!melee_web_stadium_generator_end(owner,error,sizeof(error))&&
                       strstr(error,"item borrowers")!=NULL,"live item borrower must refuse generator retirement");
  MeleeWebGameplayStats refusal_after=melee_web_gameplay_stats();
  queue_control_require(refusal_after.heap_free_bytes==refusal_before.heap_free_bytes&&
                       refusal_after.objects==refusal_before.objects&&
                       refusal_after.processes==refusal_before.processes,"refusal preserves source owners");
  original->entries[ARRAY_SIZE(original->entries)-1].x4=NULL;
  HSD_GObjPLink_80390228(borrowed);
  queue_control_require(melee_web_stadium_generator_end(owner,error,sizeof(error)),error);
  MeleeWebSourceMemoryAllocation retired;
  queue_control_require(melee_web_source_memory_allocation_read(data,&retired)==MELEE_WEB_SOURCE_MEMORY_READ_OK&&
                       !retired.live&&retired.allocation_generation==lease.allocation_generation,"exact generator SDK data retired");
  queue_control_require(melee_web_stadium_zako_snapshot_read(after,size)&&memcmp(before,after,size)==0,"original private roots restored");
  MeleeWebGameplayStats current=melee_web_gameplay_stats();
  queue_control_require(current.heap_free_bytes==baseline.heap_free_bytes&&
                       current.objects==baseline.objects&&
                       current.processes==baseline.processes,"generator two-lifetime exact heap/object/process return");
 }
 free(before);free(after);
 printf("STADIUM_GENERATOR_CONTROL lifetimes=2 heap_before=%d heap_after=%d borrowed_refusals=2 original_onload_onstart=1 ticks=0\n",baseline.heap_free_bytes,melee_web_gameplay_stats().heap_free_bytes);
 queue_control_require(melee_web_gameplay_shutdown(error,sizeof(error)),error);
 return 1;
}

int melee_web_stadium_c1_pending_queue_loss_control(void)
{
    char error[256] = {0};
    queue_control_require(melee_web_gameplay_startup(8U * 1024U * 1024U,
                                                   error, sizeof(error)), error);
    queue_control_require(melee_web_native_world_enable(error, sizeof(error)), error);
    queue_control_require(stage_info.x6A4 == NULL && active_snapshot == NULL,
                          "requires an unowned pending queue");
    struct MeleeWebStadiumC1StageInfoSnapshot snapshot = {stage_info, 0};
    HSD_GObj* borrowed[2] = {
        GObj_Create(HSD_GOBJ_CLASS_STAGE, 5, 0),
        GObj_Create(HSD_GOBJ_CLASS_STAGE, 5, 0),
    };
    queue_control_require(borrowed[0] && borrowed[1], "owned witness GObj allocation");
    HSD_GObj saved_gobjs[2];
    memcpy(&saved_gobjs[0], borrowed[0], sizeof(HSD_GObj));
    memcpy(&saved_gobjs[1], borrowed[1], sizeof(HSD_GObj));
    MeleeWebGameplayStats before = melee_web_gameplay_stats();
    MeleeWebSourceMemoryContext context;
    queue_control_require(melee_web_source_memory_context_read(&context) ==
                          MELEE_WEB_SOURCE_MEMORY_READ_OK, "source memory owner");
    struct PendingShape { void* next; HSD_GObj* gobj; HSD_GObjEvent callback; };
    void* payloads[2];
    MeleeWebSourceMemoryAllocation leases[2];
    queue_control_callback_count = 0;
    for (unsigned i = 0; i != 2; ++i) {
        Ground_801C10B8(borrowed[i], queue_control_callback);
        payloads[i] = stage_info.x6A4;
        queue_control_require(melee_web_source_memory_allocation_read(
            payloads[i], &leases[i]) == MELEE_WEB_SOURCE_MEMORY_READ_OK &&
            leases[i].live && leases[i].requested_bytes == sizeof(struct PendingShape) &&
            leases[i].source_heap_handle == context.source_heap_handle &&
            leases[i].world_generation == context.world_generation,
            "exact owned queue header lease");
        const struct PendingShape* node = payloads[i];
        queue_control_require(node->gobj == borrowed[i] &&
            node->callback == queue_control_callback &&
            node->next == (i ? payloads[0] : NULL), "actual enqueue LIFO and borrowed fields");
    }
    MeleeWebGameplayStats queued = melee_web_gameplay_stats();
    queue_control_require(queued.heap_free_bytes < before.heap_free_bytes,
                          "queue must have an observed SDK heap cost");
    queue_restore_stage_last_fragment(&snapshot);
    queue_control_require(stage_info.x6A4 == NULL && !queue_control_callback_count,
                          "partial OnInit restoration loses queue without callbacks");
    for (unsigned i = 0; i != 2; ++i) {
        MeleeWebSourceMemoryAllocation after;
        queue_control_require(melee_web_source_memory_allocation_read(payloads[i], &after) ==
            MELEE_WEB_SOURCE_MEMORY_READ_OK && after.live &&
            after.requested_bytes == leases[i].requested_bytes &&
            after.source_heap_handle == leases[i].source_heap_handle &&
            after.world_generation == leases[i].world_generation &&
            after.allocation_generation == leases[i].allocation_generation,
            "root loss leaves the exact original SDK header live");
        queue_control_require(memcmp(borrowed[i], &saved_gobjs[i], sizeof(HSD_GObj)) == 0,
                              "borrowed GObj changed during fragment");
    }
    MeleeWebGameplayStats lost = melee_web_gameplay_stats();
    queue_control_require(lost.heap_free_bytes == queued.heap_free_bytes &&
        lost.generation == before.generation && lost.ticks == before.ticks &&
        lost.objects == before.objects && lost.processes == before.processes &&
        melee_web_source_memory_healthy(), "unchanged partial source world after root loss");
    printf("GROUND_QUEUE_CONTROL scope=actual-SDK-enqueue-and-StageLast-assignment-fragment "
           "headers=2 requested_each=%zu free_before=%d free_queued=%d free_after_restore=%d "
           "observed_gap=%d callbacks=0 borrowed_gobjs_unchanged=1 "
           "expected_root_loss_reproduced=1 full_StageLast_executed=0 "
           "Stadium_OnStart_executed=0 raw_shutdown=0\n",
           sizeof(struct PendingShape), before.heap_free_bytes, queued.heap_free_bytes,
           lost.heap_free_bytes, before.heap_free_bytes - lost.heap_free_bytes);
    fflush(stdout);
    return 1;
}
#endif
