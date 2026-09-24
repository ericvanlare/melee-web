#ifndef MELEE_WEB_SOURCE_GAME_HEAP_CONTEXT_HPP
#define MELEE_WEB_SOURCE_GAME_HEAP_CONTEXT_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace melee_web::source_game_heap {

// Source addresses are identities in the original 32-bit address space.  The
// model never turns one into a host pointer.
class Address {
public:
    constexpr Address() = default;
    explicit constexpr Address(std::uint32_t value) : value_(value) {}
    constexpr std::uint32_t value() const { return value_; }
    friend constexpr bool operator==(Address a, Address b) { return a.value_ == b.value_; }
    friend constexpr bool operator!=(Address a, Address b) { return !(a == b); }

private:
    std::uint32_t value_ = 0;
};

struct Bounds {
    Address arena_lo;
    Address arena_hi;
    Address aram_lo;
    Address aram_hi;
};

// The caller supplies the observed descriptor records recovered from the
// original binary.  The terminating idx=6 record is implicit and is not
// needed here; descriptor type 0 is reserved for that excluded sentinel.
struct Descriptor {
    std::uint32_t index = 0;
    std::uint32_t type = 0;
    std::uint32_t previous = 0;
    std::uint32_t size = 0;
};

enum class Status {
    ok,
    missing_context,
    invalid_layout,
    invalid_request,
    backend_failure,
};

enum class HeapStatus {
    create = 0,
    destroy = 1,
};

struct Heap {
    std::int32_t id = -1;
    Address handle{0xffffffffU};
    Address start;
    std::uint32_t size = 0;
    std::uint32_t type = 1;
    std::int32_t transient = 1;
    HeapStatus status = HeapStatus::destroy;
};

enum class RequestKind {
    destroy_os,
    destroy_handle,
    replace_hsd_main,
    destroy_current_handle,
    new_current_handle,
    new_os,
    new_handle,
};

struct Request {
    RequestKind kind = RequestKind::destroy_os;
    std::size_t heap_index = 0;
    std::int32_t id = -1;
    Address handle{0xffffffffU};
    Address lo;
    Address hi;
};

struct RequestResult {
    Status status = Status::ok;
    std::int32_t id = -1;
    Address handle{0xffffffffU};
};

struct Snapshot {
    bool initialized = false;
    Bounds bounds;
    std::array<Heap, 6> heaps{};
    bool rebuilding = false;
    bool rebuild_complete = false;
};

// These operations are deliberately the source boundaries.  A caller that
// needs to expose nested HSD_ObjAllocInit/ForgetMemory work can execute those
// operations inside replace_hsd_main before completing that request.
class Backend {
public:
    virtual ~Backend() = default;
    virtual void destroy_os(std::int32_t id) = 0;
    virtual void destroy_handle(Address handle) = 0;
    virtual std::int32_t replace_hsd_main(Address lo, Address hi) = 0;
    virtual void destroy_current_handle() = 0;
    virtual Address new_current_handle(Address lo, Address hi) = 0;
    virtual std::int32_t new_os(Address lo, Address hi) = 0;
    virtual Address new_handle(Address lo, Address hi) = 0;
};

class Context {
public:
    Context() = default;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // Equivalent to lbHeap_80015F3C with its two root queries supplied by the
    // caller.  Initialization derives every heap start from descriptors and
    // leaves all six heaps in the source Destroy state.
    Status initialize(Bounds bounds, const std::vector<Descriptor>& descriptors);
    void clear();

    bool initialized() const { return initialized_; }
    Snapshot snapshot() const;
    const Heap& heap(std::size_t index) const;

    Status set_transient(std::size_t index, std::int32_t value);
    std::int32_t transient(std::size_t index) const;

    // Stepwise form of lbHeap_80015900.  begin_rebuild derives the adjusted
    // bounds and prepares the first source request without calling a backend.
    Status begin_rebuild();
    std::optional<Request> next_request();
    Status complete_request(const RequestResult& result);

    // Convenience adapter over the same state machine.  It cannot reorder
    // the requests exposed by next_request().
    Status rebuild(Backend& backend);

private:
    static constexpr std::size_t kHeapCount = 6;
    static constexpr std::uint32_t kRootIndex = 6;

    struct RebuildState {
        enum class Phase {
            destroy,
            main,
            destroy_current,
            current,
            create,
            done,
        } phase = Phase::done;
        std::size_t cursor = 2;
        Bounds adjusted;
        std::optional<Request> pending;
    } rebuild_;

    static bool valid_span(Address lo, Address hi);
    static bool valid_sum(Address start, std::uint32_t size, Address* end);
    bool valid_descriptor(const Descriptor& descriptor,
                          const std::vector<Descriptor>& seen) const;
    std::optional<Request> make_request();
    Status apply_result(const Request& request, const RequestResult& result);

    Bounds bounds_{};
    std::array<Heap, kHeapCount> heaps_{};
    std::vector<Descriptor> descriptors_;
    bool initialized_ = false;
};

const char* status_name(Status status);
const char* request_kind_name(RequestKind kind);

} // namespace melee_web::source_game_heap

#endif
