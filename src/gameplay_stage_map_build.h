#ifndef MELEE_WEB_GAMEPLAY_STAGE_MAP_BUILD_H
#define MELEE_WEB_GAMEPLAY_STAGE_MAP_BUILD_H
#include "native_dat.h"
#include <sysdolphin/baselib/forward.h>
#include <sysdolphin/baselib/spline.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Portable construction inputs. Original scene types are allocated by C so
 * their nested declarations and source bitfields retain their actual ABI. */
typedef struct MeleeWebMapEntryInput {
    HSD_Joint* unk0; HSD_AnimJoint** unk4; HSD_MatAnimJoint** unk8;
    HSD_ShapeAnimJoint** unkC; HSD_CameraDescPerspective* x10; void* x14;
    void** x18; HSD_FogDesc* x1C; int16_t* unk20; int32_t unk24;
    void* x28; int16_t* x2C; int x30;
} MeleeWebMapEntryInput;
typedef struct MeleeWebMapShadowInput {HSD_LightAnim* unk0; uint8_t flag;} MeleeWebMapShadowInput;
typedef struct MeleeWebMapInput {
    void* unk0; int32_t unk4; MeleeWebMapEntryInput* unk8; int32_t unkC;
    HSD_Spline** unk10; int32_t unk14; void* unk18; int32_t unk1C;
    MeleeWebMapShadowInput* unk20; int32_t unk24; void** unk28; int32_t unk2C;
} MeleeWebMapInput;
void* melee_web_stage_map_build(const MeleeWebNativeDat*,const MeleeWebMapInput*);
void* melee_web_stage_map_light_list(const MeleeWebNativeDat*,HSD_LightDesc*,HSD_LightAnim**);


#ifdef __cplusplus
}
#endif
#endif
