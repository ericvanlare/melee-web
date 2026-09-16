#ifndef MELEE_WEB_GAMEPLAY_FIGHTER_DATA_H
#define MELEE_WEB_GAMEPLAY_FIGHTER_DATA_H
#include "native_dat.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Returns source ftData owned by reader's arena. Action rows, blend rows and
 * Wait choices are borrowed from the action store; motion_count bounds the
 * borrowed blend rows while deriving any dynamics mode table extent.
 * Unhydrated pointer fields are reported by their ftData word index; callers must gate publication.
 * x48 readiness denotes owned registration roots only: each Article has a
 * separate creation mask queried by melee_web_article_unresolved. */
void* melee_web_fighter_data_decode(const MeleeWebNativeDat*, uint32_t root,
    uint32_t kind, uint32_t costume_count, uint32_t motion_count,
    void* actions, void* blends,
    void* wait_choices, uint32_t* unresolved_fields);
/* Return one of the four source Article identities retained in ftData x48.
 * The returned Article is allocated by the same reader arena as the ftData
 * root, so callers must keep that arena alive while the source Fighter runs. */
void* melee_web_fighter_data_article(void* data, uint32_t index);
/* Publish the owned guard pose descriptor; source x0[2] aliases Joint.child.
 * The descriptor owner must outlive source fighters, like the costume owner. */
void melee_web_fighter_data_set_guard(const MeleeWebNativeDat*,uint32_t root,
    void* data,void* joint,uint32_t* unresolved);
int melee_web_fighter_data_set_part_animations(void* data,void* groups,
    uint32_t group_count,uint32_t* unresolved,char* error,size_t error_size);
/* Borrow the checked native metal descriptor until every source Fighter is
 * destroyed. Validates category2 visibility indices against the real DObj
 * occurrence count before marking the constructor-reachable x5C field ready. */
int melee_web_fighter_data_set_metal(void* data,void* joint,uint32_t costumes,
    uint32_t dobj_count,uint32_t* unresolved,char* error,size_t error_size);
#ifdef __cplusplus
}
#endif
#endif
