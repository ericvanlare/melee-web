#include "gameplay_platform.h"
#include <dolphin/ai.h>
#include <dolphin/ax.h>
#include <dolphin/card.h>
#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <dolphin/os/OSCache.h>
#include <dolphin/vi.h>
#include <sysdolphin/baselib/devcom.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"CHECK failed: %s\n",#c);exit(2);}}while(0)
static void card_callback(s32 chan,s32 result){(void)chan;(void)result;fputs("UNEXPECTED_CALLBACK\n",stderr);}
static void transfer_callback(int request,int args,void* buffer,melee_source_bool cancelled){(void)request;(void)args;(void)buffer;(void)cancelled;fputs("UNEXPECTED_CALLBACK\n",stderr);}
static void report_v(const char* format,...){va_list args;va_start(args,format);OSVReport(format,args);va_end(args);}
int main(int argc,char** argv){
 CHECK(argc==2);
 if(!strcmp(argv[1],"interrupts")){
  melee_web_platform_reset_interrupts();CHECK(melee_web_platform_interrupts_enabled());
  BOOL outer=OSDisableInterrupts();CHECK(outer==1&&!melee_web_platform_interrupts_enabled());
  BOOL inner=OSDisableInterrupts();CHECK(inner==0);
  CHECK(OSRestoreInterrupts(inner)==0&&!melee_web_platform_interrupts_enabled());
  CHECK(OSRestoreInterrupts(outer)==0&&melee_web_platform_interrupts_enabled());
  CHECK(OSRestoreInterrupts(0)==1&&!melee_web_platform_interrupts_enabled());
  CHECK(OSEnableInterrupts()==0&&melee_web_platform_interrupts_enabled());
  CHECK(OSEnableInterrupts()==1);
  unsigned char data[64],saved[64];for(unsigned i=0;i<64;i++)data[i]=i+1;
  memcpy(saved,data,sizeof(data));DCFlushRange(data,sizeof(data));CHECK(!memcmp(saved,data,sizeof(data)));
  DCFlushRange(NULL,0);melee_web_platform_reset_interrupts();
  puts("Single-thread interrupt nesting and coherent cache boundary passed");return 0;
 }
 if(!strcmp(argv[1],"report")){
  OSReport("report %d %s %.2f\n",7,"ok",1.5);
  report_v("vreport %u %s\n",11u,"ok");
  return 0;
 }
 if(!strcmp(argv[1],"panic")){OSPanic("platform-test.c",37,"panic %d %s",5,"boom");}
 if(!strcmp(argv[1],"card"))CARDMountAsync(0,NULL,card_callback,card_callback);
 if(!strcmp(argv[1],"card-create"))CARDCreateAsync(0,"test",32,NULL,card_callback);
 if(!strcmp(argv[1],"dvd"))DVDConvertPathToEntrynum("/missing.dat");
 if(!strcmp(argv[1],"audio"))AXAcquireVoice(1,NULL,0);
 if(!strcmp(argv[1],"video"))VIGetNextField();
 if(!strcmp(argv[1],"transfer"))HSD_DevComRequest(0,0,0,32,0x21,1,transfer_callback,NULL);
 fputs("UNEXPECTED_RETURN\n",stderr);return 3;
}
