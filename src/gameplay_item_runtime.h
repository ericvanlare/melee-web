#ifndef MELEE_WEB_GAMEPLAY_ITEM_RUNTIME_H
#define MELEE_WEB_GAMEPLAY_ITEM_RUNTIME_H
#include "native_dat.h"
#include "gameplay_common_context.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebItemRuntime MeleeWebItemRuntime;
/* Decode the complete scalar ItemCommonData and bounce parameters. Archive
 * loading is supplied by the host; original Item_80266FCC initializes services. */
void* melee_web_item_common_decode(const MeleeWebNativeDat*,uint32_t root);
void* melee_web_item_bounce_decode(const MeleeWebNativeDat*,uint32_t root);
MeleeWebItemRuntime* melee_web_item_runtime_begin(void* common,void* bounce,const MeleeWebColorRow*,size_t,char*,size_t);
int melee_web_item_runtime_end(MeleeWebItemRuntime*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
