// Decode the declared retail initialization boundary into native source types.
// Pointer-bearing fields are rejected; PPC addresses are never transplanted.
#include "gameplay_menu_host.h"
#include "gameplay_player_selection.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint16_t be16(const uint8_t* p){return (uint16_t)p[0]<<8|p[1];}
static uint32_t be32(const uint8_t* p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static float bef(const uint8_t* p){uint32_t u=be32(p);float f;memcpy(&f,&u,4);return f;}
int melee_web_retail_setup(const uint8_t raw[0x138],uint32_t seed,MeleeWebMenuMatchSelection* out,char* error,size_t size){
    memset(out,0,sizeof(*out));
    for(unsigned i=0x38;i<0x5c;i++)if(raw[i]){
        snprintf(error,size,"Retail setup contains an unsupported callback/data pointer at 0x%x",i);return 0;
    }
    StartMeleeRules* r=&out->start.rules;
    r->match_kind=raw[0]>>5;r->x0_3=(raw[0]>>2)&7;r->timer_enabled=(raw[0]>>1)&1;r->timer_counts_up=raw[0]&1;
    r->x1_0=(raw[1]>>7)&1;
    r->x1_1=(raw[1]>>6)&1;
    r->x1_2=(raw[1]>>5)&1;
    r->x1_3=(raw[1]>>4)&1;
    r->x1_4=(raw[1]>>3)&1;
    r->x1_5=(raw[1]>>2)&1;
    r->timer_shows_hours=(raw[1]>>1)&1;
    r->friendly_fire=(raw[1]>>0)&1;
    r->is_stock=(raw[2]>>7)&1;
    r->x2_1=(raw[2]>>6)&1;
    r->x2_2=(raw[2]>>5)&1;
    r->single_button=(raw[2]>>4)&1;
    r->disable_pausing=(raw[2]>>3)&1;
    r->x2_5=(raw[2]>>2)&1;
    r->x2_6=(raw[2]>>1)&1;
    r->x2_7=(raw[2]>>0)&1;
    r->x3_0=(raw[3]>>7)&1;
    r->x3_1=(raw[3]>>6)&1;
    r->x3_2=(raw[3]>>5)&1;
    r->x3_3=(raw[3]>>4)&1;
    r->x3_4=(raw[3]>>3)&1;
    r->x3_5=(raw[3]>>2)&1;
    r->x3_6=(raw[3]>>1)&1;
    r->x3_7=(raw[3]>>0)&1;
    r->x4_0=(raw[4]>>7)&1;
    r->is_vs=(raw[4]>>6)&1;
    r->x4_2=(raw[4]>>5)&1;
    r->x4_3=(raw[4]>>4)&1;
    r->x4_4=(raw[4]>>3)&1;
    r->x4_5=(raw[4]>>2)&1;
    r->x4_6=(raw[4]>>1)&1;
    r->x4_7=(raw[4]>>0)&1;
    r->x5_0=(raw[5]>>7)&1;
    r->x5_1=(raw[5]>>6)&1;
    r->x5_2=(raw[5]>>5)&1;
    r->x5_3=(raw[5]>>4)&1;
    r->x5_4=(raw[5]>>3)&1;
    r->x5_5=(raw[5]>>2)&1;
    r->x5_6=(raw[5]>>1)&1;
    r->x5_7=(raw[5]>>0)&1;
    r->x6=raw[6];r->x7=raw[7];r->is_teams=raw[8];r->x9=raw[9];r->xA=raw[10];r->xB=(int8_t)raw[11];r->xC=(int8_t)raw[12];r->xD=raw[13];
    r->stkind=be16(raw+0xe);r->time_limit=be32(raw+0x10);r->x14=raw[0x14];r->x18=be32(raw+0x18);
    r->x20=(uint64_t)be32(raw+0x20)<<32|be32(raw+0x24);r->x28=(int32_t)be32(raw+0x28);
    r->x2C=bef(raw+0x2c);r->x30=bef(raw+0x30);r->game_speed=bef(raw+0x34);
    if(!isfinite(r->x2C)||!isfinite(r->x30)||!isfinite(r->game_speed)){
        snprintf(error,size,"Retail setup contains nonfinite rules");return 0;
    }
    for(unsigned i=0;i<6;i++){
        const uint8_t* p=raw+0x60+i*0x24;PlayerInitData* v=&out->start.players[i];
        v->ckind=(int8_t)p[0];v->slot_type=p[1];v->stocks=(int8_t)p[2];v->color=p[3];v->slot=p[4];v->x5=(int8_t)p[5];v->spawn_dir=(int8_t)p[6];v->sub_color=p[7];
        v->handicap=(int8_t)p[8];v->team=p[9];v->nametag=p[10];v->xB=p[11];
        v->rumble_enabled=(p[12]>>7)&1;
        v->xC_b1=(p[12]>>6)&1;
        v->xC_b2=(p[12]>>5)&1;
        v->xC_b3=(p[12]>>4)&1;
        v->vs_invisible=(p[12]>>3)&1;
        v->xC_b5=(p[12]>>2)&1;
        v->xC_b6=(p[12]>>1)&1;
        v->xC_b7=(p[12]>>0)&1;
        v->xD_b0=(p[13]>>7)&1;
        v->xD_b1=(p[13]>>6)&1;
        v->xD_b2=(p[13]>>5)&1;
        v->xD_b3=(p[13]>>4)&1;
        v->xD_b4=(p[13]>>3)&1;
        v->xD_b5=(p[13]>>2)&1;
        v->xD_b6=(p[13]>>1)&1;
        v->xD_b7=(p[13]>>0)&1;
        v->cpu_kind=p[14];v->cpu_level=p[15];v->x10=be16(p+0x10);v->x12=be16(p+0x12);v->hp=be16(p+0x14);
        v->attack_ratio=bef(p+0x18);v->defense_ratio=bef(p+0x1c);v->model_scale=bef(p+0x20);
        if(!isfinite(v->attack_ratio)||!isfinite(v->defense_ratio)||!isfinite(v->model_scale)){
            snprintf(error,size,"Retail setup contains nonfinite player ratios");return 0;
        }
        if(i<2){
            if(!melee_web_match_player_supported(v)||(v->slot!=0&&v->slot!=i+1)||v->stocks!=4){
                snprintf(error,size,"Retail setup requires supported human/CPU slots 1/2 and four stocks");return 0;
            }
            out->players[i].controller=v->slot?v->slot-1:i;out->players[i].stocks=v->stocks;out->players[i].costume=v->color;out->players[i].sub_color=v->sub_color;
        }else if(v->slot_type!=3){
            snprintf(error,size,"Retail setup has an unsupported active player");return 0;
        }
    }
    out->hud_layout=r->x0_3;out->random_seed=seed;return 1;
}
