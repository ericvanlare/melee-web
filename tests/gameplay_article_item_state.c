#include "gameplay_article_item_state.h"

#include <float.h>
#include <string.h>

#include <melee/it/forward.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/gobj.h>

int melee_web_article_item_snapshot(MeleeWebArticleItemSnapshot* out)
{
    HSD_GObj* gobj;

    if (out == NULL || HSD_GObj_Entities == NULL) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->fireball_life_min = FLT_MAX;
    out->fireball_life_max = -FLT_MAX;
    out->fireball_damage_max = 0;

    for (gobj = HSD_GObj_Entities->items; gobj != NULL; gobj = gobj->next) {
        Item* item = (Item*) gobj->user_data;
        if (item == NULL || item->entity != gobj) {
            continue;
        }
        ++out->item_count;
        if (item->kind != It_Kind_Mario_Fire) {
            continue;
        }
        ++out->fireball_count;
        if (item->xD44_lifeTimer < out->fireball_life_min) {
            out->fireball_life_min = item->xD44_lifeTimer;
        }
        if (item->xD44_lifeTimer > out->fireball_life_max) {
            out->fireball_life_max = item->xD44_lifeTimer;
        }
        out->fireball_pos_x = item->pos.x;
        out->fireball_pos_y = item->pos.y;
        out->fireball_vel_x = item->x40_vel.x;
        out->fireball_vel_y = item->x40_vel.y;
        {
            const float* special = (const float*) item->xC4_article_data->x4_specialAttributes;
            out->fireball_special_0 = special[0];
            out->fireball_special_4 = special[1];
            out->fireball_special_8 = special[2];
            out->fireball_special_c = special[3];
            out->fireball_special_10 = special[4];
        }
        out->fireball_attr_fall_speed = item->xCC_item_attr->x10_fall_speed;
        out->fireball_attr_fall_speed_max = item->xCC_item_attr->x14_fall_speed_max;
        out->fireball_attr_x50 = item->xCC_item_attr->x50;
        out->fireball_attr_x54 = item->xCC_item_attr->x54;
        out->fireball_attr_x58 = item->xCC_item_attr->x58;
        out->fireball_attr_x5c = item->xCC_item_attr->x5c;
        out->fireball_attr_x60 = item->xCC_item_attr->x60_scale;
        out->fireball_hit_state = (uint32_t) item->x5D4_hitboxes[0].hit.state;
        out->fireball_hit_damage = item->x5D4_hitboxes[0].hit.damage;
        out->fireball_hit_flags =
            ((uint32_t) item->x5D4_hitboxes[0].hit.x40_b0 << 0) |
            ((uint32_t) item->x5D4_hitboxes[0].hit.x40_b2 << 2) |
            ((uint32_t) item->x5D4_hitboxes[0].hit.x40_b3 << 3) |
            ((uint32_t) item->x5D4_hitboxes[0].hit.x42_b7 << 7);
        if (item->xC34_damageDealt > out->fireball_damage_max) {
            out->fireball_damage_max = item->xC34_damageDealt;
        }
    }
    if (out->fireball_count == 0) {
        out->fireball_life_min = 0.0f;
        out->fireball_life_max = 0.0f;
    }
    return 1;
}
