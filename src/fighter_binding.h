#ifndef MELEE_WEB_FIGHTER_BINDING_H
#define MELEE_WEB_FIGHTER_BINDING_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Generated source identity: 1 requires material animation, 0 is authored null,
 * -1 is an unknown fighter/costume. This does not admit a fighter to gameplay. */
int melee_web_fighter_costume_material_required(uint32_t kind, uint32_t costume);
#ifdef __cplusplus
}
#endif
#endif
