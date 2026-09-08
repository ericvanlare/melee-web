#include "gameplay_action_store.h"
#include "gameplay_compat.h"
#include <melee/ft/types.h>
#include <melee/ft/ftdata.h>
#include <melee/lb/lbanim.h>
#include <sysdolphin/baselib/gobj.h>
#include <stdlib.h>
#include <stdio.h>
static unsigned texture_count, mode_count;
static int texture_frames[32], texture_indices[32];
void ftAnim_800704F0(HSD_GObj* gobj, int idx, float frame)
{
    (void)gobj;
    if (texture_count >= 32) abort();
    texture_indices[texture_count] = idx; texture_frames[texture_count++] = (int)frame;
}
void ft_8008A1B8(HSD_GObj* gobj, int mode) { (void)gobj; if (mode != 3) abort(); ++mode_count; }
void lbBgFlash_80021C48(int a, int b) { (void)a; (void)b; abort(); }
void ftAction_80073240(HSD_GObj*);
Fighter* action_test_fighter(void) { return calloc(1, sizeof(Fighter)); }
void action_test_destroy(Fighter* fp) { free(fp); }
int action_test_load(Fighter* fp, int motion, int slot)
{
    if (slot) ftData_80085E50(fp, motion); else ftData_80085CD8(fp, fp, motion);
    FigaTree* tree = slot ? fp->x598 : fp->x590;
    if (!tree) return 0;
    unsigned nodes = 0, tracks = 0;
    while (tree->nodes[nodes] != -1) { tracks += tree->nodes[nodes]; if (++nodes > 140) abort(); }
    for (unsigned i = 0; i < tracks; ++i) if (!tree->tracks[i].ad_head || !tree->tracks[i].length) abort();
    return (int)nodes;
}
int action_test_alias(Fighter* fp) { return fp->x590 == fp->x598 && fp->x5A4 == fp->x5A8; }
float action_test_frames(Fighter* fp) { return lbAnim_8001E8F8(fp->x590); }
int action_test_cleared(Fighter* fp) { return !fp->x590 && !fp->x598 && !fp->x5A4 && !fp->x5A8; }
int action_test_commands(void* rows, unsigned motion, int actual)
{
    struct Fighter_WaitAnimData* row = &((struct Fighter_WaitAnimData*)rows)[motion];
    Fighter fp = {0}; HSD_GObj gobj = {0}; gobj.user_data = &fp;
    fp.x3E4_fighterCmdScript.u = row->xC; fp.frame_speed_mul = 1;
    texture_count = mode_count = 0;
    for (unsigned frame = 0; frame < 40 && fp.x3E4_fighterCmdScript.u; ++frame) {
        fp.cur_anim_frame = (float)frame; ftAction_80073240(&gobj);
    }
    if (fp.x3E4_fighterCmdScript.u || fp.x3E4_fighterCmdScript.loop_count) return 0;
    if (!actual) return 1;
    if (mode_count != 1 || texture_count != 8) return 0;
    const int frames[] = {1,1,2,2,1,1,0,0};
    for (unsigned i = 0; i < 8; ++i) if (texture_frames[i] != frames[i] || texture_indices[i] != (int)(i % 2)) return 0;
    return 1;
}
int action_test_rows(void* rows, void* blends, void* waits)
{
    struct Fighter_WaitAnimData* a = rows;
    struct ftData_80085FD4_ret* flags = (void*)&a[6];
    MeleeWebWaitChoice* w = waits;
    return a[2].xC != a[6].xC && a[2].x14 == a[6].x14 && flags->x10_b0 == !!((uint32_t)a[6].x10_animCurrFlags & 0x80000000) &&
        flags->x10_b1 == !!((uint32_t)a[6].x10_animCurrFlags & 0x40000000) && blends &&
        w[0].motion == 2 && w[2].motion == UINT32_MAX;
}
