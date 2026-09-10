#include "gameplay_pad_state.h"
#include <sysdolphin/baselib/controller.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MeleeWebPadState { PadLibData config; HSD_PadStatus history[3][4]; };
static uint8_t read_u8(const uint8_t** p){return *(*p)++;}
static int8_t read_i8(const uint8_t** p){uint8_t v=read_u8(p);int8_t out;memcpy(&out,&v,1);return out;}
static uint32_t read_u32(const uint8_t** p){uint32_t out=0;for(unsigned i=0;i<4;i++)out=out<<8|read_u8(p);return out;}
static int32_t read_i32(const uint8_t** p){uint32_t v=read_u32(p);int32_t out;memcpy(&out,&v,4);return out;}
static float read_f32(const uint8_t** p){uint32_t v=read_u32(p);float out;memcpy(&out,&v,4);return out;}
static void write_u8(uint8_t** p,uint8_t v){*(*p)++=v;}
static void write_i8(uint8_t** p,int8_t v){write_u8(p,(uint8_t)v);}
static void write_u32(uint8_t** p,uint32_t v){for(int i=3;i>=0;i--)write_u8(p,(uint8_t)(v>>(8*i)));}
static void write_i32(uint8_t** p,int32_t v){write_u32(p,(uint32_t)v);}
static void write_f32(uint8_t** p,float v){uint32_t bits;memcpy(&bits,&v,4);write_u32(p,bits);}

static void read_config(PadLibData* v,const uint8_t** p)
{
    v->repeat_start=read_i32(p);
    v->repeat_interval=read_i32(p);
    v->adc_type=read_i8(p);
    v->adc_th=read_i8(p);
    v->adc_angle=read_f32(p);
    v->clamp_stickType=read_u8(p);
    v->clamp_stickShift=read_u8(p);
    v->clamp_stickMax=read_i8(p);
    v->clamp_stickMin=read_i8(p);
    v->clamp_analogLRShift=read_u8(p);
    v->clamp_analogLRMax=read_u8(p);
    v->clamp_analogLRMin=read_u8(p);
    v->clamp_analogABShift=read_u8(p);
    v->clamp_analogABMax=read_u8(p);
    v->clamp_analogABMin=read_u8(p);
    v->scale_stick=read_i8(p);
    v->scale_analogLR=read_u8(p);
    v->scale_analogAB=read_u8(p);
    v->cross_dir=read_u8(p);
    v->reset_switch_status=read_u8(p);
    v->reset_switch=read_u8(p);
}

static void read_history(HSD_PadStatus* v,const uint8_t** p)
{
    v->button=read_u32(p);
    v->last_button=read_u32(p);
    v->trigger=read_u32(p);
    v->repeat=read_u32(p);
    v->release=read_u32(p);
    v->repeat_count=read_i32(p);
    v->stickX=read_i8(p);
    v->stickY=read_i8(p);
    v->subStickX=read_i8(p);
    v->subStickY=read_i8(p);
    v->analogL=read_u8(p);
    v->analogR=read_u8(p);
    v->analogA=read_u8(p);
    v->analogB=read_u8(p);
    v->nml_stickX=read_f32(p);
    v->nml_stickY=read_f32(p);
    v->nml_subStickX=read_f32(p);
    v->nml_subStickY=read_f32(p);
    v->nml_analogL=read_f32(p);
    v->nml_analogR=read_f32(p);
    v->nml_analogA=read_f32(p);
    v->nml_analogB=read_f32(p);
    v->cross_dir=read_u8(p);
    v->err=read_i8(p);
}

static void write_config(uint8_t** p,const PadLibData* v)
{
    write_i32(p,v->repeat_start);
    write_i32(p,v->repeat_interval);
    write_i8(p,v->adc_type);
    write_i8(p,v->adc_th);
    write_f32(p,v->adc_angle);
    write_u8(p,v->clamp_stickType);
    write_u8(p,v->clamp_stickShift);
    write_i8(p,v->clamp_stickMax);
    write_i8(p,v->clamp_stickMin);
    write_u8(p,v->clamp_analogLRShift);
    write_u8(p,v->clamp_analogLRMax);
    write_u8(p,v->clamp_analogLRMin);
    write_u8(p,v->clamp_analogABShift);
    write_u8(p,v->clamp_analogABMax);
    write_u8(p,v->clamp_analogABMin);
    write_i8(p,v->scale_stick);
    write_u8(p,v->scale_analogLR);
    write_u8(p,v->scale_analogAB);
    write_u8(p,v->cross_dir);
    write_u8(p,v->reset_switch_status);
    write_u8(p,v->reset_switch);
}

static void write_history(uint8_t** p,const HSD_PadStatus* v)
{
    write_u32(p,v->button);
    write_u32(p,v->last_button);
    write_u32(p,v->trigger);
    write_u32(p,v->repeat);
    write_u32(p,v->release);
    write_i32(p,v->repeat_count);
    write_i8(p,v->stickX);
    write_i8(p,v->stickY);
    write_i8(p,v->subStickX);
    write_i8(p,v->subStickY);
    write_u8(p,v->analogL);
    write_u8(p,v->analogR);
    write_u8(p,v->analogA);
    write_u8(p,v->analogB);
    write_f32(p,v->nml_stickX);
    write_f32(p,v->nml_stickY);
    write_f32(p,v->nml_subStickX);
    write_f32(p,v->nml_subStickY);
    write_f32(p,v->nml_analogL);
    write_f32(p,v->nml_analogR);
    write_f32(p,v->nml_analogA);
    write_f32(p,v->nml_analogB);
    write_u8(p,v->cross_dir);
    write_i8(p,v->err);
}

MeleeWebPadState* melee_web_pad_state_decode(const uint8_t* bytes,size_t size,char* error,size_t capacity)
{
    if(!bytes||size!=MELEE_WEB_PAD_STATE_BYTES){
        if(error&&capacity)snprintf(error,capacity,"PAD snapshot size is invalid");return NULL;
    }
    MeleeWebPadState* state=calloc(1,sizeof(*state));
    if(!state){if(error&&capacity)snprintf(error,capacity,"Cannot allocate PAD snapshot");return NULL;}
    const uint8_t* p=bytes;
    read_config(&state->config,&p);
    const PadLibData* c=&state->config;
    int valid=c->repeat_start>0&&c->repeat_interval>0&&c->adc_type>=0&&c->adc_type<=3&&
        c->adc_th>=0&&isfinite(c->adc_angle)&&c->clamp_stickType<=1&&
        c->clamp_stickShift<=1&&c->clamp_stickMin>=0&&c->clamp_stickMax>c->clamp_stickMin&&
        c->clamp_analogLRShift<=1&&c->clamp_analogLRMax>c->clamp_analogLRMin&&
        c->clamp_analogABShift<=1&&c->clamp_analogABMax>c->clamp_analogABMin&&
        c->scale_stick>0&&c->scale_analogLR&&c->scale_analogAB&&c->cross_dir<=3&&
        c->reset_switch_status<=1&&c->reset_switch<=1;
    for(unsigned bank=0;bank<3;bank++)for(unsigned slot=0;slot<4;slot++){
        HSD_PadStatus* h=&state->history[bank][slot];read_history(h,&p);
        valid=valid&&isfinite(h->nml_stickX);
        valid=valid&&isfinite(h->nml_stickY);
        valid=valid&&isfinite(h->nml_subStickX);
        valid=valid&&isfinite(h->nml_subStickY);
        valid=valid&&isfinite(h->nml_analogL);
        valid=valid&&isfinite(h->nml_analogR);
        valid=valid&&isfinite(h->nml_analogA);
        valid=valid&&isfinite(h->nml_analogB);
    }
    if(!valid){free(state);if(error&&capacity)snprintf(error,capacity,"PAD snapshot has invalid processing configuration or nonfinite history");return NULL;}
    return state;
}
void melee_web_pad_state_free(MeleeWebPadState* state){free(state);}
void melee_web_pad_state_capture(uint8_t out[MELEE_WEB_PAD_STATE_BYTES])
{
    uint8_t* p=out;write_config(&p,&HSD_PadLibData);
    HSD_PadStatus* banks[]={HSD_PadMasterStatus,HSD_PadCopyStatus,HSD_PadGameStatus};
    for(unsigned bank=0;bank<3;bank++)for(unsigned slot=0;slot<4;slot++)write_history(&p,&banks[bank][slot]);
}
void melee_web_pad_state_apply(const MeleeWebPadState* state)
{
    /* Assign only semantic processing members. The live queue, rumble pool and
     * every native pointer remain owned by the existing source context. */
    HSD_PadLibData.repeat_start=state->config.repeat_start;
    HSD_PadLibData.repeat_interval=state->config.repeat_interval;
    HSD_PadLibData.adc_type=state->config.adc_type;
    HSD_PadLibData.adc_th=state->config.adc_th;
    HSD_PadLibData.adc_angle=state->config.adc_angle;
    HSD_PadLibData.clamp_stickType=state->config.clamp_stickType;
    HSD_PadLibData.clamp_stickShift=state->config.clamp_stickShift;
    HSD_PadLibData.clamp_stickMax=state->config.clamp_stickMax;
    HSD_PadLibData.clamp_stickMin=state->config.clamp_stickMin;
    HSD_PadLibData.clamp_analogLRShift=state->config.clamp_analogLRShift;
    HSD_PadLibData.clamp_analogLRMax=state->config.clamp_analogLRMax;
    HSD_PadLibData.clamp_analogLRMin=state->config.clamp_analogLRMin;
    HSD_PadLibData.clamp_analogABShift=state->config.clamp_analogABShift;
    HSD_PadLibData.clamp_analogABMax=state->config.clamp_analogABMax;
    HSD_PadLibData.clamp_analogABMin=state->config.clamp_analogABMin;
    HSD_PadLibData.scale_stick=state->config.scale_stick;
    HSD_PadLibData.scale_analogLR=state->config.scale_analogLR;
    HSD_PadLibData.scale_analogAB=state->config.scale_analogAB;
    HSD_PadLibData.cross_dir=state->config.cross_dir;
    HSD_PadLibData.reset_switch_status=state->config.reset_switch_status;
    HSD_PadLibData.reset_switch=state->config.reset_switch;
    memcpy(HSD_PadMasterStatus,state->history[0],sizeof(state->history[0]));
    memcpy(HSD_PadCopyStatus,state->history[1],sizeof(state->history[1]));
    memcpy(HSD_PadGameStatus,state->history[2],sizeof(state->history[2]));
}
