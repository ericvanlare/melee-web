#include "gameplay_fighter_attributes.h"
#include <melee/ft/ftchangeparam.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftMario/types.h>
#include <melee/ft/kinds/ftFox/types.h>
#include <melee/ft/kinds/ftCaptain/types.h>
#include <melee/ft/kinds/ftNess/types.h>
#include <melee/ft/kinds/ftPeach/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(void*) == 4, "Fighter hydration requires the checked Wasm32 ABI");
_Static_assert(sizeof(ftCo_DatAttrs) == 0x184, "Original co attribute size");
_Static_assert(sizeof(ftMario_DatAttrs) == 0x84, "Original Mario attribute size");
_Static_assert(sizeof(ftFox_DatAttrs) == 0xD4, "Original Fox/Falco attribute size");
_Static_assert(sizeof(ftCaptain_DatAttrs) == 0x8C, "Original Captain/Ganon attribute size");
_Static_assert(sizeof(ftNessAttributes) == 0xDC, "Original Ness attribute size");
_Static_assert(sizeof(ftPe_DatAttrs) == 0xC0, "Original Peach attribute size");
_Static_assert(sizeof(itPickup) == 0x30, "Original item pickup size");
_Static_assert(sizeof(ftData) == 0x60, "Original fighter data size");
_Static_assert(sizeof(ftHurtboxInit) == 40 && offsetof(ftHurtboxInit, scale) == 36 &&
               offsetof(ftHurtboxInit, a_offset) == 12 && offsetof(ftHurtboxInit, b_offset) == 24,
               "Original hurtbox descriptor layout");
_Static_assert(sizeof(ftDynamics) == 20 && offsetof(ftDynamics, x4) == 8 &&
               offsetof(ftDynamics, x8) == 12 && offsetof(ftDynamics, x10) == 16,
               "Original dynamics descriptor layout");
_Static_assert(sizeof(BoneDynamicsDesc) == 24 && sizeof(struct ftData_x38) == 20 &&
               sizeof(struct lb_00F9_UnkDesc1Inner) == 60,
               "Original dynamics record layouts");
_Static_assert(sizeof(struct Fighter_WaitAnimData) == 24, "Original action record size");
_Static_assert(offsetof(ftData, x0) == 0 && offsetof(ftData, ext_attr) == 4 &&
               offsetof(ftData, xC) == 12 && offsetof(ftData, x40) == 0x40 &&
               offsetof(ftData, x50) == 0x50, "Original consumed ftData members");
_Static_assert(offsetof(Fighter, co_attrs) == 0x110 && offsetof(Fighter, x294_itPickup) == 0x294 &&
               offsetof(Fighter, x2C4) == 0x2c4, "Original copied Fighter members");
#define CHECK_FIELD(source_type, portable_type, at, type, name, original) \
    _Static_assert(offsetof(source_type, original) == at, "Original attribute offset: " #original); \
    _Static_assert(sizeof(((source_type*) 0)->original) == sizeof(MELEE_WEB_ATTRIBUTE_TYPE_##type), \
                   "Original attribute width: " #original); \
    _Static_assert(offsetof(portable_type, name) == at, "Portable attribute offset: " #name);
#define CHECK_CO(at, type, name, original) CHECK_FIELD(ftCo_DatAttrs, MeleeWebCoAttributes, at, type, name, original)
MELEE_WEB_CO_ATTRIBUTE_FIELDS(CHECK_CO)
#define CHECK_MARIO(at, type, name, original) CHECK_FIELD(ftMario_DatAttrs, MeleeWebMarioAttributes, at, type, name, original)
MELEE_WEB_MARIO_ATTRIBUTE_FIELDS(CHECK_MARIO)
#define CHECK_CAPTAIN(at, type, name, original) CHECK_FIELD(ftCaptain_DatAttrs, MeleeWebCaptainAttributes, at, type, name, original)
MELEE_WEB_CAPTAIN_ATTRIBUTE_FIELDS(CHECK_CAPTAIN)
#define CHECK_FOX(at, type, name, original) CHECK_FIELD(ftFox_DatAttrs, MeleeWebFoxAttributes, at, type, name, original)
MELEE_WEB_FOX_ATTRIBUTE_FIELDS(CHECK_FOX)
#define CHECK_NESS(at, type, name, original) CHECK_FIELD(ftNessAttributes, MeleeWebNessAttributes, at, type, name, original)
MELEE_WEB_NESS_ATTRIBUTE_FIELDS(CHECK_NESS)
#define CHECK_PEACH(at, type, name, original) CHECK_FIELD(ftPe_DatAttrs, MeleeWebPeachAttributes, at, type, name, original)
MELEE_WEB_PEACH_ATTRIBUTE_FIELDS(CHECK_PEACH)
#define CHECK_PICKUP(at, type, name, original) CHECK_FIELD(itPickup, MeleeWebItemPickup, at, type, name, original)
MELEE_WEB_PICKUP_ATTRIBUTE_FIELDS(CHECK_PICKUP)
#undef CHECK_CO
#undef CHECK_MARIO
#undef CHECK_CAPTAIN
#undef CHECK_FOX
#undef CHECK_NESS
#undef CHECK_PEACH
#undef CHECK_PICKUP
#undef CHECK_FIELD

static int invalid(char* error, size_t size, const char* message)
{
    if (error != NULL && size != 0) snprintf(error, size, "%s", message);
    return 0;
}
int melee_web_fighter_copy_base_attributes(const MeleeWebFighterBaseAttributes* input,
                                           MeleeWebFighterBaseAttributes* output,
                                           char* error, size_t error_size)
{
    ftCo_DatAttrs co = {0};
    itPickup pickup = {0};
    Vec2 x50 = {0};
    ftData data = {0};
    Fighter fighter = {0};
    HSD_GObj object = {0};
    MeleeWebFighterBaseAttributes result = {0};
    if (!input || !output) return invalid(error, error_size, "Fighter attribute input/output is null");
#define VALID_F32(value) isfinite(value)
#define VALID_I32(value) 1
#define VALID_U32(value) 1
#define VALID_U8(value) 1
#define COPY_CO(at, type, name, original) \
    if (!VALID_##type(input->co.name)) return invalid(error, error_size, "Nonfinite fighter attribute: " #name); \
    co.original = input->co.name;
    MELEE_WEB_CO_ATTRIBUTE_FIELDS(COPY_CO)
#define COPY_PICKUP(at, type, name, original) \
    if (!VALID_##type(input->pickup.name)) return invalid(error, error_size, "Nonfinite pickup attribute: " #name); \
    pickup.original = input->pickup.name;
    MELEE_WEB_PICKUP_ATTRIBUTE_FIELDS(COPY_PICKUP)
#undef COPY_CO
#undef COPY_PICKUP
#undef VALID_F32
#undef VALID_I32
#undef VALID_U32
#undef VALID_U8
    if (!isfinite(input->x2c4_x) || !isfinite(input->x2c4_y))
        return invalid(error, error_size, "Nonfinite fighter x50 vector");
    x50.x = input->x2c4_x; x50.y = input->x2c4_y;
    data.x0 = &co; data.x40 = &pickup; data.x50 = &x50;
    fighter.ft_data = &data;
    object.user_data = &fighter;
    ftCo_800D0FA0(&object);
#define READ_CO(at, type, name, original) result.co.name = fighter.co_attrs.original;
    MELEE_WEB_CO_ATTRIBUTE_FIELDS(READ_CO)
#define READ_PICKUP(at, type, name, original) result.pickup.name = fighter.x294_itPickup.original;
    MELEE_WEB_PICKUP_ATTRIBUTE_FIELDS(READ_PICKUP)
#undef READ_CO
#undef READ_PICKUP
    result.x2c4_x = fighter.x2C4.x; result.x2c4_y = fighter.x2C4.y;
    *output = result;
    if (error != NULL && error_size != 0) error[0] = '\0';
    return 1;
}
