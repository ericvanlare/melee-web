#ifndef MELEE_WEB_SOURCE_DEVCOM_CONTEXT_H
#define MELEE_WEB_SOURCE_DEVCOM_CONTEXT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Fixture-only continuation after the original HSD startup prefix.
int melee_web_source_devcom_run(uint32_t aram_size, uint32_t aram_base);
#ifdef __cplusplus
}
#endif
#endif
