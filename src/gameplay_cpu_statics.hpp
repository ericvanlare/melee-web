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
enum class CpuStaticEndpointStatus : std::uint32_t {
    Ok = 0, WrongPhase = 1, InvalidLayout = 2, InvariantChanged = 3,
    InvalidIndex = 4,
};
enum class CpuStaticEndpointPhase : std::uint32_t {
    Captured = 1, Sealed = 2, Restored = 3,
};
struct CpuStaticEndpointHeader {
    CpuStaticEndpointStatus status{};
    std::uint32_t schema{}, bytes{};
    CpuStaticEndpointPhase phase{};
    std::uint32_t reserved{}, reserved2{};
    std::uint64_t table_identity{}, row_count{};
};
struct CpuStaticEndpointRow {
    CpuStaticEndpointStatus status{};
    std::uint32_t schema{}, bytes{}, index{}, address{}, extent{}, policy{}, domain{};
};
static_assert(sizeof(CpuStaticEndpointHeader)==40);
static_assert(sizeof(CpuStaticEndpointRow)==32);
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
    /* Separate retained-history mode. The reviewed table, forbidden ranges,
     * and PRE bytes remain the immutable restore authority. Only one bounded
     * live endpoint bank is reused, after the host closes its copied receipt. */
    bool begin_history(std::uint32_t pre_frame) noexcept;
    bool history_begin_incarnation(std::uint64_t incarnation,
                                   std::uint32_t source_frame) noexcept;
    /* Exact read-only PRE guards for three distinct history roles. Each role
     * binds the immutable reviewed layout and every current Restore byte to
     * before_; restore count is cumulative and independent from incarnation. */
    bool history_captured_pre_admit(std::uint32_t pre_frame) const noexcept;
    bool history_armed_pre_admit(std::uint64_t incarnation,
                                 std::uint32_t pre_frame) const noexcept;
    bool history_restored_pre_admit(std::uint64_t incarnation,
                                    std::uint32_t restore_count,
                                    std::uint32_t pre_frame) const noexcept;
    bool history_seal_endpoint(std::uint64_t incarnation,
                               std::uint32_t actual_end) noexcept;
    CpuStaticEndpointHeader history_endpoint_header() const noexcept;
    CpuStaticEndpointRow history_endpoint_row(std::uint64_t index) const noexcept;
    bool history_close_endpoint(std::uint64_t incarnation,
                                std::uint32_t actual_end) noexcept;
    bool history_prepare_commit(std::uint64_t incarnation,
                                std::uint32_t actual_end) noexcept;
    bool history_admit_commit(std::uint64_t incarnation,
                              std::uint32_t actual_end) const noexcept;
    /* Final source writer: caller has just passed aggregate admission. This
     * copies immutable PRE bytes only, with no refusal branch or allocation. */
    void history_commit_prevalidated() noexcept;
    bool history_retire_admit(std::uint64_t incarnation,
                              std::uint32_t actual_end) const noexcept;
    void history_retire_confirmed() noexcept;
    /* Read-only second endpoint over the same immutable reviewed table.
     * This grants no new ranges and has no restore entry point. */
    CpuComparison capture_expected(std::vector<std::byte>&) const noexcept;
    CpuComparison compare_expected(std::span<const std::byte>) const noexcept;
    bool matches_post() const noexcept;
    CpuComparison compare_post() const noexcept;
    /* Read-only endpoint guard for the immutable reviewed table. The header
     * checks complete layout and invariant state in Captured, Sealed or
     * Restored phase. Call it before and after a row batch. Indexed row reads
     * then do O(1) phase/index checks only; they do not establish admission. */
    CpuStaticEndpointHeader endpoint_header() const noexcept;
    CpuStaticEndpointRow endpoint_row(std::uint64_t index) const noexcept;
    std::uint64_t table_identity() const noexcept { return table_identity_; }
    std::size_t row_count() const noexcept { return rows_.size(); }
private:
    enum class Phase { Configured, Captured, Sealed, Prepared, Restored };
    enum class HistoryPhase { Disabled, Ready, Recording, EndpointSealed,
                              Prepared, Restored, Retired, Failed };
    GameplayCpuStatics() = default;
    bool layout_valid() const noexcept;
    bool invariants_match() const noexcept;
    CpuStaticBounds bounds_{};
    std::vector<CpuStaticRow> rows_;
    std::vector<CpuIntervalSpan> forbidden_;
    std::vector<std::byte> before_, after_;
    std::uint64_t table_identity_{};
    Phase phase_ = Phase::Configured;
    HistoryPhase history_phase_ = HistoryPhase::Disabled;
    std::uint64_t history_incarnation_{},history_restore_incarnation_{};
    std::uint64_t history_prepared_incarnation_{};
    std::uint32_t history_pre_frame_{},history_start_frame_{},history_end_frame_{};
    std::uint32_t history_restore_end_{},history_restore_count_{},history_prepared_end_{};
    bool history_endpoint_closed_{};
};
}
