#ifndef MELEE_WEB_SOURCE_AX_STARTUP_ACCESSORS_H
#define MELEE_WEB_SOURCE_AX_STARTUP_ACCESSORS_H

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// This ABI is intentionally diagnostic-only. It exposes addresses and sizes
// from the same translation unit as each original AX static object; it does
// not provide an audio service, synthesize a source address, or mutate state.
#define MELEE_WEB_AX_ACCESSOR_VERSION 1u

enum MeleeWebAxDescriptorKind {
    MELEE_WEB_AX_SPAN = 1,
    MELEE_WEB_AX_CALLBACK = 2,
};

enum MeleeWebAxDescriptorTag {
    MELEE_WEB_AX_TAG_ALLOC_HEAD = 0x414C4844,
    MELEE_WEB_AX_TAG_ALLOC_TAIL = 0x414C544C,
    MELEE_WEB_AX_TAG_ALLOC_CALLBACK_STACK = 0x414C4342,
    MELEE_WEB_AX_TAG_SRC_CYCLES = 0x56505343,
    MELEE_WEB_AX_TAG_MIX_CYCLES = 0x56504D43,
    MELEE_WEB_AX_TAG_PB = 0x56504244,
    MELEE_WEB_AX_TAG_ITD = 0x56504954,
    MELEE_WEB_AX_TAG_UPDATES = 0x56505550,
    MELEE_WEB_AX_TAG_VPB = 0x56505642,
    MELEE_WEB_AX_TAG_STUDIO = 0x53545544,
    MELEE_WEB_AX_TAG_AUX_A = 0x41555841,
    MELEE_WEB_AX_TAG_AUX_B = 0x41555842,
    MELEE_WEB_AX_TAG_AUX_CALLBACK_A = 0x41434141,
    MELEE_WEB_AX_TAG_AUX_CALLBACK_B = 0x41434142,
    MELEE_WEB_AX_TAG_HRTF_HISTORY = 0x48495254,
    MELEE_WEB_AX_TAG_COMMAND_LIST = 0x434D444C,
    MELEE_WEB_AX_TAG_OUT_BUFFER = 0x4F554241,
    MELEE_WEB_AX_TAG_OUT_SBUFFER = 0x4F555342,
    MELEE_WEB_AX_TAG_PROFILE = 0x4F555450,
    MELEE_WEB_AX_TAG_DSP_TASK = 0x44535054,
    MELEE_WEB_AX_TAG_DRAM = 0x4452414D,
    MELEE_WEB_AX_TAG_DSP_INIT_CALLBACK = 0x44535049,
    MELEE_WEB_AX_TAG_DSP_RESUME_CALLBACK = 0x44535052,
    MELEE_WEB_AX_TAG_DSP_DONE_CALLBACK = 0x44535044,
    MELEE_WEB_AX_TAG_USER_FRAME_CALLBACK = 0x4F555443,
    MELEE_WEB_AX_TAG_DSP_SLAVE = 0x44535053,
    MELEE_WEB_AX_TAG_DSP_SLAVE_LENGTH = 0x4453504C,
};

typedef struct MeleeWebAxDescriptor {
    u32 kind;
    u32 tag;
    u32 address;
    u32 bytes;
    u32 alignment;
    u32 element_bytes;
    u32 element_count;
} MeleeWebAxDescriptor;

typedef struct MeleeWebAxState {
    u32 version;
    u32 command_list_position;
    u32 command_list_write;
    u32 command_list_cycles;
    u32 command_list_mode;
    u32 command_list_capacity_words;
    u32 command_list_flush_bytes;
    u32 out_frame;
    u32 out_dsp_ready;
    u32 dsp_init_flag;
    u32 dsp_done_flag;
    u32 dsp_task_state;
    u32 dsp_task_priority;
    u32 dsp_task_flags;
    u32 dsp_task_iram_address;
    u32 dsp_task_iram_length;
    u32 dsp_task_dram_address;
    u32 dsp_task_dram_length;
    u32 dsp_task_dram_size_bytes;
    u32 dsp_task_init_vector;
    u32 dsp_task_resume_vector;
    u32 source_callback_count;
} MeleeWebAxState;

// Each per-source-TU function returns the required descriptor count. If cap
// is large enough it writes exactly that many descriptors. The state output
// is optional and is zeroed when supplied. Callers must not pass a null
// descriptor pointer with a nonzero cap.
u32 melee_web_ax_alloc_describe(MeleeWebAxDescriptor* out, u32 cap,
                                MeleeWebAxState* state);
u32 melee_web_ax_vpb_describe(MeleeWebAxDescriptor* out, u32 cap,
                              MeleeWebAxState* state);
u32 melee_web_ax_spb_describe(MeleeWebAxDescriptor* out, u32 cap,
                              MeleeWebAxState* state);
u32 melee_web_ax_aux_describe(MeleeWebAxDescriptor* out, u32 cap,
                              MeleeWebAxState* state);
u32 melee_web_ax_cl_describe(MeleeWebAxDescriptor* out, u32 cap,
                             MeleeWebAxState* state);
u32 melee_web_ax_out_describe(MeleeWebAxDescriptor* out, u32 cap,
                              MeleeWebAxState* state);
u32 melee_web_ax_dsp_code_describe(MeleeWebAxDescriptor* out, u32 cap,
                                   MeleeWebAxState* state);

#ifdef __cplusplus
}
#endif

#endif
