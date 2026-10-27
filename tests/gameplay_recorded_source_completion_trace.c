#include "gameplay_source_files.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
static int callbacks,predicates;
static unsigned char bytes[33],tail[31];
static _Alignas(32) unsigned char destination[128];
static void callback(int id,int args,void* p,int cancelled){
 assert(id>=0x50000000&&args==0&&!p&&!cancelled);++callbacks;
 assert(!memcmp(destination,bytes,33)&&!memcmp(destination+33,tail,31));
 assert(!strcmp(melee_web_source_file_take_name(destination),"Fire.dat"));
}
static void foreign_callback(int i,int a,void* b,int c){(void)i;(void)a;(void)b;(void)c;assert(0);}
static int matches(MeleeWebSourceFileCallback cb){++predicates;return cb==callback;}
int main(int argc,char**argv){assert(argc==2);const char* mode=argv[1];char e[256]={0};
 memset(bytes,0x51,sizeof(bytes));memset(tail,0x72,sizeof(tail));memset(destination,0xa5,sizeof(destination));
 int external=!strcmp(mode,"external");
 if(external)assert(melee_web_source_files_external_set("External.dat",32,e,sizeof(e)));
 MeleeWebSourceFileInput input={"Fire.dat",bytes,sizeof(bytes),tail,sizeof(tail)};
 MeleeWebSourceFileScope* scope=melee_web_source_files_begin(&input,1,e,sizeof(e));assert(scope);
 assert(melee_web_source_file_region_register(destination,sizeof(destination),e,sizeof(e)));
 int entry=melee_web_source_file_entry(external?"External.dat":"Fire.dat"),id=0;
 if(!strcmp(mode,"default")){
  assert(melee_web_source_file_request(entry,0,(uintptr_t)destination,64,0x21,1,callback,NULL,&id,e,sizeof(e)));
  assert(!callbacks&&melee_web_source_files_pump_for(scope,e,sizeof(e))&&callbacks==1);
  assert(melee_web_source_file_region_unregister(destination,sizeof(destination),e,sizeof(e)));
  assert(melee_web_source_files_end(scope,e,sizeof(e)));return 0;
 }
 MeleeWebSourceFileRecordedCompletionPlan plan={.name=external?"External.dat":"Fire.dat",.entry=entry,.type=0x21,.priority=1,.offset=0,.destination=(uintptr_t)destination,.logical_size=external?32:33,.transfer_size=external?32:64,.region_base=destination,.region_size=sizeof(destination),.request_cursor=2,.callback_cursor=4,.last_selected_cursor=7,.callback_matches=matches};
 if(!strcmp(mode,"wrongpath")){plan.name="Other.dat";assert(!melee_web_source_files_recorded_completion_arm(scope,&plan,e,sizeof(e)));return 0;}
 if(!strcmp(mode,"wrongentry")){plan.entry++;assert(!melee_web_source_files_recorded_completion_arm(scope,&plan,e,sizeof(e)));return 0;}
 if(!strcmp(mode,"wrongextent")){plan.transfer_size-=32;assert(!melee_web_source_files_recorded_completion_arm(scope,&plan,e,sizeof(e)));return 0;}
 if(!strcmp(mode,"wrongregion")){plan.region_size-=32;assert(!melee_web_source_files_recorded_completion_arm(scope,&plan,e,sizeof(e)));return 0;}
 if(!strcmp(mode,"wrongplan")){plan.logical_size++;assert(!melee_web_source_files_recorded_completion_arm(scope,&plan,e,sizeof(e)));return 0;}
 if(external){assert(!melee_web_source_files_recorded_completion_arm(scope,&plan,e,sizeof(e)));return 0;}
 assert(melee_web_source_files_recorded_completion_arm(scope,&plan,e,sizeof(e))&&!predicates);
 assert(!melee_web_source_files_quiescent_for(scope,e,sizeof(e)));
 assert(!melee_web_source_file_region_unregister(destination,sizeof(destination),e,sizeof(e)));
 assert(!melee_web_source_files_recorded_completion_arm(scope,&plan,e,sizeof(e)));
 assert(!melee_web_source_files_recorded_preconsume((MeleeWebSourceFileScope*)(uintptr_t)16,0,e,sizeof(e)));
 assert(melee_web_source_files_recorded_preconsume(scope,0,e,sizeof(e)));
 if(!strcmp(mode,"skip")){assert(!melee_web_source_files_recorded_preconsume(scope,2,e,sizeof(e)));return 0;}
 assert(melee_web_source_files_recorded_preconsume(scope,1,e,sizeof(e)));
 if(!strcmp(mode,"early")){assert(!melee_web_source_file_request(entry,0,(uintptr_t)destination,64,0x21,1,callback,NULL,&id,e,sizeof(e)));return 0;}
 assert(melee_web_source_files_recorded_preconsume(scope,2,e,sizeof(e)));
 if(!strcmp(mode,"missing")){assert(!melee_web_source_files_recorded_preconsume(scope,3,e,sizeof(e)));return 0;}
 if(!strcmp(mode,"wrongidentity")){
  assert(!melee_web_source_file_request(entry,0,(uintptr_t)destination,64,0x23,1,callback,NULL,&id,e,sizeof(e)));return 0;
 }
 if(!strcmp(mode,"wrongcallback")){assert(!melee_web_source_file_request(entry,0,(uintptr_t)destination,64,0x21,1,foreign_callback,NULL,&id,e,sizeof(e)));return 0;}
 assert(melee_web_source_file_request(entry,0,(uintptr_t)destination,plan.transfer_size,0x21,1,callback,NULL,&id,e,sizeof(e))&&predicates==1);
 if(!strcmp(mode,"cancel")){assert(!melee_web_source_file_cancel(id,0,NULL,NULL));assert(!melee_web_source_files_pump_for(scope,e,sizeof(e)));return 0;}
 if(!strcmp(mode,"supply")){assert(!melee_web_source_file_supply(id,bytes,plan.transfer_size,e,sizeof(e)));return 0;}
 if(!strcmp(mode,"duplicate")){assert(!melee_web_source_file_request(entry,0,(uintptr_t)destination,64,0x21,1,callback,NULL,&id,e,sizeof(e)));return 0;}
 assert(melee_web_source_files_pump_for(scope,e,sizeof(e)));
 assert(!callbacks&&destination[0]==0xa5&&melee_web_source_file_take_name(destination)==NULL);
 assert(melee_web_source_files_recorded_preconsume(scope,3,e,sizeof(e)));
 assert(!callbacks&&destination[0]==0xa5);
 if(!strcmp(mode,"late")){assert(!melee_web_source_files_recorded_preconsume(scope,5,e,sizeof(e))&&!callbacks&&destination[0]==0xa5);return 0;}

 assert(melee_web_source_files_recorded_preconsume(scope,4,e,sizeof(e))&&callbacks==1);
 MeleeWebSourceFileRecordedCompletionStatus status={0};assert(melee_web_source_files_recorded_completion_status(scope,&status,e,sizeof(e)));
 assert(status.completed&&!status.failed&&status.request_seen&&status.actual_request_cursor==2&&status.actual_callback_cursor==4);
 assert(melee_web_source_files_recorded_preconsume(scope,5,e,sizeof(e))&&callbacks==1);
 assert(melee_web_source_files_quiescent_for(scope,e,sizeof(e)));
 assert(melee_web_source_file_region_unregister(destination,sizeof(destination),e,sizeof(e)));
 assert(melee_web_source_files_end(scope,e,sizeof(e)));return 0;
}
