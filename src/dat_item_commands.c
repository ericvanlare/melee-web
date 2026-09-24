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
         * six-bit opcode and advances one word after clearing hitboxes.
         * Opcode 8 (SetTimerAnimation) reads no operand. Opcode 9 mirrors
         * its {id:6,param_1:8,param_2:18} split into the native word. */
        case 0:case 1:case 2:case 3:case 4:case 15:
            out[i].Command_00.code=op;out[i].Command_00.value=w&0x3ffffff;break;
        case 8:
            ((uint32_t*)&out[i])[0]=8;break;
        case 9:
            ((uint32_t*)&out[i])[0]=9|((w>>18)&0xff)<<6|(w&0x3ffff)<<14;break;
        /* Opcodes 5 (subroutine) and 7 (goto) arrive as [dispatch word]
         * [native target pointer]; the decoder already patched the pointer
         * word's low bits to the target's emitted index. Opcode 6 (return)
         * reads no operand. */
        case 5:case 7:{
            if(i+1>=count)goto fail;
            const uint32_t target=((uint32_t*)&words[i+1])[0]&0x3ffffff;
            if(target>=count)goto fail;
            ((uint32_t*)&out[i])[0]=op;
            /* Command_05/07 advances once before reading its pointer. The
             * checked decoder stores the emitted target index in that
             * following union, not in the source dispatch word's payload. */
            out[i+1].Command_07.ptr=out+target;
            i+=1;break;
        }
        case 6:
            ((uint32_t*)&out[i])[0]=6;break;
        /* Opcode 14 uses the same one-word 26-bit hitbox index layout before
         * dispatching to it_80279680. Opcodes 17..19 dispatch the same
         * one-word layout to their itcmd variable setters. */
        case 14:case 17:case 18:case 19:
            out[i].set_throw_flags=(struct set_throw_flags){op,w&0x3ffffff};break;
        /* Opcode 16 (it_8027978C) reads its sub-opcode from source bits
         * 25..18 of the command word (big-endian itAnimlistCmdUnk.opcode).
         * The native handler reads the same field from native bits 6..13,
         * so emit the dispatch opcode in bits 0..5 and the authored
         * sub-opcode in bits 6..13. Low sub-opcodes 0..2 read both operand
         * unions, as do high sub-opcodes 10..11; all other sub-opcodes skip
         * the second operand and consume only one operand union. */
        case 16:{
            unsigned sub=(w>>18)&0xff;
            unsigned extra=(sub<=2||sub==10||sub==11)?2:1;
            if(i+extra>=count)goto fail;
            out[i].set_throw_flags=(struct set_throw_flags){16,sub};
            for(unsigned k=1;k<=extra;k++)((uint32_t*)&out[i+k])[0]=words[i+k];
            if(extra==2){
                uint32_t v=words[i+2];
                ((uint8_t*)&out[i+2])[0]=v>>24;
                ((uint8_t*)&out[i+2])[1]=v>>16;
                ((uint8_t*)&out[i+2])[2]=v>>8;
                ((uint8_t*)&out[i+2])[3]=v;
            }
            i+=extra;break;
        }
        /* Opcode 10 (it_80278F2C) consumes five words. The handler reads
         * arg2 from the first word's dispatch half (native bits 0..9 carry
         * source bits 22..31), then four words whose halfwords the original
         * reads in the opposite order, so each halveswaps. */
        case 10:{
            if(i+4>=count)goto fail;
            ((uint32_t*)&out[i])[0]=w>>22;
            for(unsigned k=1;k<5;k++){
                uint32_t v=words[i+k];
                ((uint32_t*)&out[i+k])[0]=((v&0xffff)<<16)|(v>>16);
            }
            i+=4;break;
        }
        /* Opcode 12 updates one enabled item hitbox's damage. */
        case 12:
            if(((w>>23)&7)>=4)goto fail;
            out[i].set_hitbox_damage=(struct set_hitbox_damage){op,(w>>23)&7,w&0x7fffff};break;
        case 13:
            if(((w>>23)&7)>=4)goto fail;
            out[i].set_hitbox_scale=(struct set_hitbox_scale){op,(w>>23)&7,w&0x7fffff};break;
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
void* melee_web_item_commands_entry(void* base,size_t index){
    if(!base||index>1024)return NULL;
    return (union CmdUnion*)base+index;
}
void melee_web_item_commands_destroy(void* p){free(p);}
