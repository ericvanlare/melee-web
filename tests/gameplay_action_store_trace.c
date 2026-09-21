#include "gameplay_action_store.h"
#include "gameplay_compat.h"
#include <melee/ft/types.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/fighter.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/lb/lbanim.h>
#include <melee/lb/lbcommand.h>
#include <sysdolphin/baselib/gobj.h>
#include <stdlib.h>
#include <stdio.h>
/* The C++ action store intentionally uses DAT submotion indices without
 * including the source headers. Bind its Koopa victim range to those source
 * constants here; motion-state IDs 278..287 are a different enumeration. */
_Static_assert(ftCo_SM_CaptureDamageKoopa == 278 &&
               ftCo_SM_ThrownKoopaF == 279 && ftCo_SM_ThrownKoopaB == 280 &&
               ftCo_SM_CaptureDamageKoopaAir == 281 &&
               ftCo_SM_ThrownKoopaAirF == 282 && ftCo_SM_ThrownKoopaAirB == 283,
               "Koopa victim DAT submotion range");
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
void action_test_install_table(Fighter* fp, void* rows, void* blends, unsigned count)
{
    fp->x24 = rows; fp->x28 = (unsigned char (*)[2])blends; fp->x58C = count;
}
void action_test_set_count(Fighter* fp, unsigned count) { fp->x58C = count; }
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
int action_test_auxiliary_out_of_range(Fighter* fp, int motion)
{
    FigaTree* before_tree = fp->x598;
    void* before_identity = fp->x5A8;
    FigaTree* result = ftData_80085E50(fp, motion);
    return result == NULL && fp->x598 == before_tree && fp->x5A8 == before_identity;
}
int action_test_primary_out_of_range(Fighter* destination, Fighter* source, int motion)
{
    FigaTree* before_tree = destination->x590;
    void* before_identity = destination->x5A4;
    ftData_80085CD8(destination, source, motion);
    return destination->x590 == before_tree && destination->x5A4 == before_identity;
}
int action_test_load_from(Fighter* destination, Fighter* source, int motion)
{
    ftData_80085CD8(destination, source, motion);
    FigaTree* tree = destination->x590;
    if (!tree || !destination->x5A4) return 0;
    unsigned nodes = 0;
    while (tree->nodes[nodes] != -1) { if (++nodes > 140) abort(); }
    return (int)nodes;
}
int action_test_alias(Fighter* fp) { return fp->x590 == fp->x598 && fp->x5A4 == fp->x5A8; }
void* action_test_identity(Fighter* fp) { return fp->x5A4; }
const char* action_test_identity_symbol(Fighter* fp)
{ return fp->x5A4 ? ((struct Fighter_WaitAnimData*)fp->x5A4)->x0 : NULL; }
int action_test_identity_command_live(Fighter* fp)
{
    if (!fp->x5A4) return 0;
    void* command = ((struct Fighter_WaitAnimData*)fp->x5A4)->xC;
    uint32_t word = 0;
    return command && melee_web_command_original_word_checked(command, &word);
}
int action_test_identity_command_executes(Fighter* fp)
{
    if (!fp->x5A4) return 0;
    const void* command = ((struct Fighter_WaitAnimData*)fp->x5A4)->xC;
    if (!command) return 0;
    Fighter local = {0}; HSD_GObj gobj = {0}; gobj.user_data = &local;
    local.x3E4_fighterCmdScript.u = (void*)command; local.frame_speed_mul = 1;
    for (unsigned frame = 0; frame < 40 && local.x3E4_fighterCmdScript.u; ++frame) {
        local.cur_anim_frame = (float)frame; ftAction_80073240(&gobj);
    }
    return !local.x3E4_fighterCmdScript.u && !local.x3E4_fighterCmdScript.loop_count;
}
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

/* Common appeal uses action-table rows 239/240 (ftCo_SM_AppealSR/SL).  The
 * motion-state IDs 264/265 select those rows later; they are not action-row
 * IDs.  Check the published native rows without executing a command graph:
 * DatCommands has already admitted every word, and the checked lookup proves
 * the pointer is owned decoder storage rather than the opcode-63 sentinel. */
int action_test_common_appeals(void* rows, unsigned expected_command_mask)
{
    struct Fighter_WaitAnimData* table = rows;
    for (unsigned index = 0; index < 2; ++index) {
        const unsigned motion = 239 + index;
        void* command = table[motion].xC;
        if (!!command != !!(expected_command_mask & (1U << index))) return 0;
        if (!command) continue;
        if (command == melee_web_commands_unsupported()) return 0;
        uint32_t word = 0;
        if (!melee_web_command_original_word_checked(command, &word)) return 0;
        melee_web_command_require_supported(word >> 26);
    }
    return 1;
}

/* These reads use the actual source operand types, including fields whose
 * native bit positions differ from the archive representation. */
void ftAction_80071820(HSD_GObj*,CommandInfo*);
int action_test_movement_operands(void)
{
    const MeleeWebCommandWord words[] = {
        {0x280e8123,0}, {0x01234567,0}, {0x80007fff,0}, {0xffffabcd,0}, {0x13572468,0},
        {0x44000000,0}, {0x1bb,0}, {0x7f40,0},
        {0xd8020000,0}, {0x1bb,0}, {0x6e40,0},
        {0xdc000407,0}, {0x1bb,0}, {0x7f40,0},
        {0x4d000001,0}, {0xac02a000,0}, {0xb8180000,0}, {0,0}
    };
    union CmdUnion* p=melee_web_commands_create(words,sizeof(words)/sizeof(*words));
    if(!p)return 0;
    int valid=p[0].spawn_gfx_0.opcode==10&&p[0].spawn_gfx_0.boneId==3&&
        p[0].spawn_gfx_0.useCommonBoneIDs==1&&p[0].spawn_gfx_0.useUnkBone==1&&
        p[1].spawn_gfx_1.gfxID==0x123&&p[1].spawn_gfx_1.unkFloat==0x4567&&
        p[2].spawn_gfx_2.offsetZ==-32768&&p[2].spawn_gfx_2.offsetY==32767&&
        p[3].spawn_gfx_3.offsetX==-1&&p[3].spawn_gfx_3.rangeZ==0xabcd&&
        p[4].spawn_gfx_4.rangeY==0x1357&&p[4].spawn_gfx_4.rangeX==0x2468&&
        p[5].sound_effect_0.behavior==0&&p[6].sound_effect_1.sfx_id==443&&
        p[7].sound_effect_2.volume==127&&p[7].sound_effect_2.panning==64&&
        p[8].footstep_fx_0.use_alt_bone==1&&p[8].sound_effect_0.behavior==0&&
        p[10].sound_effect_2.volume==110&&p[11].sound_effect_0.unknown==0x407&&
        p[14].set_cmd_var.idx==1&&p[14].set_cmd_var.value==1&&
        p[15].unk10.unk1==0&&p[15].unk10.unk2==21&&p[15].unk10.unk3==0&&
        p[16].unk13.unk1==6&&p[16].unk13.unk2==0;
    Fighter fp={0};HSD_GObj gobj={0};gobj.user_data=&fp;
    CommandInfo command={0};command.u=&p[14];
    ftAction_80071820(&gobj,&command);
    valid=valid&&fp.cmd_vars[1]==1&&command.u==&p[15];
    melee_web_commands_destroy(p);
    for(size_t n=1;n<5;n++)if(melee_web_commands_create(words,n))return 0;
    return valid;
}

int action_test_jab_operands(void)
{
    const MeleeWebCommandWord words[]={
        {0x2c004803,0},{0x03840258,0},{0,0},{0x29990293,0},{7,0},
        {0x2c804003,0},{0x0258ff00,0},{0x80007fff,0},{0x2999029f,0},{0x0503fc8b,0},
        {0x44040000,0},{166,0},{0x7f40,0},{0x34000384,0},{0x74000001,0},{0x40000000,0},{0x5c000000,0},{0,0}
    };
    union CmdUnion* p=melee_web_commands_create(words,sizeof(words)/sizeof(*words));
    if(!p)return 0;
    uint32_t canonical=0;
    int valid=p[0].create_hitbox_0.id==0&&p[0].create_hitbox_0.bone==9&&p[0].create_hitbox_0.damage==3&&
        p[1].create_hitbox_1.size==900&&p[1].create_hitbox_1.z_offset==600&&
        p[3].create_hitbox_3.angle==83&&p[3].create_hitbox_3.knockback_growth==100&&
        p[3].create_hitbox_3.weight_set_knockback==20&&p[3].create_hitbox_3.clank&&p[3].create_hitbox_3.rebound&&
        p[4].create_hitbox_4.hit_grounded&&p[4].create_hitbox_4.hit_aerial&&
        p[5].create_hitbox_0.id==1&&p[5].create_hitbox_0.bone==8&&
        p[6].create_hitbox_1.z_offset==-256&&p[7].create_hitbox_2.y_offset==-32768&&p[7].create_hitbox_2.x_offset==32767&&
        p[8].create_hitbox_3.ignore_thrown_fighters&&p[9].create_hitbox_4.shield_damage==-1&&
        p[10].sound_effect_0.behavior==1&&p[13].set_hitbox_scale.value==900&&p[14].set_jab_combo.disabled==1&&
        melee_web_command_original_word_checked(&p[5],&canonical)&&canonical==0x2c804003&&
        !melee_web_command_original_word_checked((char*)p+1,&canonical)&&
        !melee_web_command_original_word_checked(p+sizeof(words)/sizeof(*words),&canonical);
    const uintptr_t retired=(uintptr_t)p;
    melee_web_commands_destroy(p);
    valid=valid&&!melee_web_command_original_word_checked((void*)retired,&canonical);
    return valid;
}

int action_test_dobj_operands(void)
{
    const MeleeWebCommandWord words[] = {
        {(31U << 26) | (126U << 19) | 0x7fffdU, UINT32_MAX},
        {0, UINT32_MAX},
    };
    union CmdUnion* p = melee_web_commands_create(words, sizeof(words) / sizeof(*words));
    if (!p) return 0;
    const int valid = p[0].set_dobj_flags.idx == -2 &&
        p[0].set_dobj_flags.value == -3;
    melee_web_command_require_supported(31);
    melee_web_commands_destroy(p);
    return valid;
}

void ftAction_80073008(HSD_GObj*,CommandInfo*);
void ftAction_80071908(HSD_GObj*,CommandInfo*);
void ftAction_80071974(HSD_GObj*,CommandInfo*);
void ftAction_80071708(HSD_GObj*,CommandInfo*);
void ftAction_80071784(HSD_GObj*,CommandInfo*);
void ftAction_80072B94(HSD_GObj*,CommandInfo*);
static int throw_disable_idx;
void ftColl_8007AFC8(HSD_GObj* gobj,int hit_idx)
{
    if(!gobj||!gobj->user_data||hit_idx<0||hit_idx>=4)abort();
    ((Fighter*)gobj->user_data)->x914[hit_idx].state=HitCapsule_Disabled;
    throw_disable_idx=hit_idx;
}
static HSD_GObj* dynamics_gobj;
static Fighter_Part dynamics_part;
static float dynamics_frame;
enum_t ftCo_8009E318(HSD_GObj* gobj,Fighter_Part part,float frame)
{
    dynamics_gobj=gobj; dynamics_part=part; dynamics_frame=frame; return 0;
}
int action_test_opcode14_consumer(void)
{
    const MeleeWebCommandWord words[]={
        {(14U<<26)|(2U<<2)|1U,UINT32_MAX},
        {(14U<<26)|(2U<<2)|(1U<<1)|1U,UINT32_MAX},{0,UINT32_MAX}};
    union CmdUnion* p=melee_web_commands_create(words,3);if(!p)return 0;
    Fighter fighter={0};HSD_GObj gobj={0};gobj.user_data=&fighter;CommandInfo command={0};command.u=p;
    ftAction_80071708(&gobj,&command);
    const int first=fighter.x914[2].x42_b5&& !fighter.x914[2].x42_b7&&command.u==&p[1];
    ftAction_80071708(&gobj,&command);
    const int valid=first&&fighter.x914[2].x42_b7&&command.u==&p[2];
    melee_web_command_require_supported(14);melee_web_commands_destroy(p);
    const MeleeWebCommandWord invalid[]={{(14U<<26)|(4U<<2),UINT32_MAX}};
    p=melee_web_commands_create(invalid,1);if(p){melee_web_commands_destroy(p);return 0;}
    return valid;
}
int action_test_opcode15_consumer(void)
{
    const MeleeWebCommandWord words[]={{(15U<<26)|2U,UINT32_MAX},{0,UINT32_MAX}};
    union CmdUnion* p=melee_web_commands_create(words,2);if(!p)return 0;
    Fighter fighter={0};HSD_GObj gobj={0};gobj.user_data=&fighter;fighter.x914[2].state=HitCapsule_Enabled;
    CommandInfo command={0};command.u=p;throw_disable_idx=-1;ftAction_80071784(&gobj,&command);
    const int valid=p[0].set_throw_flags.hit_idx==2&&throw_disable_idx==2&&
        fighter.x914[2].state==HitCapsule_Disabled&&command.u==&p[1];
    melee_web_command_require_supported(15);melee_web_commands_destroy(p);
    const MeleeWebCommandWord invalid[]={{(15U<<26)|4U,UINT32_MAX}};
    p=melee_web_commands_create(invalid,1);if(p){melee_web_commands_destroy(p);return 0;}
    return valid;
}
void ftAction_80071F78(HSD_GObj*,CommandInfo*);
int action_test_opcode36_consumer(void)
{
    const MeleeWebCommandWord words[]={
        {(36U<<26)|1U,UINT32_MAX},{36U<<26,UINT32_MAX},{0,UINT32_MAX}};
    union CmdUnion* p=melee_web_commands_create(words,3);if(!p)return 0;
    Fighter fighter={0};HSD_GObj gobj={0};gobj.user_data=&fighter;
    fighter.x221E_b5=1;
    CommandInfo command={0};command.u=p;
    ftAction_80071F78(&gobj,&command);
    const int enabled=fighter.x221E_b4&&fighter.x221E_b5&&command.u==&p[1];
    ftAction_80071F78(&gobj,&command);
    const int valid=enabled&&!fighter.x221E_b4&&fighter.x221E_b5&&command.u==&p[2];
    melee_web_command_require_supported(36);melee_web_commands_destroy(p);
    return valid;
}
int action_test_opcode50_consumer(void)
{
    const MeleeWebCommandWord words[]={{(50U<<26)|50U,UINT32_MAX},{0,UINT32_MAX}};
    union CmdUnion* p=melee_web_commands_create(words,2);if(!p)return 0;
    Fighter fighter={0};HSD_GObj gobj={0};gobj.user_data=&fighter;fighter.cur_anim_frame=17.25f;
    CommandInfo command={0};command.u=p;dynamics_gobj=NULL;dynamics_part=0;dynamics_frame=0;
    ftAction_80072B94(&gobj,&command);
    const int valid=p[0].unk17.unk1==50&&dynamics_gobj==&gobj&&dynamics_part==50&&
        dynamics_frame==17.25f&&command.u==&p[1];
    melee_web_command_require_supported(50);melee_web_commands_destroy(p);return valid;
}
static Fighter* damage_fighter;
static float damage_amount;
static unsigned damage_calls;
void Fighter_TakeDamage_8006CC7C(Fighter* fighter, float amount)
{
    damage_fighter=fighter;damage_amount=amount;++damage_calls;
}
void ftAction_80072BF4(HSD_GObj*, CommandInfo*);
int action_test_opcode51_consumer(void)
{
    const int amounts[]={0,1,4,-1,-33554432,33554431};
    for(unsigned i=0;i<sizeof(amounts)/sizeof(*amounts);++i){
        const MeleeWebCommandWord words[]={
            {(51U<<26)|((uint32_t)amounts[i]&0x3ffffffU),UINT32_MAX},{0,UINT32_MAX}};
        union CmdUnion* p=melee_web_commands_create(words,2);if(!p)return 0;
        Fighter fighter={0};HSD_GObj gobj={0};gobj.user_data=&fighter;
        CommandInfo command={0};command.u=p;
        damage_fighter=NULL;damage_amount=0;damage_calls=0;
        ftAction_80072BF4(&gobj,&command);
        const int valid=p[0].unk18.damage_amount==amounts[i]&&damage_calls==1&&
            damage_fighter==&fighter&&damage_amount==(float)amounts[i]&&command.u==&p[1];
        melee_web_commands_destroy(p);if(!valid)return 0;
    }
    melee_web_command_require_supported(51);return 1;
}
int action_test_opcode21_consumer(void)
{
    const MeleeWebCommandWord words[]={{21U << 26,UINT32_MAX},{0,UINT32_MAX}};
    union CmdUnion* p=melee_web_commands_create(words,sizeof(words)/sizeof(*words));
    if(!p)return 0;
    Fighter fighter={0};HSD_GObj gobj={0};gobj.user_data=&fighter;
    const int initially_clear=!fighter.throw_flags_b1;
    CommandInfo command={0};command.u=&p[0];
    ftAction_80071908(&gobj,&command);
    const int valid=initially_clear&&fighter.throw_flags_b1&&command.u==&p[1];
    melee_web_command_require_supported(21);
    melee_web_commands_destroy(p);
    return valid;
}
int action_test_common_operands(void)
{
    const MeleeWebCommandWord words[]={
        {0x50000001,0},{0x64000002,0},{0x68000001,0},{0x6c000000,0},{0x70240002,0},
        {0x88000009,0},{0x16b90000,0},{0x1902a000,0},{0xe03c0140,0},{0x0b000000,0},{0x8c000001,0},{0x60000000,0},{0,0}
    };
    union CmdUnion* p=melee_web_commands_create(words,sizeof(words)/sizeof(*words));
    if(!p)return 0;
    int valid=p[0].set_throw_flags.hit_idx==1&&p[1].set_airborne_state.state==2&&
        p[2].set_airborne_state.state==1&&p[3].set_airborne_state.state==0&&
        p[4].set_hurt_state.bone_idx==9&&p[4].set_hurt_state.state==2&&
        p[5].set_throw_hitbox_0.idx==0&&p[5].set_throw_hitbox_0.damage==9&&
        p[6].set_throw_hitbox_1.unk0==45&&p[6].set_throw_hitbox_1.hit_x24==228&&p[6].set_throw_hitbox_1.hit_x28==0&&
        p[7].set_throw_hitbox_2.hit_x2C==50&&p[7].set_throw_hitbox_2.element==0&&
        p[7].set_throw_hitbox_2.sfx_severity==2&&p[7].set_throw_hitbox_2.sfx_kind==10&&
        p[8].smash_charge_0.charge_frames==60&&p[8].smash_charge_0.charge_rate==320&&p[9].smash_charge_1.color_anim==11;
    Fighter fighter={0};HSD_GObj gobj={0};gobj.user_data=&fighter;
    CommandInfo charge={0};charge.u=&p[8];
    ftAction_80073008(&gobj,&charge);
    valid=valid&&charge.u==&p[10]&&fighter.smash_attrs.state==SmashState_PreCharge&&
        fighter.smash_attrs.x211C_holdFrame==60.0f&&fighter.smash_attrs.x2128==11&&
        fighter.smash_attrs.x2120_damageMul==0.003906f*320.0f;
    valid=valid&&p[10].unk27.value==1;
    charge.u=&p[11];ftAction_80071974(&gobj,&charge);
    valid=valid&&fighter.throw_flags_b0&&charge.u==&p[12];
    melee_web_commands_destroy(p);
    for(size_t count=1;count<3;count++){
        void* bad=melee_web_commands_create(words+5,count);
        if(bad){melee_web_commands_destroy(bad);return 0;}
    }
    void* bad=melee_web_commands_create(words+8,1);
    if(bad){melee_web_commands_destroy(bad);return 0;}
    MeleeWebCommandWord invalid_throw[3]={{0x89000009,0},{0,0},{0,0}};
    bad=melee_web_commands_create(invalid_throw,3);
    if(bad){melee_web_commands_destroy(bad);return 0;}
    const MeleeWebCommandWord loop[]={{0x0c000003,0},{0x04000001,0},{0x10000000,0},{0,0}};
    p=melee_web_commands_create(loop,4);if(!p)return 0;
    CommandInfo info={0};info.u=p;unsigned waits=0,steps=0;
    while(info.u&&steps++<20){
        unsigned opcode=info.u->Command_00.code;
        waits+=opcode==1;
        if(!Command_Execute(&info,opcode)){valid=0;break;}
    }
    valid=valid&&!info.u&&info.loop_count==0&&waits==3&&steps==8;
    melee_web_commands_destroy(p);return valid;
}

int action_test_falco_operands(void)
{
    const uint32_t pseudo = (38U << 26) | (0x55U << 18) | (0xaaU << 10) | (5U << 6) | 6U;
    const MeleeWebCommandWord words[] = {
        {(12U << 26) | (3U << 23) | 0x123456U, UINT32_MAX},
        {(30U << 26) | 0x1abcdeU, UINT32_MAX},
        {(37U << 26) | 0x123U, UINT32_MAX},
        {pseudo, UINT32_MAX},
        {0x11111111U, UINT32_MAX}, {0x22222222U, UINT32_MAX}, {0x33333333U, UINT32_MAX},
        {0x44444444U, UINT32_MAX}, {0x55555555U, UINT32_MAX}, {0x66666666U, UINT32_MAX},
        {(41U << 26) | (126U << 19) | (3U << 12) | 0xabcu, UINT32_MAX},
        {(42U << 26) | (0x1aaau << 13) | 0x123U, UINT32_MAX},
        {(49U << 26) | (1U << 25) | 0x1234567U, UINT32_MAX},
        {(55U << 26) | (2U << 24) | (0x81U << 16) | (0x72U << 8) | 0x43U, UINT32_MAX},
        {0x77777777U, UINT32_MAX}, {0x88889999U, UINT32_MAX},
    };
    union CmdUnion* p = melee_web_commands_create(words, sizeof(words) / sizeof(*words));
    if (!p) return 0;
    uint32_t original = 0;
    int valid = p[0].set_hitbox_damage.idx == 3 && p[0].set_hitbox_damage.value == 0x123456 &&
        p[1].set_jab_rapid.state == 0x1abcde && p[2].set_fighter_vis.value == 0x123 &&
        p[3].pseudo_random_sfx_0.volume == 0x55 && p[3].pseudo_random_sfx_0.panning == 0xaa &&
        p[3].pseudo_random_sfx_0.behavior == 5 && p[3].pseudo_random_sfx_0.random_range == 6 &&
        p[4].pseudo_random_sfx_1.sfx_id == 0x11111111U && p[9].pseudo_random_sfx_1.sfx_id == 0x66666666U &&
        p[10].part_anim.unk1 == -2 && p[10].part_anim.unk2 == 3 && p[10].part_anim.unk3 == 0xabcu &&
        p[11].unk9.unk1 == 0x1aaa && p[11].unk9.unk2 == 0x123 &&
        p[12].unk16.unk3 == -1 && p[12].unk16.unk4 == 0x1234567 - 0x2000000 &&
        /* Actual little-endian source adapter reads the flag and gfx id
         * through unknown before passing the word to the sound consumer. */
        ((p[13].sound_effect_0.unknown >> 16) & 1) == 1 &&
        (p[13].sound_effect_0.unknown & 0xffff) == 0x7243 &&
        p[13].sound_effect_0.behavior == ((words[13].word >> 18) & 255) &&
        p[14].sound_effect_1.sfx_id == 0x77777777U &&
        melee_web_command_original_word_checked(&p[13], &original) && original == words[13].word;
    melee_web_commands_destroy(p);
    return valid;
}

static unsigned wind_calls;
static Fighter_GObj* wind_gobj;
static Fighter_Part wind_bone;
static int wind_timer;
static float wind_x, wind_y, wind_mag, wind_decay, wind_angle;
bool ftCo_8009E714(Fighter_GObj* gobj, Fighter_Part bone, int timer, float x,
                   float y, float mag, float decay, float angle)
{
    ++wind_calls;
    wind_gobj = gobj; wind_bone = bone; wind_timer = timer;
    wind_x = x; wind_y = y; wind_mag = mag; wind_decay = decay; wind_angle = angle;
    return true;
}
void ftAction_80073118(HSD_GObj*, CommandInfo*);
int action_test_wind_operands(void)
{
    const MeleeWebCommandWord words[] = {
        {(58U << 26) | (0x2aaaaU << 8) | 0x7fU, UINT32_MAX},
        {((uint32_t)(uint16_t)-7 << 16) | 0x1234U, UINT32_MAX},
        {((uint32_t)0x8000U << 16) | (uint16_t)-9, UINT32_MAX},
        {((uint32_t)33U << 16) | (uint16_t)-0x123, UINT32_MAX},
        {0, UINT32_MAX},
    };
    union CmdUnion* p = melee_web_commands_create(words, sizeof(words) / sizeof(*words));
    if (!p) return 0;
    uint32_t original = 0;
    Fighter fighter = {0}; HSD_GObj gobj = {0}; gobj.user_data = &fighter;
    CommandInfo command = {0}; command.u = p;
    wind_calls = 0; wind_gobj = NULL;
    const int decoded = p[0].wind_fx_0.opcode == 58 && p[0].wind_fx_0.x0_b6_17 == 0x2aaaa &&
        p[0].wind_fx_0.bone == 0x7f &&
        p[1].wind_fx_1.timer == -7 && p[1].wind_fx_1.x == 0x1234 &&
        p[2].wind_fx_2.y == (int16_t)0x8000 && p[2].wind_fx_2.mag == -9 &&
        p[3].wind_fx_3.angle == 33 && p[3].wind_fx_3.decay == (int16_t)-0x123 &&
        melee_web_command_original_word_checked(&p[0], &original) && original == words[0].word;
    ftAction_80073118(&gobj, &command);
    const int effected = wind_calls == 1 && wind_gobj == &gobj && wind_bone == 0x7f &&
        wind_timer == 33 && wind_x == 0.003906f * -7.0f &&
        wind_y == 0.003906f * 0x1234 &&
        wind_mag == 0.003906f * (float)(int16_t)0x8000 &&
        wind_decay == 0.003906f * -9.0f &&
        wind_angle == 0.003906f * -(float)0x123 && command.u == &p[4];
    melee_web_command_require_supported(58);
    melee_web_commands_destroy(p);
    return decoded && effected;
}
