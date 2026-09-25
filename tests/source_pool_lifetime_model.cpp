#include "source_address_context.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace melee_web::source;

namespace {

const char* status_name(Status status)
{
    switch (status) {
    case Status::ok: return "ok";
    case Status::missing_context: return "missing";
    case Status::invalid_context: return "invalid_context";
    case Status::invalid_request: return "invalid";
    case Status::exhausted: return "oom";
    case Status::unknown_allocation: return "unknown";
    case Status::unsupported_configuration: return "unsupported";
    }
    return "unknown";
}

std::uint32_t base;
Heap heaps[2];
std::unique_ptr<ObjectPool> pool;
Heap* selected = nullptr;

unsigned offset(Address address)
{
    return address.value() - base;
}

void emit(char op, Status status, Address result = {})
{
    std::printf("{\"op\":\"%c\",\"status\":\"%s\"", op, status_name(status));
    if (status == Status::ok && (op == 'o' || op == 'e' || op == 'a'))
        std::printf(",\"offset\":%u", offset(result));
    if (pool) {
        const auto& state = pool->state();
        std::printf(",\"used\":%u,\"free\":%zu,\"peak\":%u,\"free_chain\":[",
                    state.used, state.free.size(), state.peak);
        for (std::size_t i = 0; i < state.free.size(); ++i)
            std::printf("%s%u", i ? "," : "", offset(state.free[i]));
        std::printf("]");
    }
    std::printf(",\"heap_free\":%u}\n",
                selected && selected->free_bytes() ? *selected->free_bytes() : 0);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    base = static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 0));
    if (base > 0xffff0000U) return 2;
    if (heaps[0].create_empty(Address(base + 0x20), Address(base + 0x5000)) != Status::ok ||
        heaps[1].create_empty(Address(base + 0x5000), Address(base + 0x10000)) != Status::ok)
        return 2;
    selected = &heaps[0];
    pool = std::make_unique<ObjectPool>(heaps[0]);
    std::printf("{\"op\":\"header\",\"base\":%u}\n", base);

    char op;
    unsigned value = 0, align = 0;
    while (std::scanf(" %c %u %u", &op, &value, &align) >= 1) {
        if (op == 's') {
            if (value >= 2) { emit(op, Status::invalid_context); continue; }
            selected = &heaps[value];
            emit(op, Status::ok);
        } else if (op == 'p') {
            emit(op, pool->initialize(value, align));
        } else if (op == 'r') {
            emit(op, pool->add_free(value, *selected));
        } else if (op == 'q') {
            const auto bytes = std::uint64_t(pool->state().size) * value;
            if (!value || bytes > 0xffffffffULL) {
                emit(op, Status::invalid_request);
                continue;
            }
            const auto backing = selected->allocate(static_cast<std::uint32_t>(bytes));
            if (backing.status != Status::ok) {
                emit(op, backing.status);
                continue;
            }
            emit(op, pool->adopt_backing(*selected, backing.address, value));
        } else if (op == 'o') {
            const auto result = pool->allocate(*selected);
            emit(op, result.status, result.address);
        } else if (op == 'e') {
            const auto result = pool->allocate_existing();
            emit(op, result.status, result.address);
        } else if (op == 'R') {
            emit(op, pool->reset(value, align));
        } else if (op == 'c') {
            if (value >= 2) {
                emit(op, Status::invalid_context);
                continue;
            }
            heaps[value].clear();
            const auto lo = value ? base + 0x5000 : base + 0x20;
            const auto hi = value ? base + 0x10000 : base + 0x5000;
            emit(op, heaps[value].create_empty(Address(lo), Address(hi)));
        } else {
            emit(op, Status::invalid_request);
        }
    }
    return 0;
}
