#ifndef MELEE_WEB_GAMEPLAY_BONUS_DATA_H
#define MELEE_WEB_GAMEPLAY_BONUS_DATA_H
#include "native_dat.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebBonusData MeleeWebBonusData;
/* PdPm.dat / plLoadCommonData is a pointer root. Owns the complete source
 * threshold structure in the reader arena, which must outlive the scope. */
MeleeWebBonusData* melee_web_bonus_data_decode(const MeleeWebNativeDat*,uint32_t root);
/* Pointer-root public symbol for PdPm.dat/plLoadCommonData. Returns the
 * address of a stable pointer slot whose value is the decoded thresholds.
 * The decoded owner must outlive every source archive scope that publishes it. */
void* melee_web_bonus_data_public_data(MeleeWebBonusData*);
int melee_web_bonus_data_begin(MeleeWebBonusData*,char*,size_t);
int melee_web_bonus_data_ready(const MeleeWebBonusData*);
int melee_web_bonus_data_end(MeleeWebBonusData*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
