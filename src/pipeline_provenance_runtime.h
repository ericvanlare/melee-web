#ifndef MELEE_WEB_PIPELINE_PROVENANCE_RUNTIME_H
#define MELEE_WEB_PIPELINE_PROVENANCE_RUNTIME_H
#if !defined(MELEE_WEB_PIPELINE_PROVENANCE) || defined(MELEE_WEB_PUBLIC_RUNTIME)
#error "Private provenance runtime included outside the private configuration"
#endif
#include "pipeline_provenance.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebPipelineScope {
    MeleeWebPipelineSourceToken token, previous;
} MeleeWebPipelineScope;
typedef struct MeleeWebPipelineWorkContext {
    MeleeWebPipelineSourceToken source;
    uint64_t frame_id;
} MeleeWebPipelineWorkContext;
void melee_web_provenance_initialize(void);
MeleeWebPipelineRecorder* melee_web_provenance_recorder(void);
MeleeWebPipelineWorkContext melee_web_provenance_current(void);
void melee_web_provenance_scope_begin(const MeleeWebPipelineSourceContext*, MeleeWebPipelineScope*);
void melee_web_provenance_scope_end(MeleeWebPipelineScope*);
void melee_web_provenance_owner_begin(uint32_t classifier, uint32_t gx_link, uint32_t pass, MeleeWebPipelineScope*);
void melee_web_provenance_frame(uint64_t frame_id);
uint64_t melee_web_provenance_next_packet(void);
void melee_web_provenance_device_begin(void);
void melee_web_provenance_device_end(void);
void melee_web_provenance_renderer_begin(void);
void melee_web_provenance_renderer_end(void);
void melee_web_provenance_invalid(uint32_t reason);
/* Commands are private exports. Case changes are made at a native command
 * boundary, never while a source scope is open. Input digest is 64 hex bytes. */
int melee_web_provenance_set_case(uint32_t case_id, const char* input_sha256);
size_t melee_web_provenance_chunk_bytes(void);
const char* melee_web_provenance_drain(void);
const char* melee_web_provenance_finish(void);
const char* melee_web_provenance_status(void);
#ifdef __cplusplus
}
namespace melee_web::provenance {
class Scope {
    MeleeWebPipelineScope scope_{};
public:
    explicit Scope(const MeleeWebPipelineSourceContext& context) { melee_web_provenance_scope_begin(&context, &scope_); }
    ~Scope() { melee_web_provenance_scope_end(&scope_); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
class Execution {
    MeleeWebPipelineExecutionToken execution_{};
    MeleeWebPipelineWorkContext previous_{};
    bool pushed_ = false;
public:
    explicit Execution(const MeleeWebPipelineWorkContext&);
    ~Execution();
    Execution(const Execution&) = delete;
    Execution& operator=(const Execution&) = delete;
};
void observe(uint32_t kind, uint32_t type, uint64_t reference, uint32_t version,
             const void* descriptor, size_t size, uint32_t outcome,
             uint64_t packet_id = 0);
}
#endif
#endif
