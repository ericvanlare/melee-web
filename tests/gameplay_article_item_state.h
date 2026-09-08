#ifndef MELEE_WEB_GAMEPLAY_ARTICLE_ITEM_STATE_H
#define MELEE_WEB_GAMEPLAY_ARTICLE_ITEM_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebArticleItemSnapshot {
    uint32_t item_count;
    uint32_t fireball_count;
    float fireball_life_min;
    float fireball_life_max;
    float fireball_pos_x;
    float fireball_pos_y;
    float fireball_vel_x;
    float fireball_vel_y;
    float fireball_special_0;
    float fireball_special_4;
    float fireball_special_8;
    float fireball_special_c;
    float fireball_special_10;
    float fireball_attr_fall_speed;
    float fireball_attr_fall_speed_max;
    float fireball_attr_x50;
    float fireball_attr_x54;
    float fireball_attr_x58;
    float fireball_attr_x5c;
    float fireball_attr_x60;
    uint32_t fireball_hit_state;
    float fireball_hit_damage;
    uint32_t fireball_hit_flags;
    int32_t fireball_damage_max;
} MeleeWebArticleItemSnapshot;

/* Reads the live original p_link 9 item list. The returned fireball fields
 * describe only Item objects whose source kind is It_Kind_Mario_Fire. */
int melee_web_article_item_snapshot(MeleeWebArticleItemSnapshot* out);

#ifdef __cplusplus
}
#endif

#endif
