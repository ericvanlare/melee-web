#ifndef MELEE_WEB_DAT_ITEM_REGISTRY_H
#define MELEE_WEB_DAT_ITEM_REGISTRY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Source Item_80267978 selects [It_Kind_Kuriboh,It_PKind_Start). Native C
 * verifies these constants against the original enum at compile time. */
enum { MELEE_WEB_ITEM_REGISTRY_FIRST_KIND = 43, MELEE_WEB_ITEM_REGISTRY_COUNT = 118 };
typedef struct MeleeWebItemRegistry MeleeWebItemRegistry;
/* Inputs must be hydrated native Article pointers in exact source order. This
 * object owns the pointer table; the caller owns the Article graphs. */
MeleeWebItemRegistry* melee_web_item_registry_begin(void* const* articles,uint32_t count,char*,size_t);
int melee_web_item_registry_lookup(MeleeWebItemRegistry*,uint32_t kind,void** article,char*,size_t);
/* Restore after fighters/items using the table have been unloaded. */
int melee_web_item_registry_end(MeleeWebItemRegistry*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
