#ifndef MELEE_WEB_HSD_NATIVE_JOINT_H
#define MELEE_WEB_HSD_NATIVE_JOINT_H
#include "hsd_material_bridge.h"
#include "hsd_pobj_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UINT32_MAX is a null descriptor index. Source offsets are immutable archive
 * identities, never emulated addresses. The checked CPU importer owns every
 * input array and byte span until the native handle is destroyed. */
typedef struct MeleeWebNativeJointDesc {
    uint32_t source_offset, flags, child, next, dobj;
    float rotation[3], scale[3], translation[3], inverse_bind[3][4];
    uint8_t has_inverse_bind;
} MeleeWebNativeJointDesc;
typedef struct MeleeWebNativeDObjDesc {
    uint32_t source_offset, next, material, pobj;
} MeleeWebNativeDObjDesc;
typedef struct MeleeWebNativePObjDesc {
    uint32_t source_offset, next;
    MeleeWebPObjView geometry;
    const MeleeWebSkinEnvelope* envelopes;
    uint32_t envelope_count;
} MeleeWebNativePObjDesc;
typedef struct MeleeWebNativeTextureDesc {
    MeleeWebHsdTextureDesc texture;
    uint32_t source_offset, source_id, palette_name;
    uint8_t has_lod;
} MeleeWebNativeTextureDesc;
typedef struct MeleeWebNativeMaterialDesc {
    uint32_t source_offset;
    MeleeWebHsdMaterialDesc material;
    const MeleeWebNativeTextureDesc* textures;
} MeleeWebNativeMaterialDesc;
typedef struct MeleeWebNativeGraph {
    const MeleeWebNativeJointDesc* joints;
    const MeleeWebNativeDObjDesc* dobjs;
    const MeleeWebNativePObjDesc* pobjs;
    const MeleeWebNativeMaterialDesc* materials;
    uint32_t joint_count, dobj_count, pobj_count, material_count, root;
} MeleeWebNativeGraph;
typedef struct MeleeWebNativeJoint MeleeWebNativeJoint;
typedef struct MeleeWebNativeJointStats {
    uint32_t joints, dobjs, pobjs, materials, textures, resolved_envelopes;
    uint8_t first_diffuse[4];
    uint64_t generation;
} MeleeWebNativeJointStats;

/* Original HSD_JObjLoadJoint/ResolveRefs/class methods execute against owned
 * typed descriptors. The original GObj object destructor owns the loaded root.
 * This gate does not render, execute animation, or publish common-data globals.
 * Shutdown releases runtime objects; stale handles remain safe to destroy.
 */
MeleeWebNativeJoint* melee_web_native_joint_create(const MeleeWebNativeGraph* graph,
                                                  char* error, size_t error_size);
/* Narrow original ftCo_800C8F6C consumer: root20 is loaded by the actual
 * function and its first material receives root0.x7D8. Its temporary globals
 * are restored before returning; this is not Fighter_LoadCommonData. */
MeleeWebNativeJoint* melee_web_native_common_joint_create(const MeleeWebNativeGraph* graph,
    const uint8_t diffuse[4], char* error, size_t error_size);
int melee_web_native_joint_destroy(MeleeWebNativeJoint* joint, char* error, size_t error_size);
int melee_web_native_joint_stats(const MeleeWebNativeJoint* joint,
                                MeleeWebNativeJointStats* stats,
                                char* error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
