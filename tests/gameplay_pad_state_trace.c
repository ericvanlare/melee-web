#include "gameplay_pad_state.h"
#include <sysdolphin/baselib/controller.h>
#include <melee/gm/gm_1A36.h>
#include <melee/mn/mnmain.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
extern PadLibData default_libinfo_data;
extern void gm_801A3E88(void);

static void renew_sample(HSD_PadData* queue,u32 button)
{
    memset(queue,0,sizeof(*queue));
    queue->stat[0].button=button;
    HSD_PadLibData.qread=HSD_PadLibData.qwrite=0;
    HSD_PadLibData.qcount=1;
    HSD_PadRenewMasterStatus();
    HSD_PadRenewCopyStatus();
    HSD_PadRenewGameStatus();
    assert(HSD_PadLibData.qcount==0);
    gm_EvaluateAllControllerInputs();
}

int main(void){
    HSD_PadData queue={0};
    HSD_PadLibData=default_libinfo_data;
    HSD_PadLibData.qnum=1;HSD_PadLibData.queue=&queue;
    gm_801A3E88();
    HSD_PadStatus* banks[]={HSD_PadMasterStatus,HSD_PadCopyStatus,HSD_PadGameStatus};
    for(unsigned b=0;b<3;b++)for(unsigned p=0;p<4;p++){
        HSD_PadStatus* h=&banks[b][p];
        h->button=b?PAD_BUTTON_B:PAD_BUTTON_A;h->last_button=PAD_BUTTON_X;
        h->repeat_count=22+b+p;h->stickX=-37;h->nml_stickX=-0.5f;h->nml_analogL=0.25f;
        h->cross_dir=1;h->err=0;
    }
    unsigned char bytes[MELEE_WEB_PAD_STATE_BYTES],restored[MELEE_WEB_PAD_STATE_BYTES];
    melee_web_pad_state_capture(bytes);
    /* Independent semantic byte offsets: config is 30 bytes, native histories
     * have no copied tail padding, signed stick bytes and float bits survive. */
    assert(bytes[3]==45&&bytes[7]==8);
    assert(bytes[30+2]==1&&bytes[30+3]==0&&bytes[30+23]==22);
    assert(bytes[30+24]==(unsigned char)-37);
    assert(bytes[30+32]==0xbf&&bytes[30+33]==0x00);
    char error[256];
    MeleeWebPadState* snapshot=melee_web_pad_state_decode(bytes,sizeof(bytes),error,sizeof(error));
    assert(snapshot);
    const PadLibData saved=HSD_PadLibData;
    HSD_PadLibData.repeat_start=99;
    for(unsigned b=0;b<3;b++)memset(banks[b],0,sizeof(HSD_PadStatus)*4);
    melee_web_pad_state_apply(snapshot);
    melee_web_pad_state_capture(restored);
    assert(!memcmp(bytes,restored,sizeof(bytes)));
    assert(HSD_PadLibData.queue==&queue&&HSD_PadLibData.qnum==saved.qnum);
    assert(!memcmp(&HSD_PadLibData.rumble_info,&saved.rumble_info,sizeof(saved.rumble_info)));
    /* Held A is not a new edge; Copy/Game still release their prior B. */
    for(unsigned p=0;p<4;p++)queue.stat[p].button=PAD_BUTTON_A;
    HSD_PadLibData.qcount=1;
    HSD_PadRenewMasterStatus();HSD_PadRenewCopyStatus();HSD_PadRenewGameStatus();
    assert(HSD_PadMasterStatus[0].trigger==0&&HSD_PadMasterStatus[0].repeat_count==21);
    assert(HSD_PadCopyStatus[0].trigger==PAD_BUTTON_A&&HSD_PadCopyStatus[0].release==PAD_BUTTON_B);
    assert(HSD_PadGameStatus[0].trigger==PAD_BUTTON_A&&HSD_PadGameStatus[0].release==PAD_BUTTON_B);

    /* A No Contest can leave the LRAS+A+Start chord held while the source
     * returns to CSS. Capture the real neutral -> chord -> held sequence, then
     * show that a reset fabricates a fresh chord while retained HSD history
     * suppresses that edge and still permits a real release/repress. */
    const u32 lras=PAD_TRIGGER_L|PAD_TRIGGER_R|PAD_BUTTON_A|PAD_BUTTON_START;
    gm_801A3E88();
    for(unsigned b=0;b<3;b++)memset(banks[b],0,sizeof(HSD_PadStatus)*4);
    renew_sample(&queue,0);
    renew_sample(&queue,lras);
    assert(HSD_PadCopyStatus[0].trigger==lras&&mn_8022F218()!=0);
    renew_sample(&queue,lras);
    assert(HSD_PadCopyStatus[0].button==lras&&HSD_PadCopyStatus[0].trigger==0&&mn_8022F218()==0);
    unsigned char retained_bytes[MELEE_WEB_PAD_STATE_BYTES];
    melee_web_pad_state_capture(retained_bytes);
    MeleeWebPadState* retained=melee_web_pad_state_decode(retained_bytes,sizeof(retained_bytes),error,sizeof(error));
    assert(retained);
    gm_801A3E88();
    for(unsigned b=0;b<3;b++)memset(banks[b],0,sizeof(HSD_PadStatus)*4);
    memset(&queue,0,sizeof(queue));
    HSD_PadLibData.qread=HSD_PadLibData.qwrite=HSD_PadLibData.qcount=0;
    renew_sample(&queue,lras);
    assert(HSD_PadCopyStatus[0].trigger==lras&&mn_8022F218()!=0);
    gm_801A3E88();
    for(unsigned b=0;b<3;b++)memset(banks[b],0,sizeof(HSD_PadStatus)*4);
    melee_web_pad_state_apply(retained);
    renew_sample(&queue,lras);
    assert(HSD_PadCopyStatus[0].trigger==0&&mn_8022F218()==0);
    renew_sample(&queue,0);
    assert(HSD_PadCopyStatus[0].release==lras&&mn_8022F218()==0);
    renew_sample(&queue,lras);
    assert(HSD_PadCopyStatus[0].trigger==lras&&mn_8022F218()!=0);
    melee_web_pad_state_free(retained);
    melee_web_pad_state_free(snapshot);
    assert(!melee_web_pad_state_decode(bytes,sizeof(bytes)-1,error,sizeof(error)));
    bytes[30+32]=0x7f;bytes[30+33]=0x80;bytes[30+34]=bytes[30+35]=0;
    assert(!melee_web_pad_state_decode(bytes,sizeof(bytes),error,sizeof(error)));
    puts("Typed PAD history: exact bytes, held/released edges, LRAS reset negative control, bounded decoding and pointer ownership passed");
}
