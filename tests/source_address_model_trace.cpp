/* Synthetic differential driver. Source base is supplied, never inferred from
 * host pointers. The original oracle supplies the initial heap bounds. */
#include "source_address_context.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace melee_web::source;
struct Active { Address address; unsigned requested = 0, size = 0; bool active = false; int pool = -1; };
static Heap heap;
static std::uint32_t base;
static std::array<Active, 128> allocations;
static std::array<std::unique_ptr<ObjectPool>, 8> pools;
static unsigned offset(Address address) { return address.value() - base; }
static int parse_id(long value) { return value >= 0 && value < 128 ? int(value) : -1; }
static void cells(const char* name, const std::vector<Cell>& list)
{
    std::printf(",\"%s\":[", name);
    bool first = true;
    for (const auto& cell : list) {
        std::printf("%s{\"offset\":%u,\"bytes\":%u}", first ? "" : ",", offset(cell.start), cell.bytes);
        first = false;
    }
    std::printf("]");
}
static void emit(char op, int id, const char* status, Address result = {}, unsigned size = 0)
{
    std::printf("{\"record\":\"event\",\"op\":\"%c\",\"id\":%d,\"status\":\"%s\"", op, id, status);
    if (!std::strcmp(status, "ok") && (op == 'a' || op == 'o'))
        std::printf(",\"offset\":%u,\"size\":%u", offset(result), size);
    std::printf(",\"free_bytes\":%u,\"active\":[", *heap.free_bytes());
    bool first = true;
    for (unsigned i = 0; i < allocations.size(); ++i) {
        const auto& a = allocations[i];
        if (!a.active) continue;
        std::printf("%s{\"id\":%u,\"offset\":%u,\"requested\":%u,\"size\":%u,\"pool\":%d}",
                    first ? "" : ",", i, offset(a.address), a.requested, a.size, a.pool);
        first = false;
    }
    std::printf("]");
    cells("allocated_cells", heap.state().allocated);
    cells("free_cells", heap.state().free);
    std::printf(",\"pools\":[");
    first = true;
    for (unsigned i = 0; i < pools.size(); ++i) {
        if (!pools[i]) continue;
        const auto& p = pools[i]->state();
        std::printf("%s{\"id\":%u,\"size\":%u,\"align_mask\":%u,\"used\":%u,\"free\":%zu,"
                    "\"peak\":%u,\"free_chain\":[", first ? "" : ",", i,
                    p.size, p.align_mask, p.used, p.free.size(), p.peak);
        for (std::size_t j = 0; j < p.free.size(); ++j)
            std::printf("%s%u", j ? "," : "", offset(p.free[j]));
        std::printf("]}");
        first = false;
    }
    std::puts("]}");
}
static void run_pool(char op, int id, unsigned value, unsigned align)
{
    const unsigned pool = (op == 'p' || op == 'r') ? unsigned(id) : value;
    if (pool >= pools.size() || (op != 'p' && !pools[pool])) { emit(op, id, "invalid"); return; }
    Status status = Status::ok;
    if (op == 'p') {
        if (pools[pool] || value < 4 || value > 1024 || !align || align > 256 || (align & (align-1))) {
            emit(op, id, "invalid"); return;
        }
        pools[pool] = std::make_unique<ObjectPool>(heap);
        status = pools[pool]->initialize(value, align);
    } else if (op == 'r') {
        if (!value || value > 128) { emit(op, id, "invalid"); return; }
        status = pools[pool]->add_free(value);
    } else if (op == 'o') {
        if (id < 0 || allocations[id].active) { emit(op, id, "invalid"); return; }
        const auto a = pools[pool]->allocate();
        if (a.status != Status::ok) { emit(op, id, "oom"); return; }
        const unsigned size = pools[pool]->state().size;
        allocations[id] = {a.address, size, size, true, int(pool)};
        emit(op, id, "ok", a.address, size); return;
    } else {
        if (id < 0 || !allocations[id].active || allocations[id].pool != int(pool)) {
            emit(op, id, "invalid"); return;
        }
        status = pools[pool]->release(allocations[id].address);
        allocations[id] = {};
    }
    if (status != Status::ok) { std::fprintf(stderr, "model rejected valid oracle event\n"); std::exit(3); }
    emit(op, id, "ok");
}
int main(int argc, char** argv)
{
    if (argc != 4) return 2;
    const std::uint64_t supplied = std::strtoull(argv[1], nullptr, 0);
    const std::uint64_t begin = std::strtoull(argv[2], nullptr, 0);
    const std::uint64_t end = std::strtoull(argv[3], nullptr, 0);
    if (supplied + end > 0xffffffffULL || begin >= end) return 2;
    base = std::uint32_t(supplied);
    if (heap.create_empty(Address(base + std::uint32_t(begin)), Address(base + std::uint32_t(end))) != Status::ok) return 2;
    char line[160];
    while (std::fgets(line, sizeof line, stdin)) {
        if (!line[0] || line[0] == '\n' || line[0] == '#') continue;
        char op; long raw_id; unsigned long raw_size, raw_align;
        const int matched = std::sscanf(line, " %c %ld %lu %lu", &op, &raw_id, &raw_size, &raw_align);
        if (matched < 1) continue;
        const int id = matched >= 2 ? parse_id(raw_id) : -1;
        if ((op == 'p' && matched == 4) || ((op == 'o' || op == 'q' || op == 'r') && matched == 3)) {
            run_pool(op, id, unsigned(raw_size), op == 'p' ? unsigned(raw_align) : 0);
        } else if (op == 'a' && matched == 3 && raw_size <= 0xffffffffUL) {
            if (id < 0 || allocations[id].active || !raw_size || raw_size > 0x7fffffc0U) {
                emit(op, id, "invalid"); continue;
            }
            const auto a = heap.allocate(unsigned(raw_size));
            if (a.status != Status::ok) { emit(op, id, "oom"); continue; }
            const auto size = *heap.referent_size(a.address);
            allocations[id] = {a.address, unsigned(raw_size), size, true, -1};
            emit(op, id, "ok", a.address, size);
        } else if (op == 'f' && matched >= 2) {
            if (id < 0 || !allocations[id].active || allocations[id].pool != -1) {
                emit(op, id, "invalid"); continue;
            }
            if (heap.release(allocations[id].address) != Status::ok) return 3;
            allocations[id] = {}; emit(op, id, "ok");
        } else emit('?', -1, "invalid");
    }
}
