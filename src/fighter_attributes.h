#ifndef MELEE_WEB_FIGHTER_ATTRIBUTES_H
#define MELEE_WEB_FIGHTER_ATTRIBUTES_H

#include <stdint.h>

/* Scalar schema from pinned ft/types.h and ftMario/types.h. Each entry is
 * (body-relative byte offset, scalar type, portable name, original member).
 * The original-consumer bridge checks EVERY offset and width with the Wasm
 * compiler. Vectors are scalarized; no DAT bytes are native struct overlays.
 * Padding is not semantic data and is initialized to zero during hydration. */
#define MELEE_WEB_ATTRIBUTE_TYPE_F32 float
#define MELEE_WEB_ATTRIBUTE_TYPE_I32 int32_t
#define MELEE_WEB_ATTRIBUTE_TYPE_U32 uint32_t
#define MELEE_WEB_ATTRIBUTE_TYPE_U8 uint8_t
/* Source marks these three Link words as UNK_T (a pointer-sized field).  The
 * portable record retains their 32-bit bits; the native decoder casts them
 * back to source pointer width without pretending to know their semantics. */
#define MELEE_WEB_ATTRIBUTE_TYPE_PTR32 uint32_t

/* Luigi's 0x98 extension is kept in its own source-derived table. */
#include "gameplay_luigi_schema.h"

#define MELEE_WEB_CO_ATTRIBUTE_FIELDS(X) \
    X(0x000, F32, walk_accel_mul, walk_accel_mul) \
    X(0x004, F32, walk_accel_base, walk_accel_base) \
    X(0x008, F32, walk_max_vel, walk_max_vel) \
    X(0x00c, F32, slow_walk_max, slow_walk_max) \
    X(0x010, F32, mid_walk_point, mid_walk_point) \
    X(0x014, F32, fast_walk_min, fast_walk_min) \
    X(0x018, F32, ground_friction, ground_friction) \
    X(0x01c, F32, dash_initial_velocity, dash_initial_velocity) \
    X(0x020, F32, dash_accel_mul, dash_accel_mul) \
    X(0x024, F32, dash_accel_base, dash_accel_base) \
    X(0x028, F32, dash_max_velocity, dash_max_velocity) \
    X(0x02c, F32, run_animation_scaling, run_animation_scaling) \
    X(0x030, F32, max_run_brake_frames, max_run_brake_frames) \
    X(0x034, F32, ground_max_horizontal_velocity, ground_max_horizontal_velocity) \
    X(0x038, F32, jump_startup_time, jump_startup_time) \
    X(0x03c, F32, jump_h_initial_velocity, jump_h_initial_velocity) \
    X(0x040, F32, jump_v_initial_velocity, jump_v_initial_velocity) \
    X(0x044, F32, ground_to_air_jump_momentum_multiplier, ground_to_air_jump_momentum_multiplier) \
    X(0x048, F32, jump_h_max_velocity, jump_h_max_velocity) \
    X(0x04c, F32, hop_v_initial_velocity, hop_v_initial_velocity) \
    X(0x050, F32, air_jump_v_multiplier, air_jump_v_multiplier) \
    X(0x054, F32, air_jump_h_multiplier, air_jump_h_multiplier) \
    X(0x058, I32, max_jumps, max_jumps) \
    X(0x05c, F32, gravity, gravity) \
    X(0x060, F32, terminal_velocity, terminal_velocity) \
    X(0x064, F32, air_drift_stick_mul, air_drift_stick_mul) \
    X(0x068, F32, aerial_drift_base, aerial_drift_base) \
    X(0x06c, F32, air_drift_max, air_drift_max) \
    X(0x070, F32, aerial_friction, aerial_friction) \
    X(0x074, F32, fast_fall_velocity, fast_fall_velocity) \
    X(0x078, F32, air_max_horizontal_velocity, air_max_horizontal_velocity) \
    X(0x07c, F32, jab_2_input_window, jab_2_input_window) \
    X(0x080, F32, jab_3_input_window, jab_3_input_window) \
    X(0x084, F32, standing_turn_frames, standing_turn_frames) \
    X(0x088, F32, weight, weight) \
    X(0x08c, F32, model_scaling, model_scaling) \
    X(0x090, F32, initial_shield_size, initial_shield_size) \
    X(0x094, F32, shield_break_initial_velocity, shield_break_initial_velocity) \
    X(0x098, I32, rapid_jab_window, rapid_jab_window) \
    X(0x09c, F32, clank_animation_length, clank_animation_length) \
    X(0x0a0, I32, hit_spark_variant, hit_spark_variant) \
    X(0x0a4, I32, unused_0, unused_0) \
    X(0x0a8, F32, ledge_jump_horizontal_velocity, ledge_jump_horizontal_velocity) \
    X(0x0ac, F32, ledge_jump_vertical_velocity, ledge_jump_vertical_velocity) \
    X(0x0b0, F32, item_throw_velocity_multiplier, item_throw_velocity_multiplier) \
    X(0x0b4, F32, heavy_throw_velocity_multiplier, heavy_throw_velocity_multiplier) \
    X(0x0b8, F32, specials_ground_speed_retention, specials_ground_speed_retention) \
    X(0x0bc, F32, xBC_size, xBC.size) \
    X(0x0c0, F32, xBC_x4_x, xBC.x4.x) \
    X(0x0c4, F32, xBC_x4_y, xBC.x4.y) \
    X(0x0c8, F32, xBC_x4_z, xBC.x4.z) \
    X(0x0cc, F32, xBC_x10_x, xBC.x10.x) \
    X(0x0d0, F32, xBC_x10_y, xBC.x10.y) \
    X(0x0d4, F32, xBC_x10_z, xBC.x10.z) \
    X(0x0d8, F32, xBC_x1C, xBC.x1C) \
    X(0x0dc, F32, xDC, xDC) \
    X(0x0e0, F32, kirby_b_star_damage, kirby_b_star_damage) \
    X(0x0e4, F32, normal_landing_lag, normal_landing_lag) \
    X(0x0e8, F32, landingairn_lag, landingairn_lag) \
    X(0x0ec, F32, landingairf_lag, landingairf_lag) \
    X(0x0f0, F32, landingairb_lag, landingairb_lag) \
    X(0x0f4, F32, landingairhi_lag, landingairhi_lag) \
    X(0x0f8, F32, landingairlw_lag, landingairlw_lag) \
    X(0x0fc, F32, name_tag_height, name_tag_height) \
    X(0x100, F32, passivewall_vel_x, passivewall_vel_x) \
    X(0x104, F32, wall_jump_horizontal_velocity, wall_jump_horizontal_velocity) \
    X(0x108, F32, wall_jump_vertical_velocity, wall_jump_vertical_velocity) \
    X(0x10c, F32, passiveceil_vel_x, passiveceil_vel_x) \
    X(0x110, F32, trophy_scale, trophy_scale) \
    X(0x114, F32, x114_x, x114.x) \
    X(0x118, F32, x114_y, x114.y) \
    X(0x11c, F32, x114_z, x114.z) \
    X(0x120, F32, x120_x, x120.x) \
    X(0x124, F32, x120_y, x120.y) \
    X(0x128, F32, x120_z, x120.z) \
    X(0x12c, F32, x12C, x12C) \
    X(0x130, F32, x130_x, x130.x) \
    X(0x134, F32, x130_y, x130.y) \
    X(0x138, F32, x130_z, x130.z) \
    X(0x13c, F32, x13C, x13C) \
    X(0x140, F32, screw_attack_launch_velocity, screw_attack_launch_velocity) \
    X(0x144, F32, x144, x144) \
    X(0x148, F32, wall_jump_min_approach_speed, wall_jump_min_approach_speed) \
    X(0x14c, F32, damageice_ice_size, damageice_ice_size) \
    X(0x150, F32, x150_damageice_unk, x150_damageice_unk) \
    X(0x154, F32, x154_damageice_unk, x154_damageice_unk) \
    X(0x158, F32, damageicejump_vel_y, damageicejump_vel_y) \
    X(0x15c, F32, damageicejump_vel_x_mult, damageicejump_vel_x_mult) \
    X(0x160, F32, respawn_platform_scale, respawn_platform_scale) \
    X(0x164, F32, warp_star_hitbox_scale, warp_star_hitbox_scale) \
    X(0x168, F32, x168, x168) \
    X(0x16c, I32, camera_zoom_target_bone, camera_zoom_target_bone) \
    X(0x170, F32, x170_x, x170.x) \
    X(0x174, F32, x170_y, x170.y) \
    X(0x178, F32, x170_z, x170.z) \
    X(0x17c, F32, x17C, x17C) \
    X(0x180, U8, weight_independent_throws_mask, weight_independent_throws_mask)

#define MELEE_WEB_MARIO_ATTRIBUTE_FIELDS(X) \
    X(0x000, F32, specials_vel_x_decay, specials.vel_x_decay) \
    X(0x004, F32, specials_vel_x, specials.vel.x) \
    X(0x008, F32, specials_vel_y, specials.vel.y) \
    X(0x00c, F32, specials_grav, specials.grav) \
    X(0x010, F32, specials_terminal_vel, specials.terminal_vel) \
    X(0x014, I32, specials_cape_kind, specials.cape_kind) \
    X(0x018, F32, specialhi_freefall_mobility, specialhi.freefall_mobility) \
    X(0x01c, F32, specialhi_landing_lag, specialhi.landing_lag) \
    X(0x020, F32, specialhi_reverse_stick_range, specialhi.reverse_stick_range) \
    X(0x024, F32, specialhi_momentum_stick_range, specialhi.momentum_stick_range) \
    X(0x028, F32, specialhi_angle_diff, specialhi.angle_diff) \
    X(0x02c, F32, specialhi_vel_x, specialhi.vel_x) \
    X(0x030, F32, specialhi_grav, specialhi.grav) \
    X(0x034, F32, specialhi_vel_mul, specialhi.vel_mul) \
    X(0x038, F32, speciallw_vel_y, speciallw.vel_y) \
    X(0x03c, F32, speciallw_momentum_x, speciallw.momentum_x) \
    X(0x040, F32, speciallw_air_momentum_x, speciallw.air_momentum_x) \
    X(0x044, F32, speciallw_momentum_x_mul, speciallw.momentum_x_mul) \
    X(0x048, F32, speciallw_air_momentum_x_mul, speciallw.air_momentum_x_mul) \
    X(0x04c, F32, speciallw_friction_end, speciallw.friction_end) \
    X(0x050, I32, speciallw_unk0, speciallw.unk0) \
    X(0x054, F32, speciallw_tap_y_vel_max, speciallw.tap_y_vel_max) \
    X(0x058, F32, speciallw_tap_grav, speciallw.tap_grav) \
    X(0x05c, I32, speciallw_landing_lag, speciallw.landing_lag) \
    X(0x060, U32, cape_reflection_x0_bone_id, cape_reflection.x0_bone_id) \
    X(0x064, I32, cape_reflection_x4_max_damage, cape_reflection.x4_max_damage) \
    X(0x068, F32, cape_reflection_x8_offset_x, cape_reflection.x8_offset.x) \
    X(0x06c, F32, cape_reflection_x8_offset_y, cape_reflection.x8_offset.y) \
    X(0x070, F32, cape_reflection_x8_offset_z, cape_reflection.x8_offset.z) \
    X(0x074, F32, cape_reflection_x14_size, cape_reflection.x14_size) \
    X(0x078, F32, cape_reflection_x18_damage_mul, cape_reflection.x18_damage_mul) \
    X(0x07c, F32, cape_reflection_x1C_speed_mul, cape_reflection.x1C_speed_mul) \
    X(0x080, U8, cape_reflection_x20_behavior, cape_reflection.x20_behavior)

/* Captain and Ganondorf use the original ftCaptain_DatAttrs extension.  The
 * source keeps several fields intentionally unnamed; retain those names and
 * their scalar widths instead of collapsing the record to a guessed subset. */
#define MELEE_WEB_CAPTAIN_ATTRIBUTE_FIELDS(X) \
    X(0x000, F32, specialn_stick_range_y_neg, specialn_stick_range_y_neg) \
    X(0x004, F32, specialn_stick_range_y_pos, specialn_stick_range_y_pos) \
    X(0x008, F32, specialn_angle_diff, specialn_angle_diff) \
    X(0x00c, F32, specialn_vel_x, specialn_vel_x) \
    X(0x010, F32, specialn_vel_mul, specialn_vel_mul) \
    X(0x014, F32, specials_gr_vel_x, specials_gr_vel_x) \
    X(0x018, F32, specials_grav, specials_grav) \
    X(0x01c, F32, specials_terminal_vel, specials_terminal_vel) \
    X(0x020, F32, specials_unk0, specials_unk0) \
    X(0x024, F32, specials_unk1, specials_unk1) \
    X(0x028, F32, specials_unk2, specials_unk2) \
    X(0x02c, F32, specials_unk3, specials_unk3) \
    X(0x030, F32, specials_unk4, specials_unk4) \
    X(0x034, F32, specials_unk5, specials_unk5) \
    X(0x038, F32, specials_miss_landing_lag, specials_miss_landing_lag) \
    X(0x03c, F32, specials_hit_landing_lag, specials_hit_landing_lag) \
    X(0x040, F32, specialhi_air_friction_mul, specialhi_air_friction_mul) \
    X(0x044, F32, specialhi_horz_vel, specialhi_horz_vel) \
    X(0x048, F32, specialhi_freefall_air_spd_mul, specialhi_freefall_air_spd_mul) \
    X(0x04c, F32, specialhi_landing_lag, specialhi_landing_lag) \
    X(0x050, F32, specialhi_unk0, specialhi_unk0) \
    X(0x054, F32, specialhi_unk1, specialhi_unk1) \
    X(0x058, F32, specialhi_input_var, specialhi_input_var) \
    X(0x05c, F32, specialhi_unk2, specialhi_unk2) \
    X(0x060, F32, specialhi_catch_grav, specialhi_catch_grav) \
    X(0x064, I32, specialhi_air_var, specialhi_air_var) \
    X(0x068, F32, x68, x68) \
    X(0x06c, U32, speciallw_unk1, speciallw_unk1) \
    X(0x070, F32, speciallw_flame_particle_angle, speciallw_flame_particle_angle) \
    X(0x074, F32, speciallw_on_hit_spd_modifier, speciallw_on_hit_spd_modifier) \
    X(0x078, I32, speciallw_unk2, speciallw_unk2) \
    X(0x07c, F32, speciallw_ground_lag_mul, speciallw_ground_lag_mul) \
    X(0x080, F32, speciallw_landing_lag_mul, speciallw_landing_lag_mul) \
    X(0x084, F32, speciallw_ground_traction, speciallw_ground_traction) \
    X(0x088, F32, speciallw_air_landing_traction, speciallw_air_landing_traction)

/* Fox and Falco share the original ftFox_DatAttrs layout.  Their special
 * moves are implemented by the same source routines, while PlFx.dat and
 * PlFc.dat provide different scalar values and Article identities.  Keep the
 * serialized offsets explicit so the two kinds can share the decoder without
 * treating one character's attributes as the other character's data. */
#define MELEE_WEB_FOX_ATTRIBUTE_FIELDS(X) \
    X(0x000, F32, blaster_x0, x0_FOX_BLASTER_UNK1) \
    X(0x004, F32, blaster_x4, x4_FOX_BLASTER_UNK2) \
    X(0x008, F32, blaster_x8, x8_FOX_BLASTER_UNK3) \
    X(0x00c, F32, blaster_xC, xC_FOX_BLASTER_UNK4) \
    X(0x010, F32, blaster_angle, x10_FOX_BLASTER_ANGLE) \
    X(0x014, F32, blaster_velocity, x14_FOX_BLASTER_VEL) \
    X(0x018, F32, blaster_landing_lag, x18_FOX_BLASTER_LANDING_LAG) \
    X(0x01c, U32, blaster_shot_item_kind, x1C_FOX_BLASTER_SHOT_ITKIND) \
    X(0x020, U32, blaster_gun_item_kind, x20_FOX_BLASTER_GUN_ITKIND) \
    X(0x024, F32, illusion_gravity_delay, x24_FOX_ILLUSION_GRAVITY_DELAY) \
    X(0x028, F32, illusion_ground_velocity, x28_FOX_ILLUSION_GROUND_VEL_X) \
    X(0x02c, F32, illusion_x2C, x2C_FOX_ILLUSION_UNK1) \
    X(0x030, F32, illusion_x30, x30_FOX_ILLUSION_UNK2) \
    X(0x034, F32, illusion_ground_end_velocity, x34_FOX_ILLUSION_GROUND_END_VEL_X) \
    X(0x038, F32, illusion_ground_friction, x38_FOX_ILLUSION_GROUND_FRICTION) \
    X(0x03c, F32, illusion_air_end_velocity, x3C_FOX_ILLUSION_AIR_END_VEL_X) \
    X(0x040, F32, illusion_air_multiplier, x40_FOX_ILLUSION_AIR_MUL_X) \
    X(0x044, F32, illusion_fall_accel, x44_FOX_ILLUSION_FALL_ACCEL) \
    X(0x048, F32, illusion_terminal_velocity, x48_FOX_ILLUSION_TERMINAL_VELOCITY) \
    X(0x04c, F32, illusion_freefall_mobility, x4C_FOX_ILLUSION_FREEFALL_MOBILITY) \
    X(0x050, F32, illusion_landing_lag, x50_FOX_ILLUSION_LANDING_LAG) \
    X(0x054, F32, firefox_gravity_delay, x54_FOX_FIREFOX_GRAVITY_DELAY) \
    X(0x058, F32, firefox_velocity_x, x58_FOX_FIREFOX_VEL_X) \
    X(0x05c, F32, firefox_air_momentum_x, x5C_FOX_FIREFOX_AIR_MOMENTUM_PRESERVE_X) \
    X(0x060, F32, firefox_fall_accel, x60_FOX_FIREFOX_FALL_ACCEL) \
    X(0x064, F32, firefox_direction_stick_range, x64_FOX_FIREFOX_DIRECTION_STICK_RANGE_MIN) \
    X(0x068, F32, firefox_duration, x68_FOX_FIREFOX_DURATION) \
    X(0x06c, I32, firefox_bounce_var, x6C_FOX_FIREFOX_BOUNCE_VAR) \
    X(0x070, F32, firefox_duration_end, x70_FOX_FIREFOX_DURATION_END) \
    X(0x074, F32, firefox_speed, x74_FOX_FIREFOX_SPEED) \
    X(0x078, F32, firefox_reverse_accel, x78_FOX_FIREFOX_REVERSE_ACCEL) \
    X(0x07c, F32, firefox_ground_momentum_end, x7C_FOX_FIREFOX_GROUND_MOMENTUM_END) \
    X(0x080, F32, firefox_x80, x80_FOX_FIREFOX_UNK2) \
    X(0x084, F32, firefox_bound_velocity_x, x84_FOX_FIREFOX_BOUND_VEL_X) \
    X(0x088, F32, firefox_facing_stick_range, x88_FOX_FIREFOX_FACING_STICK_RANGE_MIN) \
    X(0x08c, F32, firefox_freefall_mobility, x8C_FOX_FIREFOX_FREEFALL_MOBILITY) \
    X(0x090, F32, firefox_landing_lag, x90_FOX_FIREFOX_LANDING_LAG) \
    X(0x094, F32, firefox_bound_angle, x94_FOX_FIREFOX_BOUND_ANGLE) \
    X(0x098, F32, reflector_release_lag, x98_FOX_REFLECTOR_RELEASE_LAG) \
    X(0x09c, F32, reflector_turn_frames, x9C_FOX_REFLECTOR_TURN_FRAMES) \
    X(0x0a0, F32, reflector_xA0, xA0_FOX_REFLECTOR_UNK1) \
    X(0x0a4, I32, reflector_gravity_delay, xA4_FOX_REFLECTOR_GRAVITY_DELAY) \
    X(0x0a8, F32, reflector_momentum_x, xA8_FOX_REFLECTOR_MOMENTUM_PRESERVE_X) \
    X(0x0ac, F32, reflector_fall_accel, xAC_FOX_REFLECTOR_FALL_ACCEL) \
    X(0x0b0, U32, reflector_bone_id, xB0_FOX_REFLECTOR_REFLECTION.x0_bone_id) \
    X(0x0b4, I32, reflector_max_damage, xB0_FOX_REFLECTOR_REFLECTION.x4_max_damage) \
    X(0x0b8, F32, reflector_offset_x, xB0_FOX_REFLECTOR_REFLECTION.x8_offset.x) \
    X(0x0bc, F32, reflector_offset_y, xB0_FOX_REFLECTOR_REFLECTION.x8_offset.y) \
    X(0x0c0, F32, reflector_offset_z, xB0_FOX_REFLECTOR_REFLECTION.x8_offset.z) \
    X(0x0c4, F32, reflector_size, xB0_FOX_REFLECTOR_REFLECTION.x14_size) \
    X(0x0c8, F32, reflector_damage_multiplier, xB0_FOX_REFLECTOR_REFLECTION.x18_damage_mul) \
    X(0x0cc, F32, reflector_speed_multiplier, xB0_FOX_REFLECTOR_REFLECTION.x1C_speed_mul) \
    X(0x0d0, U8, reflector_behavior, xB0_FOX_REFLECTOR_REFLECTION.x20_behavior)

/* Exact MarsAttributes layout from ftMars/types.h. Marth and Roy share this
 * source ABI, while their DAT values and source callbacks remain distinct. */
#define MELEE_WEB_MARS_ATTRIBUTE_FIELDS(X) \
    X(0x000, I32, x0, x0) \
    X(0x004, I32, x4, x4) \
    X(0x008, I32, x8, x8) \
    X(0x00c, F32, specialn_friction, specialn_friction) \
    X(0x010, F32, specialn_start_friction, specialn_start_friction) \
    X(0x014, F32, x14, x14) \
    X(0x018, F32, x18, x18) \
    X(0x01c, F32, x1C, x1C) \
    X(0x020, F32, x20, x20) \
    X(0x024, F32, x24, x24) \
    X(0x028, F32, x28, x28) \
    X(0x02c, F32, x2C, x2C) \
    X(0x030, F32, x30, x30) \
    X(0x034, F32, x34, x34) \
    X(0x038, F32, x38, x38) \
    X(0x03c, F32, x3C, x3C) \
    X(0x040, F32, x40, x40) \
    X(0x044, F32, x44, x44) \
    X(0x048, F32, x48, x48) \
    X(0x04c, F32, x4C, x4C) \
    X(0x050, F32, x50, x50) \
    X(0x054, F32, x54, x54) \
    X(0x058, F32, x58, x58) \
    X(0x05c, F32, x5C, x5C) \
    X(0x060, F32, x60, x60) \
    X(0x064, I32, absorb_bone, x64.x0_bone_id) \
    X(0x068, F32, absorb_offset_x, x64.x4_offset.x) \
    X(0x06c, F32, absorb_offset_y, x64.x4_offset.y) \
    X(0x070, F32, absorb_offset_z, x64.x4_offset.z) \
    X(0x074, F32, absorb_size, x64.x10_size) \
    X(0x078, F32, sword_x0, x78.x0) \
    X(0x07c, F32, sword_x4, x78.x4) \
    X(0x080, U8, sword_x8, x78.x8) \
    X(0x081, U8, sword_x9, x78.x9) \
    X(0x082, U8, sword_xA, x78.xA) \
    X(0x083, U8, sword_xB, x78.xB) \
    X(0x084, U8, sword_xC, x78.xC) \
    X(0x085, U8, sword_xD, x78.xD) \
    X(0x086, U8, sword_xE, x78.xE) \
    X(0x087, U8, sword_xF, x78.xF) \
    X(0x088, U8, sword_x10, x78.x10) \
    X(0x08c, I32, sword_x14, x78.x14) \
    X(0x090, F32, sword_x18, x78.x18) \
    X(0x094, F32, sword_x1C, x78.x1C)

/* Link and Young Link share the original ftLk_DatAttrs record.  The two
 * fighters still retain separate source callbacks and DAT values; only this
 * extension ABI is shared.  The four-byte filler at 0xc0 is intentionally not
 * exposed as a semantic field. */
#define MELEE_WEB_LINK_ATTRIBUTE_FIELDS(X) \
    X(0x000, F32, x0, x0) \
    X(0x004, F32, specialn_anim_rate, specialn_anim_rate) \
    X(0x008, F32, x8, x8) \
    X(0x00c, I32, xC, xC) \
    X(0x010, I32, x10, x10) \
    X(0x014, F32, x14, x14) \
    X(0x018, F32, x18, x18) \
    X(0x01c, F32, x1C, x1C) \
    X(0x020, F32, x20, x20) \
    X(0x024, F32, x24, x24) \
    X(0x028, F32, specialhi_pos_y_offset, specialhi_pos_y_offset) \
    X(0x02c, I32, x2C, x2C) \
    X(0x030, F32, x30, x30) \
    X(0x034, F32, x34, x34) \
    X(0x038, F32, specialairhi_drift_stick_mul, specialairhi_drift_stick_mul) \
    X(0x03c, F32, specialairhi_drift_max_mul, specialairhi_drift_max_mul) \
    X(0x040, F32, x40, x40) \
    X(0x044, F32, specialhi_grav_mul, specialhi_grav_mul) \
    X(0x048, I32, x48, x48) \
    X(0x04c, F32, attackairlw_hit_vel_y, attackairlw_hit_vel_y) \
    X(0x050, F32, attackairlw_hit_anim_frame_start, attackairlw_hit_anim_frame_start) \
    X(0x054, F32, attackairlw_hit_anim_frame_end, attackairlw_hit_anim_frame_end) \
    X(0x058, U32, attackairlw_anim_flags_0, attackairlw_anim_flags[0]) \
    X(0x05c, U32, attackairlw_anim_flags_1, attackairlw_anim_flags[1]) \
    X(0x060, U32, attackairlw_anim_flags_2, attackairlw_anim_flags[2]) \
    X(0x064, F32, sword_x0, x64.x0) \
    X(0x068, F32, sword_x4, x64.x4) \
    X(0x06c, U8, sword_x8, x64.x8) \
    X(0x06d, U8, sword_x9, x64.x9) \
    X(0x06e, U8, sword_xA, x64.xA) \
    X(0x06f, U8, sword_xB, x64.xB) \
    X(0x070, U8, sword_xC, x64.xC) \
    X(0x071, U8, sword_xD, x64.xD) \
    X(0x072, U8, sword_xE, x64.xE) \
    X(0x073, U8, sword_xF, x64.xF) \
    X(0x074, U8, sword_x10, x64.x10) \
    X(0x078, I32, sword_x14, x64.x14) \
    X(0x07c, F32, sword_x18, x64.x18) \
    X(0x080, F32, sword_x1C, x64.x1C) \
    X(0x084, I32, x84, x84) \
    X(0x088, I32, x88, x88) \
    X(0x08c, I32, x8C, x8C) \
    X(0x090, I32, x90, x90) \
    X(0x094, PTR32, x94, x94) \
    X(0x098, I32, x98, x98) \
    X(0x09c, PTR32, x9C, x9C) \
    X(0x0a0, PTR32, xA0, xA0) \
    X(0x0a4, I32, xA4, xA4) \
    X(0x0a8, I32, xA8, xA8) \
    X(0x0ac, I32, xAC, xAC) \
    X(0x0b0, I32, xB0, xB0) \
    X(0x0b4, F32, xB4, xB4) \
    X(0x0b8, I32, xB8, xB8) \
    X(0x0bc, I32, xBC, xBC) \
    X(0x0c4, I32, absorb_bone, xC4.x0_bone_id) \
    X(0x0c8, F32, absorb_offset_x, xC4.x4_offset.x) \
    X(0x0cc, F32, absorb_offset_y, xC4.x4_offset.y) \
    X(0x0d0, F32, absorb_offset_z, xC4.x4_offset.z) \
    X(0x0d4, F32, absorb_size, xC4.x10_size) \
    X(0x0d8, F32, xD8, xD8)

/* Ness owns a unique 0xDC source extension. The PK Flash/PK Thunder and
 * PSI Magnet loop counters and gravity delays are serialized integers; the
 * PK Fire trajectories, Yo-Yo scalars and the two descriptor records are
 * floats. x98 is the original AbsorbDesc and xB8 the original ReflectDesc,
 * so their source member designators stay in each row. */
#define MELEE_WEB_NESS_ATTRIBUTE_FIELDS(X) \
    X(0x000, I32, pkflash_timer1_loopframes, x0_PKFLASH_TIMER1_LOOPFRAMES) \
    X(0x004, I32, pkflash_timer2_loopframes, x4_PKFLASH_TIMER2_LOOPFRAMES) \
    X(0x008, I32, pkflash_gravity_delay, x8_PKFLASH_GRAVITY_DELAY) \
    X(0x00c, I32, pkflash_minchargeframes, xC_PKFLASH_MINCHARGEFRAMES) \
    X(0x010, F32, pkflash_unk1, x10_PKFLASH_UNK1) \
    X(0x014, F32, pkflash_fall_accel, x14_PKFLASH_FALL_ACCEL) \
    X(0x018, F32, pkflash_unk2, x18_PKFLASH_UNK2) \
    X(0x01c, F32, pkflash_landing_lag, x1C_PKFLASH_LANDING_LAG) \
    X(0x020, F32, pkfire_aerial_launch_trajectory, x20_PKFIRE_AERIAL_LAUNCH_TRAJECTORY) \
    X(0x024, F32, pkfire_aerial_velocity, x24_PKFIRE_AERIAL_VELOCITY) \
    X(0x028, F32, pkfire_grounded_launch_trajectory, x28_PKFIRE_GROUNDED_LAUNCH_TRAJECTORY) \
    X(0x02c, F32, pkfire_grounded_velocity, x2C_PKFIRE_GROUNDED_VELOCITY) \
    X(0x030, F32, pkfire_spawn_x, x30_PKFIRE_SPAWN_X) \
    X(0x034, F32, pkfire_spawn_y, x34_PKFIRE_SPAWN_Y) \
    X(0x038, F32, pkfire_landing_lag, x38_PKFIRE_LANDING_LAG) \
    X(0x03c, F32, pkthunder_unk1, x3C_PK_THUNDER_UNK1) \
    X(0x040, U32, pkthunder_loop1, x40_PK_THUNDER_LOOP1) \
    X(0x044, U32, pkthunder_loop2, x44_PK_THUNDER_LOOP2) \
    X(0x048, U32, pkthunder_gravity_delay, x48_PK_THUNDER_GRAVITY_DELAY) \
    X(0x04c, F32, pkthunder_unk2, x4C_PK_THUNDER_UNK2) \
    X(0x050, F32, pkthunder_fall_accel, x50_PK_THUNDER_FALL_ACCEL) \
    X(0x054, F32, pkthunder2_momentum, x54_PK_THUNDER_2_MOMENTUM) \
    X(0x058, F32, pkthunder2_unk1, x58_PK_THUNDER_2_UNK1) \
    X(0x05c, F32, pkthunder2_deceleration_rate, x5C_PK_THUNDER_2_DECELERATION_RATE) \
    X(0x060, F32, pkthunder2_knockdown_angle, x60_PK_THUNDER_2_KNOCKDOWN_ANGLE) \
    X(0x064, F32, pkthunder2_wallhug_angle, x64_PK_THUNDER_2_WALLHUG_ANGLE) \
    X(0x068, F32, pkthunder2_unk2, x68_PK_THUNDER_2_UNK2) \
    X(0x06c, F32, pkthunder2_freefall_anim_blend, x6C_PK_THUNDER_2_FREEFALL_ANIM_BLEND) \
    X(0x070, F32, pkthunder2_landing_lag, x70_PK_THUNDER_2_LANDING_LAG) \
    X(0x074, F32, psimagnet_release_lag, x74_PSI_MAGNET_RELEASE_LAG) \
    X(0x078, F32, psimagnet_unk1, x78_PSI_MAGNET_UNK1) \
    X(0x07c, F32, psimagnet_unk2, x7C_PSI_MAGNET_UNK2) \
    X(0x080, F32, psimagnet_unk3, x80_PSI_MAGNET_UNK3) \
    X(0x084, I32, psimagnet_frames_before_gravity, x84_PSI_MAGNET_FRAMES_BEFORE_GRAVITY) \
    X(0x088, F32, psimagnet_momentum_preservation, x88_PSI_MAGNET_MOMENTUM_PRESERVATION) \
    X(0x08c, F32, psimagnet_fall_accel, x8C_PSI_MAGNET_FALL_ACCEL) \
    X(0x090, F32, psimagnet_unk4, x90_PSI_MAGNET_UNK4) \
    X(0x094, F32, psimagnet_heal_mul, x94_PSI_MAGNET_HEAL_MUL) \
    X(0x098, I32, psimagnet_absorb_bone, x98_PSI_MAGNET_ABSORPTION.x0_bone_id) \
    X(0x09c, F32, psimagnet_absorb_offset_x, x98_PSI_MAGNET_ABSORPTION.x4_offset.x) \
    X(0x0a0, F32, psimagnet_absorb_offset_y, x98_PSI_MAGNET_ABSORPTION.x4_offset.y) \
    X(0x0a4, F32, psimagnet_absorb_offset_z, x98_PSI_MAGNET_ABSORPTION.x4_offset.z) \
    X(0x0a8, F32, psimagnet_absorb_size, x98_PSI_MAGNET_ABSORPTION.x10_size) \
    X(0x0ac, F32, yoyo_charge_duration, xAC_YOYO_CHARGE_DURATION) \
    X(0x0b0, F32, yoyo_damage_mul, xB0_YOYO_DAMAGE_MUL) \
    X(0x0b4, F32, yoyo_rehit_rate, xB4_YOYO_REHIT_RATE) \
    X(0x0b8, U32, bat_reflect_bone_id, xB8_BASEBALL_BAT.x0_bone_id) \
    X(0x0bc, I32, bat_reflect_max_damage, xB8_BASEBALL_BAT.x4_max_damage) \
    X(0x0c0, F32, bat_reflect_offset_x, xB8_BASEBALL_BAT.x8_offset.x) \
    X(0x0c4, F32, bat_reflect_offset_y, xB8_BASEBALL_BAT.x8_offset.y) \
    X(0x0c8, F32, bat_reflect_offset_z, xB8_BASEBALL_BAT.x8_offset.z) \
    X(0x0cc, F32, bat_reflect_size, xB8_BASEBALL_BAT.x14_size) \
    X(0x0d0, F32, bat_reflect_damage_mul, xB8_BASEBALL_BAT.x18_damage_mul) \
    X(0x0d4, F32, bat_reflect_speed_mul, xB8_BASEBALL_BAT.x1C_speed_mul) \
    X(0x0d8, U8, bat_reflect_behavior, xB8_BASEBALL_BAT.x20_behavior)

/* Peach owns the unique 0xC0 ftPe_DatAttrs extension. The float-fall anim
 * starts are authored zero and filled at load from motions 18/19
 * (ftPe_Init_OnLoad -> lbAnim_8001E8F8); the Toad counter's held-item table
 * keeps its source {odds, ItemKind} pairs. The pinned header annotates
 * speciallw_item_table at +0x1C, but three 8-byte entries cannot end before
 * the +0x30 member and the real C layout places the table at +0x18, which is
 * where the decoded pairs (2,0x06),(3,0x07),(1,0x0C) and the pickVeg consumer
 * (ftpeachspeciallw.c) both land. xAC is the original AbsorbDesc, so its
 * source member designators stay in the rows. */
#define MELEE_WEB_PEACH_ATTRIBUTE_FIELDS(X) \
    X(0x000, F32, floatfallf_anim_start, floatfallf_anim_start) \
    X(0x004, F32, floatfallb_anim_start, floatfallb_anim_start) \
    X(0x008, F32, floatfall_anim_start_offset, floatfall_anim_start_offset) \
    X(0x00c, F32, xC, xC) \
    X(0x010, I32, speciallw_item_table_count, speciallw_item_table_count) \
    X(0x014, I32, x14, x14) \
    X(0x018, I32, speciallw_item_0_randi_max, speciallw_item_table[0].randi_max) \
    X(0x01c, I32, speciallw_item_0_kind, speciallw_item_table[0].kind) \
    X(0x020, I32, speciallw_item_1_randi_max, speciallw_item_table[1].randi_max) \
    X(0x024, I32, speciallw_item_1_kind, speciallw_item_table[1].kind) \
    X(0x028, I32, speciallw_item_2_randi_max, speciallw_item_table[2].randi_max) \
    X(0x02c, I32, speciallw_item_2_kind, speciallw_item_table[2].kind) \
    X(0x030, I32, x30, x30) \
    X(0x034, F32, x34, x34) \
    X(0x038, F32, specials_start_accel, specials_start_accel) \
    X(0x03c, F32, specials_start_vel_x, specials_start_vel_x) \
    X(0x040, F32, x40, x40) \
    X(0x044, F32, specials_vel_x, specials_vel_x) \
    X(0x048, F32, specials_smash_vel_x, specials_smash_vel_x) \
    X(0x04c, F32, specials_vel_y, specials_vel_y) \
    X(0x050, F32, x50_gravity, x50_gravity) \
    X(0x054, F32, x54, x54) \
    X(0x058, F32, x58_gravity, x58_gravity) \
    X(0x05c, F32, x5C_terminal_vel, x5C_terminal_vel) \
    X(0x060, F32, specials_end_vel_x, specials_end_vel_x) \
    X(0x064, F32, specials_end_vel_y, specials_end_vel_y) \
    X(0x068, F32, x68, x68) \
    X(0x06c, F32, x6C, x6C) \
    X(0x070, F32, x70, x70) \
    X(0x074, F32, x74, x74) \
    X(0x078, F32, x78, x78) \
    X(0x07c, F32, x7C, x7C) \
    X(0x080, F32, x80, x80) \
    X(0x084, F32, x84, x84) \
    X(0x088, F32, x88, x88) \
    X(0x08c, F32, x8C, x8C) \
    X(0x090, I32, x90, x90) \
    X(0x094, F32, specialairn_vel_x_div, specialairn_vel_x_div) \
    X(0x098, F32, x98, x98) \
    X(0x09c, F32, specialairn_vel_y, specialairn_vel_y) \
    X(0x0a0, F32, xA0, xA0) \
    X(0x0a4, F32, xA4, xA4) \
    X(0x0a8, F32, xA8, xA8) \
    X(0x0ac, I32, absorb_bone, xAC.x0_bone_id) \
    X(0x0b0, F32, absorb_offset_x, xAC.x4_offset.x) \
    X(0x0b4, F32, absorb_offset_y, xAC.x4_offset.y) \
    X(0x0b8, F32, absorb_offset_z, xAC.x4_offset.z) \
    X(0x0bc, F32, absorb_size, xAC.x10_size)

#define MELEE_WEB_PICKUP_ATTRIBUTE_FIELDS(X) \
    X(0x000, F32, gr_light_offset_x, gr_light_offset.x) \
    X(0x004, F32, gr_light_offset_y, gr_light_offset.y) \
    X(0x008, F32, gr_light_offset_z, gr_light_offset.z) \
    X(0x00c, F32, gr_light_offset_w, gr_light_offset.w) \
    X(0x010, F32, gr_heavy_offset_x, gr_heavy_offset.x) \
    X(0x014, F32, gr_heavy_offset_y, gr_heavy_offset.y) \
    X(0x018, F32, gr_heavy_offset_z, gr_heavy_offset.z) \
    X(0x01c, F32, gr_heavy_offset_w, gr_heavy_offset.w) \
    X(0x020, F32, air_light_offset_x, air_light_offset.x) \
    X(0x024, F32, air_light_offset_y, air_light_offset.y) \
    X(0x028, F32, air_light_offset_z, air_light_offset.z) \
    X(0x02c, F32, air_light_offset_w, air_light_offset.w)

#define MELEE_WEB_DECLARE_ATTRIBUTE(offset, type, name, original) \
    MELEE_WEB_ATTRIBUTE_TYPE_##type name;
typedef struct MeleeWebCoAttributes {
    MELEE_WEB_CO_ATTRIBUTE_FIELDS(MELEE_WEB_DECLARE_ATTRIBUTE)
} MeleeWebCoAttributes;
typedef struct MeleeWebMarioAttributes {
    MELEE_WEB_MARIO_ATTRIBUTE_FIELDS(MELEE_WEB_DECLARE_ATTRIBUTE)
} MeleeWebMarioAttributes;
typedef struct MeleeWebCaptainAttributes {
    MELEE_WEB_CAPTAIN_ATTRIBUTE_FIELDS(MELEE_WEB_DECLARE_ATTRIBUTE)
} MeleeWebCaptainAttributes;
typedef struct MeleeWebFoxAttributes {
    MELEE_WEB_FOX_ATTRIBUTE_FIELDS(MELEE_WEB_DECLARE_ATTRIBUTE)
    uint8_t reserved[3];
} MeleeWebFoxAttributes;
typedef struct MeleeWebMarsAttributes {
    MELEE_WEB_MARS_ATTRIBUTE_FIELDS(MELEE_WEB_DECLARE_ATTRIBUTE)
} MeleeWebMarsAttributes;
typedef struct MeleeWebLinkAttributes {
    MELEE_WEB_LINK_ATTRIBUTE_FIELDS(MELEE_WEB_DECLARE_ATTRIBUTE)
} MeleeWebLinkAttributes;
typedef struct MeleeWebNessAttributes {
    MELEE_WEB_NESS_ATTRIBUTE_FIELDS(MELEE_WEB_DECLARE_ATTRIBUTE)
} MeleeWebNessAttributes;
typedef struct MeleeWebPeachAttributes {
    MELEE_WEB_PEACH_ATTRIBUTE_FIELDS(MELEE_WEB_DECLARE_ATTRIBUTE)
} MeleeWebPeachAttributes;
typedef struct MeleeWebItemPickup {
    MELEE_WEB_PICKUP_ATTRIBUTE_FIELDS(MELEE_WEB_DECLARE_ATTRIBUTE)
} MeleeWebItemPickup;
#undef MELEE_WEB_DECLARE_ATTRIBUTE

typedef struct MeleeWebFighterBaseAttributes {
    MeleeWebCoAttributes co;
    MeleeWebItemPickup pickup;
    float x2c4_x, x2c4_y;
} MeleeWebFighterBaseAttributes;

#endif
