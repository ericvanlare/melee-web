#include "gameplay_vs_sis.h"

#include <stdio.h>
#include <sysdolphin/baselib/sislib.h>

static int fail(char* error, size_t size, const char* message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

int melee_web_gameplay_vs_sis(MeleeWebGameplayVSSisOperation operation,
                              int font_slot, void* expected,
                              char* error, size_t error_size)
{
    if (operation == MELEE_WEB_VS_SIS_DRAIN) {
        /* Original teardown is complete-or-fatal; never fabricate completion. */
        HSD_SisLib_803A5FBC();
    } else {
        if (operation != MELEE_WEB_VS_SIS_VALIDATE_BORROW &&
            operation != MELEE_WEB_VS_SIS_PREFLIGHT_BORROW &&
            operation != MELEE_WEB_VS_SIS_RETIRE_BORROW)
            return fail(error, error_size, "Unknown VS SIS ownership operation");
        if (!expected || !HSD_SisLib_FontSlotBorrowable(font_slot))
            return fail(error, error_size,
                        "Borrowed SIS slot is invalid or has a source archive owner");
        SIS* current = HSD_SisLib_804D1124[font_slot];
        if (operation == MELEE_WEB_VS_SIS_VALIDATE_BORROW) {
            if (current != NULL)
                return fail(error, error_size,
                            "Borrowed SIS slot already has a descriptor owner");
        } else {
            if (current != NULL && current != (SIS*)expected)
                return fail(error, error_size,
                            "Borrowed SIS slot was replaced by a foreign descriptor");
            if (operation == MELEE_WEB_VS_SIS_RETIRE_BORROW &&
                current == (SIS*)expected)
                HSD_SisLib_804D1124[font_slot] = NULL;
        }
    }
    if (error && error_size) error[0] = '\0';
    return 1;
}
