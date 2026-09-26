#ifndef MELEE_WEB_GAMEPLAY_BOOTSTRAP_H
#define MELEE_WEB_GAMEPLAY_BOOTSTRAP_H

#include <stdint.h>

typedef struct MeleeWebGameplayStats {
    uint64_t generation;
} MeleeWebGameplayStats;

MeleeWebGameplayStats melee_web_gameplay_stats(void);
int melee_web_gameplay_world_exists(void);

#endif
