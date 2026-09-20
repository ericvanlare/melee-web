#pragma once

#include "gameplay_world.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace melee_web {

// Transactional owner for the active native RuntimeFiles map. The caller owns
// the source-world/cache lifetime proof: request(), commit(), and release()
// must only be used at a native boundary where any owner that may be replaced
// or cleared has been closed. This class cannot inspect those external owners.
//
// The aggregate limit applies to source/FST entries passed to put(). The two
// fixed generated native inputs, sislib_font.bin and dsp_coef.bin, remain
// per-file bounded but are excluded from that source aggregate to preserve the
// browser's existing requested-FST budget. Total native bytes are reported
// separately by active_byte_count()/staged_byte_count().
class RuntimeAssetScope {
public:
    static constexpr std::size_t kMaxFileBytes = 64U * 1024U * 1024U;
    static constexpr std::size_t kMaxAggregateBytes = 128U * 1024U * 1024U;

    explicit RuntimeAssetScope(RuntimeFiles& active) noexcept : active_(active) {}

    RuntimeAssetScope(const RuntimeAssetScope&) = delete;
    RuntimeAssetScope& operator=(const RuntimeAssetScope&) = delete;

    // Begins one staged generation without changing the active map. Expected
    // names are exact logical native names; duplicates and empty names fail
    // before any staged state is changed.
    [[nodiscard]] std::uint32_t
    request(const std::vector<std::string>& expected)
    {
        if (pending_generation_) {
            throw std::runtime_error("Runtime asset scope is already pending");
        }
        if (expected.empty()) {
            throw std::runtime_error("Runtime asset scope requires expected files");
        }

        std::set<std::string, std::less<>> next_expected;
        for (const auto& name : expected) {
            if (name.empty()) {
                throw std::runtime_error("Runtime asset scope expected name is empty");
            }
            if (!next_expected.emplace(name).second) {
                throw std::runtime_error("Duplicate expected runtime asset: " + name);
            }
        }
        if (next_generation_ == std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("Runtime asset scope generation exhausted");
        }

        staged_.clear();
        expected_ = std::move(next_expected);
        staged_bytes_ = 0;
        staged_source_bytes_ = 0;
        pending_generation_ = ++next_generation_;
        return *pending_generation_;
    }

    // Copies one complete file into the staged generation. The active map is
    // untouched until commit() succeeds.
    void put(std::uint32_t generation, std::string_view name,
             std::span<const std::uint8_t> bytes)
    {
        require_pending(generation);
        if (name.empty()) {
            throw std::runtime_error("Runtime asset name is empty");
        }
        if (!expected_.contains(name)) {
            throw std::runtime_error("Unexpected runtime asset: " +
                                     std::string(name));
        }
        if (staged_.contains(name)) {
            throw std::runtime_error("Duplicate runtime asset: " +
                                     std::string(name));
        }
        if (bytes.empty()) {
            throw std::runtime_error("Empty runtime asset: " +
                                     std::string(name));
        }
        if (bytes.size() > kMaxFileBytes) {
            throw std::runtime_error("Runtime asset exceeds 64 MiB: " +
                                     std::string(name));
        }
        if (!is_generated_input(name) &&
            staged_source_bytes_ > kMaxAggregateBytes - bytes.size()) {
            throw std::runtime_error("Runtime asset scope exceeds 128 MiB at: " +
                                     std::string(name));
        }

        auto [_, inserted] = staged_.emplace(
            std::string(name), std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
        if (!inserted) {
            throw std::runtime_error("Duplicate runtime asset: " +
                                     std::string(name));
        }
        staged_bytes_ += bytes.size();
        if (!is_generated_input(name)) staged_source_bytes_ += bytes.size();
    }

    // Publishes the complete staged map while retaining the address of the
    // caller-owned RuntimeFiles object. A failed commit leaves the active map
    // and staged map unchanged.
    void commit(std::uint32_t generation)
    {
        require_pending(generation);
        for (const auto& name : expected_) {
            if (!staged_.contains(name)) {
                throw std::runtime_error("Runtime asset scope is missing: " + name);
            }
        }

        active_.swap(staged_);
        staged_.clear();
        expected_.clear();
        staged_bytes_ = 0;
        staged_source_bytes_ = 0;
        pending_generation_.reset();
    }

    // Drops only the pending generation. The previously committed active map
    // remains byte-for-byte available after an aborted or failed import.
    void abort(std::uint32_t generation)
    {
        require_pending(generation);
        staged_.clear();
        expected_.clear();
        staged_bytes_ = 0;
        staged_source_bytes_ = 0;
        pending_generation_.reset();
    }

    // Drops only the active map. The caller must have closed every native owner
    // that could retain a pointer into it; pending staging is intentionally left
    // intact so release cannot accidentally turn an in-flight transaction into
    // a commit.
    void release() noexcept { active_.clear(); }

    [[nodiscard]] const RuntimeFiles& files() const noexcept { return active_; }

    [[nodiscard]] std::uint32_t pending_generation() const noexcept
    {
        return pending_generation_.value_or(0);
    }

    [[nodiscard]] std::size_t active_file_count() const noexcept
    {
        return active_.size();
    }

    [[nodiscard]] std::size_t active_byte_count() const noexcept
    {
        std::size_t total = 0;
        for (const auto& [_, bytes] : active_) total += bytes.size();
        return total;
    }

    [[nodiscard]] std::size_t staged_file_count() const noexcept
    {
        return staged_.size();
    }

    [[nodiscard]] std::size_t staged_byte_count() const noexcept
    {
        return staged_bytes_;
    }

    [[nodiscard]] std::size_t active_source_bytes() const noexcept
    {
        return source_bytes(active_);
    }

    [[nodiscard]] std::size_t staged_source_bytes() const noexcept
    {
        return staged_source_bytes_;
    }

#ifdef MELEE_WEB_RUNTIME_ASSET_SCOPE_TESTING
    // Test-only overflow setup; production callers cannot skip generations.
    void set_next_generation_for_test(std::uint32_t value) noexcept
    {
        next_generation_ = value;
    }
#endif

private:
    static bool is_generated_input(std::string_view name) noexcept
    {
        return name == "sislib_font.bin" || name == "dsp_coef.bin";
    }

    static std::size_t source_bytes(const RuntimeFiles& files) noexcept
    {
        std::size_t total = 0;
        for (const auto& [name, bytes] : files) {
            if (!is_generated_input(name)) total += bytes.size();
        }
        return total;
    }

    void require_pending(std::uint32_t generation) const
    {
        if (!pending_generation_ || *pending_generation_ != generation) {
            throw std::runtime_error("Stale or inactive runtime asset generation");
        }
    }

    RuntimeFiles& active_;
    RuntimeFiles staged_;
    std::set<std::string, std::less<>> expected_;
    std::optional<std::uint32_t> pending_generation_;
    std::uint32_t next_generation_ = 0;
    std::size_t staged_bytes_ = 0;
    std::size_t staged_source_bytes_ = 0;
};

} // namespace melee_web
