#include "source_handle_context.hpp"

#include <cstdint>
#include <cstdio>
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

std::uint32_t norm_ptr(Address address)
{
    return address.value() ? address.value() - 0x20000000U : 0;
}

std::uint32_t arena_offset(Address address, std::uint32_t arena_lo)
{
    return address.value() ? address.value() - arena_lo : 0;
}

bool is_heap_descriptor(Address address)
{
    return address.value() >= 0x20000638U && address.value() < 0x20000698U &&
           (address.value() - 0x20000638U) % 0x10 == 0;
}

struct Driver {
    Context context;
    std::uint32_t arena_lo;
    std::uint32_t arena_hi;
    std::map<std::string, Address> handles;
    std::map<std::string, Address> payloads;
    unsigned completion_count = 0;

    void emit(const char* op, const char* status)
    {
        const auto state = context.snapshot();
        const auto& compact = context.compact_state();
        std::printf("{\"op\":\"%s\",\"status\":\"%s\",\"current\":%u",
                    op, status, norm_ptr(state.current));
        std::printf(",\"allocations\":%u,\"max_allocations\":%u",
                    state.allocations, state.max_allocations);
        std::printf(",\"free_mem\":[");
        for (std::size_t i = 0; i < state.free_mem.size(); ++i)
            std::printf("%s%u", i ? "," : "", norm_ptr(state.free_mem[i]));
        std::printf("],\"free_heap\":[");
        for (std::size_t i = 0; i < state.free_heap.size(); ++i)
            std::printf("%s%u", i ? "," : "", norm_ptr(state.free_heap[i]));
        std::printf("],\"active\":[");
        for (std::size_t i = 0; i < state.active.size(); ++i) {
            const auto& view = state.active[i];
            const bool heap = is_heap_descriptor(view.identity);
            std::printf("%s{\"identity\":%u,\"next\":%u,\"lo\":%u,\"hi\":%u,\"heap\":%s",
                        i ? "," : "", norm_ptr(view.identity),
                        norm_ptr(view.next), arena_offset(view.lo, arena_lo),
                        heap ? arena_offset(view.hi, arena_lo) : view.hi.value(),
                        heap ? "true" : "false");
            if (heap) std::printf(",\"prev\":%u", norm_ptr(view.prev));
            std::putchar('}');
        }
        const bool pending = compact.phase == CompactPhase::waiting_ram_alarm ||
                             compact.phase == CompactPhase::waiting_devcom;
        std::printf("],\"cursor\":%u,\"pending\":",
                    arena_offset(compact.cursor, arena_lo));
        if (pending) {
            const char* kind = compact.move.transfer == TransferKind::devcom_1b
                ? "devcom_1b" : "ram_alarm";
            const int type = compact.move.transfer == TransferKind::devcom_1b ? 0x1b : 0;
            std::printf("{\"kind\":\"%s\",\"source\":%u,\"destination\":%u,\"size\":%u,\"type\":%d}",
                        kind, arena_offset(compact.move.source, arena_lo),
                        arena_offset(compact.move.destination, arena_lo),
                        compact.move.size, type);
        } else {
            std::fputs("null", stdout);
        }
        std::printf(",\"completion_count\":%u}\n", completion_count);
        std::fflush(stdout);
    }

    void drain_callbacks()
    {
        for (;;) {
            const auto phase = context.compact_state().phase;
            if (phase != CompactPhase::awaiting_callback &&
                phase != CompactPhase::awaiting_completion)
                return;
            const auto result = context.compact_callback_transition(
                context.compact_state().callback_generation);
            if (result.callback_invoked) ++completion_count;
            if (result.status != Status::compact_in_progress &&
                result.status != Status::compact_complete)
                return;
        }
    }

    void run(const std::string& line)
    {
        char op = 0;
        char first[64] = {};
        char second[64] = {};
        char third[64] = {};
        const int count = std::sscanf(line.c_str(), " %c %63s %63s %63s",
                                      &op, first, second, third);
        if (count < 1 || op == '#') return;
        if (op == 'b') {
            const auto status = context.initialize(
                {Address(0x20000000), Address(0x20000008), Address(0x20000638),
                 Address(0x2000069c)},
                {Address(arena_lo), Address(arena_hi)});
            handles.clear();
            payloads.clear();
            completion_count = 0;
            if (status != Status::ok) throw std::runtime_error("model boot failed");
            handles["current"] = context.snapshot().current;
            emit("boot", "ok");
        } else if (op == 'n') {
            const auto result = context.new_handle(Address(number(second)),
                                                    Address(number(third)));
            if (result.status != Status::ok) throw std::runtime_error("model new failed");
            handles[first] = result.handle;
            emit("new", "ok");
        } else if (op == 'a') {
            const auto owner = handles.find(first);
            if (owner == handles.end()) throw std::runtime_error("model owner missing");
            const auto result = context.allocate(owner->second, number(third));
            if (result.status != Status::ok) throw std::runtime_error("model alloc failed");
            handles[second] = result.handle;
            payloads[second] = result.payload;
            emit("alloc", "ok");
        } else if (op == 'f') {
            const auto owner = handles.find(first);
            const auto payload = payloads.find(second);
            if (owner == handles.end() || payload == payloads.end())
                throw std::runtime_error("model free label missing");
            const auto status = context.free_payload(owner->second, payload->second);
            if (status != Status::ok) throw std::runtime_error("model free failed");
            emit("free", "ok");
        } else if (op == 'm') {
            const auto owner = handles.find(first);
            if (owner == handles.end()) throw std::runtime_error("model compact owner missing");
            const auto begin = context.compact_begin(owner->second,
                                                     Address(0x80017a80), 4);
            if (begin.status == Status::ok) {
                emit("compact", "ok");
                return;
            }
            if (begin.status != Status::compact_started)
                throw std::runtime_error("model compact begin failed");
            const auto callback = context.compact_callback_transition(
                begin.state.callback_generation);
            if (callback.status != Status::compact_started)
                throw std::runtime_error("model initial callback failed");
            emit("compact", "started");
        } else if (op == 'c') {
            const auto phase = context.compact_state().phase;
            if (phase != CompactPhase::waiting_devcom)
                throw std::runtime_error("model completion without DevCom request");
            const auto transfer = context.compact_devcom_complete(
                context.compact_state().move.generation);
            if (transfer.status != Status::compact_in_progress)
                throw std::runtime_error("model DevCom completion failed");
            drain_callbacks();
            emit("complete", "ok");
        } else if (op == 's') {
            emit("state", "ok");
        } else {
            throw std::runtime_error("unknown async model operation");
        }
    }
};

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    try {
        Driver driver{Context{}, number(argv[1]), number(argv[2]), {}, {}, 0};
        std::string line;
        while (std::getline(std::cin, line)) driver.run(line);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 2;
    }
}
