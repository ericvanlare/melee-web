#include "gameplay_compat.h"
#include "stadium_screen_roots_probe.h"
#include <melee/gr/ground.h>
#include <melee/gr/grpstadium.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>
#include <string.h>

/* Descriptor traversal only: no source objects, processes or stage publication. */
static int walk(HSD_Joint* joint, void* image, StadiumScreenImageView* view,
                unsigned* budget, unsigned depth)
{
    if (depth > 256) return 0;
    for (; joint; joint = joint->next) {
        if (!(*budget)-- ) return 0;
        if (union_type_dobj(joint)) {
            for (HSD_DObjDesc* d = joint->u.dobjdesc; d; d = d->next) {
                if (!(*budget)-- ) return 0;
                if (!d->mobjdesc) continue;
                for (HSD_TObjDesc* t = d->mobjdesc->texdesc; t; t = t->next) {
                    if (!(*budget)-- ) return 0;
                    if (t->imagedesc == image) {
                        ++view->references;
                        view->image = t->imagedesc;
                        view->texture = t;
                        view->material = d->mobjdesc;
                    }
                }
            }
        }
        /* An instance references a joint elsewhere in the descriptor tree. */
        if (!(joint->flags & JOBJ_INSTANCE) && joint->child &&
            !walk(joint->child, image, view, budget, depth + 1)) return 0;
    }
    return 1;
}
int stadium_screen_image_view(void* pointer, uint32_t entry, void* image,
                              StadiumScreenImageView* view)
{
    UnkStageDat* map = pointer;
    if (!map || !view || !image || entry >= (uint32_t)map->unkC ||
        !map->unk8 || !map->unk8[entry].unk0) return 0;
    memset(view, 0, sizeof(*view));
    unsigned budget = 65536;
    return walk(map->unk8[entry].unk0, image, view, &budget, 0);
}
void* stadium_screen_source_public(void* handle, const char* name)
{
    return HSD_ArchiveGetPublicAddress((HSD_Archive*)handle, name);
}

void* stadium_screen_map_entry_joint(void* pointer, uint32_t entry)
{
    UnkStageDat* map = pointer;
    if (!map || entry >= (uint32_t) map->unkC || !map->unk8)
        return NULL;
    return map->unk8[entry].unk0;
}

size_t stadium_screen_stage_info_size(void)
{
    return sizeof(stage_info);
}

int stadium_screen_stage_info_snapshot(void* output, size_t size)
{
    if (!output || size != sizeof(stage_info))
        return 0;
    memcpy(output, &stage_info, sizeof(stage_info));
    return 1;
}

int stadium_screen_stage_is_empty(void)
{
    size_t i;
    if (stage_info.param || stage_info.itemdata || stage_info.coll_data ||
        stage_info.map_ptcl || stage_info.map_texg || stage_info.map_plit)
        return 0;
    for (i = 0; i < sizeof(stage_info.map_gobjs) /
                        sizeof(stage_info.map_gobjs[0]); ++i)
        if (stage_info.map_gobjs[i])
            return 0;
    return 1;
}

StadiumLiveImageQuery stadium_screen_live_image_query(void* view, void* image,
                                                     void* material_sentinel)
{
    HSD_MObj* material = material_sentinel;
    HSD_TObj* texture = grStadium_801D3138(view, image, &material);
    const StadiumLiveImageQuery result = { texture, material };
    return result;
}
