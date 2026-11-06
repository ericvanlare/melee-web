#include "hsd_texture_bounds.h"
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(__APPLE__) || defined(__unix__)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static int allocation_fail_index=-1;
static unsigned allocation_attempt,allocation_success,allocation_freed;
static int fail_this_allocation(void) {
    if(allocation_fail_index<0)return 0;
    return allocation_attempt++==(unsigned)allocation_fail_index;
}
void* texture_bounds_test_malloc(size_t size) {
    if(fail_this_allocation())return NULL;
    void* p=malloc(size);
    if(p)++allocation_success;
    return p;
}
void* texture_bounds_test_calloc(size_t count,size_t size) {
    if(fail_this_allocation())return NULL;
    void* p=calloc(count,size);
    if(p)++allocation_success;
    return p;
}
void texture_bounds_test_free(void* p) {
    if(p)++allocation_freed;
    free(p);
}
static void fail_at(int index) { allocation_fail_index=index;allocation_attempt=0; }
static void stop_failing(void) { allocation_fail_index=-1;allocation_attempt=0; }
static int check(int condition,const char* message) {
    if(!condition)fprintf(stderr,"FAIL: %s\n",message);
    return condition;
}
static int exact_count(const void* object,uint32_t channel,uint32_t count) {
    if(count==0)return !melee_web_texture_index_valid(object,channel,0.0f);
    return melee_web_texture_index_valid(object,channel,(float)(count-1)) &&
           !melee_web_texture_index_valid(object,channel,(float)count);
}
static int exact_state(uint32_t expected_live,
                       const void* a,uint32_t a_images,uint32_t a_palettes,
                       const void* b,uint32_t b_images,uint32_t b_palettes) {
    return melee_web_texture_bounds_live()==expected_live &&
           exact_count(a,1,a_images) && exact_count(a,10,a_palettes) &&
           exact_count(b,1,b_images) && exact_count(b,10,b_palettes);
}
#if defined(__APPLE__) || defined(__unix__)
static int commit_aborts(MeleeWebTextureBoundsPrepared* prepared) {
    const pid_t child=fork();
    if(child<0)return 0;
    if(child==0){
        struct rlimit no_core={0,0};
        (void)setrlimit(RLIMIT_CORE,&no_core);
        (void)close(STDERR_FILENO);
        melee_web_texture_bounds_commit_prevalidated(prepared);
        _exit(0);
    }
    int status=0;
    if(waitpid(child,&status,0)!=child)return 0;
    return WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT;
}
#endif

int main(void) {
    static unsigned char object_a,object_b,absent;
    const uint64_t world=0x1020304050607080ull;
    if(!check(melee_web_texture_bounds_live()==0,"empty initial registry"))return 1;

    MeleeWebTextureBoundsSnapshot* empty=melee_web_texture_bounds_capture(world);
    if(!check(empty && melee_web_texture_bounds_matches_current(empty,world),
              "capture and match empty registry"))return 1;
    if(!check(!melee_web_texture_bounds_matches_current(empty,world+1) &&
              !melee_web_texture_bounds_prepare(empty,world+1),"wrong world refuses empty snapshot"))return 1;
    MeleeWebTextureBoundsPrepared* prepared=melee_web_texture_bounds_prepare(empty,world);
#if defined(__APPLE__) || defined(__unix__)
    if(!check(prepared && !melee_web_texture_bounds_admit(prepared,world+1) &&
              commit_aborts(prepared) && melee_web_texture_bounds_live()==0 &&
              melee_web_texture_bounds_admit(prepared,world),"empty prepare/admit world fence"))return 1;
#else
    if(!check(prepared && !melee_web_texture_bounds_admit(prepared,world+1) &&
              melee_web_texture_bounds_live()==0 &&
              melee_web_texture_bounds_admit(prepared,world),"empty prepare/admit world fence"))return 1;
#endif
    fail_at(0);
    melee_web_texture_bounds_commit_prevalidated(prepared);
    const unsigned allocations_during_commit=allocation_attempt;
    stop_failing();
    if(!check(allocations_during_commit==0,"commit performs no host allocation"))return 1;
    if(!check(melee_web_texture_bounds_live()==0 &&
              melee_web_texture_bounds_matches_current(empty,world) &&
              !melee_web_texture_bounds_admit(prepared,world),"empty restore and consumed-plan refusal"))return 1;
#if defined(__APPLE__) || defined(__unix__)
    if(!check(commit_aborts(prepared),"duplicate empty commit aborts before root exchange"))return 1;
#endif
    melee_web_texture_bounds_prepared_destroy(prepared);
    melee_web_texture_bounds_snapshot_destroy(empty);
    melee_web_texture_bounds_prepared_destroy(NULL);
    melee_web_texture_bounds_snapshot_destroy(NULL);

    melee_web_texture_bounds_bind(&object_a,2,3);
    melee_web_texture_bounds_bind(&object_b,1,2);
    MeleeWebTextureBoundsSnapshot* snapshot=melee_web_texture_bounds_capture(world);
    if(!check(snapshot && melee_web_texture_bounds_matches_current(snapshot,world) &&
              exact_state(2,&object_a,2,3,&object_b,1,2),
              "capture exact two-object image/palette values"))return 1;
    if(!check(melee_web_texture_bounds_prepare(snapshot,world+1)==NULL,
              "wrong world prepare refuses"))return 1;

    /* Same-value bind and absent remove do not stale an already prepared plan. */
    prepared=melee_web_texture_bounds_prepare(snapshot,world);
    if(!check(prepared!=NULL,"prepare before no-op API calls"))return 1;
    melee_web_texture_bounds_bind(&object_a,2,3);
    melee_web_texture_bounds_remove(&absent);
    if(!check(melee_web_texture_bounds_admit(prepared,world) &&
              exact_state(2,&object_a,2,3,&object_b,1,2),
              "same-value bind and absent remove are revision-neutral"))return 1;
    melee_web_texture_bounds_prepared_destroy(prepared);

    /* Capture allocation failures must not change any live entry. */
    for(int failure=0;failure<2;failure++){
        fail_at(failure);
        MeleeWebTextureBoundsSnapshot* refused=melee_web_texture_bounds_capture(world);
        stop_failing();
        if(!check(refused==NULL &&
                  exact_state(2,&object_a,2,3,&object_b,1,2) &&
                  melee_web_texture_bounds_matches_current(snapshot,world),
                  "capture allocation failure leaves registry unchanged"))return 1;
    }

    /* Remove/reinsert at the same key is an ABA mutation even when the final
       pointer/count set equals PRE. The old prepared plan must refuse. */
    prepared=melee_web_texture_bounds_prepare(snapshot,world);
    if(!check(prepared!=NULL,"prepare before same-key ABA"))return 1;
    melee_web_texture_bounds_remove(&object_a);
    melee_web_texture_bounds_bind(&object_a,2,3);
    if(!check(melee_web_texture_bounds_matches_current(snapshot,world) &&
              !melee_web_texture_bounds_admit(prepared,world) &&
              exact_state(2,&object_a,2,3,&object_b,1,2),
              "ABA-stale admission refuses without mutation"))return 1;
    melee_web_texture_bounds_prepared_destroy(prepared);

    prepared=melee_web_texture_bounds_prepare(snapshot,world);
    if(!check(prepared && !melee_web_texture_bounds_admit(prepared,world+1) &&
              exact_state(2,&object_a,2,3,&object_b,1,2) &&
              melee_web_texture_bounds_admit(prepared,world),
              "wrong-world admission refuses without mutation"))return 1;
    melee_web_texture_bounds_prepared_destroy(prepared);

    /* Model END with a removed PRE entry and different rebound metadata. */
    melee_web_texture_bounds_remove(&object_a);
    melee_web_texture_bounds_bind(&object_b,5,0);
    if(!check(exact_state(1,&object_a,0,0,&object_b,5,0) &&
              !melee_web_texture_bounds_matches_current(snapshot,world),
              "END missing/rebound state differs from PRE"))return 1;

    /* Every host allocation in a two-row prepared plan is fallible before
       commit. Each refusal must preserve the exact END registry. */
    for(int failure=0;failure<3;failure++){
        fail_at(failure);
        MeleeWebTextureBoundsPrepared* refused=melee_web_texture_bounds_prepare(snapshot,world);
        stop_failing();
        if(!check(refused==NULL && exact_state(1,&object_a,0,0,&object_b,5,0) &&
                  !melee_web_texture_bounds_matches_current(snapshot,world),
                  "prepare allocation failure leaves END registry unchanged"))return 1;
    }

    prepared=melee_web_texture_bounds_prepare(snapshot,world);
    if(!check(prepared && melee_web_texture_bounds_admit(prepared,world) &&
              exact_state(1,&object_a,0,0,&object_b,5,0),
              "final PRE admission leaves live END registry untouched"))return 1;
    melee_web_texture_bounds_commit_prevalidated(prepared);
    if(!check(exact_state(2,&object_a,2,3,&object_b,1,2) &&
              melee_web_texture_bounds_matches_current(snapshot,world) &&
              !melee_web_texture_bounds_admit(prepared,world),
              "prepared restore reinstates exact PRE membership/counts"))return 1;
#if defined(__APPLE__) || defined(__unix__)
    if(!check(commit_aborts(prepared),"duplicate populated commit aborts before root exchange"))return 1;
#endif
    melee_web_texture_bounds_prepared_destroy(prepared);
    melee_web_texture_bounds_snapshot_destroy(snapshot);

    /* Ordinary teardown remains per-node remove/free and idempotent. */
    melee_web_texture_bounds_remove(&object_a);
    melee_web_texture_bounds_remove(&object_b);
    melee_web_texture_bounds_remove(&object_a);
    if(!check(melee_web_texture_bounds_live()==0,"ordinary cleanup returns registry to empty"))return 1;
    if(!check(allocation_success==allocation_freed,"all host snapshot/plan/binding allocations released"))return 1;
    puts("texture bounds snapshot C controls passed");
    return 0;
}
