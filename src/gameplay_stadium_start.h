#ifndef MELEE_WEB_GAMEPLAY_STADIUM_START_H
#define MELEE_WEB_GAMEPLAY_STADIUM_START_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
typedef struct MeleeWebStadiumGenerator MeleeWebStadiumGenerator;
/* Prepare/capture bracket the untouched original Stadium OnStart.  The caller
 * retains this owner on capture/refusal; end refuses live item borrowers. */
MeleeWebStadiumGenerator* melee_web_stadium_generator_prepare(char*,size_t);
int melee_web_stadium_generator_capture(MeleeWebStadiumGenerator*,char*,size_t);
int melee_web_stadium_generator_preflight(MeleeWebStadiumGenerator*,char*,size_t);
int melee_web_stadium_generator_end(MeleeWebStadiumGenerator*,char*,size_t);
/* Source-local bridge: exact private bytes, never a client-side mirrored ABI. */
size_t melee_web_stadium_zako_snapshot_size(void);
int melee_web_stadium_zako_snapshot_read(void*,size_t);
int melee_web_stadium_zako_view(void** descs,void** data);
int melee_web_stadium_zako_restore(const void*,size_t,const void* owned_data);
#endif
#ifdef __cplusplus
}
#endif
#endif
