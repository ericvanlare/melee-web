#ifndef MELEE_WEB_GAMEPLAY_AUDIO_H
#define MELEE_WEB_GAMEPLAY_AUDIO_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebAudioChannel {
 const int16_t* pcm;uint32_t frames;
 const int16_t* loop_pcm;uint32_t loop_frames;
 uint32_t current_nibble,end_nibble,loop_nibble;
 int16_t coefficients[16];
 uint16_t predictor_scale,loop_predictor_scale;
 int16_t history1,history2,loop_history1,loop_history2;
 uint8_t looping;
} MeleeWebAudioChannel;
typedef struct MeleeWebAudioSample {uint32_t id,rate,channels;MeleeWebAudioChannel channel[2];} MeleeWebAudioSample;
typedef struct MeleeWebAudioInput {
 const MeleeWebAudioSample* samples;uint32_t sample_count;
 const uint32_t* words;uint32_t word_count;
 const uint32_t* bank_starts;uint32_t bank_count;
 const uint32_t* program_offsets;uint32_t program_count;
 const int16_t* resample_coefficients;uint32_t resample_coefficient_count;
} MeleeWebAudioInput;
typedef struct MeleeWebAudio MeleeWebAudio;
typedef struct MeleeWebAudioSnapshot MeleeWebAudioSnapshot;
typedef enum MeleeWebAudioOwnedRangeKind {
 MELEE_WEB_AUDIO_RANGE_AUDIO_OWNER=1,
 MELEE_WEB_AUDIO_RANGE_AUDIO_ENTRIES,
 MELEE_WEB_AUDIO_RANGE_AUDIO_BINDINGS,
 MELEE_WEB_AUDIO_RANGE_AUDIO_PROGRAMS,
 MELEE_WEB_AUDIO_RANGE_AUDIO_WORDS,
 MELEE_WEB_AUDIO_RANGE_AUDIO_STARTS,
 MELEE_WEB_AUDIO_RANGE_EFFECTS_OWNER,
 MELEE_WEB_AUDIO_RANGE_EFFECTS_REVERB_HEAP,
 MELEE_WEB_AUDIO_RANGE_EFFECTS_DELAY_HEAP,
 /* Stream kind/index pairs occupy BASE + kind*8 + index. */
 MELEE_WEB_AUDIO_RANGE_STREAM_BASE=0x100
} MeleeWebAudioOwnedRangeKind;
typedef struct MeleeWebAudioOwnedRange {
 MeleeWebAudioOwnedRangeKind kind;
 const void* address;
 size_t byte_extent;
 const void* owner_identity;
 uint64_t generation;
} MeleeWebAudioOwnedRange;
typedef int (*MeleeWebAudioOwnedRangeVisitor)(const MeleeWebAudioOwnedRange*,void*);
typedef enum MeleeWebAudioRangeVisitResult {
 MELEE_WEB_AUDIO_RANGE_VISIT_COMPLETE=1,
 MELEE_WEB_AUDIO_RANGE_VISIT_INVALID_OWNER=-1,
 MELEE_WEB_AUDIO_RANGE_VISIT_INVALID_ARGUMENT=-2,
 MELEE_WEB_AUDIO_RANGE_VISIT_INSUFFICIENT_CAPACITY=-3,
 MELEE_WEB_AUDIO_RANGE_VISIT_STOPPED=-4
} MeleeWebAudioRangeVisitResult;
/* Four stream rows, 6*8 channel members, pointer/capacity/prepared/visited/
 * progress rows, six PCM buffers and both IO handle/data pairs. */
#define MELEE_WEB_AUDIO_STREAM_OWNED_RANGE_MAX 68u
enum { MELEE_WEB_AUDIO_OWNED_RANGE_MAX=9+MELEE_WEB_AUDIO_STREAM_OWNED_RANGE_MAX };
/* Borrow decoded PCM and SEM data through end; setup publishes owned native
 * banks/programs to original synth/SEM code. This is a single stereo32kHz
 * provider; output calls advance the actual160-sample original audio clock. */
MeleeWebAudio* melee_web_audio_begin(const MeleeWebAudioInput*,char*,size_t);
int melee_web_audio_play(MeleeWebAudio*,int sound_id,uint8_t volume,uint8_t pan,int track,int channel,char*,size_t);
/* Opt-in original lbAudio STD reverb/delay; call before the first PCM block. */
int melee_web_audio_enable_effects(MeleeWebAudio*,char*,size_t);
int melee_web_audio_render(MeleeWebAudio*,float* interleaved_stereo,uint32_t frames,char*,size_t);
int melee_web_audio_active_samples(MeleeWebAudio*,uint32_t* ids,uint32_t capacity);
/* Monotonic identity for one owned provider lifetime. This lets source hosts
 * distinguish a retained audio engine from a new allocation at the same host
 * address without exposing or interpreting that address. */
uint64_t melee_web_audio_generation(MeleeWebAudio*);
int melee_web_audio_end(MeleeWebAudio*,char*,size_t);
/* Explicit private pre-capsule opt-in. Absent streams need no preparation.
 * Present streams allocate stable authored-bound PCM storage at quiescence. */
int melee_web_audio_prepare_stream_snapshot(MeleeWebAudio*);
/* Legacy mixer/SEM/effects capture does not own HPS transport. The explicit
 * stream variant prepares stable storage and also records typed stream state
 * or its absence. Source globals, immutable assets and consumed Web Audio
 * output remain separate owners. No transfer is drained to capture. */
MeleeWebAudioSnapshot* melee_web_audio_snapshot_capture(MeleeWebAudio*);
MeleeWebAudioSnapshot* melee_web_audio_snapshot_capture_with_stream(MeleeWebAudio*);
int melee_web_audio_snapshot_admit(MeleeWebAudio*,const MeleeWebAudioSnapshot*);
int melee_web_audio_snapshot_restore(MeleeWebAudio*,const MeleeWebAudioSnapshot*);
void melee_web_audio_snapshot_destroy(MeleeWebAudioSnapshot*);
/* Exact PRE receipt binding after the CPU interval token exists. End also
 * revokes a failed interval, before source owners are retired. */
int melee_web_audio_snapshot_interval_begin(MeleeWebAudio*,const MeleeWebAudioSnapshot*,uint64_t);
int melee_web_audio_snapshot_interval_end(MeleeWebAudio*,const MeleeWebAudioSnapshot*,uint64_t);
/* Exact existing snapshot-owned scalars, borrowed only while this live owner
 * and generation remain unchanged. Pure: no pump, allocation or counter reset. */
typedef struct MeleeWebAudioCpuStorage {
 const uint32_t* command_budget;
 const uint32_t* sound_mode;
} MeleeWebAudioCpuStorage;
MeleeWebAudioCpuStorage melee_web_audio_cpu_storage(MeleeWebAudio*,uint64_t generation);
/* Read-only provenance inventory for the live owner. Pass the generation
 * captured when retaining this handle so an address reused by a later owner
 * cannot admit a retired handle. Capacity is the maximum number of rows the
 * visitor may receive; MELEE_WEB_AUDIO_OWNED_RANGE_MAX fits every build and
 * capacity is checked before the first callback. A callback
 * returns nonzero to continue. If it returns zero, the result is STOPPED and
 * any delivered prefix must be discarded. Rows describe provenance only and
 * do not admit snapshot restore. Prepared HPS streams append their named
 * mutable state and stable buffers. Borrowed input PCM/SEM and FX aux ring
 * pointees remain excluded. A row pointer is valid only during its callback;
 * copy the row by value to retain it. The callback must not end or mutate
 * this owner. */
MeleeWebAudioRangeVisitResult melee_web_audio_visit_owned_ranges(
 MeleeWebAudio*,uint64_t generation,size_t capacity,
 MeleeWebAudioOwnedRangeVisitor,void* context);
#ifdef __cplusplus
}
#endif
#endif
