#ifndef MELEE_WEB_GAMEPLAY_RUMBLE_H
#define MELEE_WEB_GAMEPLAY_RUMBLE_H
#include "native_dat.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebRumble MeleeWebRumble;
/* Own decoded LbRb command words; hardware output remains Aurora's PAD owner. */
MeleeWebRumble* melee_web_rumble_decode(const MeleeWebNativeDat*, uint32_t root,
                                       unsigned rows);
int melee_web_rumble_begin(MeleeWebRumble*, char*, size_t);
int melee_web_rumble_end(MeleeWebRumble*, char*, size_t);
/* Typed source row graph passed to lbArchive_80017040 for LbRb.dat. */
void* melee_web_rumble_source_rows(MeleeWebRumble*);
/* Clear the source manager's borrowed global after its world has shut down. */
int melee_web_rumble_clear_source(MeleeWebRumble*, char*, size_t);
int melee_web_rumble_clear_source_rows(void* expected_rows, char*, size_t);
#ifdef __cplusplus
}
#endif
#endif
