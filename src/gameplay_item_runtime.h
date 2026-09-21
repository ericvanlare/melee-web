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
/* Hydrate the complete six-field itPublicData root. The character Article
 * table is borrowed from the existing native item registry; common and
 * Pokemon/related Article rows are decoded into the supplied arena. */
void* melee_web_item_public_data_decode(const MeleeWebNativeDat*,uint32_t root,
    void* const* character_articles,uint32_t character_count);
MeleeWebItemRuntime* melee_web_item_runtime_begin(void* common,void* bounce,const MeleeWebColorRow*,size_t,char*,size_t);
/* Attach checked data before a scene's original Item_80266FCC call. */
MeleeWebItemRuntime* melee_web_item_runtime_prepare(void* common,void* bounce,const MeleeWebColorRow*,size_t,char*,size_t);
/* Attach a complete source itPublicData root before Item_80266FA8/FCC. */
MeleeWebItemRuntime* melee_web_item_runtime_prepare_source(void*,const MeleeWebColorRow*,size_t,char*,size_t);
MeleeWebItemRuntime* melee_web_item_runtime_begin_source(void*,const MeleeWebColorRow*,size_t,char*,size_t);
int melee_web_item_runtime_end(MeleeWebItemRuntime*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
