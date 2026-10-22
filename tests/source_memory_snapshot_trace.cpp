#include "gameplay_source_memory_runtime.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace { long allocation_budget = -1; }
void* operator new(std::size_t bytes) {
    if (allocation_budget == 0) throw std::bad_alloc();
    if (allocation_budget > 0) --allocation_budget;
    if (void* p=std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
namespace {
void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
MeleeWebSourceMemoryContext context() {
    MeleeWebSourceMemoryContext value{};
    check(melee_web_source_memory_context_read(&value) == MELEE_WEB_SOURCE_MEMORY_READ_OK,
          "context read");
    return value;
}
MeleeWebSourceFighterAddress fighter(void* address) {
    MeleeWebSourceFighterAddress value{};
    check(melee_web_source_memory_fighter_read(address, &value), "fighter read");
    return value;
}
void same_fighter(const MeleeWebSourceFighterAddress& a,
                  const MeleeWebSourceFighterAddress& b) {
    check(a.source_address == b.source_address && a.world_generation == b.world_generation &&
          a.allocation_generation == b.allocation_generation && a.live == b.live,
          "exact fighter lease restored");
}
}
int main() {
    using Snapshot = MeleeWebSourceMemorySnapshot;
    Snapshot *first=nullptr, *second=nullptr;
    check(melee_web_source_memory_snapshot_capture(&first) ==
          MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_INACTIVE && !first, "inactive capture refusal");
    check(melee_web_source_memory_snapshot_capture(nullptr) ==
          MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_INVALID_ARGUMENT, "null capture refusal");
    check(melee_web_source_memory_snapshot_restore(nullptr) ==
          MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_INVALID_ARGUMENT, "null restore refusal");
    check(melee_web_source_memory_begin(42, 3), "begin");
    unsigned char a[2048]{}, b[1024]{}, c[512]{};
    check(melee_web_source_memory_alloc(3, a, sizeof(a)), "allocation A");
    MeleeWebSourceFighterAddress original{};
    check(melee_web_source_memory_fighter_acquire(a+64, 256, &original), "lease A");
    const auto first_context = context();
    check(melee_web_source_memory_snapshot_capture(&first) ==
          MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_OK, "capture first");
    check(melee_web_source_memory_fighter_release(a+64), "release A");
    check(melee_web_source_memory_free(3, a), "free A");
    check(melee_web_source_memory_alloc(3, b, sizeof(b)), "allocation B");
    MeleeWebSourceFighterAddress expected{};
    check(melee_web_source_memory_fighter_acquire(b+32, 256, &expected), "lease B");
    const auto second_context = context();
    check(melee_web_source_memory_snapshot_capture(&second) ==
          MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_OK, "capture independent second");
    check(melee_web_source_memory_alloc(3, c, sizeof(c)), "allocation C");
    // Force failure at each preparation allocation. Every failed restore must
    // preserve both the live ledger and watermark before any commit.
    bool staged_success=false;
    for (long budget=0; budget<32; ++budget) {
        const auto before=context();
        allocation_budget=budget;
        const auto result=melee_web_source_memory_snapshot_restore(first);
        allocation_budget=-1;
        if (result==MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_OK) {staged_success=true;break;}
        check(result==MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_ALLOCATION_FAILED,
              "injected allocation failure is explicit");
        check(context().allocation_generation_watermark==before.allocation_generation_watermark,
              "failed restore preserves watermark");
        MeleeWebSourceMemoryAllocation still_live{};
        check(melee_web_source_memory_allocation_read(c,&still_live)==MELEE_WEB_SOURCE_MEMORY_READ_OK &&
              still_live.live, "failed restore preserves live ledger");
        same_fighter(expected,fighter(b+32));
    }
    check(staged_success,"staged restore eventually succeeds");
    // Put the independent later state back for the normal rewind/replay path.
    check(melee_web_source_memory_snapshot_restore(second)==MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_OK,
          "restore second after allocation injection");
    check(melee_web_source_memory_alloc(3,c,sizeof(c)),"recreate C after allocation injection");
    a[0]=0x5a; // The mirror API must never restore host payload bytes.
    check(melee_web_source_memory_snapshot_admit(first) ==
          MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_OK, "same lifetime admits changed mirror");
    const auto before_admit = context();
    check(before_admit.allocation_generation_watermark > second_context.allocation_generation_watermark,
          "admission preserves current watermark");
    check(melee_web_source_memory_snapshot_restore(first) ==
          MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_OK, "restore first");
    check(a[0] == 0x5a, "mirror restore does not write host bytes");
    same_fighter(original, fighter(a+64));
    check(context().allocation_generation_watermark == first_context.allocation_generation_watermark,
          "restore first watermark");
    MeleeWebSourceMemoryAllocation missing{};
    check(melee_web_source_memory_allocation_read(b, &missing) == MELEE_WEB_SOURCE_MEMORY_READ_OK && !missing.live,
          "post-checkpoint allocation absent after rewind");
    // Replay the same frees/allocations; address, allocation lifetime and lease
    // lifetime must match, proving both the heap free lists and ledger rewind.
    check(melee_web_source_memory_fighter_release(a+64), "replay release A");
    check(melee_web_source_memory_free(3, a), "replay free A");
    check(melee_web_source_memory_alloc(3, b, sizeof(b)), "replay allocation B");
    MeleeWebSourceFighterAddress replay{};
    check(melee_web_source_memory_fighter_acquire(b+32, 256, &replay), "replay lease B");
    same_fighter(expected, replay);
    check(melee_web_source_memory_snapshot_restore(second) ==
          MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_OK, "independent second remains intact");
    same_fighter(expected, fighter(b+32));
    check(context().allocation_generation_watermark == second_context.allocation_generation_watermark,
          "restore second watermark");
    check(melee_web_source_memory_end(42), "end first lifetime");
    check(melee_web_source_memory_snapshot_restore(first) ==
          MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_INACTIVE, "inactive restore refusal");
    check(melee_web_source_memory_begin(42, 3), "restart same public IDs");
    check(melee_web_source_memory_alloc(3, c, sizeof(c)), "new lifetime allocation");
    const auto restarted = context();
    check(melee_web_source_memory_snapshot_restore(first) ==
          MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_STALE_OWNER, "epoch prevents recycled-ID restore");
    check(context().allocation_generation_watermark == restarted.allocation_generation_watermark,
          "stale restore preserves new owner");
    MeleeWebSourceMemoryAllocation surviving{};
    check(melee_web_source_memory_allocation_read(c, &surviving) == MELEE_WEB_SOURCE_MEMORY_READ_OK,
          "stale restore preserves allocation");
    check(!melee_web_source_memory_free(3, a), "induce an unhealthy owner");
    check(melee_web_source_memory_snapshot_restore(first) ==
          MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_UNHEALTHY, "unhealthy restore refusal");
    Snapshot* refused=nullptr;
    check(melee_web_source_memory_snapshot_capture(&refused) ==
          MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_UNHEALTHY && !refused, "unhealthy capture refusal");
    check(!melee_web_source_memory_end(42), "unhealthy teardown reported");
    melee_web_source_memory_snapshot_destroy(first);
    melee_web_source_memory_snapshot_destroy(second);
    melee_web_source_memory_snapshot_destroy(nullptr);
    std::puts("Source-memory mirror snapshot controls: passed");
}
