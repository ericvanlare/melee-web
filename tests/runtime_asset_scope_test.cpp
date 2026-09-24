#include "runtime_asset_scope.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

template <typename Function>
void expect_failure(Function&& function, std::string_view fragment)
{
    bool failed = false;
    try {
        function();
    } catch (const std::runtime_error& error) {
        failed = true;
        assert(std::string_view(error.what()).find(fragment) !=
               std::string_view::npos);
    }
    assert(failed);
}

std::span<const std::uint8_t> bytes(std::initializer_list<std::uint8_t> value)
{
    static thread_local std::vector<std::uint8_t> storage;
    storage.assign(value.begin(), value.end());
    return storage;
}

} // namespace

int main()
{
    using melee_web::RuntimeAssetScope;
    using melee_web::RuntimeFiles;

    // A staged import is invisible until commit and does not move the active
    // map or its existing vector address.
    RuntimeFiles active{{"old.dat", {1, 2, 3}}};
    RuntimeAssetScope scope(active);
    const auto* old_data = active.at("old.dat").data();
    const auto generation = scope.request({"new.dat"});
    scope.put(generation, "new.dat", bytes({9, 8}));
    assert(scope.pending_generation() == generation);
    assert(scope.active_file_count() == 1);
    assert(scope.staged_file_count() == 1);
    assert(active.at("old.dat").data() == old_data);
    assert(active.at("old.dat") == std::vector<std::uint8_t>({1, 2, 3}));
    scope.commit(generation);
    assert(scope.pending_generation() == 0);
    assert(scope.active_file_count() == 1);
    assert(scope.active_byte_count() == 2);
    assert(active.count("old.dat") == 0);
    assert(active.at("new.dat") == std::vector<std::uint8_t>({9, 8}));

    // A failed commit and abort preserve the committed generation.
    RuntimeFiles preserved{{"stable.dat", {4, 5, 6}}};
    RuntimeAssetScope preserved_scope(preserved);
    const auto* stable_data = preserved.at("stable.dat").data();
    const auto missing_generation = preserved_scope.request({"present.dat", "missing.dat"});
    preserved_scope.put(missing_generation, "present.dat", bytes({7}));
    expect_failure([&] { preserved_scope.commit(missing_generation); }, "missing");
    assert(preserved.at("stable.dat").data() == stable_data);
    assert(preserved_scope.staged_file_count() == 1);
    preserved_scope.abort(missing_generation);
    assert(preserved_scope.pending_generation() == 0);
    assert(preserved_scope.active_file_count() == 1);
    assert(preserved.at("stable.dat").data() == stable_data);
    expect_failure([&] { preserved_scope.put(missing_generation, "present.dat", bytes({1})); },
                   "Stale");

    // Exact expected names and one pending generation are enforced.
    expect_failure([&] { (void) preserved_scope.request({"dup.dat", "dup.dat"}); }, "Duplicate expected");
    expect_failure([&] { (void) preserved_scope.request({}); }, "requires expected");
    const auto exact_generation = preserved_scope.request({"one.dat"});
    expect_failure([&] { (void) preserved_scope.request({"two.dat"}); }, "already pending");
    expect_failure([&] { preserved_scope.put(exact_generation, "other.dat", bytes({1})); }, "Unexpected");
    expect_failure([&] { preserved_scope.put(exact_generation, "", bytes({1})); }, "empty");
    expect_failure([&] { preserved_scope.put(exact_generation, "one.dat", {}); }, "Empty");
    preserved_scope.put(exact_generation, "one.dat", bytes({1}));
    expect_failure([&] { preserved_scope.put(exact_generation, "one.dat", bytes({2})); }, "Duplicate");
    preserved_scope.abort(exact_generation);

    // The per-file limit is checked before staging, and the aggregate limit
    // retains the existing requested-source budget. Generated font/DSP inputs
    // remain total-byte-visible but are excluded from source_bytes.
    RuntimeAssetScope limits(active);
    const auto oversized_generation = limits.request({"too-large.dat"});
    std::vector<std::uint8_t> oversized(RuntimeAssetScope::kMaxFileBytes + 1);
    expect_failure([&] { limits.put(oversized_generation, "too-large.dat", oversized); }, "64 MiB");
    assert(limits.staged_file_count() == 0);
    limits.abort(oversized_generation);

    const auto aggregate_generation = limits.request({"a.dat", "b.dat", "c.dat"});
    std::vector<std::uint8_t> half(RuntimeAssetScope::kMaxFileBytes, 0xa5);
    limits.put(aggregate_generation, "a.dat", half);
    limits.put(aggregate_generation, "b.dat", half);
    assert(limits.staged_byte_count() == RuntimeAssetScope::kMaxAggregateBytes);
    assert(limits.staged_source_bytes() == RuntimeAssetScope::kMaxAggregateBytes);
    expect_failure([&] { limits.put(aggregate_generation, "c.dat", bytes({1})); }, "128 MiB");
    limits.abort(aggregate_generation);

    const auto generated_generation = limits.request({"dsp_coef.bin", "sislib_font.bin"});
    limits.put(generated_generation, "dsp_coef.bin", bytes({1}));
    limits.put(generated_generation, "sislib_font.bin", bytes({2, 3}));
    assert(limits.staged_byte_count() == 3);
    assert(limits.staged_source_bytes() == 0);
    limits.commit(generated_generation);
    assert(limits.active_byte_count() == 3);
    assert(limits.active_source_bytes() == 0);

    // release() clears active bytes only; staging remains independently abortable.
    const auto release_generation = limits.request({"pending.dat"});
    limits.put(release_generation, "pending.dat", bytes({3}));
    limits.release();
    assert(limits.active_file_count() == 0);
    assert(limits.staged_file_count() == 1);
    limits.abort(release_generation);
    assert(limits.staged_file_count() == 0);

#ifdef MELEE_WEB_RUNTIME_ASSET_SCOPE_TESTING
    limits.set_next_generation_for_test(std::numeric_limits<std::uint32_t>::max() - 1);
    const auto final_generation = limits.request({"final.dat"});
    assert(final_generation == std::numeric_limits<std::uint32_t>::max());
    limits.abort(final_generation);
    expect_failure([&] { (void) limits.request({"overflow.dat"}); }, "generation exhausted");
#endif

    return 0;
}
