#include "gameplay_first_pair_file_guard.h"

#if defined(__EMSCRIPTEN__) && defined(MELEE_WEB_FIRST_PAIR_AUDIO_FILE_DIAGNOSTIC)
#include <emscripten.h>

volatile int melee_web_first_pair_file_guard_enabled = 0;

int melee_web_first_pair_file_guard_arm(void)
{
    melee_web_first_pair_file_guard_enabled = 1;
    return 1;
}

int melee_web_first_pair_file_operation_host(const char* operation)
{
    return EM_ASM_INT({
        const host = globalThis.window || globalThis;
        const callback = host.menuFirstPairNativeFileOperation;
        if (typeof callback !== 'function') {
            globalThis.__meleeWebFirstPairFileGuardFailure = 'native file guard callback is missing';
            try { host.menuFirstPairNativeFileOperationFailure?.(globalThis.__meleeWebFirstPairFileGuardFailure); }
            catch (_) {}
            return 0;
        }
        try {
            const accepted = callback(UTF8ToString($0)) === true;
            if (!accepted) globalThis.__meleeWebFirstPairFileGuardFailure = 'native file operation refused';
            return accepted ? 1 : 0;
        } catch (error) {
            globalThis.__meleeWebFirstPairFileGuardFailure = String(error);
            try { host.menuFirstPairNativeFileOperationFailure?.(String(error)); }
            catch (_) {}
            return 0;
        }
    }, operation) != 0;
}
#endif
