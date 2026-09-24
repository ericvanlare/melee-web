#include "source_arq_context.h"

#include <string.h>

static int range_contains(uint32_t base, uint32_t length,
                          uint32_t address, uint32_t extent)
{
    const uint64_t end = (uint64_t)address + (uint64_t)extent;
    const uint64_t limit = (uint64_t)base + (uint64_t)length;
    return extent != 0 && end >= address &&
           (uint64_t)address >= base && end <= limit;
}

static int host_range_valid(const unsigned char* bytes, uint32_t length)
{
    const uintptr_t start = (uintptr_t)bytes;
    const uintptr_t end = start + (uintptr_t)length;
    return bytes && length && end >= start;
}

static int host_ranges_overlap(const unsigned char* first, uint32_t first_length,
                               const unsigned char* second, uint32_t second_length)
{
    const uintptr_t first_start = (uintptr_t)first;
    const uintptr_t second_start = (uintptr_t)second;
    const uintptr_t first_end = first_start + (uintptr_t)first_length;
    const uintptr_t second_end = second_start + (uintptr_t)second_length;
    return first_start < second_end && second_start < first_end;
}

static int is_busy(const MeleeWebSourceArqContext* context)
{
    return context->pending || context->pumping;
}

static unsigned char* find_mainmem(MeleeWebSourceArqContext* context,
                                   uint32_t address, uint32_t length)
{
    size_t index;
    for (index = 0; index < context->span_count; ++index) {
        MeleeWebSourceArqSpan* span = &context->spans[index];
        if (range_contains(span->address, span->length, address, length))
            return span->bytes + (address - span->address);
    }
    return NULL;
}

static unsigned char* find_aram(MeleeWebSourceArqContext* context,
                                uint32_t address, uint32_t length)
{
    if (!range_contains(0, context->aram_length, address, length))
        return NULL;
    return context->aram + address;
}

int melee_web_source_arq_bind(MeleeWebSourceArqContext* context,
                              const MeleeWebSourceArqSpan* spans,
                              size_t span_count,
                              unsigned char* aram,
                              uint32_t aram_length)
{
    size_t index;
    size_t prior;
    if (!context || !spans || !aram || !span_count ||
        span_count > MELEE_WEB_SOURCE_ARQ_MAX_SPANS || !aram_length)
        return MELEE_WEB_SOURCE_ARQ_INVALID;
    if (is_busy(context)) return MELEE_WEB_SOURCE_ARQ_BUSY;
    if (!host_range_valid(aram, aram_length) ||
        aram_length % MELEE_WEB_SOURCE_ARQ_ALIGNMENT)
        return MELEE_WEB_SOURCE_ARQ_INVALID;
    for (index = 0; index < span_count; ++index) {
        if (!spans[index].bytes || !spans[index].length ||
            spans[index].address % MELEE_WEB_SOURCE_ARQ_ALIGNMENT ||
            spans[index].length % MELEE_WEB_SOURCE_ARQ_ALIGNMENT ||
            !host_range_valid(spans[index].bytes, spans[index].length) ||
            (uint64_t)spans[index].address + spans[index].length >
                UINT64_C(0x100000000))
            return MELEE_WEB_SOURCE_ARQ_INVALID;
        if (host_ranges_overlap(spans[index].bytes, spans[index].length,
                                aram, aram_length))
            return MELEE_WEB_SOURCE_ARQ_INVALID;
        for (prior = 0; prior < index; ++prior) {
            const uint64_t start = spans[index].address;
            const uint64_t end = start + spans[index].length;
            const uint64_t prior_start = spans[prior].address;
            const uint64_t prior_end = prior_start + spans[prior].length;
            if (start < prior_end && prior_start < end)
                return MELEE_WEB_SOURCE_ARQ_INVALID;
            if (host_ranges_overlap(spans[index].bytes, spans[index].length,
                                    spans[prior].bytes, spans[prior].length))
                return MELEE_WEB_SOURCE_ARQ_INVALID;
        }
    }
    memcpy(context->spans, spans, span_count * sizeof(*spans));
    context->span_count = span_count;
    context->aram = aram;
    context->aram_length = aram_length;
    context->initialized = 1;
    context->pending = 0;
    context->pumping = 0;
    context->pending_mainmem = NULL;
    context->pending_aram = NULL;
    context->pending_length = 0;
    context->callback = NULL;
    context->submitted_count = 0;
    return MELEE_WEB_SOURCE_ARQ_OK;
}

int melee_web_source_arq_shutdown(MeleeWebSourceArqContext* context)
{
    if (!context || !context->initialized)
        return MELEE_WEB_SOURCE_ARQ_UNINITIALIZED;
    if (is_busy(context)) return MELEE_WEB_SOURCE_ARQ_BUSY;
    memset(context, 0, sizeof(*context));
    return MELEE_WEB_SOURCE_ARQ_OK;
}

int melee_web_source_arq_register_callback(MeleeWebSourceArqContext* context,
                                            MeleeWebSourceArqCallback callback)
{
    if (!context || !context->initialized)
        return MELEE_WEB_SOURCE_ARQ_UNINITIALIZED;
    if (is_busy(context)) return MELEE_WEB_SOURCE_ARQ_BUSY;
    context->callback = callback;
    return MELEE_WEB_SOURCE_ARQ_OK;
}

int melee_web_source_arq_start(MeleeWebSourceArqContext* context,
                               uint32_t type,
                               uint32_t mainmem_address,
                               uint32_t aram_address,
                               uint32_t length)
{
    unsigned char* mainmem;
    unsigned char* aram;
    if (!context || !context->initialized)
        return MELEE_WEB_SOURCE_ARQ_UNINITIALIZED;
    if (context->pending) return MELEE_WEB_SOURCE_ARQ_BUSY;
    if (!context->callback) return MELEE_WEB_SOURCE_ARQ_MISSING_CALLBACK;
    if (type != MELEE_WEB_SOURCE_ARQ_MRAM_TO_ARAM &&
        type != MELEE_WEB_SOURCE_ARQ_ARAM_TO_MRAM)
        return MELEE_WEB_SOURCE_ARQ_INVALID;
    if (!length || length % MELEE_WEB_SOURCE_ARQ_ALIGNMENT ||
        mainmem_address % MELEE_WEB_SOURCE_ARQ_ALIGNMENT ||
        aram_address % MELEE_WEB_SOURCE_ARQ_ALIGNMENT)
        return MELEE_WEB_SOURCE_ARQ_INVALID;
    mainmem = find_mainmem(context, mainmem_address, length);
    aram = find_aram(context, aram_address, length);
    if (!mainmem || !aram) return MELEE_WEB_SOURCE_ARQ_INVALID;
    context->pending_type = type;
    context->pending_mainmem = mainmem;
    context->pending_aram = aram;
    context->pending_length = length;
    context->pending = 1;
    ++context->submitted_count;
    return MELEE_WEB_SOURCE_ARQ_OK;
}

int melee_web_source_arq_pump(MeleeWebSourceArqContext* context)
{
    if (!context || !context->initialized)
        return MELEE_WEB_SOURCE_ARQ_UNINITIALIZED;
    if (context->pumping) return MELEE_WEB_SOURCE_ARQ_REENTRANT_PUMP;
    if (!context->pending) return MELEE_WEB_SOURCE_ARQ_NO_PENDING;
    if (!context->callback) return MELEE_WEB_SOURCE_ARQ_MISSING_CALLBACK;
    context->pumping = 1;
    if (context->pending_type == MELEE_WEB_SOURCE_ARQ_MRAM_TO_ARAM) {
        memcpy(context->pending_aram, context->pending_mainmem,
               context->pending_length);
    } else if (context->pending_type == MELEE_WEB_SOURCE_ARQ_ARAM_TO_MRAM) {
        memcpy(context->pending_mainmem, context->pending_aram,
               context->pending_length);
    } else {
        context->pumping = 0;
        return MELEE_WEB_SOURCE_ARQ_INVALID;
    }
    context->pending = 0;
    context->pending_mainmem = NULL;
    context->pending_aram = NULL;
    context->pending_length = 0;
    context->callback();
    context->pumping = 0;
    return MELEE_WEB_SOURCE_ARQ_OK;
}

int melee_web_source_arq_pending(const MeleeWebSourceArqContext* context)
{
    return context && context->initialized && context->pending;
}

uint32_t melee_web_source_arq_submitted_count(
    const MeleeWebSourceArqContext* context)
{
    return context ? context->submitted_count : 0;
}
