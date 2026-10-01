#pragma once
#include "dat_fighter_runtime.hpp"
#include "gameplay_action_store.h"
#include <map>
namespace melee_web {
// Native conversion of the bounded original action command graph. All non-control
// operands use canonical numeric fields; branches require DAT relocations.
// Finite loops share the original three-slot call/loop stack; their source
// bodies are validated without unrolling the authored iteration count. Cycles
// must include a positive relative wait or original animation wait.
enum class DatCommandKind { Fighter, ColorOverlay };
/* A fighter command graph is interpreted against the animation frame clock.
 * Keep that source end-frame beside the root so asynchronous timer cycles can
 * be checked against the same wrap boundary as Command_02. */
struct DatCommandRoot {
    uint32_t offset;
    float animation_end_frame;
    uint8_t animation_loops;
};
class DatCommands {
public:
    DatCommands(std::shared_ptr<const DatArchive>, std::span<const uint32_t> roots, DatCommandKind = DatCommandKind::Fighter);
    DatCommands(std::shared_ptr<const DatArchive>, std::span<const DatCommandRoot> roots, DatCommandKind = DatCommandKind::Fighter);
    ~DatCommands();
    DatCommands(const DatCommands&) = delete;
    DatCommands& operator=(const DatCommands&) = delete;
    void* at(uint32_t offset) const;
    size_t word_count() const noexcept { return indices_.size(); }
private:
    std::map<uint32_t, size_t> indices_;
    void* native_ = nullptr;
    DatCommandKind kind_;
};
}
