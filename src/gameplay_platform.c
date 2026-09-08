#include "gameplay_platform.h"
#include <dolphin/ai.h>
#include <dolphin/ax.h>
#include <dolphin/card.h>
#include <dolphin/dvd.h>
#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <dolphin/os/OSCache.h>
#include <dolphin/os/OSReset.h>
#include <dolphin/vi.h>
#include <sysdolphin/baselib/devcom.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static int interrupts_enabled=1;
int melee_web_platform_interrupts_enabled(void){return interrupts_enabled;}
void melee_web_platform_reset_interrupts(void){atomic_signal_fence(memory_order_seq_cst);interrupts_enabled=1;}
BOOL OSDisableInterrupts(void){int previous=interrupts_enabled;atomic_signal_fence(memory_order_seq_cst);interrupts_enabled=0;return previous;}
BOOL OSEnableInterrupts(void){int previous=interrupts_enabled;interrupts_enabled=1;atomic_signal_fence(memory_order_seq_cst);return previous;}
BOOL OSRestoreInterrupts(BOOL level){int previous=interrupts_enabled;interrupts_enabled=level!=0;atomic_signal_fence(memory_order_seq_cst);return previous;}
/* CPU accesses share coherent Wasm linear memory; there are no separate dirty
 * CPU cache lines to flush. Preserve compiler ordering without changing bytes.
 * GPU upload/synchronization remains the renderer's responsibility. */
void DCFlushRange(void* addr,u32 bytes){(void)addr;(void)bytes;atomic_signal_fence(memory_order_seq_cst);}
void DCInvalidateRange(void* addr,u32 bytes){(void)addr;(void)bytes;atomic_signal_fence(memory_order_seq_cst);}
void DCFlushRangeNoSync(void* addr,u32 bytes){(void)addr;(void)bytes;atomic_signal_fence(memory_order_seq_cst);}

_Noreturn void melee_web_platform_unavailable(const char* operation)
{
    fprintf(stderr,"Unsupported gameplay platform operation: %s\n",operation);
    fflush(stderr);
    abort();
}
/* These unavailable systems are deliberately excluded from this target's
 * provider libraries. Do not link this boundary alongside real replacements.
 * Named parameters keep each signature checked against its actual SDK header. */
#define U __attribute__((unused))
#define STOP(ret,name,args) ret name args { melee_web_platform_unavailable(#name); }
STOP(void,OSResetSystem,(int reset U,u32 code U,BOOL force U))
STOP(void,VISetBlack,(BOOL black U))
STOP(VIRetraceCallback,VISetPostRetraceCallback,(VIRetraceCallback cb U))
STOP(VIRetraceCallback,VISetPreRetraceCallback,(VIRetraceCallback cb U))
STOP(void,VIWaitForRetrace,(void))
STOP(u32,VIGetNextField,(void))
STOP(s32,CARDCheckAsync,(s32 chan U,CARDCallback cb U))
STOP(s32,CARDClose,(CARDFileInfo* file U))
STOP(s32,CARDCreateAsync,(s32 chan U,const char* name U,u32 size U,CARDFileInfo* file U,CARDCallback cb U))
STOP(s32,CARDFastOpen,(s32 chan U,s32 number U,CARDFileInfo* file U))
STOP(s32,CARDGetXferredBytes,(s32 chan U))
STOP(s32,CARDRead,(const CARDFileInfo* file U,void* addr U,s32 length U,s32 offset U))
STOP(s32,CARDReadAsync,(const CARDFileInfo* file U,void* addr U,s32 length U,s32 offset U,CARDCallback cb U))
STOP(s32,CARDSetStatusAsync,(s32 chan U,s32 number U,const CARDStat* status U,CARDCallback cb U))
STOP(s32,CARDWrite,(const CARDFileInfo* file U,const void* addr U,s32 length U,s32 offset U))
STOP(s32,CARDWriteAsync,(const CARDFileInfo* file U,const void* addr U,s32 length U,s32 offset U,CARDCallback cb U))
STOP(s32,CARDDeleteAsync,(s32 chan U,const char* name U,CARDCallback cb U))
/* Aurora omits this declaration; signature matches original card.h. */
STOP(s32,CARDFormatAsync,(s32 chan U,CARDCallback cb U))
STOP(s32,CARDFreeBlocks,(s32 chan U,s32* bytes U,s32* files U))
STOP(s32,CARDGetStatus,(s32 chan U,s32 file U,CARDStat* status U))
STOP(s32,CARDMountAsync,(s32 chan U,void* work U,CARDCallback detach U,CARDCallback attach U))
STOP(s32,CARDOpen,(s32 chan U,const char* name U,CARDFileInfo* file U))
STOP(s32,CARDProbeEx,(s32 chan U,s32* memory U,s32* sector U))
STOP(s32,CARDRenameAsync,(s32 chan U,const char* old_name U,const char* new_name U,CARDCallback cb U))
STOP(s32,CARDUnmount,(s32 chan U))
STOP(DVDDiskID*,DVDGetCurrentDiskID,(void))
#if !defined(MELEE_WEB_AUDIO_STREAM)
STOP(s32,DVDGetDriveStatus,(void))
STOP(s32,DVDConvertPathToEntrynum,(const char* path U))
#endif
STOP(BOOL,DVDFastOpen,(s32 entry U,DVDFileInfo* file U))
STOP(BOOL,DVDClose,(DVDFileInfo* file U))
#if !defined(MELEE_WEB_AUDIO)
STOP(void,AISetStreamVolLeft,(u8 volume U))
STOP(void,AISetStreamVolRight,(u8 volume U))
#endif
#if !defined(MELEE_WEB_AUDIO)
typedef void (*VoiceCallback)(void*);
STOP(AXVPB*,AXAcquireVoice,(u32 priority U,VoiceCallback callback U,u32 context U))
STOP(void,AXFreeVoice,(AXVPB* voice U))
STOP(void,AXSetVoiceAddr,(AXVPB* voice U,AXPBADDR* addr U))
STOP(void,AXSetVoiceAdpcm,(AXVPB* voice U,AXPBADPCM* adpcm U))
STOP(void,AXSetVoiceCurrentAddr,(AXVPB* voice U,u32 addr U))
STOP(void,AXSetVoiceEndAddr,(AXVPB* voice U,u32 addr U))
STOP(void,AXSetVoiceItdOn,(AXVPB* voice U))
STOP(void,AXSetVoiceItdTarget,(AXVPB* voice U,u16 left U,u16 right U))
STOP(void,AXSetVoiceLoopAddr,(AXVPB* voice U,u32 addr U))
STOP(void,AXSetVoiceMix,(AXVPB* voice U,AXPBMIX* mix U))
STOP(void,AXSetVoicePriority,(AXVPB* voice U,u32 priority U))
STOP(void,AXSetVoiceSrc,(AXVPB* voice U,AXPBSRC* src U))
STOP(void,AXSetVoiceSrcRatio,(AXVPB* voice U,float ratio U))
STOP(void,AXSetVoiceState,(AXVPB* voice U,u16 state U))
STOP(void,AXSetVoiceVe,(AXVPB* voice U,AXPBVE* envelope U))
#endif
/* These two missing declarations match original GXPixel.h/GXManage.h. */
STOP(void,GXInitFogAdjTable,(GXFogAdjTable* table U,u16 width U,float projection[4][4] U))
STOP(void,GXWaitDrawDone,(void))
STOP(void,GXSetCopyClamp,(GXFBClamp clamp U))
#if !defined(MELEE_WEB_AUDIO_STREAM)
STOP(int,HSD_DevComRequest,(int file U,uintptr_t src U,uintptr_t dst U,size_t bytes U,int type U,int priority U,HSD_DevComCallback callback U,void* args U))
STOP(int,HSD_DevComCancelEx,(int request U,u32 flags U,HSD_DevComCallback callback U,void* args U))
#endif
#undef STOP
#undef U
