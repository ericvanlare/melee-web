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
typedef struct MeleeWebNativeSplineDesc {
    uint32_t source_offset, type, control_count, point_count;
    float tension, total_length;
    const float* points; /* point_count tightly packed XYZ triples */
    const float* segment_lengths; /* control_count normalized arc boundaries */
    const float* segment_polynomials; /* optional (control_count-1)*5 floats */
} MeleeWebNativeSplineDesc;
typedef struct MeleeWebNativeJointDesc {
    uint32_t source_offset, flags, child, next, dobj;
    float rotation[3], scale[3], translation[3], inverse_bind[3][4];
    uint8_t has_inverse_bind;
    const MeleeWebNativeSplineDesc* spline;
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
    uint8_t has_lod, has_tev, tev_fields[28];
    uint32_t tev_active;
} MeleeWebNativeTextureDesc;
typedef struct MeleeWebNativeMaterialDesc {
    uint32_t source_offset;
    MeleeWebHsdMaterialDesc material;
    const MeleeWebNativeTextureDesc* textures;
    uint8_t has_pixel_engine, pixel_engine[12];
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

/* Descriptor-only hydration performs no HSD allocation. Mutable display lists
 * are copied into owned aligned buffers; source loaders may rewrite them. Keep its owner and
 * checked input payloads alive until every source-created JObj is destroyed.
 * The descriptor pointer is HSD_Joint*, the object pointer is HSD_JObj*. */
int melee_web_native_world_enable(char* error, size_t error_size);
MeleeWebNativeJoint* melee_web_native_joint_hydrate(const MeleeWebNativeGraph*, char*, size_t);
void* melee_web_native_joint_descriptor(MeleeWebNativeJoint*, char*, size_t);
void* melee_web_native_joint_descriptor_at(MeleeWebNativeJoint*,uint32_t index,uint32_t expected_source_offset,char*,size_t);
/* Same owner lifetime as the root. Index is the checked graph material index;
 * permits original pre-load storage flag initialization such as map shadows. */
void* melee_web_native_joint_material_descriptor(MeleeWebNativeJoint*,uint32_t,char*,size_t);
void* melee_web_native_joint_object(MeleeWebNativeJoint*, char*, size_t);
/* Material descriptors must come from DatMaterialAnimation and remain alive
 * through runtime removal. These calls execute original HSD animation code. */
int melee_web_native_joint_add_material_animation(MeleeWebNativeJoint*, void*, char*, size_t);
int melee_web_native_joint_request_animation(MeleeWebNativeJoint*, float, char*, size_t);
int melee_web_native_joint_animate(MeleeWebNativeJoint*, char*, size_t);

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
