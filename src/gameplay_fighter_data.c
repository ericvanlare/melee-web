#include "gameplay_fighter_data.h"
#include "fighter_attributes.h"
#include "gameplay_donkey_schema.h"
#include "gameplay_koopa_schema.h"
#include "gameplay_mewtwo_schema.h"
#include "gameplay_pikachu_schema.h"
#include "gameplay_purin_schema.h"
#include "gameplay_article_data.h"
#include <melee/ft/types.h>
#include <melee/ft/ftwaitanim.h>
#include <melee/ft/kinds/ftMario/types.h>
#include <melee/ft/kinds/ftLuigi/types.h>
#include <melee/ft/kinds/ftDonkey/types.h>
#include <melee/ft/kinds/ftKoopa/types.h>
#include <melee/ft/kinds/ftMewtwo/types.h>
#include <melee/ft/kinds/ftPikachu/types.h>
#include <melee/ft/kinds/ftPichu/types.h>
#include <melee/ft/kinds/ftPurin/types.h>
#include <melee/ft/kinds/ftFox/types.h>
#include <melee/ft/kinds/ftCaptain/types.h>
#include <melee/ft/kinds/ftMars/types.h>
#include <melee/ft/kinds/ftLink/types.h>
#include <melee/ft/kinds/ftNess/types.h>
#include <melee/ft/kinds/ftPeach/types.h>
#include <melee/ft/kinds/ftGameWatch/types.h>
#include <melee/ft/kinds/ftKirby/types.h>
#include <melee/ft/kinds/ftPopo/types.h>
#include <melee/ft/kinds/ftSamus/types.h>
#include <melee/ft/kinds/ftYoshi/types.h>
#include <melee/ft/kinds/ftZelda/types.h>
#include <melee/ft/kinds/ftSeak/types.h>
#include <melee/ft/dobjlist.h>
#include <sysdolphin/baselib/jobj.h>
#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(ftData) == 0x60 && sizeof(void*) == 4, "Native fighter ABI");
typedef struct Counted { uint32_t count; void* data; } Counted;
_Static_assert(sizeof(Counted) == 8, "Visibility descriptor ABI");
_Static_assert(sizeof(ftLk_DatAttrs) == 0xDC, "Link extension ABI");
_Static_assert(sizeof(ftNessAttributes) == 0xDC, "Ness extension ABI");
_Static_assert(sizeof(ftPe_DatAttrs) == 0xC0, "Peach extension ABI");
_Static_assert(sizeof(ftCaptain_DatAttrs) == 0x8C, "Captain/Ganon extension ABI");
_Static_assert(sizeof(ftLuigiAttributes) == MELEE_WEB_LUIGI_ATTRIBUTE_BYTES, "Luigi extension ABI");
_Static_assert(sizeof(ftDonkeyAttributes) == MELEE_WEB_DONKEY_ATTRIBUTE_BYTES, "Donkey extension ABI");
_Static_assert(sizeof(ftKoopaAttributes) == MELEE_WEB_KOOPA_ATTRIBUTE_BYTES, "Koopa extension ABI");
_Static_assert(sizeof(ftMewtwoAttributes) == MELEE_WEB_MEWTWO_ATTRIBUTE_BYTES, "Mewtwo extension ABI");
_Static_assert(sizeof(ftPikachuAttributes) == MELEE_WEB_PIKACHU_ATTRIBUTE_BYTES,
               "Pikachu/Pichu shared extension ABI");
_Static_assert(sizeof(ftPurinAttributes) == MELEE_WEB_PURIN_ATTRIBUTE_BYTES,
               "Purin extension ABI");
_Static_assert(sizeof(ftGameWatchAttributes) == 0x94, "Game & Watch extension ABI");
_Static_assert(offsetof(ftGameWatchAttributes, x4_GAMEWATCH_COLOR) == 0x04 &&
               offsetof(ftGameWatchAttributes, x14_GAMEWATCH_OUTLINE) == 0x14 &&
               offsetof(ftGameWatchAttributes, x34_GAMEWATCH_JUDGE_ROLL) == 0x34 &&
               offsetof(ftGameWatchAttributes, x80_GAMEWATCH_PANIC_ABSORPTION) == 0x80,
               "Game & Watch extension field offsets");
_Static_assert(sizeof(struct ftKb_DatAttrs) == 0x424 &&
               offsetof(struct ftKb_DatAttrs, jumpaerial_unk) == 0x34 &&
               offsetof(struct ftKb_DatAttrs, specialn_pe_absorbdesc) == 0x3EC &&
               offsetof(struct ftKb_DatAttrs, specialn_zd_reflectdesc) == 0x400 &&
               __builtin_offsetof(struct ftKb_DatAttrs, specialn_zd_reflectdesc.x20_behavior) == 0x420,
               "Kirby extension ABI and mixed-width fields");
_Static_assert(sizeof(ftIceClimberAttributes) == 0x15C,
               "Ice Climbers extension ABI");
_Static_assert(sizeof(ftSs_DatAttrs) == MELEE_WEB_SAMUS_ATTRIBUTE_BYTES,
               "Samus extension ABI");
_Static_assert(sizeof(ftYoshiAttributes) == MELEE_WEB_YOSHI_ATTRIBUTE_BYTES &&
               sizeof(struct ftYs_DatAttrs) == 0x120,
               "Yoshi extension and source overlay ABI");
_Static_assert(sizeof(ftZelda_DatAttrs) == 0xA8, "Zelda source extension ABI");
_Static_assert(sizeof(ftSeakAttributes) == 0x74, "Sheik source extension ABI");
#define CHECK_YOSHI_PORTABLE(offset,type,name,original) \
    _Static_assert(offsetof(MeleeWebYoshiAttributes, name) == offset, \
                   "Yoshi portable field offset");
MELEE_WEB_YOSHI_ATTRIBUTE_FIELDS(CHECK_YOSHI_PORTABLE)
#undef CHECK_YOSHI_PORTABLE
_Static_assert(offsetof(ftYoshiAttributes, x0) == 0x00 &&
               offsetof(ftYoshiAttributes, x38) == 0x38 &&
               offsetof(ftYoshiAttributes, x3C) == 0x3C &&
               offsetof(ftYoshiAttributes, x48) == 0x48 &&
               offsetof(ftYoshiAttributes, xA4) == 0xA4 &&
               offsetof(ftYoshiAttributes, xDC) == 0xDC &&
               offsetof(ftYoshiAttributes, pad_xEC) == 0xEC &&
               offsetof(struct ftYs_DatAttrs, specialhi_base_angle) == 0xF8 &&
               offsetof(struct ftYs_DatAttrs, speciallw_star_offset) == 0x118,
               "Yoshi source attribute overlay offsets");
#define CHECK_SAMUS(offset,type,name,original) \
    _Static_assert(offsetof(ftSs_DatAttrs, original) == offset, "Samus source field offset"); \
    _Static_assert(offsetof(MeleeWebSamusAttributes, name) == offset, "Samus portable field offset"); \
    _Static_assert(sizeof(((ftSs_DatAttrs*)0)->original) == 4, "Samus source field width"); \
    _Static_assert(sizeof(((MeleeWebSamusAttributes*)0)->name) == 4, "Samus portable field width");
MELEE_WEB_SAMUS_ATTRIBUTE_FIELDS(CHECK_SAMUS)
#undef CHECK_SAMUS
#define CHECK_PIKACHU_SOURCE_F32(value) _Generic((value), float: 1, default: 0)
#define CHECK_PIKACHU_SOURCE_I32(value) \
    _Generic((value), signed char: (sizeof(value) == sizeof(int32_t)), \
             short: (sizeof(value) == sizeof(int32_t)), \
             int: (sizeof(value) == sizeof(int32_t)), \
             long: (sizeof(value) == sizeof(int32_t)), \
             long long: (sizeof(value) == sizeof(int32_t)), default: 0)
#define CHECK_PIKACHU_SOURCE_U32(value) \
    _Generic((value), unsigned char: (sizeof(value) == sizeof(uint32_t)), \
             unsigned short: (sizeof(value) == sizeof(uint32_t)), \
             unsigned int: (sizeof(value) == sizeof(uint32_t)), \
             unsigned long: (sizeof(value) == sizeof(uint32_t)), \
             unsigned long long: (sizeof(value) == sizeof(uint32_t)), default: 0)
#define CHECK_PIKACHU_SOURCE_ITEM(value) _Generic((value), ItemKind: 1, default: 0)
#define CHECK_PIKACHU_PORTABLE_F32(value) _Generic((value), float: 1, default: 0)
#define CHECK_PIKACHU_PORTABLE_I32(value) _Generic((value), int32_t: 1, default: 0)
#define CHECK_PIKACHU_PORTABLE_U32(value) _Generic((value), uint32_t: 1, default: 0)
#define CHECK_PIKACHU_PORTABLE_ITEM(value) _Generic((value), int32_t: 1, default: 0)
#define CHECK_PIKACHU_PORTABLE_TYPE(type, value) CHECK_PIKACHU_PORTABLE_TYPE_IMPL(type, value)
#define CHECK_PIKACHU_PORTABLE_TYPE_IMPL(type, value) CHECK_PIKACHU_PORTABLE_##type(value)
#define CHECK_PIKACHU_SOURCE_TYPE(type, value) CHECK_PIKACHU_SOURCE_TYPE_IMPL(type, value)
#define CHECK_PIKACHU_SOURCE_TYPE_IMPL(type, value) CHECK_PIKACHU_SOURCE_##type(value)
#define CHECK_PIKACHU(offset,type,name,original,component,source) \
    _Static_assert(offsetof(ftPikachuAttributes, original) + component == offset, \
                   "Pikachu source attribute offset"); \
    _Static_assert(offsetof(MeleeWebPikachuAttributes, name) == offset, \
                   "Pikachu portable attribute offset"); \
    _Static_assert(sizeof(((ftPikachuAttributes*)0)->source) == \
                   sizeof(MELEE_WEB_PIKACHU_TYPE_##type), \
                   "Pikachu source attribute width"); \
    _Static_assert(sizeof(((MeleeWebPikachuAttributes*)0)->name) == \
                   sizeof(MELEE_WEB_PIKACHU_TYPE_##type), \
                   "Pikachu portable attribute width"); \
    _Static_assert(CHECK_PIKACHU_SOURCE_TYPE(type, ((ftPikachuAttributes*)0)->source), \
                   "Pikachu source attribute type"); \
    _Static_assert(CHECK_PIKACHU_PORTABLE_TYPE(type, ((MeleeWebPikachuAttributes*)0)->name), \
                   "Pikachu portable attribute type");
MELEE_WEB_PIKACHU_ATTRIBUTE_FIELDS(CHECK_PIKACHU)
#undef CHECK_PIKACHU
#undef CHECK_PIKACHU_SOURCE_TYPE_IMPL
#undef CHECK_PIKACHU_SOURCE_TYPE
#undef CHECK_PIKACHU_PORTABLE_TYPE_IMPL
#undef CHECK_PIKACHU_PORTABLE_TYPE
#undef CHECK_PIKACHU_PORTABLE_U32
#undef CHECK_PIKACHU_PORTABLE_I32
#undef CHECK_PIKACHU_PORTABLE_F32
#undef CHECK_PIKACHU_PORTABLE_ITEM
#undef CHECK_PIKACHU_SOURCE_ITEM
#undef CHECK_PIKACHU_SOURCE_U32
#undef CHECK_PIKACHU_SOURCE_I32
#undef CHECK_PIKACHU_SOURCE_F32
#define CHECK_LUIGI_SOURCE_F32(value) _Generic((value), float: 1, default: 0)
#define CHECK_LUIGI_SOURCE_I32(value) \
    _Generic((value), signed char: (sizeof(value) == sizeof(int32_t)), \
             short: (sizeof(value) == sizeof(int32_t)), \
             int: (sizeof(value) == sizeof(int32_t)), \
             long: (sizeof(value) == sizeof(int32_t)), \
             long long: (sizeof(value) == sizeof(int32_t)), default: 0)
#define CHECK_LUIGI_PORTABLE_F32(value) _Generic((value), float: 1, default: 0)
#define CHECK_LUIGI_PORTABLE_I32(value) _Generic((value), int32_t: 1, default: 0)
#define CHECK_LUIGI_SOURCE_TYPE(type, value) CHECK_LUIGI_SOURCE_TYPE_IMPL(type, value)
#define CHECK_LUIGI_SOURCE_TYPE_IMPL(type, value) CHECK_LUIGI_SOURCE_##type(value)
#define CHECK_LUIGI_PORTABLE_TYPE(type, value) CHECK_LUIGI_PORTABLE_TYPE_IMPL(type, value)
#define CHECK_LUIGI_PORTABLE_TYPE_IMPL(type, value) CHECK_LUIGI_PORTABLE_##type(value)
#define CHECK_LUIGI(offset,type,name,original) \
    _Static_assert(offsetof(ftLuigiAttributes, original) == offset, "Luigi source attribute offset"); \
    _Static_assert(offsetof(MeleeWebLuigiAttributes, name) == offset, "Luigi portable attribute offset"); \
    _Static_assert(sizeof(((ftLuigiAttributes*)0)->original) == sizeof(MELEE_WEB_LUIGI_TYPE_##type), "Luigi source attribute width"); \
    _Static_assert(sizeof(((MeleeWebLuigiAttributes*)0)->name) == sizeof(MELEE_WEB_LUIGI_TYPE_##type), "Luigi portable attribute width"); \
    _Static_assert(CHECK_LUIGI_SOURCE_TYPE(type, ((ftLuigiAttributes*)0)->original), "Luigi source attribute type"); \
    _Static_assert(CHECK_LUIGI_PORTABLE_TYPE(type, ((MeleeWebLuigiAttributes*)0)->name), "Luigi portable attribute type");
MELEE_WEB_LUIGI_ATTRIBUTE_FIELDS(CHECK_LUIGI)
#undef CHECK_LUIGI
#undef CHECK_LUIGI_PORTABLE_TYPE_IMPL
#undef CHECK_LUIGI_PORTABLE_TYPE
#undef CHECK_LUIGI_SOURCE_TYPE_IMPL
#undef CHECK_LUIGI_SOURCE_TYPE
#undef CHECK_LUIGI_PORTABLE_I32
#undef CHECK_LUIGI_PORTABLE_F32
#undef CHECK_LUIGI_SOURCE_I32
#undef CHECK_LUIGI_SOURCE_F32
#define CHECK_DONKEY_SOURCE_F32(value) _Generic((value), float: 1, default: 0)
#define CHECK_DONKEY_SOURCE_I32(value) \
    _Generic((value), signed char: (sizeof(value) == sizeof(int32_t)), \
             short: (sizeof(value) == sizeof(int32_t)), \
             int: (sizeof(value) == sizeof(int32_t)), \
             long: (sizeof(value) == sizeof(int32_t)), \
             long long: (sizeof(value) == sizeof(int32_t)), default: 0)
#define CHECK_DONKEY_PORTABLE_F32(value) _Generic((value), float: 1, default: 0)
#define CHECK_DONKEY_PORTABLE_I32(value) _Generic((value), int32_t: 1, default: 0)
#define CHECK_DONKEY_SOURCE_TYPE(type, value) CHECK_DONKEY_SOURCE_TYPE_IMPL(type, value)
#define CHECK_DONKEY_SOURCE_TYPE_IMPL(type, value) CHECK_DONKEY_SOURCE_##type(value)
#define CHECK_DONKEY_PORTABLE_TYPE(type, value) CHECK_DONKEY_PORTABLE_TYPE_IMPL(type, value)
#define CHECK_DONKEY_PORTABLE_TYPE_IMPL(type, value) CHECK_DONKEY_PORTABLE_##type(value)
#define CHECK_DONKEY(offset,type,name,original) \
    _Static_assert(offsetof(ftDonkeyAttributes, original) == offset, "Donkey source attribute offset"); \
    _Static_assert(offsetof(MeleeWebDonkeyAttributes, name) == offset, "Donkey portable attribute offset"); \
    _Static_assert(sizeof(((ftDonkeyAttributes*)0)->original) == sizeof(MELEE_WEB_DONKEY_TYPE_##type), "Donkey source attribute width"); \
    _Static_assert(sizeof(((MeleeWebDonkeyAttributes*)0)->name) == sizeof(MELEE_WEB_DONKEY_TYPE_##type), "Donkey portable attribute width"); \
    _Static_assert(CHECK_DONKEY_SOURCE_TYPE(type, ((ftDonkeyAttributes*)0)->original), "Donkey source attribute type"); \
    _Static_assert(CHECK_DONKEY_PORTABLE_TYPE(type, ((MeleeWebDonkeyAttributes*)0)->name), "Donkey portable attribute type");
MELEE_WEB_DONKEY_ATTRIBUTE_FIELDS(CHECK_DONKEY)
#undef CHECK_DONKEY
#undef CHECK_DONKEY_PORTABLE_TYPE_IMPL
#undef CHECK_DONKEY_PORTABLE_TYPE
#undef CHECK_DONKEY_SOURCE_TYPE_IMPL
#undef CHECK_DONKEY_SOURCE_TYPE
#undef CHECK_DONKEY_PORTABLE_I32
#undef CHECK_DONKEY_PORTABLE_F32
#undef CHECK_DONKEY_SOURCE_I32
#undef CHECK_DONKEY_SOURCE_F32
#define CHECK_KOOPA_SOURCE_F32(value) _Generic((value), float: 1, default: 0)
#define CHECK_KOOPA_SOURCE_I32(value) \
    _Generic((value), signed char: (sizeof(value) == sizeof(int32_t)), \
             short: (sizeof(value) == sizeof(int32_t)), \
             int: (sizeof(value) == sizeof(int32_t)), \
             long: (sizeof(value) == sizeof(int32_t)), \
             long long: (sizeof(value) == sizeof(int32_t)), default: 0)
#define CHECK_KOOPA_SOURCE_U32(value) \
    _Generic((value), unsigned char: (sizeof(value) == sizeof(uint32_t)), \
             unsigned short: (sizeof(value) == sizeof(uint32_t)), \
             unsigned int: (sizeof(value) == sizeof(uint32_t)), \
             unsigned long: (sizeof(value) == sizeof(uint32_t)), \
             unsigned long long: (sizeof(value) == sizeof(uint32_t)), default: 0)
#define CHECK_KOOPA_PORTABLE_F32(value) _Generic((value), float: 1, default: 0)
#define CHECK_KOOPA_PORTABLE_I32(value) _Generic((value), int32_t: 1, default: 0)
#define CHECK_KOOPA_PORTABLE_U32(value) _Generic((value), uint32_t: 1, default: 0)
#define CHECK_KOOPA_SOURCE_TYPE(type, value) CHECK_KOOPA_SOURCE_TYPE_IMPL(type, value)
#define CHECK_KOOPA_SOURCE_TYPE_IMPL(type, value) CHECK_KOOPA_SOURCE_##type(value)
#define CHECK_KOOPA_PORTABLE_TYPE(type, value) CHECK_KOOPA_PORTABLE_TYPE_IMPL(type, value)
#define CHECK_KOOPA_PORTABLE_TYPE_IMPL(type, value) CHECK_KOOPA_PORTABLE_##type(value)
#define CHECK_KOOPA(offset,type,name,original) \
    _Static_assert(offsetof(ftKoopaAttributes, original) == offset, "Koopa source attribute offset"); \
    _Static_assert(offsetof(MeleeWebKoopaAttributes, name) == offset, "Koopa portable attribute offset"); \
    _Static_assert(sizeof(((ftKoopaAttributes*)0)->original) == sizeof(MELEE_WEB_KOOPA_TYPE_##type), "Koopa source attribute width"); \
    _Static_assert(sizeof(((MeleeWebKoopaAttributes*)0)->name) == sizeof(MELEE_WEB_KOOPA_TYPE_##type), "Koopa portable attribute width"); \
    _Static_assert(CHECK_KOOPA_SOURCE_TYPE(type, ((ftKoopaAttributes*)0)->original), "Koopa source attribute type"); \
    _Static_assert(CHECK_KOOPA_PORTABLE_TYPE(type, ((MeleeWebKoopaAttributes*)0)->name), "Koopa portable attribute type");
MELEE_WEB_KOOPA_ATTRIBUTE_FIELDS(CHECK_KOOPA)
#undef CHECK_KOOPA
#undef CHECK_KOOPA_PORTABLE_TYPE_IMPL
#undef CHECK_KOOPA_PORTABLE_TYPE
#undef CHECK_KOOPA_SOURCE_TYPE_IMPL
#undef CHECK_KOOPA_SOURCE_TYPE
#undef CHECK_KOOPA_PORTABLE_U32
#undef CHECK_KOOPA_PORTABLE_I32
#undef CHECK_KOOPA_PORTABLE_F32
#undef CHECK_KOOPA_SOURCE_U32
#undef CHECK_KOOPA_SOURCE_I32
#undef CHECK_KOOPA_SOURCE_F32
#define CHECK_MEWTWO_SOURCE_F32(value) _Generic((value), float: 1, default: 0)
#define CHECK_MEWTWO_SOURCE_I32(value) \
    _Generic((value), signed char: (sizeof(value) == sizeof(int32_t)), \
             short: (sizeof(value) == sizeof(int32_t)), \
             int: (sizeof(value) == sizeof(int32_t)), \
             long: (sizeof(value) == sizeof(int32_t)), \
             long long: (sizeof(value) == sizeof(int32_t)), default: 0)
#define CHECK_MEWTWO_SOURCE_U32(value) \
    _Generic((value), unsigned char: (sizeof(value) == sizeof(uint32_t)), \
             unsigned short: (sizeof(value) == sizeof(uint32_t)), \
             unsigned int: (sizeof(value) == sizeof(uint32_t)), \
             unsigned long: (sizeof(value) == sizeof(uint32_t)), \
             unsigned long long: (sizeof(value) == sizeof(uint32_t)), default: 0)
#define CHECK_MEWTWO_SOURCE_U8(value) \
    _Generic((value), unsigned char: 1, default: 0)
#define CHECK_MEWTWO_PORTABLE_F32(value) _Generic((value), float: 1, default: 0)
#define CHECK_MEWTWO_PORTABLE_I32(value) _Generic((value), int32_t: 1, default: 0)
#define CHECK_MEWTWO_PORTABLE_U32(value) _Generic((value), uint32_t: 1, default: 0)
#define CHECK_MEWTWO_PORTABLE_U8(value) _Generic((value), uint8_t: 1, default: 0)
#define CHECK_MEWTWO_SOURCE_TYPE(type, value) CHECK_MEWTWO_SOURCE_TYPE_IMPL(type, value)
#define CHECK_MEWTWO_SOURCE_TYPE_IMPL(type, value) CHECK_MEWTWO_SOURCE_##type(value)
#define CHECK_MEWTWO_PORTABLE_TYPE(type, value) CHECK_MEWTWO_PORTABLE_TYPE_IMPL(type, value)
#define CHECK_MEWTWO_PORTABLE_TYPE_IMPL(type, value) CHECK_MEWTWO_PORTABLE_##type(value)
#define CHECK_MEWTWO(offset,type,name,original) \
    _Static_assert(offsetof(ftMewtwoAttributes, original) == offset, "Mewtwo source attribute offset"); \
    _Static_assert(offsetof(MeleeWebMewtwoAttributes, name) == offset, "Mewtwo portable attribute offset"); \
    _Static_assert(sizeof(((ftMewtwoAttributes*)0)->original) == sizeof(MELEE_WEB_MEWTWO_TYPE_##type), "Mewtwo source attribute width"); \
    _Static_assert(sizeof(((MeleeWebMewtwoAttributes*)0)->name) == sizeof(MELEE_WEB_MEWTWO_TYPE_##type), "Mewtwo portable attribute width"); \
    _Static_assert(CHECK_MEWTWO_SOURCE_TYPE(type, ((ftMewtwoAttributes*)0)->original), "Mewtwo source attribute type"); \
    _Static_assert(CHECK_MEWTWO_PORTABLE_TYPE(type, ((MeleeWebMewtwoAttributes*)0)->name), "Mewtwo portable attribute type");
MELEE_WEB_MEWTWO_ATTRIBUTE_FIELDS(CHECK_MEWTWO)
#undef CHECK_MEWTWO
#undef CHECK_MEWTWO_PORTABLE_TYPE_IMPL
#undef CHECK_MEWTWO_PORTABLE_TYPE
#undef CHECK_MEWTWO_SOURCE_TYPE_IMPL
#undef CHECK_MEWTWO_SOURCE_TYPE
#undef CHECK_MEWTWO_PORTABLE_U8
#undef CHECK_MEWTWO_PORTABLE_U32
#undef CHECK_MEWTWO_PORTABLE_I32
#undef CHECK_MEWTWO_PORTABLE_F32
#undef CHECK_MEWTWO_SOURCE_U8
#undef CHECK_MEWTWO_SOURCE_U32
#undef CHECK_MEWTWO_SOURCE_I32
#undef CHECK_MEWTWO_SOURCE_F32
#define CHECK_PURIN_SOURCE_F32(value) _Generic((value), float: 1, default: 0)
#define CHECK_PURIN_SOURCE_I32(value) \
    _Generic((value), signed char: (sizeof(value) == sizeof(int32_t)), \
             short: (sizeof(value) == sizeof(int32_t)), \
             int: (sizeof(value) == sizeof(int32_t)), \
             long: (sizeof(value) == sizeof(int32_t)), \
             long long: (sizeof(value) == sizeof(int32_t)), default: 0)
/* UNK_T is void* in the pinned source build.  Keep this category opaque and
 * prove only its pointer-width storage; no pointer is published by this ABI. */
#define CHECK_PURIN_SOURCE_OPAQUE32(value) (sizeof(value) == sizeof(uint32_t))
#define CHECK_PURIN_SOURCE_PAD4(value) (sizeof(value) == 4)
#define CHECK_PURIN_SOURCE_PAD8(value) (sizeof(value) == 8)
#define CHECK_PURIN_PORTABLE_F32(value) _Generic((value), float: 1, default: 0)
#define CHECK_PURIN_PORTABLE_I32(value) _Generic((value), int32_t: 1, default: 0)
#define CHECK_PURIN_PORTABLE_OPAQUE32(value) _Generic((value), uint32_t: 1, default: 0)
#define CHECK_PURIN_PORTABLE_PAD4(value) (sizeof(value) == 4)
#define CHECK_PURIN_PORTABLE_PAD8(value) (sizeof(value) == 8)
#define CHECK_PURIN_SOURCE_TYPE(type, value) CHECK_PURIN_SOURCE_TYPE_IMPL(type, value)
#define CHECK_PURIN_SOURCE_TYPE_IMPL(type, value) CHECK_PURIN_SOURCE_##type(value)
#define CHECK_PURIN_PORTABLE_TYPE(type, value) CHECK_PURIN_PORTABLE_TYPE_IMPL(type, value)
#define CHECK_PURIN_PORTABLE_TYPE_IMPL(type, value) CHECK_PURIN_PORTABLE_##type(value)
#define CHECK_PURIN(offset,type,dst,portable_member,portable_component,source_member,source_expr) \
    _Static_assert(offsetof(ftPurinAttributes, source_member) + portable_component == offset, \
                   "Purin source attribute offset"); \
    _Static_assert(offsetof(MeleeWebPurinAttributes, portable_member) + portable_component == offset, \
                   "Purin portable attribute offset"); \
    _Static_assert(sizeof(((ftPurinAttributes*)0)->source_expr) == \
                   MELEE_WEB_PURIN_TYPE_BYTES_##type, "Purin source attribute width"); \
    _Static_assert(sizeof(((MeleeWebPurinAttributes*)0)->dst) == \
                   MELEE_WEB_PURIN_TYPE_BYTES_##type, "Purin portable attribute width"); \
    _Static_assert(CHECK_PURIN_SOURCE_TYPE(type, ((ftPurinAttributes*)0)->source_expr), \
                   "Purin source attribute type"); \
    _Static_assert(CHECK_PURIN_PORTABLE_TYPE(type, ((MeleeWebPurinAttributes*)0)->dst), \
                   "Purin portable attribute type");
MELEE_WEB_PURIN_ATTRIBUTE_FIELDS(CHECK_PURIN)
#undef CHECK_PURIN
#undef CHECK_PURIN_PORTABLE_TYPE_IMPL
#undef CHECK_PURIN_PORTABLE_TYPE
#undef CHECK_PURIN_SOURCE_TYPE_IMPL
#undef CHECK_PURIN_SOURCE_TYPE
#undef CHECK_PURIN_PORTABLE_PAD8
#undef CHECK_PURIN_PORTABLE_PAD4
#undef CHECK_PURIN_PORTABLE_OPAQUE32
#undef CHECK_PURIN_PORTABLE_I32
#undef CHECK_PURIN_PORTABLE_F32
#undef CHECK_PURIN_SOURCE_PAD8
#undef CHECK_PURIN_SOURCE_PAD4
#undef CHECK_PURIN_SOURCE_OPAQUE32
#undef CHECK_PURIN_SOURCE_I32
#undef CHECK_PURIN_SOURCE_F32
#define WORD(o) r->word(r->context, (o))
#define BYTE(o) r->byte(r->context, (o))
#define PTR(o,n) r->pointer(r->context, (o),(n))
#define REGION(o,n) r->region(r->context,(o),(n))
#define NEW(t,n) ((t*) r->allocate(r->context,(n),sizeof(t)))
#define REQUIRE(c,m) do { if (!(c)) r->reject(r->context,(m)); } while (0)
static uint32_t required(const MeleeWebNativeDat* r,uint32_t slot,size_t size)
{
    uint32_t at=PTR(slot,size); REQUIRE(at != UINT32_MAX,"Required fighter data pointer is null");
    REGION(at,size); return at;
}
static float floating(const MeleeWebNativeDat* r,uint32_t at)
{
    uint32_t bits=WORD(at); float value; memcpy(&value,&bits,4);
    REQUIRE(isfinite(value),"Native fighter scalar is nonfinite"); return value;
}
static Counted* visibility(const MeleeWebNativeDat* r,uint32_t at,uint32_t count,unsigned category)
{
    REGION(at,count*8); Counted* groups=NEW(Counted,count);
    for (uint32_t i=0;i<count;++i) {
        uint32_t n=WORD(at+i*8); REQUIRE(n<=128,"Visibility variants exceed selector capacity");
        groups[i].count=n; groups[i].data=NEW(Counted,n);
        uint32_t variants=PTR(at+i*8+4,n?n*8:1);
        REQUIRE(!n || variants!=UINT32_MAX,"Visibility variants missing");
        if(n) REGION(variants,n*8);
        for(uint32_t j=0;j<n;++j) {
            Counted* v=&((Counted*)groups[i].data)[j];
            v->count=WORD(variants+j*8);
            uint32_t limit=category==2?32:124;
            REQUIRE(v->count<=limit,"Visibility list exceeds source capacity");
            uint32_t indices=PTR(variants+j*8+4,v->count?v->count:1);
            REQUIRE(!v->count || indices!=UINT32_MAX,"Visibility indices missing");
            v->data=NEW(uint8_t,v->count);
            if(v->count) REGION(indices,v->count);
            for(uint32_t k=0;k<v->count;++k) {
                uint8_t index=BYTE(indices+k); REQUIRE(index<limit,"Visibility index exceeds source capacity");
                ((uint8_t*)v->data)[k]=index;
            }
        }
    }
    return groups;
}
static FtPartsVisLookup* gamewatch_part_visibility(const MeleeWebNativeDat* r,
                                                    uint32_t at,uint32_t models)
{
    REGION(at,(size_t)models*sizeof(FtPartsVisLookup));
    FtPartsVisLookup* lookup=NEW(FtPartsVisLookup,models);
    for(uint32_t model=0;model<models;++model) {
        const uint32_t row=at+model*sizeof(FtPartsVisLookup);
        const uint32_t variants=WORD(row);
        lookup[model].x0=(int)variants;
        const uint32_t table=PTR(row+4,variants?variants*sizeof(TempS):1);
        REQUIRE(!variants||table!=UINT32_MAX,
                "Game & Watch part-visibility variants are missing");
        if(!variants)continue;
        REQUIRE(r->extent && r->extent(r->context,table)>=variants*sizeof(TempS),
                "Game & Watch part-visibility variants cross their authored table bound");
        REGION(table,(size_t)variants*sizeof(TempS));
        lookup[model].x4=NEW(TempS,variants);
        for(uint32_t variant=0;variant<variants;++variant) {
            const uint32_t entry=table+variant*sizeof(TempS);
            const uint32_t count=WORD(entry);
            lookup[model].x4[variant].x0=(int)count;
            const uint32_t indices=PTR(entry+4,count?count:1);
            REQUIRE(!count||indices!=UINT32_MAX,
                    "Game & Watch part-visibility index list is missing");
            if(!count)continue;
            REQUIRE(r->extent && r->extent(r->context,indices)>=count,
                    "Game & Watch part-visibility indices cross their authored table bound");
            REGION(indices,count);
            lookup[model].x4[variant].x4=NEW(u8,count);
            for(uint32_t index=0;index<count;++index)
                lookup[model].x4[variant].x4[index]=BYTE(indices+index);
        }
    }
    return lookup;
}
static FtSFXArr* sound_array(const MeleeWebNativeDat* r,uint32_t slot)
{
    uint32_t at=PTR(slot,8); if(at==UINT32_MAX) return NULL;
    REGION(at,8); FtSFXArr* out=NEW(FtSFXArr,1); out->num=(int)WORD(at);
    REQUIRE(out->num>=0 && out->num<=1024,"Fighter sound list count invalid");
    uint32_t data=PTR(at+4,out->num?out->num*4:1);
    REQUIRE(!out->num || data!=UINT32_MAX,"Fighter sound IDs missing");
    out->sfx_ids=NEW(s32,out->num); if(out->num) REGION(data,out->num*4);
    for(int i=0;i<out->num;++i) out->sfx_ids[i]=(s32)WORD(data+i*4);
    return out;
}
static WaitStruct* crouch_wait_choices(const MeleeWebNativeDat* r,uint32_t root,uint32_t motions)
{
    uint32_t at=PTR(root+0x28,8);
    if(at==UINT32_MAX)return NULL;
    REQUIRE(r->extent,"Crouch Wait requires authored table bounds");
    const uint32_t capacity=r->extent(r->context,at)/8;
    uint32_t count=0;
    uint64_t total=0;
    for(;;++count) {
        REQUIRE(count<capacity && count<=1024,"Crouch Wait choices do not terminate within source bound");
        REGION(at+count*8,8);
        int32_t motion=(int32_t)WORD(at+count*8);
        if(motion==-1)break;
        REQUIRE(count<1024,"Crouch Wait choices exceed checked capacity");
        int32_t weight=(int32_t)WORD(at+count*8+4);
        REQUIRE(motion>=0 && (uint32_t)motion<motions && weight>=0,
                "Crouch Wait motion or weight is invalid");
        total+=(uint32_t)weight;
        REQUIRE(total<=INT32_MAX,"Crouch Wait weights overflow original int");
    }
    REQUIRE(total>=100,"Crouch Wait choices do not cover the original random range");
    WaitStruct* out=NEW(WaitStruct,count+1);
    for(uint32_t i=0;i<=count;++i) {
        out[i].u.i.x=(int32_t)WORD(at+i*8);
        out[i].u.i.y=(int32_t)WORD(at+i*8+4);
    }
    return out;
}
static void* purin_parts(const MeleeWebNativeDat* r,uint32_t table,uint32_t costumes)
{
    typedef struct { uint32_t unused; FtPartsDesc desc; } PurinParts;
    _Static_assert(offsetof(PurinParts,desc)==4 && sizeof(PurinParts)==12,
                   "Purin source custom-part wrapper ABI");
    REQUIRE(PTR(table,1)==UINT32_MAX,"Purin first custom-part slot must be null");
    uint32_t at=required(r,table+4,12);
    PurinParts* out=NEW(PurinParts,1);
    out->unused=WORD(at);
    out->desc.model_num=WORD(at+4);
    REQUIRE(out->desc.model_num>0 && out->desc.model_num<=11,
            "Purin custom-part model count exceeds source capacity");
    uint32_t visibility_table=required(r,at+8,costumes*16);
    /* All four categories use the same 32-entry fighter_x2040 DObj owner. */
    out->desc.vis_table=r->allocate(r->context,costumes,16);
    for(uint32_t c=0;c<costumes;++c)for(unsigned category=0;category<4;++category) {
        uint32_t p=PTR(visibility_table+c*16+category*4,out->desc.model_num*8);
        if(p!=UINT32_MAX)
            out->desc.vis_table[c][category]=visibility(r,p,out->desc.model_num,2);
    }
    return out;
}

int melee_web_fighter_data_check_purin_part(void* data,uint32_t costume,
    uint32_t dobj_count,char* error,size_t size)
{
#define PURIN_REQUIRE(c,m) do { if(!(c)){if(error&&size)snprintf(error,size,"%s",m);return 0;} } while(0)
    ftData* d=data;
    PURIN_REQUIRE(d && d->x48_items && !d->x48_items[0] && d->x48_items[1] &&
                  costume>0 && costume<5 && dobj_count>0 && dobj_count<=32,
                  "Purin custom-part owner identity is invalid");
    FtPartsDesc* desc=(FtPartsDesc*)((char*)d->x48_items[1]+4);
    PURIN_REQUIRE(desc->model_num>0 && desc->model_num<=11 && desc->vis_table,
                  "Purin custom-part descriptor is incomplete");
    for(unsigned category=0;category<4;++category) {
        Counted* groups=desc->vis_table[costume][category];
        if(!groups)groups=desc->vis_table[0][category];
        if(!groups)continue;
        for(uint32_t m=0;m<desc->model_num;++m) {
            Counted* variants=groups[m].data;
            PURIN_REQUIRE(groups[m].count<=128 && (!groups[m].count || variants),
                          "Purin custom-part visibility variants are invalid");
            for(uint32_t v=0;v<groups[m].count;++v) {
                uint8_t* indices=variants[v].data;
                PURIN_REQUIRE(variants[v].count<=32 && (!variants[v].count || indices),
                              "Purin custom-part visibility indices are missing");
                for(uint32_t i=0;i<variants[v].count;++i)
                    PURIN_REQUIRE(indices[i]<dobj_count,
                                  "Purin custom-part index exceeds its hydrated DObj occurrences");
            }
        }
    }
    if(error&&size)*error=0;
    return 1;
#undef PURIN_REQUIRE
}

void* melee_web_fighter_data_decode(const MeleeWebNativeDat* r,uint32_t root,
    uint32_t kind,uint32_t costumes,uint32_t motion_count,void* actions,void* blends,
    void* choices,uint32_t* unresolved)
{
    if (!r || !unresolved) return NULL;
    REQUIRE(kind==FTKIND_MARIO || kind==FTKIND_DRMARIO || kind==FTKIND_FOX ||
        kind==FTKIND_FALCO || kind==FTKIND_MARS || kind==FTKIND_EMBLEM ||
        kind==FTKIND_LINK || kind==FTKIND_CLINK || kind==FTKIND_CAPTAIN || kind==FTKIND_GANON ||
        kind==FTKIND_DONKEY || kind==FTKIND_KOOPA || kind==FTKIND_LUIGI || kind==FTKIND_PIKACHU || kind==FTKIND_PICHU ||
        kind==FTKIND_NESS || kind==FTKIND_MEWTWO || kind==FTKIND_PURIN || kind==FTKIND_PEACH ||
        kind==FTKIND_GAMEWATCH || kind==FTKIND_KIRBY || kind==FTKIND_SAMUS ||
        kind==FTKIND_YOSHI || kind==FTKIND_ZELDA || kind==FTKIND_SEAK ||
        kind==FTKIND_POPO || kind==FTKIND_NANA,
        "Native fighter extension schema unavailable");
    REQUIRE(costumes>0 && costumes<=16,"Native costume count exceeds checked bound");
    REQUIRE(motion_count>0 && motion_count<=1024,"Native motion count exceeds checked bound");
    REGION(root,0x60); ftData* d=NEW(ftData,1); *unresolved=0;
    /* Every source pointer starts explicitly unresolved until decoded below. */
    for(unsigned field=0;field<24;++field)
        if(PTR(root+field*4,1)!=UINT32_MAX) *unresolved |= 1U<<field;
    uint32_t at=required(r,root,0x184); d->x0=NEW(ftCo_DatAttrs,1);
#define READ_F32(o) floating(r,o)
#define READ_I32(o) ((int32_t)WORD(o))
#define READ_U32(o) WORD(o)
#define READ_ITEM(o) ((int32_t)WORD(o))
#define READ_U8(o) BYTE(o)
#define READ_PTR32(o) ((void*)(uintptr_t)READ_U32(o))
#define CO(o,t,n,orig) d->x0->orig=READ_##t(at+o);
    MELEE_WEB_CO_ATTRIBUTE_FIELDS(CO)
#undef CO
    if(kind==FTKIND_MARIO || kind==FTKIND_DRMARIO) {
        /* ftMr_Init_OnLoadForDrMario copies the full Mario ABI from Dr.
         * Mario's own DAT, including his distinct cape Article kind. */
        at=required(r,root+4,0x84); ftMario_DatAttrs* mario=NEW(ftMario_DatAttrs,1); d->ext_attr=mario;
#define MARIO(o,t,n,orig) mario->orig=READ_##t(at+o);
        MELEE_WEB_MARIO_ATTRIBUTE_FIELDS(MARIO)
#undef MARIO
    } else if(kind==FTKIND_FOX || kind==FTKIND_FALCO) {
        /* Fox and Falco use one original extension ABI.  The source keeps
         * separate DAT values and Article identities, so decode the complete
         * ftFox_DatAttrs record for either kind rather than borrowing Fox's
         * values or reducing the extension to a common subset. */
        at=required(r,root+4,0xD4); ftFox_DatAttrs* fox=NEW(ftFox_DatAttrs,1); d->ext_attr=fox;
#define FOX(o,t,n,orig) fox->orig=READ_##t(at+o);
        MELEE_WEB_FOX_ATTRIBUTE_FIELDS(FOX)
#undef FOX
        REQUIRE(fox->xB0_FOX_REFLECTOR_REFLECTION.x0_bone_id<140,"Native reflector bone index invalid");
    } else if(kind==FTKIND_MARS || kind==FTKIND_EMBLEM) {
        at=required(r,root+4,0x98); MarsAttributes* mars=NEW(MarsAttributes,1); d->ext_attr=mars;
#define MARS(o,t,n,orig) mars->orig=READ_##t(at+o);
        MELEE_WEB_MARS_ATTRIBUTE_FIELDS(MARS)
#undef MARS
        REQUIRE(mars->x64.x0_bone_id>=0 && mars->x64.x0_bone_id<140 && mars->x64.x10_size>0,
                "Native Marth/Roy counter descriptor invalid");
    } else if(kind==FTKIND_LINK || kind==FTKIND_CLINK) {
        at=required(r,root+4,0xDC); ftLk_DatAttrs* link=NEW(ftLk_DatAttrs,1); d->ext_attr=link;
#define LINK(o,t,n,orig) link->orig=READ_##t(at+o);
        MELEE_WEB_LINK_ATTRIBUTE_FIELDS(LINK)
#undef LINK
        REQUIRE(link->xC4.x0_bone_id>=0 && link->xC4.x0_bone_id<140 && link->xC4.x10_size>0,
                "Native Link absorb descriptor invalid");
    } else if(kind==FTKIND_NESS) {
        /* Ness owns a unique ftNessAttributes ABI at root+4. The authored
         * extent is exactly 0xDC bytes; integer loop counters and the two
         * descriptor records keep their original categories. */
        at=required(r,root+4,0xDC); ftNessAttributes* ness=NEW(ftNessAttributes,1); d->ext_attr=ness;
#define NESS(o,t,n,orig) ness->orig=READ_##t(at+o);
        MELEE_WEB_NESS_ATTRIBUTE_FIELDS(NESS)
#undef NESS
        REQUIRE(ness->x98_PSI_MAGNET_ABSORPTION.x0_bone_id>=0 &&
                    ness->x98_PSI_MAGNET_ABSORPTION.x0_bone_id<140 &&
                    ness->x98_PSI_MAGNET_ABSORPTION.x10_size>0,
                "Native Ness absorb descriptor invalid");
        REQUIRE(ness->xB8_BASEBALL_BAT.x0_bone_id<140 && ness->xB8_BASEBALL_BAT.x4_max_damage>0 &&
                    ness->xB8_BASEBALL_BAT.x14_size>0,
                "Native Ness bat reflection descriptor invalid");
    } else if(kind==FTKIND_PEACH) {
        /* Peach owns a unique ftPe_DatAttrs ABI at root+4. The authored
         * extent is exactly 0xC0 bytes; the Toad counter's held-item table
         * keeps its source {odds, ItemKind} pairs and xAC is the original
         * AbsorbDesc. floatfallf/b_anim_start are authored zero and filled
         * at load from motions 18/19 by ftPe_Init_OnLoad. */
        at=required(r,root+4,0xC0); ftPe_DatAttrs* peach=NEW(ftPe_DatAttrs,1); d->ext_attr=peach;
#define PEACH(o,t,n,orig) peach->orig=READ_##t(at+o);
        MELEE_WEB_PEACH_ATTRIBUTE_FIELDS(PEACH)
#undef PEACH
        REQUIRE(peach->speciallw_item_table_count>0 &&
                    peach->speciallw_item_table_count<=
                        (int)(sizeof(peach->speciallw_item_table)/sizeof(peach->speciallw_item_table[0])),
                "Native Peach Toad counter item table exceeds its source extent");
        REQUIRE(peach->xAC.x0_bone_id>=0 && peach->xAC.x0_bone_id<140 && peach->xAC.x10_size>0,
                "Native Peach absorb descriptor invalid");
    } else if(kind==FTKIND_SAMUS) {
        at=required(r,root+4,MELEE_WEB_SAMUS_ATTRIBUTE_BYTES);
        ftSs_DatAttrs* samus=NEW(ftSs_DatAttrs,1); d->ext_attr=samus;
#define SAMUS(o,t,n,orig) samus->orig=READ_##t(at+o);
        MELEE_WEB_SAMUS_ATTRIBUTE_FIELDS(SAMUS)
#undef SAMUS
    } else if(kind==FTKIND_YOSHI) {
        at=required(r,root+4,MELEE_WEB_YOSHI_ATTRIBUTE_BYTES);
        ftYoshiAttributes* yoshi=NEW(ftYoshiAttributes,1); d->ext_attr=yoshi;
        /* Yoshi's original callbacks view this one allocation through both
         * ftYoshiAttributes and ftYs_DatAttrs. Preserve every source word at
         * its exact offset, including overlay-only fields and padding. */
        for(uint32_t offset=0;offset<MELEE_WEB_YOSHI_ATTRIBUTE_BYTES;offset+=4) {
            const uint32_t value=WORD(at+offset);
            memcpy((uint8_t*)yoshi+offset,&value,sizeof(value));
        }
    } else if(kind==FTKIND_ZELDA) {
        at=required(r,root+4,sizeof(ftZelda_DatAttrs));
        ftZelda_DatAttrs* zelda=NEW(ftZelda_DatAttrs,1); d->ext_attr=zelda;
        /* All source fields through ReflectDesc::x1C are 32-bit scalars.
         * Preserve the final byte-sized behavior flag and its padding in
         * source byte order rather than swapping that mixed-width lane. */
        for(uint32_t offset=0;offset<offsetof(ftZelda_DatAttrs,x84)+offsetof(ReflectDesc,x20_behavior);offset+=4) {
            const uint32_t value=WORD(at+offset);
            memcpy((uint8_t*)zelda+offset,&value,sizeof(value));
        }
        for(uint32_t offset=offsetof(ftZelda_DatAttrs,x84)+offsetof(ReflectDesc,x20_behavior);
            offset<sizeof(*zelda);++offset)
            ((uint8_t*)zelda)[offset]=BYTE(at+offset);
    } else if(kind==FTKIND_SEAK) {
        at=required(r,root+4,sizeof(ftSeakAttributes));
        ftSeakAttributes* seak=NEW(ftSeakAttributes,1); d->ext_attr=seak;
        for(uint32_t offset=0;offset<sizeof(*seak);offset+=4) {
            const uint32_t value=WORD(at+offset);
            memcpy((uint8_t*)seak+offset,&value,sizeof(value));
        }
    } else if(kind==FTKIND_DONKEY) {
        at=required(r,root+4,MELEE_WEB_DONKEY_ATTRIBUTE_BYTES); ftDonkeyAttributes* donkey=NEW(ftDonkeyAttributes,1); d->ext_attr=donkey;
#define DONKEY(o,t,n,orig) donkey->orig=READ_##t(at+o);
        MELEE_WEB_DONKEY_ATTRIBUTE_FIELDS(DONKEY)
#undef DONKEY
    } else if(kind==FTKIND_KOOPA) {
        at=required(r,root+4,MELEE_WEB_KOOPA_ATTRIBUTE_BYTES);
        ftKoopaAttributes* koopa=NEW(ftKoopaAttributes,1); d->ext_attr=koopa;
#define KOOPA(o,t,n,orig) koopa->orig=READ_##t(at+o);
        MELEE_WEB_KOOPA_ATTRIBUTE_FIELDS(KOOPA)
#undef KOOPA
    } else if(kind==FTKIND_MEWTWO) {
        at=required(r,root+4,MELEE_WEB_MEWTWO_ATTRIBUTE_BYTES);
        ftMewtwoAttributes* mewtwo=NEW(ftMewtwoAttributes,1); d->ext_attr=mewtwo;
#define MEWTWO(o,t,n,orig) mewtwo->orig=READ_##t(at+o);
        MELEE_WEB_MEWTWO_ATTRIBUTE_FIELDS(MEWTWO)
#undef MEWTWO
    } else if(kind==FTKIND_LUIGI) {
        at=required(r,root+4,MELEE_WEB_LUIGI_ATTRIBUTE_BYTES); ftLuigiAttributes* luigi=NEW(ftLuigiAttributes,1); d->ext_attr=luigi;
#define LUIGI(o,t,n,orig) luigi->orig=READ_##t(at+o);
        MELEE_WEB_LUIGI_ATTRIBUTE_FIELDS(LUIGI)
#undef LUIGI
    } else if(kind==FTKIND_PIKACHU || kind==FTKIND_PICHU) {
        /* Pichu's local header is a sparse wrapper, but ftPc_Init_OnLoad
         * calls ftPk_Init_OnLoadForPichu and all shared special callbacks
         * consume this complete source record. Keep one exact ABI and retain
         * the family-specific item words from each DAT. */
        at=required(r,root+4,MELEE_WEB_PIKACHU_ATTRIBUTE_BYTES);
        ftPikachuAttributes* pikachu=NEW(ftPikachuAttributes,1); d->ext_attr=pikachu;
#define PIKACHU(o,t,n,original,component,source) pikachu->source=READ_##t(at+o);
        MELEE_WEB_PIKACHU_ATTRIBUTE_FIELDS(PIKACHU)
#undef PIKACHU
    } else if(kind==FTKIND_PURIN) {
        at=required(r,root+4,MELEE_WEB_PURIN_ATTRIBUTE_BYTES);
        ftPurinAttributes* purin=NEW(ftPurinAttributes,1); d->ext_attr=purin;
#define PURIN_READ_F32(o,dst) purin->dst=floating(r,at+o)
#define PURIN_READ_I32(o,dst) purin->dst=READ_I32(at+o)
#define PURIN_READ_OPAQUE32(o,dst) purin->dst=(void*)(uintptr_t)READ_U32(at+o)
#define PURIN_READ_PAD4(o,dst) do { \
        for(unsigned purin_byte=0; purin_byte<4; ++purin_byte) \
            purin->dst[purin_byte]=BYTE(at+o+purin_byte); \
    } while(0)
#define PURIN_READ_PAD8(o,dst) do { \
        for(unsigned purin_byte=0; purin_byte<8; ++purin_byte) \
            purin->dst[purin_byte]=BYTE(at+o+purin_byte); \
    } while(0)
#define PURIN_READ_IMPL(type,o,dst) PURIN_READ_##type(o,dst)
#define PURIN_READ(type,o,dst) PURIN_READ_IMPL(type,o,dst)
#define PURIN(o,type,dst,portable_member,portable_component,source_member,source_expr) \
        PURIN_READ(type,o,source_expr);
        MELEE_WEB_PURIN_ATTRIBUTE_FIELDS(PURIN)
#undef PURIN
#undef PURIN_READ
#undef PURIN_READ_IMPL
#undef PURIN_READ_PAD8
#undef PURIN_READ_PAD4
#undef PURIN_READ_OPAQUE32
#undef PURIN_READ_I32
#undef PURIN_READ_F32
    } else if(kind==FTKIND_GAMEWATCH) {
        at=required(r,root+4,sizeof(ftGameWatchAttributes));
        ftGameWatchAttributes* gw=NEW(ftGameWatchAttributes,1);d->ext_attr=gw;
        gw->x0_GAMEWATCH_WIDTH=floating(r,at);
        for(unsigned color=0;color<4;++color) {
            const uint32_t color_at=at+4+color*4;
            gw->x4_GAMEWATCH_COLOR[color].r=BYTE(color_at);
            gw->x4_GAMEWATCH_COLOR[color].g=BYTE(color_at+1);
            gw->x4_GAMEWATCH_COLOR[color].b=BYTE(color_at+2);
            gw->x4_GAMEWATCH_COLOR[color].a=BYTE(color_at+3);
        }
        gw->x14_GAMEWATCH_OUTLINE.r=BYTE(at+0x14);
        gw->x14_GAMEWATCH_OUTLINE.g=BYTE(at+0x15);
        gw->x14_GAMEWATCH_OUTLINE.b=BYTE(at+0x16);
        gw->x14_GAMEWATCH_OUTLINE.a=BYTE(at+0x17);
        gw->x18_GAMEWATCH_CHEF_LOOPFRAME=floating(r,at+0x18);
        gw->x1C_GAMEWATCH_CHEF_MAX=floating(r,at+0x1C);
        gw->x20_GAMEWATCH_JUDGE_MOMENTUM_PRESERVE=floating(r,at+0x20);
        gw->x24_GAMEWATCH_JUDGE_MOMENTUM_MUL=floating(r,at+0x24);
        gw->x28_GAMEWATCH_JUDGE_VEL_Y=floating(r,at+0x28);
        gw->x2C_GAMEWATCH_JUDGE_FRICTION1=floating(r,at+0x2C);
        gw->x30_GAMEWATCH_JUDGE_FRICTION2=floating(r,at+0x30);
        for(unsigned roll=0;roll<9;++roll)
            gw->x34_GAMEWATCH_JUDGE_ROLL[roll]=READ_I32(at+0x34+roll*4);
        gw->x58_GAMEWATCH_RESCUE_STICK_RANGE=floating(r,at+0x58);
        gw->x5C_GAMEWATCH_RESCUE_ANGLE_UNK=floating(r,at+0x5C);
        gw->x60_GAMEWATCH_RESCUE_LANDING=floating(r,at+0x60);
        gw->x64_GAMEWATCH_PANIC_MOMENTUM_PRESERVE=floating(r,at+0x64);
        gw->x68_GAMEWATCH_PANIC_MOMENTUM_MUL=floating(r,at+0x68);
        gw->x6C_GAMEWATCH_PANIC_FALL_ACCEL=floating(r,at+0x6C);
        gw->x70_GAMEWATCH_PANIC_VEL_Y_MAX=floating(r,at+0x70);
        gw->x74_GAMEWATCH_PANIC_DAMAGE_ADD=floating(r,at+0x74);
        gw->x78_GAMEWATCH_PANIC_DAMAGE_MUL=floating(r,at+0x78);
        gw->x7C_GAMEWATCH_PANIC_TURN_FRAMES=floating(r,at+0x7C);
        gw->x80_GAMEWATCH_PANIC_ABSORPTION.x0_bone_id=READ_I32(at+0x80);
        gw->x80_GAMEWATCH_PANIC_ABSORPTION.x4_offset=(Vec3){
            floating(r,at+0x84),floating(r,at+0x88),floating(r,at+0x8C)};
        gw->x80_GAMEWATCH_PANIC_ABSORPTION.x10_size=floating(r,at+0x90);
        REQUIRE(gw->x80_GAMEWATCH_PANIC_ABSORPTION.x0_bone_id>=0 &&
                gw->x80_GAMEWATCH_PANIC_ABSORPTION.x0_bone_id<140 &&
                gw->x80_GAMEWATCH_PANIC_ABSORPTION.x10_size>0,
                "Native Game & Watch absorption descriptor invalid");
    } else if(kind==FTKIND_KIRBY) {
        at=required(r,root+4,sizeof(struct ftKb_DatAttrs));
        struct ftKb_DatAttrs* kirby=NEW(struct ftKb_DatAttrs,1);d->ext_attr=kirby;
        /* The source ABI is a 0x424-byte record with two non-word lanes:
         * one s16 plus padding at +0x34, and ReflectDesc's behavior byte at
         * +0x420. Decode all remaining source words through the checked DAT
         * reader so relocations are rejected and target byte order is native. */
        for(uint32_t offset=0;offset<sizeof(*kirby);offset+=4) {
            if(offset==0x34) {
                const uint16_t value=r->half(r->context,at+offset);
                memcpy((uint8_t*)kirby+offset,&value,sizeof(value));
            } else if(offset==0x420) {
                ((uint8_t*)kirby)[offset]=BYTE(at+offset);
            } else {
                const uint32_t value=WORD(at+offset);
                memcpy((uint8_t*)kirby+offset,&value,sizeof(value));
            }
        }
    } else if(kind==FTKIND_POPO || kind==FTKIND_NANA) {
        /* Popo and Nana share this source extension, but their constructors
         * consume different fields. Copy every word in source byte order;
         * validate the actual float lanes while preserving integer/padding
         * words without reinterpretation. */
        at=required(r,root+4,sizeof(ftIceClimberAttributes));
        ftIceClimberAttributes* ice=NEW(ftIceClimberAttributes,1);d->ext_attr=ice;
        for(uint32_t offset=0;offset<sizeof(*ice);offset+=4) {
            const bool integer_word=offset==0x1C||offset==0x68;
            const bool padding=offset==0x20||offset==0xCC||
                (offset>=0xD4&&offset<0x12C)||offset==0x150;
            const uint32_t value=WORD(at+offset);
            if(!integer_word&&!padding)(void)floating(r,at+offset);
            memcpy((uint8_t*)ice+offset,&value,sizeof(value));
        }
    } else if(kind==FTKIND_CAPTAIN || kind==FTKIND_GANON) {
        at=required(r,root+4,0x8C); ftCaptain_DatAttrs* captain=NEW(ftCaptain_DatAttrs,1); d->ext_attr=captain;
#define CAPTAIN(o,t,n,orig) captain->orig=READ_##t(at+o);
        MELEE_WEB_CAPTAIN_ATTRIBUTE_FIELDS(CAPTAIN)
#undef CAPTAIN
    } else {
        REQUIRE(0,"Native fighter extension schema unavailable");
    }
    at=required(r,root+8,0x18); d->x8=NEW(struct ftData_x8,1);
    d->x8->x0.model_num=WORD(at); REQUIRE(d->x8->x0.model_num<=11,"Fighter model count exceeds source capacity");
    uint32_t table=required(r,at+4,costumes*16);
    d->x8->x0.vis_table=r->allocate(r->context,costumes,16);
    for(uint32_t c=0;c<costumes;++c) for(unsigned category=0;category<4;++category) {
        uint32_t p=PTR(table+c*16+category*4,d->x8->x0.model_num*8);
        if(p!=UINT32_MAX) d->x8->x0.vis_table[c][category]=visibility(r,p,d->x8->x0.model_num,category);
    }
    /* ftAnim_80070200 stores this count into CostumeTObjList::costume_tobjs[5]
     * and asserts "fighter tobj num over!" beyond it; larger maps have no
     * defined original state. */
    d->x8->x8.x8=WORD(at+8); REQUIRE(d->x8->x8.x8<=5,"Costume texture map exceeds source capacity");
    table=required(r,at+12,costumes*4); d->x8->x8.xC=NEW(u16*,costumes);
    for(uint32_t c=0;c<costumes;++c) {
        uint32_t p=PTR(table+c*4,d->x8->x8.x8*2);
        if(p==UINT32_MAX) continue;
        REGION(p,d->x8->x8.x8*2); d->x8->x8.xC[c]=NEW(u16,d->x8->x8.x8);
        for(uint32_t j=0;j<d->x8->x8.x8;++j) d->x8->x8.xC[c][j]=r->half(r->context,p+j*2);
    }
    d->x8->x10=BYTE(at+16); d->x8->x11=BYTE(at+17); d->x8->x12=BYTE(at+18);
    d->x8->x13=BYTE(at+19); d->x8->x14=BYTE(at+20);
    d->xC=actions; d->x10=blends; d->x24=choices;
    d->x28=crouch_wait_choices(r,root,motion_count);
    at=required(r,root+0x2c,20); d->x2C=NEW(ftDynamics,1);
    d->x2C->dynamicsNum=READ_I32(at);
    const unsigned dynamics_capacity=sizeof(((struct ArticleDynamicBones*)0)->array)/
                                     sizeof(((struct ArticleDynamicBones*)0)->array[0]);
    REQUIRE(d->x2C->dynamicsNum>=0 && (unsigned)d->x2C->dynamicsNum<dynamics_capacity,
            "Native fighter dynamics count exceeds source Fighter storage");
    REQUIRE(kind!=FTKIND_PURIN || d->x2C->dynamicsNum==1,
            "Purin costume dynamics require one initial body chain");
    if(d->x2C->dynamicsNum) {
        const uint32_t bones=required(r,at+4,(size_t)d->x2C->dynamicsNum*sizeof(BoneDynamicsDesc));
        uint32_t stored_bones=d->x2C->dynamicsNum;
        if(kind==FTKIND_PURIN) {
            REQUIRE(r->extent,"Purin costume dynamics require authored table bounds");
            const uint32_t bytes=r->extent(r->context,bones);
            REQUIRE(bytes%sizeof(BoneDynamicsDesc)==0,
                    "Purin dynamics extent contains a partial descriptor");
            stored_bones=bytes/sizeof(BoneDynamicsDesc);
            REQUIRE(stored_bones>=5 && stored_bones<=dynamics_capacity &&
                    stored_bones>=(uint32_t)d->x2C->dynamicsNum,
                    "Purin dynamics extent cannot cover source costume chains");
            REGION(bones,bytes);
        }
        d->x2C->ftDynamicBones=NEW(struct ArticleDynamicBones,1);
        for(uint32_t i=0;i<stored_bones;++i) {
            const uint32_t row=bones+(uint32_t)i*sizeof(BoneDynamicsDesc);
            BoneDynamicsDesc* out=&d->x2C->ftDynamicBones->array[i];
            out->bone_id=(enum_t)READ_I32(row);
            REQUIRE(out->bone_id>=0 && out->bone_id<140,
                    "Native fighter dynamics bone exceeds source part storage");
            out->dyn_desc.count=READ_U32(row+8);
            REQUIRE(out->dyn_desc.count>0 && out->dyn_desc.count<=140,
                    "Native fighter dynamics chain exceeds source part storage");
            const uint32_t values=required(r,row+4,
                (size_t)out->dyn_desc.count*sizeof(struct lb_00F9_UnkDesc1Inner));
            struct lb_00F9_UnkDesc1Inner* parameters=
                NEW(struct lb_00F9_UnkDesc1Inner,out->dyn_desc.count);
            for(unsigned j=0;j<out->dyn_desc.count;++j)for(unsigned k=0;k<15;++k) {
                const uint32_t field=values+j*sizeof(*parameters)+k*4;
                const float checked=floating(r,field);
                memcpy((uint8_t*)&parameters[j]+k*4,&checked,4);
            }
            /* lb_80011710 treats the serialized parameter array as the
             * lb_unk1 view of DynamicsData. Runtime linked DynamicsData is
             * allocated separately by lb_8000FD48 for each Fighter. */
            out->dyn_desc.data=(struct DynamicsData*)parameters;
            out->dyn_desc.pos=(Vec3){floating(r,row+12),floating(r,row+16),floating(r,row+20)};
        }
    } else REQUIRE(PTR(at+4,1)==UINT32_MAX,
                   "Empty native fighter dynamics has a nonnull bone table");
    d->x2C->x4=READ_I32(at+8);
    REQUIRE(d->x2C->x4>=0 && d->x2C->x4<=11,
            "Native fighter dynamics auxiliary count exceeds source capacity");
    if(d->x2C->x4) {
        const uint32_t rows=required(r,at+12,(size_t)d->x2C->x4*sizeof(struct ftData_x38));
        d->x2C->x8=NEW(struct ftData_x38,d->x2C->x4);
        for(int i=0;i<d->x2C->x4;++i) {
            const uint32_t row=rows+(uint32_t)i*sizeof(struct ftData_x38);
            d->x2C->x8[i].x0=READ_I32(row);
            REQUIRE(d->x2C->x8[i].x0>=0 && d->x2C->x8[i].x0<140,
                    "Native fighter dynamics auxiliary bone exceeds source part storage");
            d->x2C->x8[i].x4=(Vec3){floating(r,row+4),floating(r,row+8),floating(r,row+12)};
            d->x2C->x8[i].x10=floating(r,row+16);
        }
    } else REQUIRE(PTR(at+12,1)==UINT32_MAX || kind==FTKIND_PEACH,
                   "Empty native fighter dynamics has a nonnull auxiliary table");
    /* Peach's authored x4 auxiliary count is zero, but the +0xC pointer is
     * relocated and points just past her bone table. The original
     * ftData dynamics consumers ignore the pointer when the count is zero,
     * so that shape stays admissible instead of inventing an auxiliary row. */
    uint32_t dynamics_table=PTR(at+16,1);
    if(dynamics_table!=UINT32_MAX) {
        /* Authored dynamics modes carry one integer chain cutoff per active
         * body (Marth/Roy/Ganondorf: three; Donkey: one). The mode selector is the second byte of
         * every source blend row, and the original field is typed FigaTree***
         * even though ftdynamics.c compares these pointer-width values as
         * small integers. Derive the table extent from all authored selectors:
         * Roy has a sixth row for selector 5, while Marth's table ends at 4.
         * The referenced-region check below keeps an adjacent descriptor from
         * being consumed as a fabricated mode row.
         */
        const bool sword_or_cape_modes =
            (kind==FTKIND_MARS||kind==FTKIND_EMBLEM||kind==FTKIND_GANON) &&
            d->x2C->dynamicsNum==3;
        const bool donkey_modes = kind==FTKIND_DONKEY && d->x2C->dynamicsNum==1;
        /* Mewtwo and Peach author dynamics modes over their blend rows like
         * the sword/cape families above. */
        const bool mewtwo_modes = kind==FTKIND_MEWTWO && d->x2C->dynamicsNum==1;
        /* Peach authors 86 dynamics modes over her nine body chains; the
         * selector is the second byte of every blend row like the families
         * above. */
        const bool peach_modes = kind==FTKIND_PEACH && d->x2C->dynamicsNum==9;
        /* Zelda has nine independently scheduled chains. GALE01r2's 311
         * blend rows author selectors 0..35 and a corresponding 36-row table. */
        const bool zelda_modes = kind==FTKIND_ZELDA && d->x2C->dynamicsNum==9;
        REQUIRE(sword_or_cape_modes || donkey_modes || mewtwo_modes || peach_modes || zelda_modes,
                "Native fighter dynamics mode schema unavailable");
        REQUIRE(blends,"Native fighter dynamics selectors are missing");
        unsigned mode_count=0;
        const uint8_t* blend_bytes=(const uint8_t*)blends;
        for(uint32_t motion=0;motion<motion_count;++motion) {
            const unsigned mode=blend_bytes[motion*2+1];
            if(mode+1>mode_count)mode_count=mode+1;
        }
        REQUIRE(mode_count>0,"Native fighter dynamics mode table is empty");
        REGION(dynamics_table,(size_t)mode_count*4);
        d->x2C->x10=NEW(FigaTree**,mode_count);
        for(unsigned mode=0;mode<mode_count;++mode) {
            uint32_t row=required(r,dynamics_table+mode*4,
                                  d->x2C->dynamicsNum*4);
            d->x2C->x10[mode]=NEW(FigaTree*,d->x2C->dynamicsNum);
            for(int bone=0;bone<d->x2C->dynamicsNum;++bone) {
                uint32_t cutoff=WORD(row+bone*4);
                /* ftdynamics.c compares the authored value as an int against
                 * the chain index, so 0x100 (ftCo_8009CB40's own sentinel)
                 * selects the whole chain instead of a bounded cutoff.
                 * Peach's mode[0] bones 2-7 author exactly this value;
                 * Marth/Roy/Ganondorf/Donkey never do. */
                REQUIRE(cutoff==0x100 || cutoff<=d->x2C->ftDynamicBones->array[bone].dyn_desc.count,
                        "Native fighter dynamics cutoff exceeds its source chain");
                d->x2C->x10[mode][bone]=(FigaTree*)(uintptr_t)cutoff;
            }
        }
    } else d->x2C->x10=NULL;
    at=required(r,root+0x30,8); d->x30=NEW(struct ftData_x30,1); d->x30->count=(int)WORD(at);
    REQUIRE(d->x30->count>=0 && d->x30->count<=15,"Native hurtbox count exceeds source capacity");
    uint32_t rows=PTR(at+4,d->x30->count?d->x30->count*40:1);
    REQUIRE(!d->x30->count || rows!=UINT32_MAX,"Native hurtbox rows missing");
    if(d->x30->count) REGION(rows,d->x30->count*40);
    d->x30->inits=NEW(ftHurtboxInit,d->x30->count);
    for(int i=0;i<d->x30->count;++i) {
        uint32_t p=rows+i*40; ftHurtboxInit* h=&d->x30->inits[i];
        /* Named layout is checked by the existing source attribute bridge. */
        uint32_t words[10]; for(unsigned j=0;j<10;++j) words[j]=WORD(p+j*4);
        memcpy(h,words,40);
        REQUIRE(words[0]<140 && words[1]<=2 && words[2]<=1,"Native hurtbox bone/height/grabbable invalid");
        for(unsigned j=3;j<10;++j) (void)floating(r,p+j*4);
        REQUIRE(isfinite(h->scale),"Native hurtbox scale nonfinite");
    }
    at=required(r,root+0x34,8); d->x34=NEW(struct ftData_x34,1);
    d->x34->x0=WORD(at); d->x34->scale=floating(r,at+4);
    at=required(r,root+0x38,sizeof(((Fighter*)0)->x1614)/sizeof(((Fighter*)0)->x1614[0])*20);
    const unsigned reflected=sizeof(((Fighter*)0)->x1614)/sizeof(((Fighter*)0)->x1614[0]);
    d->x38=NEW(struct ftData_x38,reflected);
    for(unsigned i=0;i<reflected;++i) {
        d->x38[i].x0=WORD(at+i*20); d->x38[i].x4=(Vec3){floating(r,at+i*20+4),floating(r,at+i*20+8),floating(r,at+i*20+12)};
        d->x38[i].x10=floating(r,at+i*20+16);
    }
    at=required(r,root+0x3c,24); d->x3C=NEW(UnkFloat6_Camera,1);
    d->x3C->x0=(Vec3){floating(r,at),floating(r,at+4),floating(r,at+8)};
    d->x3C->xC=(Vec3){floating(r,at+12),floating(r,at+16),floating(r,at+20)};
    at=required(r,root+0x40,0x30); d->x40=NEW(itPickup,1);
#define PICKUP(o,t,n,orig) d->x40->orig=READ_##t(at+o);
    MELEE_WEB_PICKUP_ATTRIBUTE_FIELDS(PICKUP)
#undef PICKUP
    at=required(r,root+0x44,28); d->x44=NEW(ftData_x44_t,1);
    d->x44->unk0=r->half(r->context,at); d->x44->unk2=r->half(r->context,at+2);
    d->x44->unk4=r->half(r->context,at+4); d->x44->unk6=r->half(r->context,at+6);
    d->x44->unk8=r->half(r->context,at+8); d->x44->unkA=r->half(r->context,at+10);
    d->x44->unkC=floating(r,at+12); d->x44->ledge_snap_x=floating(r,at+16);
    d->x44->ledge_snap_y=floating(r,at+20); d->x44->ledge_snap_height=floating(r,at+24);
    at=required(r,root+0x4c,56); d->x4C_sfx=NEW(FtSFX,1);
    d->x4C_sfx->smash=sound_array(r,at); d->x4C_sfx->x20=sound_array(r,at+32);
    d->x4C_sfx->x1C=(int)(uintptr_t)sound_array(r,at+28);
    for(unsigned i=1;i<14;++i) if(i!=7 && i!=8) ((int*)d->x4C_sfx)[i]=(int)WORD(at+i*4);
    at=required(r,root+0x50,8); d->x50=NEW(Vec2,1); d->x50->x=floating(r,at); d->x50->y=floating(r,at+4);
    /* ftCo_8009F834 rotates through five Fighter_Part entries for effect 0x8D. */
    at=required(r,root+0x54,5*sizeof(int)); int* effect_parts=NEW(int,5);
    for(unsigned i=0;i<5;++i) effect_parts[i]=(int)WORD(at+i*4);
    d->x54=(int)(uintptr_t)effect_parts;
    at=required(r,root+0x58,28); d->x58=NEW(struct ftData_x58_t,1);
    d->x58->x0=BYTE(at); d->x58->x1=BYTE(at+1); d->x58->x4=floating(r,at+4);
    d->x58->x8=BYTE(at+8); d->x58->x9=BYTE(at+9); d->x58->xC=floating(r,at+12);
    d->x58->x10=BYTE(at+16); d->x58->x11=BYTE(at+17); d->x58->x18=floating(r,at+24);
    REQUIRE(d->x58->x0<140 && d->x58->x1<140 && d->x58->x8<140 && d->x58->x9<140 &&
        d->x58->x10<140 && d->x58->x11<140,"Native IK bone index invalid");
    const unsigned item_slots=(kind==FTKIND_POPO||kind==FTKIND_NANA)?3:
                               kind==FTKIND_GAMEWATCH?11:
                               (kind==FTKIND_LINK||kind==FTKIND_CLINK)?7:
                               kind==FTKIND_SEAK?4:
                               kind==FTKIND_ZELDA?2:
                               (kind==FTKIND_LUIGI||kind==FTKIND_KOOPA)?1:
                               (kind==FTKIND_PIKACHU||kind==FTKIND_PICHU)?3:
                               kind==FTKIND_PURIN||kind==FTKIND_MEWTWO?2:
                               kind==FTKIND_NESS?11:
                               kind==FTKIND_PEACH?5:4;
    /* Fixed native capacity bounds the shared accessor for every admitted
     * family; only the exact source extent is read and remaining slots stay
     * null. Ness's authored table has eleven Article slots (all required by
     * ftNs_Init_OnLoad); slot 6 remains a Link joint, never an Article. The
     * allocation covers the accessor's index bound (seven elsewhere), while
     * item_slots above is only the authored read extent. */
    const unsigned item_capacity=(kind==FTKIND_NESS||kind==FTKIND_GAMEWATCH)?11:7;
    d->x48_items=NEW(void*,item_capacity); memset(d->x48_items,0,item_capacity*sizeof(void*));
    at=UINT32_MAX;
    if(kind==FTKIND_PURIN) {
        /* Purin owns a custom visibility wrapper in x48 slot 1. Its native
         * costume graph and archive handle are retained by the asset owner. */
        REQUIRE(costumes==5,"Purin custom-part visibility requires five source costumes");
        at=required(r,root+0x48,8);
        d->x48_items[1]=purin_parts(r,at,costumes);
    } else {
        at=PTR(root+0x48,item_slots*4);
        if(kind==FTKIND_DONKEY)
            REQUIRE(at==UINT32_MAX,"Donkey source ftData Article table is not null");
        REQUIRE((kind!=FTKIND_CAPTAIN && kind!=FTKIND_GANON) || at==UINT32_MAX,
                "Captain-family source ftData must not invent an Article table");
        if(at!=UINT32_MAX) {
            REGION(at,item_slots*4);
            for(unsigned i=0;i<item_slots;++i) {
                if(kind==FTKIND_NANA) {
                    /* ftNn_Init_OnLoad does not register or consume its
                     * duplicate x48 Article roots; those identities belong
                     * to Popo's original OnLoad path. Preserve the authored
                     * three-root table without hydrating Nana's unresolved
                     * GumStrings joint references as independent Articles. */
                    REQUIRE(PTR(at+i*4,24)!=UINT32_MAX,
                            "Nana source x48 Article root is missing");
                    continue;
                }
                const bool link_joint=i==6&&(kind==FTKIND_LINK||kind==FTKIND_CLINK);
                const bool yoshi_joint=i==3&&kind==FTKIND_YOSHI;
                const bool gamewatch_parts=kind==FTKIND_GAMEWATCH&&i==10;
                const size_t minimum=(link_joint||yoshi_joint)?64:gamewatch_parts?
                    (size_t)d->x8->x0.model_num*sizeof(FtPartsVisLookup):24;
                uint32_t p=PTR(at+i*4,minimum), article_unresolved;
                /* Link's seventh entry is the source HSD_Joint descriptor used by
                 * ftParts_800753D4, not an Article root. Its native descriptor is
                 * hydrated by the C++ asset owner after this ftData decode. */
                if(link_joint) {
                    REQUIRE(p!=UINT32_MAX,
                        "Link part descriptor is missing");
                } else if(yoshi_joint) {
                    REQUIRE(p!=UINT32_MAX,
                        "Yoshi special-N source joint is missing");
                } else if(gamewatch_parts) {
                    REQUIRE(p!=UINT32_MAX,
                            "Game & Watch part-visibility descriptor is missing");
                    d->x48_items[i]=gamewatch_part_visibility(r,p,d->x8->x0.model_num);
                } else if(p!=UINT32_MAX) {
                    d->x48_items[i]=melee_web_article_decode(r,p,&article_unresolved);
                }
            }
        }
    }
    if(kind==FTKIND_MARIO)
        REQUIRE(d->x48_items[0] && d->x48_items[2],"Mario OnLoad requires fireball and cape Articles");
    else if(kind==FTKIND_DRMARIO)
        REQUIRE(d->x48_items[1] && d->x48_items[3],
            "Dr. Mario OnLoad requires vitamin and sheet Articles");
    else if(kind==FTKIND_FOX)
        REQUIRE(d->x48_items[0] && d->x48_items[1] && d->x48_items[2],
            "Fox OnLoad requires laser, blaster and illusion Articles");
    else if(kind==FTKIND_FALCO)
        REQUIRE(kind==FTKIND_FALCO && d->x48_items[0] && d->x48_items[1] && d->x48_items[3],
            "Falco OnLoad requires laser, blaster and Phantasm Articles");
    else if(kind==FTKIND_LINK || kind==FTKIND_CLINK)
        REQUIRE(at!=UINT32_MAX && d->x48_items[0] && d->x48_items[1] && d->x48_items[2] &&
            d->x48_items[3] && d->x48_items[4],
            "Link OnLoad requires its five Article identities and part descriptor");
    else if(kind==FTKIND_LUIGI)
        REQUIRE(at!=UINT32_MAX && d->x48_items[0],
            "Luigi OnLoad requires its fire Article identity");
    else if(kind==FTKIND_KOOPA)
        REQUIRE(at!=UINT32_MAX && d->x48_items[0],
            "Koopa OnLoad requires its Flame Article identity");
    else if(kind==FTKIND_MEWTWO)
        REQUIRE(at!=UINT32_MAX && d->x48_items[0] && d->x48_items[1],
            "Mewtwo OnLoad requires its Disable and Shadow Ball Article identities");
    else if(kind==FTKIND_PIKACHU || kind==FTKIND_PICHU)
        REQUIRE(at!=UINT32_MAX && d->x48_items[0] && d->x48_items[1] && d->x48_items[2],
            "Pikachu-family OnLoad requires its three Article identities");
    else if(kind==FTKIND_CAPTAIN || kind==FTKIND_GANON)
        REQUIRE(at==UINT32_MAX, "Captain-family source ftData Article table is not null");
    else if(kind==FTKIND_DONKEY)
        REQUIRE(at==UINT32_MAX, "Donkey source ftData Article table is not null");
    else if(kind==FTKIND_PURIN)
        REQUIRE(at!=UINT32_MAX && d->x48_items[0]==NULL && d->x48_items[1]!=NULL,
            "Purin custom-part wrapper is missing");
    else if(kind==FTKIND_NESS)
        REQUIRE(at!=UINT32_MAX && d->x48_items[0] && d->x48_items[1] && d->x48_items[2] &&
            d->x48_items[3] && d->x48_items[4] && d->x48_items[5] && d->x48_items[6] &&
            d->x48_items[7] && d->x48_items[8] && d->x48_items[9] && d->x48_items[10],
            "Ness OnLoad requires its eleven PK Fire/Flash/Thunder, Bat and Yoyo Articles");
    else if(kind==FTKIND_PEACH)
        REQUIRE(at!=UINT32_MAX && d->x48_items[0] && d->x48_items[1] && d->x48_items[2] &&
            d->x48_items[3] && d->x48_items[4],
            "Peach OnLoad requires its five Explode/Turnip/Parasol/Toad/ToadSpore Articles");
    else if(kind==FTKIND_GAMEWATCH)
        REQUIRE(at!=UINT32_MAX && d->x48_items[0] && d->x48_items[1] &&
            d->x48_items[2] && d->x48_items[3] && d->x48_items[4] &&
            d->x48_items[5] && d->x48_items[6] && d->x48_items[7] &&
            d->x48_items[8] && d->x48_items[9] && d->x48_items[10],
            "Game & Watch OnLoad requires ten Article identities and its part-visibility descriptor");
    else if(kind==FTKIND_KIRBY)
        REQUIRE(at!=UINT32_MAX && d->x48_items[0] && d->x48_items[1] &&
                    d->x48_items[2] && d->x48_items[3],
                "Kirby OnLoad requires Cutter Beam, Hammer and both helper Articles");
    else if(kind==FTKIND_POPO||kind==FTKIND_NANA)
        REQUIRE(at!=UINT32_MAX &&
            (kind==FTKIND_NANA ||
             (d->x48_items[0] && d->x48_items[1] && d->x48_items[2])),
            "Ice Climber source x48 roots or Popo Articles are missing");
    else if(kind==FTKIND_SAMUS)
        REQUIRE(at!=UINT32_MAX && d->x48_items[0] && d->x48_items[1] &&
                d->x48_items[2] && d->x48_items[3],
                "Samus OnLoad requires Bomb, Charge Shot, Missile and Grapple Beam Articles");
    else if(kind==FTKIND_YOSHI)
        REQUIRE(at!=UINT32_MAX && d->x48_items[0] && d->x48_items[1] &&
                d->x48_items[2] && !d->x48_items[3],
                "Yoshi OnLoad requires Egg Throw, Star, Egg Lay and its separately-owned joint");
    else if(kind==FTKIND_ZELDA)
        REQUIRE(at!=UINT32_MAX && d->x48_items[0] && d->x48_items[1],
                "Zelda OnLoad requires DinFire and DinFire explosion Articles");
    else if(kind==FTKIND_SEAK)
        REQUIRE(at!=UINT32_MAX && d->x48_items[0] && d->x48_items[1] &&
                d->x48_items[2] && d->x48_items[3],
                "Sheik OnLoad requires thrown/held needles, vanish and chain Articles");
    else
        REQUIRE((kind==FTKIND_MARS||kind==FTKIND_EMBLEM) && at==UINT32_MAX,
                "Marth/Roy source ftData must not invent an Article table");
    const unsigned ready[]={0,1,2,10,11,12,13,14,15,16,17,18,19,20,21,22};
    for(unsigned i=0;i<sizeof(ready)/sizeof(ready[0]);++i) {
        /* Keep the Link part descriptor unresolved until its source HSD_Joint
         * has been converted to the native 32-bit descriptor ABI. */
        if(ready[i]==18 && (kind==FTKIND_LINK||kind==FTKIND_CLINK||kind==FTKIND_YOSHI)) continue;
        *unresolved &= ~(1U<<ready[i]);
    }
    if(actions) *unresolved &= ~(1U<<3);
    if(blends) *unresolved &= ~(1U<<4);
    if(choices) *unresolved &= ~(1U<<9);
    return d;
}

void* melee_web_fighter_data_article(void* data, uint32_t kind, uint32_t index)
{
    if (!data || kind==FTKIND_PURIN || !((ftData*)data)->x48_items) return NULL;
    /* Ness's authored table has eleven Article slots, all consumed by
     * ftNs_Init_OnLoad. Every other family keeps the source seven-slot
     * array bound (Link's slot 6 is a joint, never an Article). */
    if (index >= (kind==FTKIND_NESS||kind==FTKIND_GAMEWATCH?11U:6U) ||
        (kind==FTKIND_YOSHI&&index==3) ||
        (kind==FTKIND_GAMEWATCH&&index==10)) return NULL;
    return ((ftData*)data)->x48_items[index];
}

int melee_web_fighter_data_set_link_part(void* data,void* joint,uint32_t* unresolved,
    char* error,size_t size)
{
    ftData* d=data;
#define LINK_PART_REQUIRE(c,m) do { if(!(c)){if(error&&size)snprintf(error,size,"%s",m);return 0;} } while(0)
    LINK_PART_REQUIRE(d&&d->x48_items&&joint&&unresolved&&(*unresolved&(1U<<18))&&!d->x48_items[6],
        "Link part descriptor requires an unresolved source part root");
    d->x48_items[6]=joint;*unresolved&=~(1U<<18);if(error&&size)*error=0;return 1;
#undef LINK_PART_REQUIRE
}

int melee_web_fighter_data_set_yoshi_joint(void* data,void* joint,uint32_t* unresolved,
    char* error,size_t size)
{
    ftData* d=data;
#define YOSHI_JOINT_REQUIRE(c,m) do { if(!(c)){if(error&&size)snprintf(error,size,"%s",m);return 0;} } while(0)
    YOSHI_JOINT_REQUIRE(d&&d->x48_items&&joint&&unresolved&&(*unresolved&(1U<<18))&&
        !d->x48_items[3]&&d->x48_items[2],
        "Yoshi special-N joint requires Egg Lay's owned model descriptor");
    d->x48_items[3]=joint;*unresolved&=~(1U<<18);if(error&&size)*error=0;return 1;
#undef YOSHI_JOINT_REQUIRE
}

int melee_web_fighter_data_set_metal(void* data,void* joint,uint32_t costumes,
    uint32_t dobj_count,uint32_t* unresolved,char* error,size_t size)
{
    ftData* d=data;
#define METAL_REQUIRE(c,m) do { if(!(c)){if(error&&size)snprintf(error,size,"%s",m);return 0;} } while(0)
    METAL_REQUIRE(d&&joint&&unresolved&&(*unresolved&(1U<<23))&&!d->x5C,
                  "Metal descriptor requires an unresolved nonnull source root");
    METAL_REQUIRE(costumes&&costumes<=16&&dobj_count&&dobj_count<=32&&d->x8&&d->x8->x0.vis_table&&d->x8->x0.model_num<=11,
                  "Metal descriptor or visibility count exceeds source capacity");
    for(uint32_t c=0;c<costumes;c++) {
        Counted* groups=d->x8->x0.vis_table[c][2];
        if(!groups)continue;
        for(uint32_t i=0;i<d->x8->x0.model_num;i++) {
            Counted* variants=groups[i].data;
            METAL_REQUIRE(groups[i].count<=128&&(!groups[i].count||variants),"Metal visibility variants are invalid");
            for(uint32_t j=0;j<groups[i].count;j++) {
                uint8_t* indices=variants[j].data;
                METAL_REQUIRE(variants[j].count<=32&&(!variants[j].count||indices),"Metal visibility indices are invalid");
                for(uint32_t k=0;k<variants[j].count;k++)
                    METAL_REQUIRE(indices[k]<dobj_count,"Metal visibility index exceeds hydrated DObj occurrences");
            }
        }
    }
    d->x5C=joint;*unresolved&=~(1U<<23);if(error&&size)*error=0;return 1;
#undef METAL_REQUIRE
}

void melee_web_fighter_data_set_guard(const MeleeWebNativeDat* r,uint32_t root,
    uint32_t kind,void* data,void* joint,uint32_t* unresolved)
{
    _Static_assert(offsetof(HSD_Joint,child)==2*sizeof(HSD_Joint*),"Source guard child alias");
    ftData* d=data; HSD_Joint* pose=joint;
    REQUIRE(d&&unresolved&&(*unresolved&(1U<<8))&&!d->x20,
            "Guard pose requires an unresolved source descriptor");
    uint32_t at=required(r,root+0x20,4);
    if(kind==FTKIND_YOSHI&&!pose) {
        /* The pinned Yoshi ftData_x20 record has a null x0 guard-joint root
         * and a zero x8 scalar. Yoshi's authored guard motion handlers use
         * the costume skeleton directly; preserve this exact null record. */
        REQUIRE(PTR(at,4)==UINT32_MAX,
                "Yoshi's null guard record gained a source joint relocation");
        struct ftData_x20* guard=NEW(struct ftData_x20,1);
        guard->x0=NULL;guard->x8=floating(r,at+4);
        d->x20=guard;*unresolved&=~(1U<<8);return;
    }
    REQUIRE(pose&&pose->child,"Guard pose requires a source joint with a child");
    required(r,at,64);
    /* Source guard consumers read only x0. The next relocated object starts
     * at +4; the decompiler's unused x8 member is not serialized here. */
    struct ftData_x20* guard=NEW(struct ftData_x20,1);
    guard->x0=(HSD_Joint**)pose;
    d->x20=guard; *unresolved&=~(1U<<8);
}

int melee_web_fighter_data_set_part_animations(void* data,void* groups,
    uint32_t group_count,uint32_t* unresolved,char* error,size_t size)
{
    ftData* d=data;
#define PART_REQUIRE(c,m) do { if(!(c)){if(error&&size)snprintf(error,size,"%s",m);return 0;} } while(0)
    PART_REQUIRE(d&&groups&&unresolved&&(*unresolved&(1U<<7))&&!d->x1C,
                 "Part animations require an unresolved nonnull source table");
    PART_REQUIRE(group_count>0&&group_count<=5,
                 "Part animation group count exceeds source Fighter storage");
    struct ftData_x1C** table=groups;
    for(uint32_t i=0;i<group_count;++i)
        PART_REQUIRE(table[i]&&table[i]->x2&&table[i]->x4&&table[i]->x8,
                     "Part animation group is incomplete");
    d->x1C=table;*unresolved&=~(1U<<7);if(error&&size)*error=0;return 1;
#undef PART_REQUIRE
}
