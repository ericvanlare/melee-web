
#include "gameplay_source_files.h"
#include "gameplay_cpu_interval.h"
#include "gameplay_platform.h"
#include <sysdolphin/baselib/devcom.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned motors,callbacks;
void PADControlMotor(unsigned c,unsigned v){assert(c==2&&v==1);++motors;}
_Noreturn void melee_web_platform_unavailable(const char* message){fprintf(stderr,"UNEXPECTED provider: %s\n",message);abort();}
void fixture_bank_barrier(void);
static void callback(int r,int args,void* p,int cancelled){assert(!cancelled);++callbacks;}
int main(int argc,char** argv){
 unsigned char bytes[64]={1,2,3};
 _Alignas(32) unsigned char destination[64]={0};
 MeleeWebSourceFileInput input={"test.dat",bytes,sizeof(bytes)};
 char error[160];
 MeleeWebSourceFileScope* scope=melee_web_source_files_begin(&input,1,error,sizeof(error));assert(scope);
 assert(melee_web_source_file_region_register(destination,sizeof(destination),error,sizeof(error)));
 int entry=melee_web_source_file_entry("test.dat");assert(entry>=0);
 int id=HSD_DevComRequest(entry,0,(uintptr_t)destination,32,0x21,1,callback,NULL);assert(id>0);
 assert(!callbacks);assert(melee_web_source_files_pump(error,sizeof(error)));assert(callbacks==1&&memcmp(bytes,destination,32)==0);
 melee_web_cpu_interval_pad_motor(2,1);assert(motors==1);
 uint64_t token=melee_web_cpu_interval_begin();assert(token&&!melee_web_cpu_interval_begin());
 assert(HSD_DevComRequest(entry,0,(uintptr_t)destination,32,0x21,1,callback,NULL)==-1);
 assert(melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_SOURCE_FILE);
 assert(callbacks==1&&!melee_web_source_file_drive_busy());
 assert(!melee_web_cpu_interval_admit(token));
 assert(!melee_web_cpu_interval_end(token+1));assert(melee_web_cpu_interval_end(token));
 assert(!melee_web_cpu_interval_active_failure()&&melee_web_cpu_interval_failure());
 if(argc>1){HSD_DevComRequest(entry,1,(uintptr_t)destination,32,0x21,1,callback,NULL);assert(0);}
 token=melee_web_cpu_interval_begin();assert(token&&melee_web_cpu_interval_admit(token));
 fixture_bank_barrier();assert(melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_SSM);
 assert(melee_web_cpu_interval_end(token));
 token=melee_web_cpu_interval_begin();assert(token);
 assert(HSD_DevComRequest(-1,0,0,0,0,0,NULL,NULL)==-1);
 assert(melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_HPS);
 assert(melee_web_cpu_interval_end(token));
 token=melee_web_cpu_interval_begin();assert(token);
 melee_web_cpu_interval_pad_motor(2,1);assert(motors==1&&melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_MOTOR);
 assert(melee_web_cpu_interval_end(token));melee_web_cpu_interval_pad_motor(2,1);assert(motors==2);
 assert(melee_web_source_file_region_unregister(destination,sizeof(destination),error,sizeof(error)));
 assert(melee_web_source_files_end(scope,error,sizeof(error)));
 puts("Actual interval/source request/SSM/HPS/motor guards and inactive continuation passed");return 0;
}
