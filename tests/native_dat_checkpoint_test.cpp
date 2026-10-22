#include "native_dat.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>

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
