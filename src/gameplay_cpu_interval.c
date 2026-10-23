#define MELEE_WEB_CPU_INTERVAL_IMPLEMENTATION 1
#include "gameplay_cpu_interval.h"
#include <dolphin/pad.h>
#include <limits.h>
static uint64_t epoch;
static unsigned active, failure;
uint64_t melee_web_cpu_interval_begin(void)
{
#if defined(__EMSCRIPTEN_PTHREADS__)
 return 0;
#else
 if(active||epoch==UINT64_MAX)return 0;
 ++epoch;active=1;failure=0;return epoch;
#endif
}
int melee_web_cpu_interval_admit(uint64_t token)
{return active&&token&&token==epoch&&!failure;}
int melee_web_cpu_interval_attempt(enum MeleeWebCpuIntervalEffect effect)
{
 if(!active)return 1;
 if(!failure)failure=effect?effect:UINT_MAX;
 return 0;
}
unsigned melee_web_cpu_interval_failure(void){return failure;}
int melee_web_cpu_interval_active_failure(void){return active&&failure;}
int melee_web_cpu_interval_end(uint64_t token)
{
 if(!active||!token||token!=epoch)return 0;
 active=0;return 1; /* Failure stays readable until the next explicit begin. */
}
void melee_web_cpu_interval_pad_motor(uint32_t channel,uint32_t command)
{
 if(melee_web_cpu_interval_attempt(MELEE_WEB_CPU_EFFECT_MOTOR))
  PADControlMotor(channel,command);
}
