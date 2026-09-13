#include "source_game_heap_context.hpp"

#include <cstdint>
#include <vector>

using namespace melee_web::source_game_heap;

int main()
{
    const Bounds bounds{
        Address(0x10000000U), Address(0x12000000U),
        Address(0x0062a000U), Address(0x0162a000U),
    };

    // Type 0 is the excluded source table terminator, never a heap
    // descriptor supplied to this observed-mode model.
    Context unsupported;
    if (unsupported.initialize(bounds, {{2, 0, 6, 0x800}}) !=
        Status::invalid_layout)
        return 1;

    // A malformed transient partition must not poison the state machine. The
    // second begin_rebuild must be allowed to retry after the caller restores
    // a valid transient selection.
    Context retry;
    if (retry.initialize(bounds, {{2, 1, 6, 0xffffffffU}}) != Status::ok)
        return 2;
    if (retry.set_transient(2, 0) != Status::ok)
        return 3;
    if (retry.begin_rebuild() != Status::invalid_layout)
        return 4;
    if (retry.set_transient(2, 1) != Status::ok)
        return 5;
    if (retry.begin_rebuild() != Status::ok)
        return 6;
    return 0;
}
