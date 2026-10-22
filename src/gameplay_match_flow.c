#include "gameplay_match_flow.h"
#include "gameplay_match_clock.h"
#include "gameplay_bootstrap.h"
#include <melee/lb/lbaudio_ax.h>
#include <melee/gm/gm_1A45.h>
#include <melee/gm/forward.h>
#include <sysdolphin/baselib/gobj.h>
#include <stdio.h>
#include <stdlib.h>

extern int melee_web_match_source_frame(void);
extern int melee_web_match_source_result(void);
extern uint32_t melee_web_match_source_frames(void);
extern int melee_web_match_end_state(void);
struct MeleeWebMatchFlow {
    uint64_t generation;
    int source_valid, complete;
    int opening_demo;
};
static MeleeWebMatchFlow* owner;
static uint64_t owner_epoch;
static int fail(char* error, size_t size, const char* message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}
static int live(const MeleeWebMatchFlow* flow)
{
    return flow && flow == owner &&
        flow->generation == melee_web_gameplay_generation();
}
static void source_frame(void)
{
    owner->source_valid = melee_web_match_source_frame();
}
MeleeWebMatchFlow* melee_web_match_flow_begin(int opening_demo,
                                              char* error, size_t size)
{
    const uint64_t generation = melee_web_gameplay_generation();
    if (owner || !generation) {
        fail(error, size, "Original match flow requires an unowned source world");
        return NULL;
    }
    MeleeWebMatchFlow* flow = calloc(1, sizeof(*flow));
    if (!flow) {
        fail(error, size, "Cannot allocate original match flow owner");
        return NULL;
    }
    if (!melee_web_source_clock_begin(MELEE_WEB_SOURCE_CLOCK_MATCH)) {
        free(flow);
        fail(error, size, "Original scene clock is already owned");
        return NULL;
    }
    if (!melee_web_source_cadence_begin()) {
        melee_web_source_clock_end();
        free(flow);
        fail(error, size, "Original PAD cadence is already owned");
        return NULL;
    }
    flow->generation = generation;
    flow->opening_demo = opening_demo != 0;
    if (++owner_epoch == 0) ++owner_epoch;
    owner = flow;
    return flow;
}
int melee_web_match_flow_renew(void* context, char* error, size_t size)
{
    if (!live(context) || !melee_web_source_cadence_tick())
        return fail(error, size, "Original PAD cadence lost ownership");
    return 1;
}
int melee_web_match_flow_pre(void* context, char* error, size_t size)
{
    MeleeWebMatchFlow* flow = context;
    if (!live(flow) || flow->complete)
        return fail(error, size, "Original match flow is not active");
    flow->source_valid = 0;
    if (!melee_web_source_clock_pre(source_frame) || !flow->source_valid)
        return fail(error, size, "Unsupported original match scene or clock state");
    /* Retail order: source OnFrame, process mask, audio update, GObj scheduler.
     * PCM transport remains owned by the caller after the source frame. */
    lbAudioAx_80027DF8();
    return 1;
}
int melee_web_match_flow_post(void* context, char* error, size_t size)
{
    MeleeWebMatchFlow* flow = context;
    int request = 0;
    if (!live(flow) || !melee_web_source_clock_post() ||
        !melee_web_source_clock_request(&request))
        return fail(error, size, "Original match clock lost its frame ownership");
    if (request != 0) {
        if (request != 1 || (!flow->opening_demo &&
            melee_web_match_end_state() != 3 &&
            !(melee_web_match_end_state() == 0 &&
              melee_web_match_source_result() == OUTCOME_NO_CONTEST)))
            return fail(error, size, "Unexpected original match transition request");
        flow->complete = 1;
    }
    return 1;
}
int melee_web_match_flow_present(MeleeWebMatchFlow* flow, char* error, size_t size)
{
    if (!live(flow) || !melee_web_source_clock_present())
        return fail(error, size, "Original match presentation clock lost ownership");
    return 1;
}
int melee_web_match_flow_ending(const MeleeWebMatchFlow* flow)
{
    return live(flow) && melee_web_match_end_state() != 0;
}
int melee_web_match_flow_complete(const MeleeWebMatchFlow* flow)
{
    return live(flow) && flow->complete;
}
int melee_web_match_flow_end(MeleeWebMatchFlow* flow, char* error, size_t size)
{
    if (!flow) return 1;
    if (!live(flow))
        return fail(error, size, "Original match flow lost its world owner");
    if (!melee_web_source_clock_end()) {
        switch (melee_web_source_clock_end_failure()) {
        case MELEE_WEB_SOURCE_CLOCK_END_FAILURE_OWNER:
            return fail(error, size, "Original match clock process-mask owner changed");
        case MELEE_WEB_SOURCE_CLOCK_END_FAILURE_CALLBACK:
            return fail(error, size, "Original match clock cannot close inside its source callback");
        case MELEE_WEB_SOURCE_CLOCK_END_FAILURE_GOBJ:
            return fail(error, size, "Original match clock cannot close with an active GObj callback");
        case MELEE_WEB_SOURCE_CLOCK_END_FAILURE_MASK_VALUE:
            return fail(error, size, "Original match process-mask value changed");
        case MELEE_WEB_SOURCE_CLOCK_END_FAILURE_CONTROLLER_MAP:
            return fail(error, size, "Original controller-map owner changed");
        default:
            return fail(error, size, "Original match clock cannot restore its owner");
        }
    }
    if (!melee_web_source_cadence_end())
        return fail(error, size, "Original PAD cadence cannot restore its owner");
    owner = NULL;
    free(flow);
    return 1;
}

int melee_web_match_flow_paused(const MeleeWebMatchFlow* flow)
{
    return live(flow) && gm_801A45E8(1);
}
uint32_t melee_web_match_flow_frames(const MeleeWebMatchFlow* flow)
{
    return live(flow) ? melee_web_match_source_frames() : 0;
}
int melee_web_match_flow_result(const MeleeWebMatchFlow* flow)
{
    return live(flow) ? melee_web_match_source_result() : OUTCOME_NONE;
}


struct MeleeWebMatchFlowSnapshot {
    MeleeWebMatchFlow* identity;
    uint64_t epoch;
    MeleeWebMatchFlow value;
};
static int flow_snapshot_boundary(MeleeWebMatchFlow* flow,char* e,size_t n)
{
    if (!live(flow) || HSD_GObj_804D781C || HSD_GObj_804D7818 || HSD_GObj_804D7814)
        return fail(e,n,"Flow snapshot requires a quiescent live owner");
    return 1;
}
MeleeWebMatchFlowSnapshot* melee_web_match_flow_snapshot_capture(
    MeleeWebMatchFlow* flow,char* e,size_t n)
{
    if (!flow_snapshot_boundary(flow,e,n)) return NULL;
    MeleeWebMatchFlowSnapshot* snapshot=malloc(sizeof(*snapshot));
    if (!snapshot) {fail(e,n,"Cannot allocate match-flow snapshot");return NULL;}
    snapshot->identity=flow;snapshot->epoch=owner_epoch;snapshot->value=*flow;
    if(e&&n)*e=0;
    return snapshot;
}
int melee_web_match_flow_snapshot_admit(MeleeWebMatchFlow* flow,
    const MeleeWebMatchFlowSnapshot* snapshot,char* e,size_t n)
{
    if (!snapshot) return fail(e,n,"Match-flow snapshot is missing");
    if (!flow_snapshot_boundary(flow,e,n)) return 0;
    if (snapshot->identity!=flow || snapshot->epoch!=owner_epoch ||
        snapshot->value.generation!=flow->generation ||
        snapshot->value.opening_demo!=flow->opening_demo)
        return fail(e,n,"Match-flow lifetime or scene identity changed");
    if(e&&n)*e=0;
    return 1;
}
int melee_web_match_flow_snapshot_restore(MeleeWebMatchFlow* flow,
    const MeleeWebMatchFlowSnapshot* snapshot,char* e,size_t n)
{
    if (!melee_web_match_flow_snapshot_admit(flow,snapshot,e,n)) return 0;
    *flow=snapshot->value;
    return 1;
}
void melee_web_match_flow_snapshot_destroy(MeleeWebMatchFlowSnapshot* snapshot)
{
    free(snapshot);
}
