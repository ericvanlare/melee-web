#ifndef MELEE_WEB_GAMEPLAY_FIGHTER_DATA_H
#define MELEE_WEB_GAMEPLAY_FIGHTER_DATA_H
#include "native_dat.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Returns source ftData owned by reader's arena. Action rows, blend rows and
 * Wait choices are borrowed from the action store. Unhydrated pointer fields
 * are reported by their ftData word index; callers must gate publication.
 * x48 readiness denotes owned registration roots only: each Article has a
 * separate creation mask queried by melee_web_article_unresolved. */
void* melee_web_fighter_data_decode(const MeleeWebNativeDat*, uint32_t root,
    uint32_t kind, uint32_t costume_count, void* actions, void* blends,
    void* wait_choices, uint32_t* unresolved_fields);
/* Borrow the checked native metal descriptor until every source Fighter is
 * destroyed. Validates category2 visibility indices against the real DObj
 * occurrence count before marking the constructor-reachable x5C field ready. */
int melee_web_fighter_data_set_metal(void* data,void* joint,uint32_t costumes,
    uint32_t dobj_count,uint32_t* unresolved,char* error,size_t error_size);
#ifdef __cplusplus
}
#endif
#endif
