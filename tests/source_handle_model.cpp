#include "source_handle_context.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

using namespace melee_web::source_handle;

namespace {

std::uint32_t number(const char* text)
{
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed, 0);
    if (consumed != std::string(text).size() || value > 0xffffffffULL)
        throw std::runtime_error("source u32 out of range");
    return static_cast<std::uint32_t>(value);
}

std::uint32_t norm_ptr(Address address, std::uint32_t global_base)
{
    if (!address.value()) return 0;
    return address.value() - global_base;
}

std::uint32_t arena_offset(Address address, std::uint32_t arena_lo)
{
    if (!address.value()) return 0;
    return address.value() - arena_lo;
}

bool is_heap_descriptor(Address address, std::uint32_t heap_base)
{
    return address.value() >= heap_base && address.value() < heap_base + 6 * 0x10 &&
           (address.value() - heap_base) % 0x10 == 0;
}

void json_status(Status status)
{
    std::printf("\"status\":\"%s\"", status_name(status));
}

struct Driver {
    Context context;
    std::uint32_t global_base;
    std::uint32_t mem_base;
    std::uint32_t heap_base;
    std::uint32_t current_slot;
    std::uint32_t arena_lo;
    std::uint32_t arena_hi;
    std::map<std::string, Address> handles;
    std::map<std::string, Address> payloads;

    void emit(const std::string& op, Status status, HandleResult result = {})
    {
        const auto state = context.snapshot();
        std::printf("{\"op\":\"%s\",", op.c_str());
        json_status(status);
        if (result.status == Status::ok && result.handle.value()) {
            std::printf(",\"handle\":%u,\"payload\":%u,\"size\":%u",
                        norm_ptr(result.handle, global_base),
                        arena_offset(result.payload, arena_lo), result.size);
        }
        std::printf(",\"initialized\":%s,\"current\":%u,\"allocations\":%u,\"max_allocations\":%u,\"free_mem\":[",
                    state.initialized ? "true" : "false", norm_ptr(state.current, global_base),
                    state.allocations, state.max_allocations);
        for (std::size_t i = 0; i < state.free_mem.size(); ++i)
            std::printf("%s%u", i ? "," : "", norm_ptr(state.free_mem[i], global_base));
        std::printf("],\"free_heap\":[");
        for (std::size_t i = 0; i < state.free_heap.size(); ++i)
            std::printf("%s%u", i ? "," : "", norm_ptr(state.free_heap[i], global_base));
        std::printf("],\"active\":[");
        for (std::size_t i = 0; i < state.active.size(); ++i) {
            const auto& view = state.active[i];
            const bool heap = is_heap_descriptor(view.identity, heap_base);
            std::printf("%s{\"identity\":%u,\"next\":%u,\"lo\":%u,\"hi\":%u,\"heap\":%s",
                        i ? "," : "", norm_ptr(view.identity, global_base),
                        norm_ptr(view.next, global_base), arena_offset(view.lo, arena_lo),
                        heap ? arena_offset(view.hi, arena_lo) : view.hi.value(),
                        heap ? "true" : "false");
            if (heap)
                std::printf(",\"prev\":%u", norm_ptr(view.prev, global_base));
            std::printf("}");
        }
        std::puts("]}");
        std::fflush(stdout);
    }

    void run(const std::string& line)
    {
        char op = 0;
        char first[80] = {};
        char second[80] = {};
        char third[80] = {};
        char fourth[80] = {};
        const int count = std::sscanf(line.c_str(), " %c %79s %79s %79s %79s",
                                      &op, first, second, third, fourth);
        if (count < 1 || op == '#') return;
        if (op == 'b') {
            const auto status = context.initialize(
                {Address(global_base), Address(mem_base), Address(heap_base), Address(current_slot)},
                {Address(arena_lo), Address(arena_hi)});
            handles.clear(); payloads.clear();
            if (status == Status::ok) handles["current"] = context.snapshot().current;
            emit("boot", status);
            return;
        }
        if (op == 'n' || op == 'c') {
            if (count != 4) throw std::runtime_error("new expects label lo hi");
            const auto result = op == 'n'
                ? context.new_handle(Address(number(second)), Address(number(third)))
                : context.new_current(Address(number(second)), Address(number(third)));
            if (result.status == Status::ok) {
                handles[first] = result.handle;
                if (op == 'c') handles["current"] = result.handle;
            }
            emit(op == 'n' ? "new" : "current_new", result.status, result);
            return;
        }
        if (op == 'a') {
            if (count != 4) throw std::runtime_error("alloc expects heap child size");
            const auto owner = handles.find(first);
            if (owner == handles.end()) { emit("alloc", Status::unknown_handle); return; }
            const auto result = context.allocate(owner->second, number(third));
            if (result.status == Status::ok) {
                handles[second] = result.handle;
                payloads[second] = result.payload;
            }
            emit("alloc", result.status, result);
            return;
        }
        if (op == 'f') {
            if (count != 3) throw std::runtime_error("free expects heap child");
            const auto owner = handles.find(first), payload = payloads.find(second);
            if (owner == handles.end() || payload == payloads.end()) {
                emit("free", Status::unknown_payload); return;
            }
            const auto status = context.free_payload(owner->second, payload->second);
            emit("free", status);
            return;
        }
        if (op == 'd') {
            const auto owner = handles.find(first);
            const auto status = owner == handles.end() ? Status::unknown_handle :
                context.destroy(owner->second);
            emit("destroy", status);
            return;
        }
        if (op == 's') {
            const auto status = context.destroy_current();
            handles.erase("current");
            emit("destroy_current", status);
            return;
        }
        if (op == 'm') {
            const auto owner = handles.find(first);
            const auto status = owner == handles.end() ? Status::unknown_handle :
                context.compact(owner->second);
            emit("compact", status);
            return;
        }
        if (op == 'k') {
            const auto status = context.compact_current();
            emit("compact_current", status);
            return;
        }
        if (op == 'x') {
            context.clear(); handles.clear(); payloads.clear(); emit("clear", Status::ok); return;
        }
        throw std::runtime_error("unknown source-handle operation");
    }
};

} // namespace

int main(int argc, char** argv)
{
    if (argc != 7) return 2;
    try {
        // argv: global base, explicit MemEntry root, explicit heap-handle
        // root, explicit current-handle slot, arena low and arena high. The
        // model receives every source root from the caller; the driver does
        // not infer them from a host allocation.
        Driver driver{Context{}, number(argv[1]), number(argv[2]), number(argv[3]),
                      number(argv[4]), number(argv[5]), number(argv[6]), {}, {}};
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!line.empty()) driver.run(line);
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 2;
    }
}
