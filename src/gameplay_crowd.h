#ifndef MELEE_WEB_GAMEPLAY_CROWD_H
#define MELEE_WEB_GAMEPLAY_CROWD_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Owns the original crowd manager created by gm_16AE's match startup. */
int melee_web_crowd_begin(char*, size_t);
int melee_web_crowd_active(void);
int melee_web_crowd_end(char*, size_t);
#ifdef __cplusplus
}
#endif
#endif
