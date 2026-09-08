#ifndef MELEE_WEB_GAMEPLAY_GROUND_DATA_H
#define MELEE_WEB_GAMEPLAY_GROUND_DATA_H
#include "native_dat.h"
#ifdef __cplusplus
extern "C" {
#endif
void* melee_web_ground_data_decode(const MeleeWebNativeDat*, uint32_t root);
/* Returns prior source parameter pointer. Caller retains both owners and restores
 * it after all stage/fighter consumers have been destroyed. */
void* melee_web_ground_data_publish(void* decoded);
#ifdef __cplusplus
}
#endif
#endif
