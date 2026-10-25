#ifndef MELEE_WEB_TOBJ_WRITER_DIAGNOSTIC_H
#define MELEE_WEB_TOBJ_WRITER_DIAGNOSTIC_H
#include "gameplay_class_piece_diagnostic.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct MeleeWebSourceMemoryPayloadSnapshot;
struct MeleeWebSourcePayloadComparison;
#define MELEE_WEB_TOBJ_WRITER_CAPACITY 128u
/* Private diagnostic only: an actual source write occurrence plus CAPTURE_TIME
 * PRE join, never current class ownership or restore authority. */
typedef struct MeleeWebTObjWriterEvent {
 uint32_t sequence,command,status,reason,pre_row,source,requested,field_offset;
 uint64_t host,generation,object,field;
 uint32_t piece_source,piece_offset,physical,original_requested,piece_generation;
 uint32_t class_info,saved_class_info,flags;
 uint8_t before[48],after[48];
 uint32_t flags_before,flags_after,outer_frame,outer_phase; uint64_t outer_epoch;
} MeleeWebTObjWriterEvent;
typedef struct MeleeWebTObjWriterEvidence {
 uint32_t schema,bytes,status,reason,hook_active,count,seen,overflow;
 uint64_t epoch,world,session,piece_instance,capture_serial,topology_revision;
 uint32_t post_count,drawn_count,site_kind,line,path_bytes,first_difference_matches;
 uint32_t matched_index,matched_count,complete,row,offset,source,requested,expected,actual,canonical_class;
 uint64_t host,generation; char path[232];uint32_t pre_frame,reserved;
 MeleeWebTObjWriterEvent events[MELEE_WEB_TOBJ_WRITER_CAPACITY];
} MeleeWebTObjWriterEvidence;
uint64_t melee_web_tobj_writer_arm(const struct MeleeWebSourceMemoryPayloadSnapshot*,MeleeWebClassPieceAnchor,uint32_t command,uint32_t pre_frame);
void melee_web_tobj_writer_phase(uint32_t command);
/* Existing native two-tick guard already checked frame; this is only outer
 * draw association, not a source-PC or lifetime proof. phase1 draw300,2 draw301. */
void melee_web_tobj_writer_boundary(uint32_t phase,uint32_t frame);
void melee_web_tobj_writer_mark(uint64_t session,uint32_t endpoint);
void melee_web_tobj_writer_disarm(uint64_t session);
uint64_t melee_web_tobj_writer_before(const void* object,const void* field,uint32_t object_bytes,const void* canonical_info,const char* file,uint32_t line);
void melee_web_tobj_writer_after(uint64_t ticket);
void melee_web_tobj_writer_finalize(const struct MeleeWebSourceMemoryPayloadSnapshot* pre,const struct MeleeWebSourceMemoryPayloadSnapshot* post,struct MeleeWebSourcePayloadComparison original,MeleeWebClassPieceAnchor,uint64_t session);
const MeleeWebTObjWriterEvidence* melee_web_tobj_writer_evidence(uint64_t session);
#ifdef __cplusplus
}
#endif
#endif
