#ifndef MELEE_WEB_HSD_NATIVE_ARRAYS_H
#define MELEE_WEB_HSD_NATIVE_ARRAYS_H
#include "hsd_pobj_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebNativeArrays MeleeWebNativeArrays;
/* Copies validated indexed-array bounds; payload bytes remain owner-borrowed.
 * Keep registration and bytes alive through Aurora frame submission. */
MeleeWebNativeArrays* melee_web_native_arrays_register(const MeleeWebPObjAttribute*,uint32_t,char*,size_t);
void melee_web_native_arrays_remove(MeleeWebNativeArrays*);
int melee_web_native_array_bound(uint32_t attr,const void* data,uint32_t stride,uint32_t* bytes);
#ifdef __cplusplus
}
#endif
#endif
