#ifndef MELEE_WEB_AUDIO_STREAM_H
#define MELEE_WEB_AUDIO_STREAM_H
#include "gameplay_audio.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebAudioStreamBlock {
 uint32_t offset,size,end,next;
 const uint8_t* payload[2];uint32_t channel_bytes;
 MeleeWebAudioChannel channel[2];
} MeleeWebAudioStreamBlock;
typedef struct MeleeWebAudioStreamInput {
 const char* path;const uint8_t* bytes;size_t size;
 uint32_t rate,channels;MeleeWebAudioChannel header[2];
 const MeleeWebAudioStreamBlock* blocks;uint32_t count;
} MeleeWebAudioStreamInput;
typedef struct MeleeWebAudioStream MeleeWebAudioStream;
typedef struct MeleeWebAudioStreamSnapshot MeleeWebAudioStreamSnapshot;

typedef struct MeleeWebAudioStreamOwnerInfo {
 uint32_t present,prepared,input_count;
 uint64_t audio_generation,stream_generation;
 const void* audio_identity;
 const void* stream_identity;
 const void* registry_identity;
 const void* io_identity;
} MeleeWebAudioStreamOwnerInfo;

typedef enum MeleeWebAudioStreamRangeKind {
 MELEE_WEB_AUDIO_STREAM_RANGE_SELECTED_FILE=1,
 MELEE_WEB_AUDIO_STREAM_RANGE_HEADERS,
 MELEE_WEB_AUDIO_STREAM_RANGE_SLOTS,
 MELEE_WEB_AUDIO_STREAM_RANGE_SLOT_FILES,
 MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_PCM,
 MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_FRAMES,
 MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_LOOP_PCM,
 MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_LOOP_AND_NIBBLES,
 MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_COEFFICIENTS,
 MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_PREDICTORS,
 MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_HISTORIES,
 MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_LOOPING,
 MELEE_WEB_AUDIO_STREAM_RANGE_PCM_POINTERS,
 MELEE_WEB_AUDIO_STREAM_RANGE_PCM_BYTES,
 MELEE_WEB_AUDIO_STREAM_RANGE_PCM_CAPACITIES,
 MELEE_WEB_AUDIO_STREAM_RANGE_PREPARED,
 MELEE_WEB_AUDIO_STREAM_RANGE_VISITED_POINTER,
 MELEE_WEB_AUDIO_STREAM_RANGE_VISITED_BYTES,
 MELEE_WEB_AUDIO_STREAM_RANGE_PROGRESS,
 MELEE_WEB_AUDIO_STREAM_RANGE_AUX_HANDLE,
 MELEE_WEB_AUDIO_STREAM_RANGE_AUX_BYTES,
 MELEE_WEB_AUDIO_STREAM_RANGE_RELAY_HANDLE,
 MELEE_WEB_AUDIO_STREAM_RANGE_RELAY_BYTES
} MeleeWebAudioStreamRangeKind;
typedef struct MeleeWebAudioStreamOwnedRange {
 MeleeWebAudioStreamRangeKind kind;
 uint32_t index;
 const void* address;
 size_t byte_extent;
 const void* owner_identity;
 uint64_t generation;
} MeleeWebAudioStreamOwnedRange;
typedef int (*MeleeWebAudioStreamOwnedRangeVisitor)(const MeleeWebAudioStreamOwnedRange*,void*);
typedef enum MeleeWebAudioStreamRangeVisitResult {
 MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_COMPLETE=1,
 MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_INVALID_OWNER=-1,
 MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_INVALID_ARGUMENT=-2,
 MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_INSUFFICIENT_CAPACITY=-3,
 MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_STOPPED=-4,
 MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_NOT_QUIESCENT=-5
} MeleeWebAudioStreamRangeVisitResult;
/* Bounded, dormant-by-default evidence used only by the actual-source HPS
 * stream trace.  These values copy already validated request/slot metadata;
 * pointer-looking fields are identities only and must never be dereferenced
 * by the caller. */
#define MELEE_WEB_AUDIO_STREAM_PROBE_EVENT_CAP 128u
typedef struct MeleeWebAudioStreamProbeRequest {
 uint32_t id,type,priority,file,slot,block_offset;
 uint64_t size;
 uintptr_t source,destination,callback,args;
} MeleeWebAudioStreamProbeRequest;
typedef struct MeleeWebAudioStreamProbeSlot {
 uint32_t valid,slot,file,block_offset;
 uintptr_t pcm[2];
 uint64_t frames[2],pcm_fnv1a[2];
} MeleeWebAudioStreamProbeSlot;
typedef struct MeleeWebAudioStreamProbeBoundary {
 uint32_t pending_requests,pumping,source_loading;
} MeleeWebAudioStreamProbeBoundary;
typedef struct MeleeWebAudioStreamProbeEvent {
 uint32_t sequence,kind,id,type,priority,file,slot,block_offset;
 uint32_t previous_file,previous_block_offset,current_file,current_block_offset;
 uint64_t size;
 uintptr_t source,destination,callback,args;
} MeleeWebAudioStreamProbeEvent;
enum {
 MELEE_WEB_AUDIO_STREAM_PROBE_QUEUED=1,
 MELEE_WEB_AUDIO_STREAM_PROBE_SELECTED=2,
 MELEE_WEB_AUDIO_STREAM_PROBE_CALLBACK=3,
 MELEE_WEB_AUDIO_STREAM_PROBE_SLOT_REPLACED=4
};
/* Borrows validated native HPS descriptors; owns byte transport and three slots.
 * Start is performed by the original lbAudio/AXDriver file-path call. */
MeleeWebAudioStream* melee_web_audio_stream_begin(MeleeWebAudio*,const MeleeWebAudioStreamInput*,char*,size_t);
/* The original HPS loader uses one file identity per selected path.  This
 * registry keeps those identities separate while sharing the source's three
 * decoded slots.  The inputs and their descriptors are borrowed until end. */
MeleeWebAudioStream* melee_web_audio_stream_begin_registry(MeleeWebAudio*,const MeleeWebAudioStreamInput*,uint32_t,char*,size_t);
int melee_web_audio_stream_end(MeleeWebAudioStream*,char*,size_t);
int melee_web_audio_stream_pump_for(MeleeWebAudio*,char*,size_t);
int melee_web_audio_stream_resolve(MeleeWebAudio*,uint32_t address,const MeleeWebAudioChannel**,uint32_t* base);
int melee_web_audio_stream_owned(MeleeWebAudio*);
int melee_web_audio_stream_progress(MeleeWebAudio*,uint32_t* completed_payloads,uint32_t* revisited_blocks);
/* Read-only semantic identity for diagnostics and retail-comparison traces.
 * The returned path is borrowed from the validated stream input. */
const char* melee_web_audio_stream_path(MeleeWebAudio*);
/* Private speculative-capsule leaf. PCM preparation allocates stable active
 * and decode-scratch buffers sized from every validated registered block.
 * It is a pre-capsule operation; ordinary decode remains unchanged until it
 * has been called successfully. */
int melee_web_audio_stream_prepare_snapshot(MeleeWebAudio*,char*,size_t);
/* Success with *out==NULL records an absent stream. A present stream must be
 * prepared and naturally quiescent: no queued requests, active pump, pending
 * IO, or original source load. Captures made during an interval are refused. */
int melee_web_audio_stream_snapshot_capture(MeleeWebAudio*,MeleeWebAudioStreamSnapshot** out,char*,size_t);
/* Bind only the exact pre-interval receipt after interval admission. The live
 * binding fences owner lifetime and restore admission; HPS requests and queued
 * work still fail the existing interval effect guard until separately reviewed. */
int melee_web_audio_stream_snapshot_bind_interval(MeleeWebAudio*,const MeleeWebAudioStreamSnapshot*,uint64_t token,char*,size_t);
int melee_web_audio_stream_snapshot_admit(MeleeWebAudio*,const MeleeWebAudioStreamSnapshot*,uint64_t token,char*,size_t);
int melee_web_audio_stream_snapshot_restore(MeleeWebAudio*,const MeleeWebAudioStreamSnapshot*,uint64_t token,char*,size_t);
/* Revokes only the matching binding, including after a sticky interval error. */
int melee_web_audio_stream_snapshot_end_interval(MeleeWebAudio*,const MeleeWebAudioStreamSnapshot*,uint64_t token);
/* Frees receipt-owned copies. It refuses while this receipt is still bound. */
int melee_web_audio_stream_snapshot_destroy(MeleeWebAudioStreamSnapshot*,char*,size_t);
int melee_web_audio_stream_owner_info(MeleeWebAudio*,MeleeWebAudioStreamOwnerInfo*);
MeleeWebAudioStreamRangeVisitResult melee_web_audio_stream_visit_owned_ranges(
 MeleeWebAudio*,uint64_t audio_generation,size_t capacity,
 MeleeWebAudioStreamOwnedRangeVisitor,void* context,size_t* required);
/* The opt-in trace probe observes accepted HPS requests and source-owned slot
 * identities only.  It never changes request ordering or audio state.  A
 * selected 0x21 header request is included as a refill precursor because its
 * callback may enqueue and complete a 0x23 payload in the same pump. */
int melee_web_audio_stream_probe_begin(MeleeWebAudio*);
int melee_web_audio_stream_probe_end(MeleeWebAudio*);
int melee_web_audio_stream_probe_boundary(MeleeWebAudio*,MeleeWebAudioStreamProbeBoundary*);
/* Returns 1 for the next scheduler-selected 0x21/0x23 request targeting an
 * occupied slot, 0 when none is currently queued, and -1 on invalid owner. */
int melee_web_audio_stream_probe_next_refill(MeleeWebAudio*,MeleeWebAudioStreamProbeRequest*,MeleeWebAudioStreamProbeSlot*);
int melee_web_audio_stream_probe_slot(MeleeWebAudio*,uint32_t slot,MeleeWebAudioStreamProbeSlot*);
int melee_web_audio_stream_probe_event_count(MeleeWebAudio*,uint32_t* count,uint32_t* overflow);
int melee_web_audio_stream_probe_event(MeleeWebAudio*,uint32_t index,MeleeWebAudioStreamProbeEvent*);
#ifdef __cplusplus
}
#endif
#endif
