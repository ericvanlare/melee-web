#include "dat_item_commands.h"
#include <melee/lb/types.h>
#include <stdlib.h>
_Static_assert(sizeof(union CmdUnion)==4,"Original item command word ABI");
static int signed_bits(uint32_t v,unsigned n){uint32_t sign=1U<<(n-1);return (int)(v^sign)-(int)sign;}
void* melee_web_item_commands_create(const uint32_t* words,size_t count){
    if(!words||!count||count>1024)return NULL;
    union CmdUnion* out=calloc(count,sizeof(*out));if(!out)return NULL;
    for(size_t i=0;i<count;i++){
        uint32_t w=words[i],op=w>>26;
        switch(op){
        /* Opcode 15 dispatches to it_802796C4, which consumes only the
         * six-bit opcode and advances one word after clearing hitboxes. */
        case 0:case 1:case 2:case 15:
            out[i].Command_00.code=op;out[i].Command_00.value=w&0x3ffffff;break;
        /* Opcode 14 uses the same one-word 26-bit hitbox index layout before
         * dispatching to it_80279680. */
        case 14:case 17:case 18:case 19:
            out[i].set_throw_flags=(struct set_throw_flags){op,w&0x3ffffff};break;
        /* Opcode 12 updates one enabled item hitbox's damage. */
        case 12:
            out[i].set_hitbox_damage=(struct set_hitbox_damage){op,(w>>23)&7,w&0x7fffff};break;
        case 11:{
            if(i+5>=count||((w>>23)&7)>=4)goto fail;
            out[i].it_create_hitbox_0=(struct it_create_hitbox_0){op,(w>>23)&7,(w>>20)&7,(w>>13)&127,w&8191};
            uint32_t v=words[i+1];out[i+1].create_hitbox_1=(struct spawn_hitbox_1){v>>16,signed_bits(v&65535,16)};
            v=words[i+2];out[i+2].create_hitbox_2=(struct spawn_hitbox_2){signed_bits(v>>16,16),signed_bits(v&65535,16)};
            v=words[i+3];out[i+3].create_hitbox_3=(struct spawn_hitbox_3){v>>23,(v>>14)&511,(v>>5)&511,(v>>4)&1,(v>>3)&1,(v>>2)&1,(v>>1)&1,v&1};
            v=words[i+4];out[i+4].it_create_hitbox_4=(struct it_create_hitbox_4){v>>23,(v>>18)&31,(v>>17)&1,signed_bits((v>>9)&255,8),(v>>6)&7,(v>>2)&15,(v>>1)&1,v&1};
            /* Original item consumer reads operand5 only through u8 pointers. */
            v=words[i+5];for(unsigned k=0;k<4;k++)((uint8_t*)&out[i+5])[k]=v>>(24-8*k);
            i+=5;break;
        }
        default:goto fail;
        }
    }
    return out;
fail:free(out);return NULL;
}
void melee_web_item_commands_destroy(void* p){free(p);}
