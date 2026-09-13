#include "source_game_heap_context.hpp"

#include <cstdio>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace melee_web::source_game_heap;

namespace {

std::uint32_t number(const char* text)
{
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed, 0);
    if (consumed != std::string(text).size() || value > 0xffffffffULL)
        throw std::runtime_error("source u32 out of range");
    return static_cast<std::uint32_t>(value);
}

const std::vector<Descriptor> kDescriptors = {
    // Supplied by the source-layout reader; Context has no embedded table.
    {2, 1, 6, 0x800},
    {3, 1, 2, 0x4f8800},
    {4, 2, 6, 0x64b400},
    {5, 4, 6, 0x96c800},
};

struct RecordingBackend final : Backend {
    struct Call {
        Request request;
        std::int32_t result_id = -1;
        Address result_handle{0xffffffffU};
    };

    std::vector<Call> calls;
    std::map<std::uint32_t, int> tokens;
    int next_token = 0;
    std::int32_t next_id = 100;
    const Context* context = nullptr;

    std::size_t index_for_handle(Address handle) const
    {
        if (!context) return 0;
        for (std::size_t index = 0; index < 6; ++index)
            if (context->heap(index).handle == handle) return index;
        return 0;
    }

    std::size_t index_for_span(Address lo, Address hi) const
    {
        if (!context) return 0;
        for (std::size_t index = 0; index < 6; ++index) {
            const auto& heap = context->heap(index);
            if (heap.start == lo && heap.start.value() + heap.size == hi.value())
                return index;
        }
        return 0;
    }

    int token(Address address) const
    {
        const auto found = tokens.find(address.value());
        return found == tokens.end() ? -1 : found->second;
    }

    void destroy_os(std::int32_t id) override
    {
        calls.push_back({Request{RequestKind::destroy_os, 0, id,
                                 Address(0xffffffffU), {}, {}}, -1,
                         Address(0xffffffffU)});
    }

    void destroy_handle(Address handle) override
    {
        calls.push_back({Request{RequestKind::destroy_handle, index_for_handle(handle), -1, handle,
                                 {}, {}}, -1, Address(0xffffffffU)});
    }

    std::int32_t replace_hsd_main(Address lo, Address hi) override
    {
        const auto id = next_id++;
        calls.push_back({Request{RequestKind::replace_hsd_main, 0, -1,
                                 Address(0xffffffffU), lo, hi}, id,
                         Address(0xffffffffU)});
        return id;
    }

    void destroy_current_handle() override
    {
        calls.push_back({Request{RequestKind::destroy_current_handle, 1, -1,
                                 Address(0xffffffffU), {}, {}}, -1,
                         Address(0xffffffffU)});
    }

    Address new_current_handle(Address lo, Address hi) override
    {
        const auto address = Address(0x70000000U +
                                     static_cast<std::uint32_t>(next_token * 0x10));
        tokens[address.value()] = next_token++;
        calls.push_back({Request{RequestKind::new_current_handle, 1, -1,
                                 Address(0xffffffffU), lo, hi}, -1, address});
        return address;
    }

    std::int32_t new_os(Address lo, Address hi) override
    {
        const auto id = next_id++;
        calls.push_back({Request{RequestKind::new_os, index_for_span(lo, hi), -1,
                                 Address(0xffffffffU), lo, hi}, id,
                         Address(0xffffffffU)});
        return id;
    }

    Address new_handle(Address lo, Address hi) override
    {
        const auto address = Address(0x70000000U +
                                     static_cast<std::uint32_t>(next_token * 0x10));
        tokens[address.value()] = next_token++;
        calls.push_back({Request{RequestKind::new_handle, index_for_span(lo, hi), -1,
                                 Address(0xffffffffU), lo, hi}, -1, address});
        return address;
    }
};

std::uint32_t offset(Address address, std::uint32_t base)
{
    return address.value() ? address.value() - base : 0;
}

void emit(const std::string& op, Status status, const Context& context,
          const RecordingBackend& backend, std::size_t call_begin,
          std::uint32_t arena_lo, std::uint32_t aram_lo)
{
    std::printf("{\"op\":\"%s\",\"status\":\"%s\",\"calls\":[",
                op.c_str(), melee_web::source_game_heap::status_name(status));
    for (std::size_t i = call_begin; i < backend.calls.size(); ++i) {
        const auto& call = backend.calls[i];
        const auto& request = call.request;
        if (i != call_begin) std::printf(",");
        std::printf("{\"kind\":\"%s\",\"index\":%zu",
                    request_kind_name(request.kind), request.heap_index);
        if (request.kind == RequestKind::destroy_os)
            std::printf(",\"id\":%d", request.id);
        if (request.kind == RequestKind::destroy_handle)
            std::printf(",\"handle\":%d", backend.token(request.handle));
        if (request.kind == RequestKind::replace_hsd_main ||
            request.kind == RequestKind::new_os ||
            request.kind == RequestKind::new_handle ||
            request.kind == RequestKind::new_current_handle) {
            const bool aram = request.kind == RequestKind::new_current_handle ||
                              (request.heap_index < 6 &&
                               context.heap(request.heap_index).type == 4);
            const auto base = aram ? aram_lo : arena_lo;
            std::printf(",\"lo\":%u,\"hi\":%u",
                        offset(request.lo, base), offset(request.hi, base));
        }
        if (request.kind == RequestKind::new_current_handle ||
            request.kind == RequestKind::new_handle)
            std::printf(",\"handle\":%d", backend.token(call.result_handle));
        std::printf("}");
    }
    std::printf("],\"heaps\":[");
    for (std::size_t i = 0; i < 6; ++i) {
        const auto& heap = context.heap(i);
        const bool aram = i == 1 || heap.type == 4;
        if (i) std::printf(",");
        std::printf("{\"index\":%zu,\"id\":%d,\"handle\":%d,"
                    "\"start\":%u,\"size\":%u,\"type\":%u,"
                    "\"transient\":%d,\"status\":%d}",
                    i, heap.id,
                    backend.token(heap.handle),
                    offset(heap.start, aram ? aram_lo : arena_lo), heap.size,
                    heap.type, heap.transient, static_cast<int>(heap.status));
    }
    std::puts("]}");
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 5) return 2;
    try {
        const auto arena_lo = number(argv[1]);
        const auto arena_hi = number(argv[2]);
        const auto aram_lo = number(argv[3]);
        const auto aram_hi = number(argv[4]);
        Context context;
        RecordingBackend backend;
        backend.context = &context;
        std::string line;
        while (std::getline(std::cin, line)) {
            char op = 0;
            unsigned index = 0;
            int value = 0;
            const int fields = std::sscanf(line.c_str(), " %c %u %d",
                                           &op, &index, &value);
            if (fields < 1 || op == '#') continue;
            const auto call_begin = backend.calls.size();
            if (op == 'b') {
                const auto status = context.initialize(
                    {Address(arena_lo), Address(arena_hi), Address(aram_lo),
                     Address(aram_hi)}, kDescriptors);
                emit("boot", status, context, backend, call_begin,
                     arena_lo, aram_lo);
            } else if (op == 't' && fields == 3) {
                const auto status = context.set_transient(index, value);
                emit("transient", status, context, backend, call_begin,
                     arena_lo, aram_lo);
            } else if (op == 'r') {
                const auto status = context.rebuild(backend);
                emit("rebuild", status, context, backend, call_begin,
                     arena_lo, aram_lo);
            } else if (op == 'q') {
                auto status = context.begin_rebuild();
                while (status == Status::ok) {
                    const auto request = context.next_request();
                    if (!request) break;
                    switch (request->kind) {
                    case RequestKind::destroy_os:
                        backend.destroy_os(request->id);
                        break;
                    case RequestKind::destroy_handle:
                        backend.destroy_handle(request->handle);
                        break;
                    case RequestKind::replace_hsd_main:
                        backend.replace_hsd_main(request->lo, request->hi);
                        break;
                    case RequestKind::destroy_current_handle:
                        backend.destroy_current_handle();
                        break;
                    case RequestKind::new_current_handle:
                        backend.new_current_handle(request->lo, request->hi);
                        break;
                    case RequestKind::new_os:
                        backend.new_os(request->lo, request->hi);
                        break;
                    case RequestKind::new_handle:
                        backend.new_handle(request->lo, request->hi);
                        break;
                    }
                    const auto& result = backend.calls.back();
                    status = context.complete_request(
                        {Status::ok, result.result_id, result.result_handle});
                }
                emit("rebuild", status, context, backend, call_begin,
                     arena_lo, aram_lo);
            } else {
                emit("invalid", Status::invalid_request, context, backend,
                     call_begin, arena_lo, aram_lo);
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 2;
    }
}
