#ifndef MELEE_WEB_GAMEPLAY_THP_CPU_H
#define MELEE_WEB_GAMEPLAY_THP_CPU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CPU-owned equivalent of THP's 0x4000 locked-cache workspace. The scope is
 * one original MoviePlayer lifetime; stores require an owned scratch source
 * and a registered decoder-output destination. */
int melee_web_thp_cpu_begin(void);
void* melee_web_thp_cpu_scratch(void);
int melee_web_thp_cpu_output_register(void*, size_t);
int melee_web_thp_cpu_output_unregister(void*, size_t);
int melee_web_thp_cpu_store_valid(const void*, const void*, uint32_t);
int melee_web_thp_cpu_zero_valid(const void*, uint32_t);
int melee_web_thp_cpu_wait_valid(uint32_t);
int melee_web_thp_cpu_end(void);

uint32_t melee_web_thp_cpu_store(void*, void*, uint32_t);
void melee_web_thp_cpu_zero(void*, uint32_t);
void melee_web_thp_cpu_wait(uint32_t);

#ifdef __cplusplus
}
#endif

#endif
