/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_WEB_SHADOW_WOBJ_DIAGNOSTIC_H
#define MELEE_WEB_SHADOW_WOBJ_DIAGNOSTIC_H

#include <stdint.h>

#if !defined(MELEE_WEB_SHADOW_WOBJ_DIAGNOSTIC_IMPLEMENTATION) && \
    (defined(__GNUC__) || defined(__clang__))
#define MELEE_WEB_SHADOW_WOBJ_DIAGNOSTIC_IMPORT __attribute__((weak))
#else
#define MELEE_WEB_SHADOW_WOBJ_DIAGNOSTIC_IMPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

int melee_web_shadow_wobj_diagnostic_active(void)
    MELEE_WEB_SHADOW_WOBJ_DIAGNOSTIC_IMPORT;
uint32_t melee_web_shadow_wobj_diagnostic_before(
    uint32_t site, const void* fp, const void* shadow,
    const void* camera, const void* eye, const void* interest,
    const void* target, const void* position)
    MELEE_WEB_SHADOW_WOBJ_DIAGNOSTIC_IMPORT;
void melee_web_shadow_wobj_diagnostic_after(uint32_t token,
                                            const void* target,
                                            const void* position)
    MELEE_WEB_SHADOW_WOBJ_DIAGNOSTIC_IMPORT;
void melee_web_shadow_wobj_diagnostic_context(uint32_t command,
                                              uint32_t command_entry_frame);
void melee_web_shadow_wobj_diagnostic_flush(void);

#ifdef __cplusplus
}
#endif

#undef MELEE_WEB_SHADOW_WOBJ_DIAGNOSTIC_IMPORT
#endif
