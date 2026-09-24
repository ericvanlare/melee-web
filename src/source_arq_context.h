#ifndef MELEE_WEB_SOURCE_ARQ_CONTEXT_H
#define MELEE_WEB_SOURCE_ARQ_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MELEE_WEB_SOURCE_ARQ_MAX_SPANS 8u
#define MELEE_WEB_SOURCE_ARQ_ALIGNMENT 32u

enum {
    MELEE_WEB_SOURCE_ARQ_OK = 0,
    MELEE_WEB_SOURCE_ARQ_INVALID = -1,
    MELEE_WEB_SOURCE_ARQ_UNINITIALIZED = -2,
    MELEE_WEB_SOURCE_ARQ_BUSY = -3,
    MELEE_WEB_SOURCE_ARQ_NO_PENDING = -4,
    MELEE_WEB_SOURCE_ARQ_REENTRANT_PUMP = -5,
    MELEE_WEB_SOURCE_ARQ_MISSING_CALLBACK = -6,
};

enum {
    MELEE_WEB_SOURCE_ARQ_MRAM_TO_ARAM = 0,
    MELEE_WEB_SOURCE_ARQ_ARAM_TO_MRAM = 1,
};

typedef struct MeleeWebSourceArqSpan {
    uint32_t address;
    unsigned char* bytes;
    uint32_t length;
} MeleeWebSourceArqSpan;

typedef void (*MeleeWebSourceArqCallback)(void);

typedef struct MeleeWebSourceArqContext {
    MeleeWebSourceArqSpan spans[MELEE_WEB_SOURCE_ARQ_MAX_SPANS];
    size_t span_count;
    unsigned char* aram;
    uint32_t aram_length;
    unsigned initialized;
    unsigned pending;
    unsigned pumping;
    uint32_t pending_type;
    unsigned char* pending_mainmem;
    unsigned char* pending_aram;
    uint32_t pending_length;
    MeleeWebSourceArqCallback callback;
    uint32_t submitted_count;
} MeleeWebSourceArqContext;

/* The context must be zero-initialized before its first bind. Descriptors and
 * backing bytes remain caller-owned; host ranges must be disjoint. While
 * pending, rebind and shutdown are rejected so a source callback cannot
 * outlive its spans. Before rebinding, reset/tear down the original ARQ queue
 * separately; this provider does not own those source queue globals. */
int melee_web_source_arq_bind(MeleeWebSourceArqContext* context,
                              const MeleeWebSourceArqSpan* spans,
                              size_t span_count,
                              unsigned char* aram,
                              uint32_t aram_length);
int melee_web_source_arq_shutdown(MeleeWebSourceArqContext* context);
int melee_web_source_arq_register_callback(MeleeWebSourceArqContext* context,
                                            MeleeWebSourceArqCallback callback);
int melee_web_source_arq_start(MeleeWebSourceArqContext* context,
                               uint32_t type,
                               uint32_t mainmem_address,
                               uint32_t aram_address,
                               uint32_t length);
int melee_web_source_arq_pump(MeleeWebSourceArqContext* context);
int melee_web_source_arq_pending(const MeleeWebSourceArqContext* context);
uint32_t melee_web_source_arq_submitted_count(
    const MeleeWebSourceArqContext* context);

#ifdef __cplusplus
}
#endif

#endif
