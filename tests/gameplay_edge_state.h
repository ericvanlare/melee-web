#ifndef MELEE_WEB_GAMEPLAY_EDGE_STATE_H
#define MELEE_WEB_GAMEPLAY_EDGE_STATE_H

#include <stdint.h>

typedef struct MeleeWebEdgeSnapshot {
    int valid;
    int motion_id;
    int ground_or_air;
    int floor_index;
    int floor_skip;
    int joint_id_skip;
    int joint_id_only;
    int floor_next;
    int floor_prev;
    uint32_t floor_flags;
    uint32_t x34_flags;
    uint32_t x35_flags;
    uint32_t x130_flags;
    int x130_locked;
    int x130_clear;
    int x38;
    int facing_dir;
    float fighter_facing_dir;
    int ledge_cooldown;
    int fighter_x2224_b2;
    int fighter_x2219_b1;
    int fighter_x2223_b4;
    int fighter_x221d_b7;
    float nudge_x;
    float nudge_y;
    int ledge_id_right;
    int ledge_id_left;
    int32_t env_flags;
    int32_t prev_env_flags;
    float x;
    float prev_x;
    float delta_x;
    float self_x;
    float self_y;
    float gr_vel;
    float ecb_bottom_x;
    float ecb_bottom_y;
    float ecb_left_x;
    float ecb_left_y;
    float ecb_right_x;
    float ecb_right_y;
    float ecb_top_x;
    float ecb_top_y;
    float prev_bottom_x;
    float prev_bottom_y;
    float prev_left_x;
    float prev_left_y;
    float prev_right_x;
    float prev_right_y;
    float prev_top_x;
    float prev_top_y;
    float desired_bottom_x;
    float desired_bottom_y;
    float desired_left_x;
    float desired_left_y;
    float desired_right_x;
    float desired_right_y;
    float desired_top_x;
    float desired_top_y;
    float x64_bottom_x;
    float x64_bottom_y;
    float x64_left_x;
    float x64_left_y;
    float x64_right_x;
    float x64_right_y;
    float x64_top_x;
    float x64_top_y;
    float xe4_bottom_x;
    float xe4_bottom_y;
    float xe4_left_x;
    float xe4_left_y;
    float xe4_right_x;
    float xe4_right_y;
    float xe4_top_x;
    float xe4_top_y;
    float coll_x;
    float coll_y;
    float coll_prev_x;
    float coll_prev_y;
    float coll_last_x;
    float coll_last_y;
    float coll_x28_x;
    float coll_x28_y;
    float coll_lstick_x;
    int coll_x13c;
    float contact_x;
    float contact_y;
    float floor_normal_x;
    float floor_normal_y;
    float floor_v0_x;
    float floor_v0_y;
    float floor_v1_x;
    float floor_v1_y;
    int stick_x;
    int stick_y;
    uint64_t held_buttons;
} MeleeWebEdgeSnapshot;

#ifdef __cplusplus
extern "C" {
#endif
int melee_web_edge_snapshot(uint32_t slot, MeleeWebEdgeSnapshot* out);
#ifdef __cplusplus
}
#endif

#endif
