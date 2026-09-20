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
/* Publish fully checked borrowed item graphs into the existing registration
 * identity. All owners must outlive original item instances. */
typedef struct MeleeWebItemStateDesc {void* animation;void* material;void* shape;void* commands;} MeleeWebItemStateDesc;
/* joint_optional is reserved for a checked source model descriptor whose
 * x0_joint is authored null. It does not make a missing model descriptor
 * publishable, and its zero-bone/zero-attachment form is validated here. */
int melee_web_article_publish(const MeleeWebNativeDat*,void* article,void* special,
    const MeleeWebItemStateDesc*,uint32_t states,void* joint,uint32_t bones,int32_t attach,uint8_t flags,
    int joint_optional,char* error,size_t error_size);
uint32_t melee_web_article_unresolved(const void* article);
void melee_web_article_require_ready(const void* article);
#ifdef __cplusplus
}
#endif
#endif
