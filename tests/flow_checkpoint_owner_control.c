/* Compile the exact production flow owner as wasm32. Lifecycle prerequisites
 * are synthetic; this tests callback admission, not original match behavior. */
#include "../src/gameplay_match_flow.c"
#include <assert.h>
#include <string.h>
static uint64_t fixture_generation=9;
uint64_t melee_web_gameplay_generation(void){return fixture_generation;}
HSD_GObj *HSD_GObj_804D7814,*HSD_GObj_804D7818,*HSD_GObj_804D781C;
int main(void)
{
    char error[256];
    MeleeWebMatchFlow flow={fixture_generation,1,0,0};
    owner=&flow;owner_epoch=7;
    MeleeWebMatchFlowSnapshot* snapshot=melee_web_match_flow_snapshot_capture(&flow,error,sizeof(error));
    assert(snapshot);
    const MeleeWebMatchFlow before=flow;
    HSD_GObj marker={0};
    HSD_GObj** slots[]={&HSD_GObj_804D781C,&HSD_GObj_804D7818,&HSD_GObj_804D7814};
    for(unsigned i=0;i<3;++i){
        *slots[i]=&marker;
        assert(!melee_web_match_flow_snapshot_capture(&flow,error,sizeof(error)));
        assert(!melee_web_match_flow_snapshot_admit(&flow,snapshot,error,sizeof(error)));
        assert(!melee_web_match_flow_snapshot_restore(&flow,snapshot,error,sizeof(error)));
        assert(!memcmp(&before,&flow,sizeof(flow)) && *slots[i]==&marker);
        *slots[i]=NULL;
    }
    flow.source_valid=0;flow.complete=1;
    assert(melee_web_match_flow_snapshot_restore(&flow,snapshot,error,sizeof(error)));
    assert(!memcmp(&before,&flow,sizeof(flow)));
    melee_web_match_flow_snapshot_destroy(snapshot);
    puts("Actual wasm32 flow owner callback controls passed");
}
