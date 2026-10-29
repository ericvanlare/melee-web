#ifndef MELEE_WEB_RNG_DRAW_OWNER_H
#define MELEE_WEB_RNG_DRAW_OWNER_H
struct HSD_GObj;
#ifdef __cplusplus
extern "C" {
#endif
/* Immutable full-source capability. This names source callbacks; it does not
 * grant gameplay authority or attest allocation generations. */
typedef struct {
 void (*stadium_callback)(struct HSD_GObj*);
 void (*particle_callback)(struct HSD_GObj*);
 void (*fighter_callback)(struct HSD_GObj*);
 int (*fighter_membership)(const struct HSD_GObj*, unsigned*, unsigned*, unsigned*);
} MeleeWebRngDrawOwnerBindings;
void melee_web_rng_draw_probe_bind_source_owners(const MeleeWebRngDrawOwnerBindings* bindings);
void melee_web_rng_draw_probe_install_source_owners(void);
#ifdef __cplusplus
}
#endif
#endif
