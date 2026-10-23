#pragma once
#include "gameplay_cpu_comparison.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace melee_web {
/* Private diagnostic table, supplied only after the reviewed runner binds every
 * row to the exact final wasm/map/manifest and fixed source/object policy.
 * These numeric fields are NOT independently a proof of source ownership.
 * This primitive has no browser export or arbitrary-memory public activation. */
enum class CpuStaticPolicy : std::uint32_t { Restore = 1, Invariant = 2 };
enum class CpuStaticDomain : std::uint32_t { OriginalSourceC = 1, OriginalMplibBridge = 2 };
struct CpuStaticRow {
    std::uint32_t address, bytes;
    CpuStaticPolicy policy;
    CpuStaticDomain domain;
};
static_assert(sizeof(CpuStaticRow)==16);
struct CpuIntervalSpan { const void* address; std::size_t bytes; };
struct CpuStaticBounds { std::uintptr_t global_begin, data_end; };
class GameplayCpuStatics final {
public:
    static std::unique_ptr<GameplayCpuStatics> configure(
        std::span<const CpuStaticRow>, CpuStaticBounds,
        std::span<const CpuIntervalSpan> forbidden) noexcept;
    bool capture() noexcept;
    bool seal_post() noexcept;
    bool admit() const noexcept;
    bool prepare_commit() noexcept;
    /* Caller has admitted ALL aggregate owners. This is bounded copies only;
     * no checks, allocation, callback or failure branch after its first write. */
    bool commit_prevalidated() noexcept;
    /* Read-only second endpoint over the same immutable reviewed table.
     * This grants no new ranges and has no restore entry point. */
    CpuComparison capture_expected(std::vector<std::byte>&) const noexcept;
    CpuComparison compare_expected(std::span<const std::byte>) const noexcept;
    bool matches_post() const noexcept;
    CpuComparison compare_post() const noexcept;
    std::uint64_t table_identity() const noexcept { return table_identity_; }
    std::size_t row_count() const noexcept { return rows_.size(); }
private:
    enum class Phase { Configured, Captured, Sealed, Prepared, Restored };
    GameplayCpuStatics() = default;
    bool layout_valid() const noexcept;
    bool invariants_match() const noexcept;
    CpuStaticBounds bounds_{};
    std::vector<CpuStaticRow> rows_;
    std::vector<CpuIntervalSpan> forbidden_;
    std::vector<std::byte> before_, after_;
    std::uint64_t table_identity_{};
    Phase phase_ = Phase::Configured;
};
}
