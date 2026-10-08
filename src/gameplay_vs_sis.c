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

#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#include "gameplay_source_memory_runtime.h"
#include <string.h>

static int diagnostic_sis_roots_empty(void)
{
    /* Check heads without walking potentially retired foreign lists. The
     * source accessor derives the slot count from both authored arrays. */
    return used_head == NULL && HSD_SisLib_804D7978 == NULL &&
           HSD_SisLib_804D797C == NULL &&
           HSD_SisLib_AllFontSlotsEmpty();
}

int melee_web_diagnostic_sis_begin(MeleeWebDiagnosticSisOwner* owner,
                                  char* error, size_t error_size)
{
    MeleeWebSourceMemoryContext context;
    MeleeWebSourceMemoryAllocation allocation;
    void* prior_heap;
    if (!owner || owner->heap || owner->world_generation ||
        owner->allocation_generation)
        return fail(error, error_size, "Diagnostic SIS owner slot must be empty");
    if (melee_web_source_memory_context_read(&context) !=
        MELEE_WEB_SOURCE_MEMORY_READ_OK)
        return fail(error, error_size, "Diagnostic SIS requires a healthy source heap");
    if (!diagnostic_sis_roots_empty())
        return fail(error, error_size, "Diagnostic SIS refuses live text/context/font owners");
    prior_heap = HSD_SisLib_HeapOwner();
    if (prior_heap &&
        (melee_web_source_memory_allocation_read(prior_heap, &allocation) !=
             MELEE_WEB_SOURCE_MEMORY_READ_OK || allocation.live))
        return fail(error, error_size, "Diagnostic SIS refuses a live allocator owner");
    /* Original source initializer, as used by ordinary Match startup. No
     * allocator arithmetic changes and no manager/camera/proc dispatch. */
    HSD_SisLib_803A6048(MELEE_WEB_DIAGNOSTIC_SIS_HEAP_BYTES);
    owner->heap = HSD_SisLib_HeapOwner();
    owner->world_generation = context.world_generation;
    owner->source_heap_handle = context.source_heap_handle;
    if (!owner->heap ||
        melee_web_source_memory_allocation_read(owner->heap, &allocation) !=
            MELEE_WEB_SOURCE_MEMORY_READ_OK || !allocation.live ||
        allocation.world_generation != context.world_generation ||
        allocation.source_heap_handle != context.source_heap_handle ||
        allocation.requested_bytes != MELEE_WEB_DIAGNOSTIC_SIS_HEAP_BYTES ||
        allocation.allocation_generation <= context.allocation_generation_watermark)
        return fail(error, error_size, "Diagnostic SIS initialization lacks its exact new lease");
    owner->allocation_generation = allocation.allocation_generation;
    if (error && error_size) error[0] = '\0';
    return 1;
}

int melee_web_diagnostic_sis_end(MeleeWebDiagnosticSisOwner* owner,
                                char* error, size_t error_size)
{
    MeleeWebSourceMemoryContext context;
    MeleeWebSourceMemoryAllocation allocation;
    if (!owner || !owner->heap || !owner->allocation_generation)
        return fail(error, error_size, "Diagnostic SIS has no owned allocator to drain");
    if (melee_web_source_memory_context_read(&context) !=
            MELEE_WEB_SOURCE_MEMORY_READ_OK ||
        context.world_generation != owner->world_generation ||
        context.source_heap_handle != owner->source_heap_handle ||
        HSD_SisLib_HeapOwner() != owner->heap ||
        melee_web_source_memory_allocation_read(owner->heap, &allocation) !=
            MELEE_WEB_SOURCE_MEMORY_READ_OK || !allocation.live ||
        allocation.allocation_generation != owner->allocation_generation ||
        allocation.requested_bytes != MELEE_WEB_DIAGNOSTIC_SIS_HEAP_BYTES)
        return fail(error, error_size, "Diagnostic SIS allocator lease was replaced or retired");
    /* StageLast owns/removes its SIS context/texts and restores borrowed slot1
     * first. Refuse any remaining owner rather than sweep a foreign graph. */
    if (!diagnostic_sis_roots_empty())
        return fail(error, error_size, "Diagnostic SIS refuses live text/context/font owners");
    HSD_SisLib_803A5FBC();
    if (melee_web_source_memory_allocation_read(owner->heap, &allocation) !=
            MELEE_WEB_SOURCE_MEMORY_READ_OK || allocation.live ||
        !diagnostic_sis_roots_empty())
        return fail(error, error_size, "Diagnostic SIS drain did not retire its exact lease");
    memset(owner, 0, sizeof(*owner));
    if (error && error_size) error[0] = '\0';
    return 1;
}
#endif
