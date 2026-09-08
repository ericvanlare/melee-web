#include "gameplay_edge_state.h"

#include <melee/ft/types.h>
#include <melee/mp/mplib.h>
#include <melee/pl/player.h>

#include <string.h>

int melee_web_edge_snapshot(uint32_t slot, MeleeWebEdgeSnapshot* out)
{
    if (out == NULL || slot >= 4) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    StaticPlayer* player = Player_GetPtrForSlot(slot);
    if (player == NULL || player->player_entity[0] == NULL ||
        player->player_entity[0]->user_data == NULL) {
        return 0;
    }
    Fighter* fp = player->player_entity[0]->user_data;
    CollData* coll = &fp->coll_data;
    out->valid = 1;
    out->motion_id = fp->motion_id;
    out->ground_or_air = fp->ground_or_air;
    out->floor_index = coll->floor.index;
    out->floor_skip = coll->floor_skip;
    out->joint_id_skip = coll->joint_id_skip;
    out->joint_id_only = coll->joint_id_only;
    out->floor_flags = coll->floor.flags;
    out->x34_flags = (uint32_t)(coll->x34_flags.b0 |
                                (coll->x34_flags.b1234 << 1) |
                                (coll->x34_flags.b5 << 5) |
                                (coll->x34_flags.b6 << 6) |
                                (coll->x34_flags.b7 << 7));
    out->x35_flags = (uint32_t)(coll->x35_flags.b0 |
                                (coll->x35_flags.b1234 << 1) |
                                (coll->x35_flags.b5 << 5) |
                                (coll->x35_flags.b6 << 6) |
                                (coll->x35_flags.b7 << 7));
    out->x130_flags = coll->x130_flags;
    out->x130_locked = (coll->x130_flags & CollData_X130_Locked) != 0;
    out->x130_clear = (coll->x130_flags & CollData_X130_Clear) != 0;
    out->x38 = coll->x38;
    out->facing_dir = coll->facing_dir;
    out->fighter_facing_dir = fp->facing_dir;
    out->ledge_cooldown = fp->x2064_ledgeCooldown;
    out->fighter_x2224_b2 = fp->x2224_b2;
    out->fighter_x2219_b1 = fp->x2219_b1;
    out->fighter_x2223_b4 = fp->x2223_b4;
    out->fighter_x221d_b7 = fp->x221D_b7;
    out->nudge_x = fp->xF8_playerNudgeVel.x;
    out->nudge_y = fp->xF8_playerNudgeVel.y;
    out->ledge_id_right = coll->ledge_id_right;
    out->ledge_id_left = coll->ledge_id_left;
    out->env_flags = coll->env_flags;
    out->prev_env_flags = coll->prev_env_flags;
    out->x = fp->cur_pos.x;
    out->prev_x = fp->prev_pos.x;
    out->delta_x = fp->pos_delta.x;
    out->self_x = fp->self_vel.x;
    out->self_y = fp->self_vel.y;
    out->gr_vel = fp->gr_vel;
    out->ecb_bottom_x = coll->ecb.bottom.x;
    out->ecb_bottom_y = coll->ecb.bottom.y;
    out->ecb_left_x = coll->ecb.left.x;
    out->ecb_left_y = coll->ecb.left.y;
    out->ecb_right_x = coll->ecb.right.x;
    out->ecb_right_y = coll->ecb.right.y;
    out->ecb_top_x = coll->ecb.top.x;
    out->ecb_top_y = coll->ecb.top.y;
    out->prev_bottom_x = coll->prev_ecb.bottom.x;
    out->prev_bottom_y = coll->prev_ecb.bottom.y;
    out->prev_left_x = coll->prev_ecb.left.x;
    out->prev_left_y = coll->prev_ecb.left.y;
    out->prev_right_x = coll->prev_ecb.right.x;
    out->prev_right_y = coll->prev_ecb.right.y;
    out->prev_top_x = coll->prev_ecb.top.x;
    out->prev_top_y = coll->prev_ecb.top.y;
    out->desired_bottom_x = coll->desired_ecb.bottom.x;
    out->desired_bottom_y = coll->desired_ecb.bottom.y;
    out->desired_left_x = coll->desired_ecb.left.x;
    out->desired_left_y = coll->desired_ecb.left.y;
    out->desired_right_x = coll->desired_ecb.right.x;
    out->desired_right_y = coll->desired_ecb.right.y;
    out->desired_top_x = coll->desired_ecb.top.x;
    out->desired_top_y = coll->desired_ecb.top.y;
    out->x64_bottom_x = coll->x64_ecb.bottom.x;
    out->x64_bottom_y = coll->x64_ecb.bottom.y;
    out->x64_left_x = coll->x64_ecb.left.x;
    out->x64_left_y = coll->x64_ecb.left.y;
    out->x64_right_x = coll->x64_ecb.right.x;
    out->x64_right_y = coll->x64_ecb.right.y;
    out->x64_top_x = coll->x64_ecb.top.x;
    out->x64_top_y = coll->x64_ecb.top.y;
    out->xe4_bottom_x = coll->xE4_ecb.bottom.x;
    out->xe4_bottom_y = coll->xE4_ecb.bottom.y;
    out->xe4_left_x = coll->xE4_ecb.left.x;
    out->xe4_left_y = coll->xE4_ecb.left.y;
    out->xe4_right_x = coll->xE4_ecb.right.x;
    out->xe4_right_y = coll->xE4_ecb.right.y;
    out->xe4_top_x = coll->xE4_ecb.top.x;
    out->xe4_top_y = coll->xE4_ecb.top.y;
    out->coll_x = coll->cur_pos.x;
    out->coll_y = coll->cur_pos.y;
    out->coll_prev_x = coll->prev_pos.x;
    out->coll_prev_y = coll->prev_pos.y;
    out->coll_last_x = coll->last_pos.x;
    out->coll_last_y = coll->last_pos.y;
    out->coll_x28_x = coll->x28_vec.x;
    out->coll_x28_y = coll->x28_vec.y;
    out->coll_lstick_x = coll->lstick_x;
    out->coll_x13c = coll->x13C;
    out->contact_x = coll->contact.x;
    out->contact_y = coll->contact.y;
    out->floor_normal_x = coll->floor.normal.x;
    out->floor_normal_y = coll->floor.normal.y;
    out->stick_x = fp->input.lstick[0].x;
    out->stick_y = fp->input.lstick[0].y;
    out->held_buttons = fp->input.held_buttons[0];

    CollLine* lines = mpGetGroundCollLine();
    if (lines != NULL && out->floor_index >= 0) {
        out->floor_next = mpLineGetNext(out->floor_index);
        out->floor_prev = mpLineGetPrev(out->floor_index);
        Vec3 v0, v1;
        mpLineGetV0Pos(out->floor_index, &v0);
        mpLineGetV1Pos(out->floor_index, &v1);
        out->floor_v0_x = v0.x;
        out->floor_v0_y = v0.y;
        out->floor_v1_x = v1.x;
        out->floor_v1_y = v1.y;
    } else {
        out->floor_next = -1;
        out->floor_prev = -1;
    }
    return 1;
}
