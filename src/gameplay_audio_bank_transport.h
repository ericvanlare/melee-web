#ifndef MELEE_WEB_AUDIO_BANK_TRANSPORT_H
#define MELEE_WEB_AUDIO_BANK_TRANSPORT_H
#include "gameplay_audio.h"
#include "gameplay_audio_residency.h"
#ifndef __cplusplus
#include <sysdolphin/baselib/devcom.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Resident source files and auxiliary byte storage for the original synth's
 * load queue. This provider does not replace its bank or callback state. */
int melee_web_audio_bank_transport_begin(MeleeWebAudio*,MeleeWebAudioResidency*,const char* sem_path,char*,size_t);
int melee_web_audio_bank_transport_end(char*,size_t);
int melee_web_audio_bank_transport_active(void);
int melee_web_audio_bank_transport_busy(void);
void melee_web_audio_bank_transport_configure(uint32_t bytes);
int melee_web_audio_bank_transport_path(const char*);
int melee_web_audio_bank_transport_file(int);
#ifndef __cplusplus
int melee_web_audio_bank_transport_request(int,uintptr_t,uintptr_t,size_t,int,int,HSD_DevComCallback,void*);
int melee_web_audio_bank_transport_cancel(int,u32,HSD_DevComCallback,void*);
#endif
void melee_web_audio_bank_transport_pump(void);
/* Source unload removes address mappings after stopping its voices. */
void melee_web_audio_bank_transport_forget(int entry,uint32_t base);
int melee_web_audio_bank_transport_resolve(MeleeWebAudio*,uint32_t,uint32_t,uint32_t*,uint32_t*,uint32_t*);
/* SEM uses the already validated source words; original AXDriver relocation
 * still builds every source table in a fresh audio-heap allocation. */
uint32_t melee_web_audio_bank_sem_size(const char*);
void melee_web_audio_bank_sem_read(const char*,void*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
