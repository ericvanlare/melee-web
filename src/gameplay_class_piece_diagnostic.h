#ifndef MELEE_WEB_CLASS_PIECE_DIAGNOSTIC_H
#define MELEE_WEB_CLASS_PIECE_DIAGNOSTIC_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Private diagnostic only. Never authorizes restoration or source ownership. */
typedef struct MeleeWebClassPieceAnchor {
 uint64_t epoch,world,instance,serial; uint32_t ready,reason;
} MeleeWebClassPieceAnchor;
typedef struct MeleeWebClassPieceEvent {
 uint64_t serial,host,generation; uint32_t kind,source,requested,physical,class_before,class_after;
} MeleeWebClassPieceEvent;
typedef struct MeleeWebClassPieceWitness {
 uint32_t schema,bytes,status,reason,first_difference_matches,hook_active,coverage_state,record_state;
 uint64_t epoch,world,instance,capture_serial,event_serial,host,generation;
 uint32_t row,offset,source,requested,piece_source,piece_offset,physical,original_requested;
 uint32_t piece_generation,class_info,class_obj_size,confirmed,saved_class_info,live_class_info;
 MeleeWebClassPieceEvent first_event; uint32_t reserved[4];
} MeleeWebClassPieceWitness;
/* Separately versioned historical provenance. scope=1 CAPTURE_TIME_ONLY;
 * never proves current piece/class lifetime, never grants restore authority. */
typedef struct MeleeWebClassPieceCaptureWitness {
 uint32_t schema,bytes,status,reason,scope,first_difference_matches,hook_active,live_reason;
 uint32_t coverage_state,record_state,pre_row,post_row,offset,source,requested,reserved0;
 uint64_t epoch,world,instance,capture_serial,event_serial,host,generation;
 uint32_t piece_source,piece_offset,physical,original_requested,piece_generation;
 uint32_t class_info,saved_class_info,flags,reserved[2];
} MeleeWebClassPieceCaptureWitness;
void melee_web_class_piece_cold(const void* list,int32_t count);
void melee_web_class_piece_alloc(void* piece,int32_t requested,uint32_t physical);
void melee_web_class_piece_promote(void* piece,int32_t requested);
void melee_web_class_piece_free(void* piece,int32_t requested);
void melee_web_class_piece_label(void* piece,const void* info,int32_t size,uint32_t stage,const void* previous);
void melee_web_class_piece_descriptor(const void* info);
void melee_web_class_piece_amnesia(void);
MeleeWebClassPieceAnchor melee_web_class_piece_arm(void);
void melee_web_class_piece_disarm(MeleeWebClassPieceAnchor anchor);
#ifdef __cplusplus
}
#endif

#endif
