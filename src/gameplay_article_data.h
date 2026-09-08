#ifndef MELEE_WEB_GAMEPLAY_ARTICLE_DATA_H
#define MELEE_WEB_GAMEPLAY_ARTICLE_DATA_H
#include "native_dat.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Registration-only typed Article roots. The reader owns all allocations.
 * A six-bit mask identifies source graph fields not hydrated for item creation.
 * Only roots returned here may be supplied to the native item registry. */
void* melee_web_article_decode(const MeleeWebNativeDat*, uint32_t root, uint32_t* unresolved);
uint32_t melee_web_article_unresolved(const void* article);
void melee_web_article_require_ready(const void* article);
#ifdef __cplusplus
}
#endif
#endif
