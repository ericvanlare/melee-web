#include "native_dat.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>

namespace {
struct VisitCapture {
    std::array<const void*, 4> addresses{};
    std::array<std::size_t, 4> extents{};
    std::size_t count = 0;
    bool abort = false;
};
bool capture_allocation(const melee_web::NativeDatAllocationRange& row,
                        void* context) noexcept {
    auto* capture = static_cast<VisitCapture*>(context);
    if (!capture || capture->abort || capture->count >= capture->addresses.size())
        return false;
    capture->addresses[capture->count] = row.address;
    capture->extents[capture->count] = row.bytes;
    ++capture->count;
    return true;
}
}

int main() {
    // A valid empty-symbol DAT with four immutable data bytes. No disc data or
    // original-game behavior is claimed by these descriptor ownership controls.
    std::array<std::uint8_t,36> bytes{};
    bytes[3] = 36;
    bytes[7] = 4;
    auto archive = std::make_shared<melee_web::DatArchive>(bytes);
    melee_web::NativeDatArena owner(archive), other(archive);
    const auto* api = owner.reader();
    auto* first = static_cast<std::uint32_t*>(api->allocate(api->context,3,4));
    auto* second = static_cast<void**>(api->allocate(api->context,2,sizeof(void*)));
    first[0] = 0x12345678; first[1] = 9; first[2] = 10;
    second[0] = first; second[1] = first + 2;

    // Read only the real arena ledger. Slices preserve append order, exact
    // extents and metadata; visitor calls leave allocations and payload bytes
    // untouched and use no NativeDat allocation callback.
    VisitCapture inventory_rows{};
    auto inventory = owner.visit_owned_allocations(0, 2, capture_allocation,
                                                    &inventory_rows);
    assert(inventory.status == melee_web::NativeDatAllocationVisitStatus::Complete);
    assert(inventory.arena_storage && inventory.identity_token);
    assert(inventory.allocation_count == 2 && inventory.total_bytes ==
           12 + 2 * sizeof(void*) && inventory.visited_count == 2);
    assert(inventory_rows.count == 2 && inventory_rows.addresses[0] == first &&
           inventory_rows.addresses[1] == second && inventory_rows.extents[0] == 12 &&
           inventory_rows.extents[1] == 2 * sizeof(void*));
    auto metadata = owner.visit_owned_allocations(2, 0, nullptr, nullptr);
    assert(metadata.status == melee_web::NativeDatAllocationVisitStatus::Complete &&
           metadata.visited_count == 0 && metadata.allocation_count == 2 &&
           metadata.total_bytes == inventory.total_bytes &&
           metadata.arena_storage == inventory.arena_storage &&
           metadata.identity_token == inventory.identity_token);
    VisitCapture second_slice{};
    auto slice = owner.visit_owned_allocations(1, 1, capture_allocation, &second_slice);
    assert(slice.status == melee_web::NativeDatAllocationVisitStatus::Complete &&
           slice.visited_count == 1 && second_slice.count == 1 &&
           second_slice.addresses[0] == second && second_slice.extents[0] == 2 * sizeof(void*));
    assert(owner.visit_owned_allocations(3, 0, nullptr, nullptr).status ==
           melee_web::NativeDatAllocationVisitStatus::InvalidArgument);
    assert(owner.visit_owned_allocations(1, 2, capture_allocation, &second_slice).status ==
           melee_web::NativeDatAllocationVisitStatus::InvalidArgument);
    assert(owner.visit_owned_allocations(0, 1, nullptr, nullptr).status ==
           melee_web::NativeDatAllocationVisitStatus::InvalidArgument);
    assert(owner.visit_owned_allocations(0, 0, capture_allocation, &second_slice).status ==
           melee_web::NativeDatAllocationVisitStatus::InvalidArgument);
    VisitCapture aborting{}; aborting.abort = true;
    auto aborted = owner.visit_owned_allocations(0, 2, capture_allocation, &aborting);
    assert(aborted.status == melee_web::NativeDatAllocationVisitStatus::Aborted &&
           aborted.visited_count == 0 && aborted.visited_count != inventory.allocation_count);
    assert(first[0] == 0x12345678 && first[1] == 9 && first[2] == 10 &&
           second[0] == first && second[1] == first + 2);
    VisitCapture repeat_rows{};
    auto unchanged = owner.visit_owned_allocations(0, 2, capture_allocation,
                                                    &repeat_rows);
    assert(unchanged.status == melee_web::NativeDatAllocationVisitStatus::Complete &&
           unchanged.arena_storage == inventory.arena_storage &&
           unchanged.identity_token == inventory.identity_token &&
           unchanged.allocation_count == inventory.allocation_count &&
           unchanged.total_bytes == inventory.total_bytes);

    auto baseline = owner.checkpoint();
    assert(baseline.bytes() == 12 + 2 * sizeof(void*));
    first[0] = 42; first[2] = 64; second[1] = nullptr;
    auto changed = owner.checkpoint();
    assert(!other.can_restore(baseline));
    assert(!other.restore(baseline));
    assert(first[0] == 42 && second[1] == nullptr);
    assert(owner.can_restore(baseline) && owner.restore(baseline));
    assert(first[0] == 0x12345678 && first[1] == 9 && first[2] == 10);
    assert(second[0] == first && second[1] == first + 2);
    assert(owner.restore(changed) && first[0] == 42 && second[1] == nullptr);
    assert(owner.restore(baseline)); // independent, reusable captured bytes
    auto moved = std::move(changed);
    assert(!owner.can_restore(changed) && !owner.restore(changed));
    assert(owner.restore(moved) && first[2] == 64);

    // Any later descriptor allocation changes the topology. Reject before
    // touching even the first otherwise-matching region.
    auto* added = static_cast<unsigned char*>(api->allocate(api->context,1,1));
    *added = 87; first[0] = 99;
    assert(!owner.can_restore(baseline) && !owner.restore(baseline));
    assert(first[0] == 99 && *added == 87 && second[1] == nullptr);
    assert(archive->data()[0] == 0);

    // A checkpoint retains only the identity token and copied bytes, never
    // descriptor ownership. Destruction and address reuse cannot revive it.
    auto prior = std::make_unique<melee_web::NativeDatArena>(archive);
    auto retired = prior->checkpoint();
    prior.reset();
    auto replacement = std::make_unique<melee_web::NativeDatArena>(archive);
    assert(!replacement->restore(retired));
    assert(replacement->restore(replacement->checkpoint()));
    std::cout << "Native DAT descriptor checkpoint ownership controls passed\n";
}
