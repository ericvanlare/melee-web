#ifndef MELEE_WEB_GAMEPLAY_RETAIL_STATE_H
#define MELEE_WEB_GAMEPLAY_RETAIL_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Fighter;
struct HSD_GObj;

/* Read the existing exported primary-entity identity. Never advance its tracker. */
int melee_web_retail_primary_identity(unsigned slot, const struct HSD_GObj*,
    uint32_t* match_index, uint32_t* generation);

/* One declared Fighter field list shared by the retail JSON printer and the
 * networked per-tick checksum. Visiting only reads source state. Floats are
 * passed as their exact IEEE-754 bits; the input record is passed as its 20
 * native four-byte words followed by its 28 byte timers, never as raw struct
 * bytes. Fields are visited in the order the retail JSON stream declares. */
typedef struct MeleeWebFighterFieldVisitor {
    void (*unsigned_field)(void* context, const char* name, uint32_t value);
    void (*signed_field)(void* context, const char* name, int32_t value);
    void (*bits_field)(void* context, const char* name, uint32_t bits);
    void (*vector_field)(void* context, const char* name,
                         const uint32_t bits[3]);
    void (*input_field)(void* context, const char* name,
                        const uint32_t words[20], const uint8_t timers[28]);
} MeleeWebFighterFieldVisitor;

void melee_web_fighter_fields_visit(unsigned slot, const struct Fighter* fighter,
                                    const MeleeWebFighterFieldVisitor* visitor,
                                    void* context);

#ifdef __cplusplus
}
#endif

#endif
