#include "gameplay_source_memory_runtime.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace {
long allocation_budget = -1;
void* allocation_alias = nullptr;
}
void* operator new(std::size_t bytes) {
    if (allocation_alias) {
        void* p = allocation_alias;
        allocation_alias = nullptr;
        return p;
    }
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
MeleeWebSourceMemoryAllocation allocation(void* host) {
    MeleeWebSourceMemoryAllocation value{};
    check(melee_web_source_memory_allocation_read(host, &value) ==
          MELEE_WEB_SOURCE_MEMORY_READ_OK && value.live,
          "live payload allocation read");
    return value;
}
void same_allocation(void* host, const MeleeWebSourceMemoryAllocation& expected) {
    const auto actual = allocation(host);
    check(actual.source_heap_handle == expected.source_heap_handle &&
          actual.requested_bytes == expected.requested_bytes &&
          actual.world_generation == expected.world_generation &&
          actual.allocation_generation == expected.allocation_generation &&
          actual.live == expected.live,
          "payload refusal preserves allocation ledger");
}
void same_context(const MeleeWebSourceMemoryContext& expected) {
    const auto actual = context();
    check(actual.source_heap_handle == expected.source_heap_handle &&
          actual.world_generation == expected.world_generation &&
          actual.allocation_generation_watermark ==
              expected.allocation_generation_watermark,
          "payload operation preserves source-memory context");
}
void fill(unsigned char* bytes, size_t count, unsigned char seed) {
    for (size_t i = 0; i < count; ++i)
        bytes[i] = static_cast<unsigned char>(seed + i * 13u);
}
void same_bytes(const unsigned char* actual, const unsigned char* expected,
                size_t count, const char* message) {
    check(std::memcmp(actual, expected, count) == 0, message);
}
void check_endpoint_row_refusal(const MeleeWebSourcePayloadEndpointRow& row,
        MeleeWebSourceMemoryPayloadSnapshotStatus status,const char* message) {
    check(row.status==status&&!row.schema&&!row.bytes&&!row.source_address&&
          !row.requested_bytes&&!row.reserved&&!row.index&&!row.host_address&&
          !row.allocation_generation,message);
}
void check_endpoint_cell_refusal(const MeleeWebSourcePayloadEndpointCell& cell,
        MeleeWebSourceMemoryPayloadSnapshotStatus status,const char* message) {
    check(cell.status==status&&!cell.schema&&!cell.bytes&&!cell.kind&&
          !cell.source_address&&!cell.cell_bytes&&!cell.index,message);
}
void check_endpoint_header_refusal(const MeleeWebSourcePayloadEndpointHeader& header,
        MeleeWebSourceMemoryPayloadSnapshotStatus status,const char* message) {
    check(header.status==status&&!header.schema&&!header.bytes&&
          !header.source_heap_handle&&!header.heap_bytes&&!header.owner_epoch&&
          !header.reserved&&
          !header.world_generation&&!header.allocation_watermark&&
          !header.topology_revision&&!header.arena_begin&&!header.arena_end&&
          !header.row_count&&!header.free_cell_count&&!header.allocated_cell_count&&
          !header.payload_bytes,message);
}

void witness_refused(const MeleeWebSourceMemoryPayloadSnapshot* snapshot,
                     MeleeWebSourceMemoryPayloadSnapshotStatus admission) {
    const auto value=melee_web_source_memory_payload_snapshot_witness(snapshot);
    MeleeWebSourcePayloadWitness expected{};
    expected.schema=1;expected.bytes=sizeof(expected);expected.status=1;
    expected.admission_status=admission;
    check(std::memcmp(&value,&expected,sizeof(value))==0,
          "witness refusal returns no partial metadata or payload bytes");
}
void source_memory_witness_controls() {
    // These byte arrays model a live acquired Fighter root only for the lease
    // API control. They do not claim a real Fighter/member or browser owner.
    unsigned char tiny[1]={17}, a[80]{};
    fill(a,sizeof(a),11);unsigned char saved[sizeof(a)];std::memcpy(saved,a,sizeof(a));
    check(melee_web_source_memory_begin(901,7),"witness begin");
    check(melee_web_source_memory_alloc(7,tiny,sizeof(tiny)),"witness tiny allocation");
    check(melee_web_source_memory_alloc(7,a,sizeof(a)),"witness allocation");
    MeleeWebSourceFighterAddress root{};
    check(melee_web_source_memory_fighter_acquire(a,sizeof(a),&root),"witness modeled root lease");
    auto captured=melee_web_source_memory_payload_snapshot_capture();
    check(captured.snapshot,"witness capture");
    const auto before=context();const auto row=allocation(a);
    const auto equal=melee_web_source_memory_payload_snapshot_witness(captured.snapshot);
    MeleeWebSourcePayloadWitness empty{};empty.schema=1;empty.bytes=sizeof(empty);
    check(std::memcmp(&equal,&empty,sizeof(equal))==0,"equal witness has no invented difference");
    a[33]^=0x5a;
    allocation_budget=0;
    const auto value=melee_web_source_memory_payload_snapshot_witness(captured.snapshot);
    const auto again=melee_web_source_memory_payload_snapshot_witness(captured.snapshot);
    allocation_budget=-1;
    check(value.schema==1&&value.bytes==168&&value.status==2&&!value.admission_status&&
          value.row==1&&value.offset==33&&value.host_address==reinterpret_cast<uintptr_t>(a)&&
          value.source_address==root.source_address&&value.requested_bytes==sizeof(a)&&
          value.allocation_generation==row.allocation_generation&&value.owner_epoch&&
          value.world_generation==901&&value.topology_revision&&
          value.window_offset==25&&value.window_count==32,
          "actual witness exact row identity/generation and middle window");
    check(!value.root_kind&&!value.root_count&&!value.root_host&&!value.root_generation,
          "live Fighter lease is not allocation provenance; UNKNOWN");
    check(!std::memcmp(value.expected,saved+25,32)&&!std::memcmp(value.actual,a+25,32),
          "actual saved/live window, not fabricated scalar evidence");
    check(!std::memcmp(&value,&again,sizeof(value)),"pure deterministic noalloc witness");
    same_context(before);same_allocation(a,row);
    check(a[33]==static_cast<unsigned char>(saved[33]^0x5a),"witness does not restore source bytes");
    // Reproduce the missed case: the lease remains live across a free/reuse,
    // then a NEW snapshot admits the replacement allocation. Lease lifetime
    // is not the allocation lifetime, even with identical host/source/world.
    a[33]=saved[33];
    check(melee_web_source_memory_free(7,a)&&melee_web_source_memory_alloc(7,a,sizeof(a)),
          "free/reuse leaves historical Fighter lease alive");
    const auto replacement=allocation(a);const auto stale_lease=fighter(a);
    check(stale_lease.live&&stale_lease.allocation_generation==root.allocation_generation&&
          replacement.allocation_generation!=row.allocation_generation,
          "actual API exposes stale live lease beside new allocation generation");
    witness_refused(captured.snapshot,MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED);
    melee_web_source_memory_payload_snapshot_destroy(captured.snapshot);
    captured=melee_web_source_memory_payload_snapshot_capture();check(captured.snapshot,"fresh reused-allocation snapshot");
    a[33]^=0x5a;
    const auto fresh=melee_web_source_memory_payload_snapshot_witness(captured.snapshot);
    check(fresh.status==2&&fresh.host_address==reinterpret_cast<uintptr_t>(a)&&
          fresh.source_address==stale_lease.source_address&&fresh.world_generation==stale_lease.world_generation&&
          fresh.allocation_generation==replacement.allocation_generation&&
          !fresh.root_kind&&!fresh.root_count&&!fresh.root_host&&!fresh.root_generation,
          "fresh admitted snapshot cannot misattribute stale same-host/source/world Fighter lease");
    check(melee_web_source_memory_fighter_release(a),"retire typed root without allocation reuse");
    const auto retired=melee_web_source_memory_payload_snapshot_witness(captured.snapshot);
    check(retired.status==2&&retired.root_kind==0&&!retired.root_count&&!retired.root_host&&
          !retired.root_generation,"retired root is UNKNOWN before any dereference");
    a[33]=saved[33];a[79]^=1;
    const auto tail=melee_web_source_memory_payload_snapshot_witness(captured.snapshot);
    check(tail.status==2&&tail.offset==79&&tail.window_offset==71&&tail.window_count==9&&
          !std::memcmp(tail.expected,saved+71,9)&&!std::memcmp(tail.actual,a+71,9),
          "last-byte bounds clamp without wrapping");
    for(unsigned i=9;i<32;++i)check(!tail.expected[i]&&!tail.actual[i],"window tail stays zero");
    a[79]=saved[79];tiny[0]^=1;
    const auto small=melee_web_source_memory_payload_snapshot_witness(captured.snapshot);
    check(small.status==2&&small.row==0&&!small.offset&&!small.window_offset&&small.window_count==1&&
          small.expected[0]==17&&small.actual[0]==16&&small.root_kind==0,
          "one-byte allocation bounds and UNKNOWN provenance");
    for(unsigned i=1;i<32;++i)check(!small.expected[i]&&!small.actual[i],"tiny tail stays zero");
    check(melee_web_source_memory_free(7,a),"witness free before extent change");
    check(melee_web_source_memory_alloc(7,a,sizeof(a)-1),"witness reused pointer different extent");
    witness_refused(captured.snapshot,MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED);
    melee_web_source_memory_payload_snapshot_destroy(captured.snapshot);
    captured=melee_web_source_memory_payload_snapshot_capture();check(captured.snapshot,"capture new extent");
    check(melee_web_source_memory_free(7,a)&&melee_web_source_memory_alloc(7,a,sizeof(a)-1),
          "same-address same-extent allocation reuse");
    witness_refused(captured.snapshot,MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED);
    melee_web_source_memory_payload_snapshot_destroy(captured.snapshot);
    captured=melee_web_source_memory_payload_snapshot_capture();check(captured.snapshot,"capture before owner retirement");
    check(melee_web_source_memory_end(901),"witness retire owner");
    witness_refused(captured.snapshot,MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INACTIVE);
    check(melee_web_source_memory_begin(901,7),"witness recreate same public IDs");
    witness_refused(captured.snapshot,MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_STALE_OWNER);
    witness_refused(nullptr,MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_ARGUMENT);
    check(melee_web_source_memory_end(901),"witness final retirement");
    melee_web_source_memory_payload_snapshot_destroy(captured.snapshot);
    std::puts("Actual payload witness: pure windows, UNKNOWN including fresh-snapshot stale lease, bounds, extent/reuse/owner refusals passed");
}
void source_memory_payload_controls() {
    unsigned char a[128]{}, b[73]{}, c[41]{}, d[19]{};
    unsigned char expected_a[sizeof(a)]{}, expected_b[sizeof(b)]{};
    unsigned char before_a[sizeof(a)]{}, before_b[sizeof(b)]{}, before_c[sizeof(c)]{};
    check(melee_web_source_memory_begin(701, 3), "payload owner begin");
    fill(a, sizeof(a), 3);
    fill(b, sizeof(b), 41);
    std::memcpy(expected_a, a, sizeof(a));
    std::memcpy(expected_b, b, sizeof(b));
    check(melee_web_source_memory_alloc(3, a, sizeof(a)), "payload allocation A");
    check(melee_web_source_memory_alloc(3, b, sizeof(b)), "payload allocation B");
    const auto before_bad_extent = context();
    const auto before_bad_extent_a = allocation(a);
    const auto before_bad_extent_b = allocation(b);
    void* wrapped_payload = reinterpret_cast<void*>(
        std::numeric_limits<uintptr_t>::max() - 1u);
    check(melee_web_source_memory_alloc(3, wrapped_payload, 4),
          "record malformed wrapped host extent for refusal control");
    const auto malformed = melee_web_source_memory_payload_snapshot_capture();
    check(malformed.status ==
              MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_STATE &&
          !malformed.snapshot, "wrapped live extent is refused before reading");
    same_bytes(a, expected_a, sizeof(a), "bad-extent refusal preserves A");
    same_bytes(b, expected_b, sizeof(b), "bad-extent refusal preserves B");
    same_allocation(a, before_bad_extent_a);
    same_allocation(b, before_bad_extent_b);
    check(melee_web_source_memory_free(3, wrapped_payload),
          "remove malformed test allocation");
    const auto after_bad_extent = context();
    check(after_bad_extent.source_heap_handle == before_bad_extent.source_heap_handle &&
          after_bad_extent.world_generation == before_bad_extent.world_generation &&
          after_bad_extent.allocation_generation_watermark >
              before_bad_extent.allocation_generation_watermark,
          "malformed-range control leaves a healthy owner after cleanup");
    const auto initial_context = context();
    const auto initial_a = allocation(a);
    const auto initial_b = allocation(b);
    MeleeWebSourceMemorySnapshot* mirror_checkpoint = nullptr;
    check(melee_web_source_memory_snapshot_capture(&mirror_checkpoint) ==
          MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_OK && mirror_checkpoint,
          "capture unchanged mirror checkpoint beside payload checkpoint");

    // The candidate uses one allocation for its complete private block. Its
    // only capture allocation failure must not touch payloads, ledger or owner.
    allocation_budget = 0;
    const auto failed = melee_web_source_memory_payload_snapshot_capture();
    allocation_budget = -1;
    check(failed.status == MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_ALLOCATION_FAILED &&
          !failed.snapshot, "payload capture allocation failure is explicit");
    same_bytes(a, expected_a, sizeof(a), "failed payload capture preserves A");
    same_bytes(b, expected_b, sizeof(b), "failed payload capture preserves B");
    same_context(initial_context);
    same_allocation(a, initial_a);
    same_allocation(b, initial_b);

    // Simulate an allocator alias into the live owner. The producer must spot
    // the full snapshot-block overlap before constructing or writing anything.
    allocation_alias = a;
    const auto aliased = melee_web_source_memory_payload_snapshot_capture();
    check(!allocation_alias &&
          aliased.status ==
              MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OVERLAPPING_STORAGE &&
          !aliased.snapshot, "payload snapshot owner overlap is refused");
    same_bytes(a, expected_a, sizeof(a), "overlap refusal preserves A");
    same_bytes(b, expected_b, sizeof(b), "overlap refusal preserves B");
    same_context(initial_context);
    same_allocation(a, initial_a);
    same_allocation(b, initial_b);

    allocation_budget = 1;
    const auto captured = melee_web_source_memory_payload_snapshot_capture();
    check(allocation_budget == 0,
          "payload capture has one checked storage-allocation point");
    allocation_budget = -1;
    check(captured.status == MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK &&
          captured.snapshot, "payload capture succeeds after injected failure");
    auto* snapshot = captured.snapshot;
    check(melee_web_source_memory_payload_snapshot_admit(snapshot) ==
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK,
          "unchanged payload topology admits");
    check(melee_web_source_memory_payload_snapshot_matches_live(snapshot)==1,
          "captured payload matches exact live bytes");
    allocation_budget=0;
    check(melee_web_source_memory_payload_snapshot_admit(snapshot)==
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK,
          "full payload endpoint batch admission before reads");
    const auto endpoint=melee_web_source_memory_payload_endpoint_header(snapshot);
    check(endpoint.status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
          endpoint.schema==1&&endpoint.bytes==sizeof(endpoint)&&
          endpoint.source_heap_handle==3&&endpoint.heap_bytes&&!endpoint.reserved&&endpoint.owner_epoch&&
          endpoint.world_generation==701&&
          endpoint.allocation_watermark==initial_context.allocation_generation_watermark&&
          endpoint.topology_revision&&endpoint.arena_begin<endpoint.arena_end&&
          endpoint.row_count==2&&endpoint.free_cell_count==1&&
          endpoint.allocated_cell_count==2&&endpoint.payload_bytes==sizeof(a)+sizeof(b),
          "full endpoint header reports admitted owner, topology and counts");
    const auto endpoint_a=melee_web_source_memory_payload_endpoint_row(snapshot,0);
    const auto endpoint_b=melee_web_source_memory_payload_endpoint_row(snapshot,1);
    check(endpoint_a.status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
          endpoint_a.schema==1&&endpoint_a.bytes==sizeof(endpoint_a)&&endpoint_a.index==0&&
          !endpoint_a.reserved&&
          endpoint_a.host_address==reinterpret_cast<uintptr_t>(a)&&
          endpoint_a.source_address>=endpoint.arena_begin&&
          endpoint_a.source_address<endpoint.arena_end&&
          endpoint_a.requested_bytes==sizeof(a)&&
          endpoint_a.allocation_generation==initial_a.allocation_generation&&
          endpoint_b.status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
          endpoint_b.index==1&&endpoint_b.host_address==reinterpret_cast<uintptr_t>(b)&&
          endpoint_b.source_address>=endpoint.arena_begin&&
          endpoint_b.source_address<endpoint.arena_end&&
          endpoint_b.requested_bytes==sizeof(b)&&
          endpoint_b.allocation_generation==initial_b.allocation_generation,
          "endpoint rows expose exact ordered live allocation identities");
    const auto free_cell=melee_web_source_memory_payload_endpoint_free_cell(snapshot,0);
    const auto allocated_b=melee_web_source_memory_payload_endpoint_allocated_cell(snapshot,0);
    const auto allocated_a=melee_web_source_memory_payload_endpoint_allocated_cell(snapshot,1);
    check(free_cell.status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
          free_cell.kind==MELEE_WEB_SOURCE_PAYLOAD_ENDPOINT_FREE_CELL&&free_cell.index==0&&
          free_cell.source_address>=endpoint.arena_begin&&free_cell.cell_bytes&&
          uint64_t(free_cell.source_address)+free_cell.cell_bytes==endpoint.arena_end&&
          allocated_b.status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
          allocated_b.kind==MELEE_WEB_SOURCE_PAYLOAD_ENDPOINT_ALLOCATED_CELL&&
          uint64_t(allocated_b.source_address)+32==endpoint_b.source_address&&
          allocated_b.cell_bytes==128&&
          allocated_a.status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
          uint64_t(allocated_a.source_address)+32==endpoint_a.source_address&&
          allocated_a.cell_bytes==160,
          "endpoint preserves ordered live free and allocated source cells");
    check_endpoint_row_refusal(melee_web_source_memory_payload_endpoint_row(snapshot,2),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_INDEX,
          "endpoint bad row index returns no row metadata");
    check_endpoint_row_refusal(melee_web_source_memory_payload_endpoint_row(snapshot,
              std::numeric_limits<uint64_t>::max()),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_INDEX,
          "endpoint maximum row index refuses before narrowing");
    check_endpoint_cell_refusal(melee_web_source_memory_payload_endpoint_free_cell(snapshot,1),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_INDEX,
          "endpoint bad free-cell index returns no metadata");
    check_endpoint_cell_refusal(melee_web_source_memory_payload_endpoint_allocated_cell(snapshot,2),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_INDEX,
          "endpoint bad allocated-cell index returns no metadata");
    check_endpoint_cell_refusal(melee_web_source_memory_payload_endpoint_allocated_cell(snapshot,
              std::numeric_limits<uint64_t>::max()),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_INDEX,
          "endpoint maximum cell index refuses before narrowing");
    check(melee_web_source_memory_payload_snapshot_admit(snapshot)==
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&allocation_budget==0,
          "payload endpoint getter batch is allocation-free and re-admitted");
    allocation_budget=-1;
    fill(a, sizeof(a), 99);
    fill(b, sizeof(b), 171);
    check(reinterpret_cast<const unsigned char*>(endpoint_a.host_address)[0]==a[0]&&
          reinterpret_cast<const unsigned char*>(endpoint_b.host_address)[0]==b[0]&&
          a[0]!=expected_a[0]&&b[0]!=expected_b[0],
          "endpoint row addresses name current bytes rather than saved payload bytes");
    allocation_budget = 0;
    check(melee_web_source_memory_payload_snapshot_matches_live(snapshot)==0,
          "changed payload bytes fail pure equality without allocating");
    const auto differing=melee_web_source_memory_payload_snapshot_compare_live(snapshot);
    check(differing.status==2&&differing.allocation_index==0&&differing.byte_offset==0&&
          differing.host_address==reinterpret_cast<uintptr_t>(a)&&differing.requested_bytes==sizeof(a)&&
          differing.expected_byte==expected_a[0]&&differing.actual_byte==a[0],
          "pure payload evidence identifies exact first admitted allocation/offset/bytes");
    const auto diagnostic=melee_web_source_memory_payload_failure_witness(snapshot,differing);
    check(diagnostic.bytes==256&&diagnostic.first_difference_matches==1&&diagnostic.window.row==differing.allocation_index,
          "fixed failure witness crosschecks original execution without allocating");
#if !defined(MELEE_WEB_CPU_INTERVAL_ENABLED) || !MELEE_WEB_CPU_INTERVAL_ENABLED
    check(!diagnostic.hook_active&&!diagnostic.attribution&&diagnostic.reason==1,
          "inactive runtime truthfully qualifies missing hook without pool storage");
#endif
    const auto restored =
        melee_web_source_memory_payload_snapshot_restore(snapshot);
    check(melee_web_source_memory_payload_snapshot_matches_live(snapshot)==1,
          "restored payload matches without allocating");
    allocation_budget = -1;
    check(restored == MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK,
          "payload restore succeeds without allocating");
    same_bytes(a, expected_a, sizeof(a), "payload A restored exactly");
    same_bytes(b, expected_b, sizeof(b), "payload B restored exactly");
    same_context(initial_context);
    same_allocation(a, initial_a);
    same_allocation(b, initial_b);

    // A new allocation invalidates the complete set. Refusal must preserve all
    // bytes, including the newly live payload, and must not rewrite the ledger.
    fill(a, sizeof(a), 11);
    fill(b, sizeof(b), 21);
    fill(c, sizeof(c), 31);
    std::memcpy(before_a, a, sizeof(a));
    std::memcpy(before_b, b, sizeof(b));
    std::memcpy(before_c, c, sizeof(c));
    check(melee_web_source_memory_alloc(3, c, sizeof(c)),
          "post-snapshot payload allocation");
    const auto changed_context = context();
    const auto changed_a = allocation(a);
    const auto changed_b = allocation(b);
    const auto changed_c = allocation(c);
    allocation_budget = 0;
    const auto current_header =
        melee_web_source_memory_payload_current_endpoint_header(snapshot);
    check(current_header.status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
          current_header.schema==1&&current_header.bytes==sizeof(current_header)&&
          current_header.source_heap_handle==changed_context.source_heap_handle&&
          !current_header.reserved&&current_header.owner_epoch&&
          current_header.world_generation==changed_context.world_generation&&
          current_header.allocation_watermark==changed_context.allocation_generation_watermark&&
          current_header.topology_revision&&current_header.arena_begin<current_header.arena_end&&
          current_header.row_count==3&&
          current_header.payload_bytes==sizeof(a)+sizeof(b)+sizeof(c)&&
          current_header.allocated_cell_count>=current_header.row_count,
          "PRE owner authenticates a fresh complete allocation-free END header");
    const void* current_hosts[3]={a,b,c};
    const MeleeWebSourceMemoryAllocation current_allocations[3]={changed_a,changed_b,changed_c};
    const std::size_t current_sizes[3]={sizeof(a),sizeof(b),sizeof(c)};
    MeleeWebSourcePayloadEndpointRow current_rows[3]{};
    for(std::uint64_t i=0;i<current_header.row_count;++i){
        current_rows[i]=melee_web_source_memory_payload_current_endpoint_row(
            snapshot,&current_header,i);
        check(current_rows[i].status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
              current_rows[i].index==i&&current_rows[i].host_address==
                  reinterpret_cast<std::uintptr_t>(current_hosts[i])&&
              current_rows[i].source_address>=current_header.arena_begin&&
              current_rows[i].requested_bytes==current_sizes[i]&&
              current_rows[i].allocation_generation==current_allocations[i].allocation_generation,
              "END getter enumerates exact current rows through the PRE owner");
    }
    bool current_row_in_allocated_cell[3]={false,false,false};
    for(std::uint64_t i=0;i<current_header.allocated_cell_count;++i){
        const auto cell=melee_web_source_memory_payload_current_endpoint_allocated_cell(
            snapshot,&current_header,i);
        check(cell.status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
              cell.kind==MELEE_WEB_SOURCE_PAYLOAD_ENDPOINT_ALLOCATED_CELL&&cell.index==i&&
              cell.cell_bytes&&cell.source_address>=current_header.arena_begin&&
              std::uint64_t(cell.source_address)+cell.cell_bytes<=current_header.arena_end,
              "END getter enumerates each current allocated cell");
        for(unsigned row=0;row<3;++row){
            const auto payload_end=std::uint64_t(current_rows[row].source_address)+
                current_rows[row].requested_bytes;
            const auto cell_end=std::uint64_t(cell.source_address)+cell.cell_bytes;
            if(std::uint64_t(cell.source_address)+32<=current_rows[row].source_address&&
               payload_end<=cell_end){
                check(!current_row_in_allocated_cell[row],"END row maps to one allocated cell");
                current_row_in_allocated_cell[row]=true;
            }
        }
    }
    for(bool belongs:current_row_in_allocated_cell)
        check(belongs,"END rows map into the complete current allocated-cell set");
    for(std::uint64_t i=0;i<current_header.free_cell_count;++i){
        const auto cell=melee_web_source_memory_payload_current_endpoint_free_cell(
            snapshot,&current_header,i);
        check(cell.status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
              cell.kind==MELEE_WEB_SOURCE_PAYLOAD_ENDPOINT_FREE_CELL&&cell.index==i&&
              cell.cell_bytes&&cell.source_address>=current_header.arena_begin&&
              std::uint64_t(cell.source_address)+cell.cell_bytes<=current_header.arena_end,
              "END getter enumerates each current free cell");
    }
    check_endpoint_row_refusal(melee_web_source_memory_payload_current_endpoint_row(
              snapshot,&current_header,3),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_INDEX,
          "END row rejects an out-of-range index without metadata");
    check_endpoint_cell_refusal(melee_web_source_memory_payload_current_endpoint_free_cell(
              snapshot,&current_header,std::numeric_limits<uint64_t>::max()),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_INDEX,
          "END cell rejects maximum index before narrowing");
    check_endpoint_row_refusal(melee_web_source_memory_payload_current_endpoint_row(
              snapshot,nullptr,0),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_ARGUMENT,
          "END row requires the returned admitted header");
    allocation_budget = -1;
    check(melee_web_source_memory_payload_snapshot_restore(snapshot) ==
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED,
          "new allocation refuses payload restore");
    check_endpoint_header_refusal(melee_web_source_memory_payload_endpoint_header(snapshot),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED,
          "stale pre snapshot refuses payload endpoint header without metadata");
    check_endpoint_row_refusal(melee_web_source_memory_payload_endpoint_row(snapshot,0),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED,
          "changed topology refuses indexed row without metadata");
    check_endpoint_cell_refusal(melee_web_source_memory_payload_endpoint_allocated_cell(snapshot,0),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED,
          "changed topology refuses indexed cell without metadata");
    check(melee_web_source_memory_payload_snapshot_matches_live(snapshot)==0,
          "equality refuses changed allocation topology");
    same_bytes(a, before_a, sizeof(a), "topology refusal preserves A");
    same_bytes(b, before_b, sizeof(b), "topology refusal preserves B");
    same_bytes(c, before_c, sizeof(c), "topology refusal preserves C");
    same_context(changed_context);
    same_allocation(a, changed_a);
    same_allocation(b, changed_b);
    same_allocation(c, changed_c);

    // A fresh post-allocation read snapshot describes the current complete
    // source allocation set. It does not grant the old pre snapshot restore
    // authority over this changed topology.
    const auto post_capture=melee_web_source_memory_payload_snapshot_capture();
    check(post_capture.status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
          post_capture.snapshot,"fresh post-allocation payload snapshot");
    const auto post_header=melee_web_source_memory_payload_endpoint_header(post_capture.snapshot);
    check(post_header.status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
          post_header.schema==1&&post_header.bytes==sizeof(post_header)&&
          post_header.source_heap_handle==changed_context.source_heap_handle&&
          post_header.world_generation==changed_context.world_generation&&
          post_header.allocation_watermark==changed_context.allocation_generation_watermark&&
          post_header.row_count==3&&post_header.payload_bytes==sizeof(a)+sizeof(b)+sizeof(c)&&
          post_header.allocated_cell_count>=post_header.row_count,
          "fresh post snapshot header binds the current complete allocation and cell counts");
    const void* post_hosts[3]={a,b,c};
    const MeleeWebSourceMemoryAllocation post_allocations[3]={changed_a,changed_b,changed_c};
    const std::size_t post_sizes[3]={sizeof(a),sizeof(b),sizeof(c)};
    MeleeWebSourcePayloadEndpointRow post_rows[3]{};
    for(std::uint64_t i=0;i<post_header.row_count;++i){
        post_rows[i]=melee_web_source_memory_payload_endpoint_row(post_capture.snapshot,i);
        check(post_rows[i].status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
              post_rows[i].index==i&&post_rows[i].host_address==
                  reinterpret_cast<std::uintptr_t>(post_hosts[i])&&
              current_rows[i].source_address==post_rows[i].source_address&&
              post_rows[i].requested_bytes==post_sizes[i]&&
              post_rows[i].allocation_generation==post_allocations[i].allocation_generation,
              "END and fresh POST rows expose the same exact source/host identities and extents");
    }
    bool row_in_allocated_cell[3]={false,false,false};
    for(std::uint64_t i=0;i<post_header.allocated_cell_count;++i){
        const auto cell=melee_web_source_memory_payload_endpoint_allocated_cell(
            post_capture.snapshot,i);
        check(cell.status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
              cell.kind==MELEE_WEB_SOURCE_PAYLOAD_ENDPOINT_ALLOCATED_CELL&&cell.index==i&&
              cell.cell_bytes&&cell.source_address>=post_header.arena_begin&&
              std::uint64_t(cell.source_address)+cell.cell_bytes<=post_header.arena_end,
              "fresh post snapshot enumerates each live allocated source cell");
        for(unsigned row=0;row<3;++row){
            const auto payload_end=post_rows[row].source_address+post_rows[row].requested_bytes;
            const auto cell_end=std::uint64_t(cell.source_address)+cell.cell_bytes;
            if(cell.source_address<=post_rows[row].source_address&&payload_end<=cell_end){
                check(!row_in_allocated_cell[row],"fresh row belongs to one allocated source cell");
                row_in_allocated_cell[row]=true;
            }
        }
    }
    for(bool belongs:row_in_allocated_cell)
        check(belongs,"fresh allocation rows are covered by enumerated allocated source cells");
    for(std::uint64_t i=0;i<post_header.free_cell_count;++i){
        const auto cell=melee_web_source_memory_payload_endpoint_free_cell(post_capture.snapshot,i);
        check(cell.status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
              cell.kind==MELEE_WEB_SOURCE_PAYLOAD_ENDPOINT_FREE_CELL&&cell.index==i&&
              cell.cell_bytes&&cell.source_address>=post_header.arena_begin&&
              std::uint64_t(cell.source_address)+cell.cell_bytes<=post_header.arena_end,
              "fresh post snapshot enumerates each current free source cell");
    }
    // The returned END header is a short-lived read fence. A later topology
    // revision invalidates its indexed reads even if the caller still holds it.
    check(melee_web_source_memory_alloc(3,d,sizeof(d)),"change topology after END header");
    check_endpoint_row_refusal(melee_web_source_memory_payload_current_endpoint_row(
              snapshot,&current_header,0),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED,
          "END row refuses an intervening topology revision");
    check(melee_web_source_memory_free(3,d),"restore live row set after END fence control");
    const auto after_end_fence_context=context();
    const auto after_end_fence_a=allocation(a);
    const auto after_end_fence_b=allocation(b);
    const auto after_end_fence_c=allocation(c);
    check_endpoint_cell_refusal(melee_web_source_memory_payload_current_endpoint_allocated_cell(
              snapshot,&current_header,0),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED,
          "END cell refuses a stale admitted header after topology returns");
    check(melee_web_source_memory_payload_snapshot_restore(snapshot)==
              MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED,
          "fresh post observation does not authorize stale pre-snapshot restore");
    same_bytes(a,before_a,sizeof(a),"stale pre restore after post read preserves A");
    same_bytes(b,before_b,sizeof(b),"stale pre restore after post read preserves B");
    same_bytes(c,before_c,sizeof(c),"stale pre restore after post read preserves C");
    same_context(after_end_fence_context);
    same_allocation(a,after_end_fence_a);
    same_allocation(b,after_end_fence_b);
    same_allocation(c,after_end_fence_c);
    melee_web_source_memory_payload_snapshot_destroy(post_capture.snapshot);

    check(melee_web_source_memory_free(3, c), "free post-snapshot payload");
    check(melee_web_source_memory_payload_snapshot_restore(snapshot) ==
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED,
          "free after snapshot remains rejected");
    same_bytes(a, before_a, sizeof(a), "free refusal preserves A");
    same_bytes(b, before_b, sizeof(b), "free refusal preserves B");
    fill(a, sizeof(a), 13);
    fill(b, sizeof(b), 23);
    std::memcpy(before_a, a, sizeof(a));
    std::memcpy(before_b, b, sizeof(b));
    check(melee_web_source_memory_snapshot_restore(mirror_checkpoint) ==
          MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_OK,
          "existing mirror-only restore keeps its behavior");
    check(melee_web_source_memory_payload_snapshot_restore(snapshot) ==
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED,
          "mirror rewind cannot revive older payload snapshot");
    same_bytes(a, before_a, sizeof(a), "mirror-only restore preserves payload A");
    same_bytes(b, before_b, sizeof(b), "mirror-only restore preserves payload B");
    check(context().allocation_generation_watermark ==
          initial_context.allocation_generation_watermark,
          "mirror-only restore still restores its existing watermark");
    melee_web_source_memory_snapshot_destroy(mirror_checkpoint);
    melee_web_source_memory_payload_snapshot_destroy(snapshot);

    // Reusing the same host pointer creates a new allocation lifetime. The
    // older payload snapshot cannot regain admission even when address and size
    // are equal.
    const auto reuse_capture = melee_web_source_memory_payload_snapshot_capture();
    check(reuse_capture.status == MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK &&
          reuse_capture.snapshot, "capture before host-pointer reuse");
    const auto old_b = allocation(b);
    fill(a, sizeof(a), 51);
    fill(b, sizeof(b), 61);
    std::memcpy(before_a, a, sizeof(a));
    std::memcpy(before_b, b, sizeof(b));
    check(melee_web_source_memory_free(3, b), "free before host-pointer reuse");
    check(melee_web_source_memory_alloc(3, b, sizeof(b)),
          "reuse same host pointer");
    const auto new_b = allocation(b);
    check(new_b.allocation_generation != old_b.allocation_generation,
          "host-pointer reuse advances generation");
    const auto reused_context = context();
    const auto reused_a = allocation(a);
    const auto reused_b = allocation(b);
    check(melee_web_source_memory_payload_snapshot_restore(reuse_capture.snapshot) ==
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED,
          "reused host pointer refuses old payload restore");
    same_bytes(a, before_a, sizeof(a), "reuse refusal preserves A");
    same_bytes(b, before_b, sizeof(b), "reuse refusal preserves B");
    same_context(reused_context);
    same_allocation(a, reused_a);
    same_allocation(b, reused_b);
    melee_web_source_memory_payload_snapshot_destroy(reuse_capture.snapshot);

    const auto old_world = melee_web_source_memory_payload_snapshot_capture();
    check(old_world.status == MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK &&
          old_world.snapshot, "capture before world change");
    check(melee_web_source_memory_end(701), "end payload owner before world change");
    check_endpoint_header_refusal(melee_web_source_memory_payload_current_endpoint_header(
              old_world.snapshot),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INACTIVE,
          "END header refuses after source owner retirement");
    check(melee_web_source_memory_begin(702, 3), "begin changed world");
    fill(a, sizeof(a), 71);
    fill(b, sizeof(b), 81);
    check(melee_web_source_memory_alloc(3, a, sizeof(a)),
          "changed-world allocation A");
    check(melee_web_source_memory_alloc(3, b, sizeof(b)),
          "changed-world allocation B");
    std::memcpy(before_a, a, sizeof(a));
    std::memcpy(before_b, b, sizeof(b));
    const auto world_context = context();
    const auto world_a = allocation(a);
    const auto world_b = allocation(b);
    check(melee_web_source_memory_payload_snapshot_restore(old_world.snapshot) ==
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_STALE_OWNER,
          "changed world refuses old payload snapshot");
    check_endpoint_header_refusal(melee_web_source_memory_payload_current_endpoint_header(
              old_world.snapshot),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_STALE_OWNER,
          "END header refuses an owner snapshot from a prior world");
    check_endpoint_row_refusal(melee_web_source_memory_payload_endpoint_row(old_world.snapshot,0),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_STALE_OWNER,
          "changed world refuses old endpoint row without metadata");
    same_bytes(a, before_a, sizeof(a), "world refusal preserves A");
    same_bytes(b, before_b, sizeof(b), "world refusal preserves B");
    same_context(world_context);
    same_allocation(a, world_a);
    same_allocation(b, world_b);
    melee_web_source_memory_payload_snapshot_destroy(old_world.snapshot);

    const auto old_heap = melee_web_source_memory_payload_snapshot_capture();
    check(old_heap.status == MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK &&
          old_heap.snapshot, "capture before heap change");
    check(melee_web_source_memory_end(702), "end before heap change");
    check(melee_web_source_memory_begin(702, 4), "begin changed source heap");
    check(melee_web_source_memory_alloc(4, a, sizeof(a)),
          "changed-heap allocation A");
    check(melee_web_source_memory_alloc(4, b, sizeof(b)),
          "changed-heap allocation B");
    fill(a, sizeof(a), 91);
    fill(b, sizeof(b), 101);
    std::memcpy(before_a, a, sizeof(a));
    std::memcpy(before_b, b, sizeof(b));
    const auto heap_context = context();
    const auto heap_a = allocation(a);
    const auto heap_b = allocation(b);
    check(melee_web_source_memory_payload_snapshot_restore(old_heap.snapshot) ==
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_STALE_OWNER,
          "changed heap refuses old payload snapshot");
    same_bytes(a, before_a, sizeof(a), "heap refusal preserves A");
    same_bytes(b, before_b, sizeof(b), "heap refusal preserves B");
    same_context(heap_context);
    same_allocation(a, heap_a);
    same_allocation(b, heap_b);
    melee_web_source_memory_payload_snapshot_destroy(old_heap.snapshot);

    const auto old_epoch = melee_web_source_memory_payload_snapshot_capture();
    check(old_epoch.status == MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK &&
          old_epoch.snapshot, "capture before same-ID owner restart");
    check(melee_web_source_memory_end(702), "end before same-ID owner restart");
    check(melee_web_source_memory_begin(702, 4), "restart same world and heap IDs");
    check(melee_web_source_memory_alloc(4, a, sizeof(a)),
          "same-ID restart allocation A");
    check(melee_web_source_memory_alloc(4, b, sizeof(b)),
          "same-ID restart allocation B");
    fill(a, sizeof(a), 107);
    fill(b, sizeof(b), 117);
    std::memcpy(before_a, a, sizeof(a));
    std::memcpy(before_b, b, sizeof(b));
    const auto epoch_context = context();
    const auto epoch_a = allocation(a);
    const auto epoch_b = allocation(b);
    check(melee_web_source_memory_payload_snapshot_restore(old_epoch.snapshot) ==
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_STALE_OWNER,
          "owner epoch rejects recycled world and heap IDs");
    same_bytes(a, before_a, sizeof(a), "epoch refusal preserves A");
    same_bytes(b, before_b, sizeof(b), "epoch refusal preserves B");
    same_context(epoch_context);
    same_allocation(a, epoch_a);
    same_allocation(b, epoch_b);
    melee_web_source_memory_payload_snapshot_destroy(old_epoch.snapshot);

    const auto unhealthy_snapshot =
        melee_web_source_memory_payload_snapshot_capture();
    check(unhealthy_snapshot.status == MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK &&
          unhealthy_snapshot.snapshot, "capture before unhealthy owner");
    fill(a, sizeof(a), 111);
    fill(b, sizeof(b), 121);
    std::memcpy(before_a, a, sizeof(a));
    std::memcpy(before_b, b, sizeof(b));
    check(!melee_web_source_memory_free(4, c), "induce unhealthy payload owner");
    const auto unhealthy_capture =
        melee_web_source_memory_payload_snapshot_capture();
    check(unhealthy_capture.status ==
              MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_UNHEALTHY &&
          !unhealthy_capture.snapshot,
          "unhealthy owner refuses payload capture");
    check(melee_web_source_memory_payload_snapshot_restore(
              unhealthy_snapshot.snapshot) ==
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_UNHEALTHY,
          "unhealthy owner refuses payload restore");
    check_endpoint_header_refusal(melee_web_source_memory_payload_endpoint_header(
              unhealthy_snapshot.snapshot),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_UNHEALTHY,
          "unhealthy owner refuses full endpoint header without metadata");
    check_endpoint_row_refusal(melee_web_source_memory_payload_endpoint_row(
              unhealthy_snapshot.snapshot,0),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_UNHEALTHY,
          "unhealthy owner refuses indexed endpoint row without metadata");
    same_bytes(a, before_a, sizeof(a), "unhealthy refusal preserves A");
    same_bytes(b, before_b, sizeof(b), "unhealthy refusal preserves B");
    melee_web_source_memory_payload_snapshot_destroy(unhealthy_snapshot.snapshot);
    check(!melee_web_source_memory_end(702), "unhealthy payload owner teardown");
}
}
int main() {
    using Snapshot = MeleeWebSourceMemorySnapshot;
    source_memory_witness_controls();
    source_memory_payload_controls();
    const auto inactive_payload =
        melee_web_source_memory_payload_snapshot_capture();
    check(inactive_payload.status ==
              MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INACTIVE &&
          !inactive_payload.snapshot, "inactive payload capture refusal");
    check(melee_web_source_memory_payload_snapshot_admit(nullptr) ==
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_ARGUMENT,
          "null payload admission refusal");
    check_endpoint_header_refusal(melee_web_source_memory_payload_endpoint_header(nullptr),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_ARGUMENT,
          "null endpoint header refusal has no metadata");
    check_endpoint_row_refusal(melee_web_source_memory_payload_endpoint_row(nullptr,0),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_ARGUMENT,
          "null endpoint row refusal has no metadata");
    check_endpoint_header_refusal(
          melee_web_source_memory_payload_current_endpoint_header(nullptr),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_ARGUMENT,
          "null current END header refusal has no metadata");
    check_endpoint_row_refusal(melee_web_source_memory_payload_current_endpoint_row(
              nullptr,nullptr,0),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_ARGUMENT,
          "null current END row refusal has no metadata");
    check_endpoint_cell_refusal(melee_web_source_memory_payload_endpoint_free_cell(nullptr,0),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_ARGUMENT,
          "null endpoint cell refusal has no metadata");
    check_endpoint_cell_refusal(melee_web_source_memory_payload_current_endpoint_free_cell(
              nullptr,nullptr,0),
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_ARGUMENT,
          "null current END cell refusal has no metadata");
    check(melee_web_source_memory_payload_snapshot_restore(nullptr) ==
          MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_ARGUMENT,
          "null payload restore refusal");
    melee_web_source_memory_payload_snapshot_destroy(nullptr);
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
    std::puts("Source-memory payload snapshot controls: passed");
}
