#ifndef MELEE_WEB_SOURCE_ARAM_CONTEXT_HPP
#define MELEE_WEB_SOURCE_ARAM_CONTEXT_HPP

#include <cstdint>
#include <vector>

namespace melee_web::source_aram {

// A source address is an identity in the original 32-bit address space. It
// is never converted to, or dereferenced as, a host pointer.
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

struct Layout {
    // ARInit supplies 0x4000 on the original machine. Keep it explicit so a
    // declared machine profile, rather than a captured address, owns the
    // source root and relocation tests can exercise another declared base.
    Address initial_base;
    std::uint32_t capacity = 0;
    std::uint32_t hardware_size = 0;
};

enum class Status {
    ok,
    missing_context,
    invalid_layout,
    invalid_request,
    already_initialized,
    exhausted,
    empty,
};

struct Result {
    Status status = Status::missing_context;
    Address address;
    std::uint32_t size = 0;
};

struct Allocation {
    Address address;
    std::uint32_t size = 0;
};

struct Snapshot {
    bool initialized = false;
    Address initial_base;
    Address stack_pointer;
    std::uint32_t capacity = 0;
    std::uint32_t free_blocks = 0;
    std::uint32_t hardware_size = 0;
    std::vector<Allocation> allocations;
};

class Context {
public:
    Context() = default;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // Address-only equivalent of ARInit's state. A second initialization is
    // rejected without changing the existing stack, matching ARInit's early
    // return once its init flag is set.
    Status initialize(Layout layout);
    // Discard model context; this is not a model of the unobserved ARReset API.
    void clear();

    bool initialized() const { return initialized_; }
    std::uint32_t hardware_size() const { return hardware_size_; }
    const Snapshot snapshot() const;

    // ARAlloc requires 32-byte alignment, advances the stack, and records the
    // length for LIFO ARFree. Zero is aligned and remains a valid source call.
    Result allocate(std::uint64_t length);

    // ARFree returns the new stack pointer and optionally writes the popped
    // length. The model returns that length in Result::size instead.
    Result free();

private:
    Layout layout_{};
    Address stack_pointer_{};
    std::vector<std::uint32_t> lengths_;
    bool initialized_ = false;
    std::uint32_t hardware_size_ = 0;
};

const char* status_name(Status status);

} // namespace melee_web::source_aram

#endif
