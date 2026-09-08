#include "gameplay_compat.h"
#include <melee/lb/lbcommand.h>
#include <melee/ft/ftaction.h>
#include <melee/ft/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern void melee_web_command_set_loop(CommandInfo*,u32);
/* ColorOverlay stores numeric command headers and byte-preserved GXColor,
 * unlike the typed Fighter command union. Shared control functions still run
 * unchanged; only SetLoop reads an immediate operand in this proven corpus. */
int melee_web_color_command_execute(CommandInfo* info,u32 op){
    if(op==3){melee_web_command_set_loop(info,*(u32*)info->u&0x3ffffff);return 1;}
    return Command_Execute(info,op);
}
static int16_t half_signed(uint32_t v){int32_t s=v&65535;if(s&32768)s-=65536;return s;}
void melee_web_color_fighter_command(Fighter_GObj* gobj,CommandInfo* command,int op,int skip){
    const uint32_t* w=(const uint32_t*)command->u;
    union CmdUnion native[5]={{0}};CommandInfo temporary=*command;temporary.u=native;
    unsigned length;
    switch(op){
    case 21:
        length=5;
        native[0].spawn_gfx_0=(struct spawn_gfx_0){21,(w[0]>>18)&255,(w[0]>>17)&1,(w[0]>>16)&1,(w[0]>>15)&1,w[0]&32767};
        native[1].spawn_gfx_1=(struct spawn_gfx_1){w[1]>>16,w[1]&65535};
        native[2].spawn_gfx_2=(struct spawn_gfx_2){half_signed(w[2]>>16),half_signed(w[2])};
        native[3].spawn_gfx_3=(struct spawn_gfx_3){half_signed(w[3]>>16),w[3]&65535};
        native[4].spawn_gfx_4=(struct spawn_gfx_4){w[4]>>16,w[4]&65535};
        if(skip)ftAction_800711DC(gobj,&temporary);else ftAction_80071028(gobj,&temporary);break;
    case 22:
        length=3;
        native[0].sound_effect_0=(struct sound_effect_0){22,(w[0]>>18)&255,w[0]&0x3ffff};
        native[1].sound_effect_1.sfx_id=w[1];
        native[2].sound_effect_2=(struct sound_effect_2){w[2]>>16,(w[2]>>8)&255,w[2]&255};
        if(skip)ftAction_80071CA4(gobj,&temporary);else ftAction_80071B50(gobj,&temporary);break;
    case 23:
        length=1;native[0].unk21=(struct unk21){23,(w[0]>>25)&1,(w[0]>>17)&255};
        if(skip)ftAction_80073108(gobj,&temporary);else ftAction_800730B8(gobj,&temporary);break;
    default:fprintf(stderr,"Unsupported native color Fighter opcode %d\n",op);abort();
    }
    command->u+=length;
}
