#include "gameplay_effect_runtime.h"
#include "gameplay_bootstrap.h"
#include <melee/ef/eflib.h>
#include <melee/ef/efdata.h>
#include <melee/ef/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/generator.h>
#include <sysdolphin/baselib/particle.h>
#include <sysdolphin/baselib/jobj.h>
#include <stdio.h>
#include <sysdolphin/baselib/displayfunc.h>
extern EF_DAT_Entry efAsync_DatEntries[51];
extern void melee_web_particle_clear_all(void);
static uint64_t generation;
static int fail(char* e,size_t n,const char* text){if(e&&n)snprintf(e,n,"%s",text);return 0;}
int melee_web_effect_runtime_active(void){return generation&&generation==melee_web_gameplay_stats().generation;}
int melee_web_effect_runtime_begin(char* e,size_t n){
    uint64_t current=melee_web_gameplay_stats().generation;
    if(!current||generation||efLib_EffectCount||hsd_804D78E0)
        return fail(e,n,"Original effect runtime already owned or world absent");
    for(unsigned i=0;i<50;i++)if(efAsync_DatEntries[i].data)
        return fail(e,n,"Initialize original effects before publishing banks");
    HSD_GObj** links=(HSD_GObj**)HSD_GObj_Entities;
    if(links[11]||links[12])return fail(e,n,"Original effect process links already occupied");
    efLib_Init();generation=current;if(e&&n)*e=0;return 1;
}
int melee_web_effect_runtime_end(char* e,size_t n){
    if(!generation)return 1;
    if(generation!=melee_web_gameplay_stats().generation)
        return fail(e,n,"Effect runtime source world changed");
    HSD_GObj** links=(HSD_GObj**)HSD_GObj_Entities;
    for(unsigned link=11;link<=12;link++)while(links[link])HSD_GObjPLink_80390228(links[link]);
    /* Drain the source's deferred joint transforms with all simulation links
     * skipped, then use the same source destructor as normal particle expiry. */
    hsd_8039EE24(UINT32_MAX);
    melee_web_particle_clear_all();
    while(hsd_804D78FC){
        /* Generators may share an AppSRT.  The owning generator cannot be
         * removed until every later borrower has released its reference, so
         * make a full source-list pass instead of retrying only the head. */
        const u16 before=hsd_804D78E0;
        HSD_Generator* generator=hsd_804D78FC;
        while(generator){
            HSD_Generator* next=generator->next;
            hsd_8039D4DC(generator);
            generator=next;
        }
        if(hsd_804D78E0>=before)
            return fail(e,n,"Original generators retain external SRT references");
    }
    if(efLib_EffectCount||hsd_804D78E0)return fail(e,n,"Original effects remain alive after teardown");
    HSD_JObjSetSPtclCallback(NULL);HSD_JObjSetDPtclCallback(NULL);hsd_804D7900=NULL;psCamera=NULL;
    generation=0;if(e&&n)*e=0;return 1;
}
