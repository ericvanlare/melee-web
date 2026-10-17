#ifndef MELEE_WEB_GAMEPLAY_FIRST_PAIR_FILE_GUARD_H
#define MELEE_WEB_GAMEPLAY_FIRST_PAIR_FILE_GUARD_H

#ifdef __cplusplus
extern "C" {
#endif
#if defined(__EMSCRIPTEN__) && defined(MELEE_WEB_FIRST_PAIR_AUDIO_FILE_DIAGNOSTIC)
int melee_web_first_pair_file_guard_arm(void);
extern volatile int melee_web_first_pair_file_guard_enabled;
int melee_web_first_pair_file_operation_host(const char* operation);
#endif
#ifdef __cplusplus
}
#endif

/* Private diagnostic seam. The private native flag defaults off, so ordinary
 * source and audio requests do not cross into JavaScript. An enabled host
 * callback records failure outside Wasm memory before refusing the operation. */
#if defined(__EMSCRIPTEN__) && defined(MELEE_WEB_FIRST_PAIR_AUDIO_FILE_DIAGNOSTIC)
static inline int melee_web_first_pair_file_operation(const char* operation)
{
    return !melee_web_first_pair_file_guard_enabled ||
           melee_web_first_pair_file_operation_host(operation);
}
#else
static inline int melee_web_first_pair_file_operation(const char* operation)
{
    (void)operation;
    return 1;
}
#endif

#endif
