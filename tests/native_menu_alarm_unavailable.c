#include <stdio.h>
#include <stdlib.h>
#include <dolphin/types.h>
/* Explicit limits of the native menu browser target. Never synthesize a
 * successful alarm or snapshot transition when no provider ran it. */
/* OSCreateAlarm and OSSetAlarm are provided by gameplay_source_alarm.c.
 * They abort outside an owned movie alarm scope, and periodic alarms are
 * accepted only inside that scope. */
/* THP's retail decoder requires GameCube locked-cache hardware. The CSS-first
 * browser menu route does not enter GM_OPENING; stop if that boundary is ever
 * reached instead of mapping its 0xE0000000 cache to ordinary Wasm memory. */
void DCZeroRange(void* address,u32 bytes){(void)address;(void)bytes;fputs("Native menu reached unsupported THP cache zeroing\n",stderr);abort();}
void LCEnable(void){fputs("Native menu reached unsupported THP locked-cache enable\n",stderr);abort();}
u32 LCStoreData(void* destination,void* source,u32 bytes){(void)destination;(void)source;(void)bytes;fputs("Native menu reached unsupported THP locked-cache store\n",stderr);abort();}
void LCQueueWait(u32 length){(void)length;fputs("Native menu reached unsupported THP locked-cache wait\n",stderr);abort();}
u32 PPCMfhid2(void){fputs("Native menu reached unsupported PPC HID2 read\n",stderr);abort();}
/* These signatures match sysdolphin/baselib/hsd_3B34.h. */
s32 hsd_803B51C8(s32 image,s32 height,s32 width,char* comment,s32 limit){(void)image;(void)height;(void)width;(void)comment;(void)limit;fputs("Native menu snapshot reached unsupported JPEG encode\n",stderr);abort();}
void hsd_803B5C2C(s32 quality){(void)quality;fputs("Native menu snapshot reached unsupported JPEG quality setup\n",stderr);abort();}
s32 hsd_803B6BE4(char* image,s32 size,void* output){(void)image;(void)size;(void)output;fputs("Native menu snapshot reached unsupported JPEG decode\n",stderr);abort();}
