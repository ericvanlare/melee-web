#ifndef MELEE_WEB_GAMEPLAY_PLATFORM_H
#define MELEE_WEB_GAMEPLAY_PLATFORM_H
#ifdef __cplusplus
extern "C" {
#endif
/* Logical source interrupt mask for the explicitly single-threaded gameplay
 * scheduler. Deferred device callbacks must be pumped only while enabled.
 * This does not emulate hardware interrupts or provide thread synchronization. */
int melee_web_platform_interrupts_enabled(void);
void melee_web_platform_reset_interrupts(void);
/* Missing platform services terminate with their exact operation name. Their
 * typed exported functions must never return fabricated success or callbacks. */
#ifdef __cplusplus
[[noreturn]] void melee_web_platform_unavailable(const char* operation);
#else
_Noreturn void melee_web_platform_unavailable(const char* operation);
#endif
#ifdef __cplusplus
}
#endif
#endif
