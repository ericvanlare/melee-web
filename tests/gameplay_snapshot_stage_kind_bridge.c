/*
 * C-only boundary for the original StageInfo declaration.  ground.h includes
 * source C headers that are not valid C++ (notably an anonymous union), so the
 * C++ probe calls this exact-state getter instead of including that header.
 */
#include <stdint.h>
#include <melee/gr/ground.h>

uint32_t melee_web_snapshot_actual_stage_ground_kind(void)
{
    return (uint32_t)stage_info.grkind;
}

/* Private v7 eligibility, from the actual source enum rather than a caller ID. */
int melee_web_snapshot_actual_stage_is_final_destination(void)
{
    return stage_info.grkind == Gr_Kind_Last;
}
