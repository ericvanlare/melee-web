#include <melee/ft/types.h>

#include <math.h>

int fox_test_native_fighter_data(void* data)
{
    const ftData* decoded = data;
    if (!decoded || !decoded->x2C || decoded->x2C->dynamicsNum != 1 ||
        decoded->x2C->x4 != 1 || decoded->x2C->x10 ||
        !decoded->x2C->ftDynamicBones || !decoded->x2C->x8)
        return 0;
    const BoneDynamicsDesc* bone = &decoded->x2C->ftDynamicBones->array[0];
    if (bone->bone_id != 17 || bone->dyn_desc.count != 4 || !bone->dyn_desc.data ||
        fabsf(bone->dyn_desc.pos.x - 1.0f) > 0.000001f ||
        fabsf(bone->dyn_desc.pos.y - 1.0f) > 0.000001f ||
        fabsf(bone->dyn_desc.pos.z - 0.0436332f) > 0.000001f)
        return 0;
    const struct lb_00F9_UnkDesc1Inner* parameters =
        (const struct lb_00F9_UnkDesc1Inner*)bone->dyn_desc.data;
    if (parameters[0].unk_0 != 1.0f || parameters[3].unk_0 != 1.0f ||
        fabsf(parameters[0].unk_18 - 0.7853982f) > 0.000001f ||
        fabsf(parameters[3].unk_38 - 0.0523599f) > 0.000001f)
        return 0;
    return decoded->x2C->x8[0].x0 == 41 && decoded->x2C->x8[0].x4.x == 0.0f &&
           decoded->x2C->x8[0].x4.y == 2.0f && decoded->x2C->x8[0].x4.z == 0.0f &&
           decoded->x2C->x8[0].x10 == 3.0f;
}
