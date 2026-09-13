#include "source_address_context.hpp"
#include "source_handle_context.hpp"
#include "source_aram_context.hpp"

#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

/*
 * Small JSON-lines adapter for the already merged source-address allocator.
 * This is deliberately a driver, not a second allocator: all ownership and
 * pool transitions are delegated to source_address_context.cpp.  The Python
 * replay tool supplies source addresses as integers and treats returned
 * addresses as validation results.
 */
using namespace melee_web::source;
using HandleAddress = melee_web::source_handle::Address;
using HandleContext = melee_web::source_handle::Context;
using HandleResult = melee_web::source_handle::HandleResult;
using HandleStatus = melee_web::source_handle::Status;
using AramAddress = melee_web::source_aram::Address;
using AramContext = melee_web::source_aram::Context;
using AramResult = melee_web::source_aram::Result;
using AramStatus = melee_web::source_aram::Status;

namespace {

std::uint32_t number(const std::string& value)
{
    std::size_t consumed = 0;
    const auto result = std::stoull(value, &consumed, 0);
    if (consumed != value.size() || result > 0xffffffffULL)
        throw std::runtime_error("number outside source u32");
    return static_cast<std::uint32_t>(result);
}

std::string field(const std::string& line, const std::string& name)
{
    const std::string needle = "\"" + name + "\":";
    const auto begin = line.find(needle);
    if (begin == std::string::npos) throw std::runtime_error("missing field " + name);
    auto cursor = begin + needle.size();
    while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t')) ++cursor;
    if (cursor >= line.size()) throw std::runtime_error("empty field " + name);
    if (line[cursor] == '"') {
        const auto end = line.find('"', cursor + 1);
        if (end == std::string::npos) throw std::runtime_error("unterminated field " + name);
        return line.substr(cursor + 1, end - cursor - 1);
    }
    auto end = cursor;
    while (end < line.size() && line[end] != ',' && line[end] != '}') ++end;
    while (end > cursor && (line[end - 1] == ' ' || line[end - 1] == '\t')) --end;
    return line.substr(cursor, end - cursor);
}

std::uint32_t handle_number(const std::string& value)
{
    std::size_t consumed = 0;
    const auto result = std::stoull(value, &consumed, 0);
    if (consumed != value.size() || result > 0xffffffffULL)
        throw std::runtime_error("number outside source u32");
    return static_cast<std::uint32_t>(result);
}

std::string status_name(Status status)
{
    switch (status) {
    case Status::ok: return "ok";
    case Status::missing_context: return "missing_context";
    case Status::invalid_context: return "invalid_context";
    case Status::invalid_request: return "invalid_request";
    case Status::exhausted: return "exhausted";
    case Status::unknown_allocation: return "unknown_allocation";
    case Status::unsupported_configuration: return "unsupported_configuration";
    }
    return "unknown_status";
}

void emit_status(const std::string& op, Status status)
{
    std::cout << "{\"op\":\"" << op << "\",\"status\":\""
              << status_name(status) << "\"}\n";
}

void emit_pool_status(const std::string& op, Status status, const ObjectPool* pool,
                      const std::optional<Address>& backing = {})
{
    std::cout << "{\"op\":\"" << op << "\",\"status\":\""
              << status_name(status) << "\"";
    if (pool) {
        const auto& state = pool->state();
        std::cout << ",\"size\":" << state.size
                  << ",\"align_mask\":" << state.align_mask
                  << ",\"used\":" << state.used
                  << ",\"peak\":" << state.peak
                  << ",\"free_count\":" << state.free.size()
                  << ",\"free_chain\":[";
        for (std::size_t i = 0; i < state.free.size(); ++i)
            std::cout << (i ? "," : "") << state.free[i].value();
        std::cout << "]";
    }
    if (backing) std::cout << ",\"backing\":" << backing->value();
    std::cout << "}\n";
}

const char* handle_status_name(HandleStatus status)
{
    return melee_web::source_handle::status_name(status);
}

const char* aram_status_name(AramStatus status)
{
    return melee_web::source_aram::status_name(status);
}

struct Model {
    std::map<std::uint32_t, std::unique_ptr<Heap>> heaps;
    std::map<std::uint32_t, std::unique_ptr<ObjectPool>> pools;
    std::map<std::uint32_t, std::uint32_t> pool_heaps;
    HandleContext handles;
    AramContext aram;
    std::map<std::string, HandleAddress> handle_labels;
    std::map<std::string, HandleAddress> payload_labels;

    Heap& heap(std::uint32_t id)
    {
        const auto found = heaps.find(id);
        if (found == heaps.end()) throw std::runtime_error("unknown heap id");
        return *found->second;
    }

    ObjectPool& pool(std::uint32_t id)
    {
        const auto found = pools.find(id);
        if (found == pools.end()) throw std::runtime_error("unknown pool id");
        return *found->second;
    }

    HandleAddress handle(const std::string& label) const
    {
        const auto found = handle_labels.find(label);
        if (found == handle_labels.end()) throw std::runtime_error("unknown source handle label");
        return found->second;
    }

    HandleAddress payload(const std::string& label) const
    {
        const auto found = payload_labels.find(label);
        if (found == payload_labels.end()) throw std::runtime_error("unknown source payload label");
        return found->second;
    }

    void emit_handle(const std::string& op, HandleStatus status, HandleResult result = {})
    {
        std::cout << "{\"op\":\"" << op << "\",\"status\":\""
                  << handle_status_name(status) << "\"";
        if (status == HandleStatus::ok) {
            std::cout << ",\"handle\":" << result.handle.value()
                      << ",\"payload\":" << result.payload.value()
                      << ",\"size\":" << result.size;
        }
        std::cout << "}\n";
    }

    void emit_aram(const std::string& op, AramStatus status, AramResult result = {})
    {
        std::cout << "{\"op\":\"" << op << "\",\"status\":\""
                  << aram_status_name(status) << "\"";
        if (status == AramStatus::ok)
            std::cout << ",\"address\":" << result.address.value()
                      << ",\"size\":" << result.size;
        std::cout << "}\n";
    }

    void run(const std::string& line)
    {
        const auto op = field(line, "op");
        if (op == "heap_create") {
            const auto id = number(field(line, "heap"));
            auto& target = heaps[id];
            if (!target) target = std::make_unique<Heap>();
            const auto status = target->create_empty(
                Address(number(field(line, "begin"))), Address(number(field(line, "end"))));
            emit_status(op, status);
            return;
        }
        if (op == "heap_clear") {
            heap(number(field(line, "heap"))).clear();
            emit_status(op, Status::ok);
            return;
        }
        if (op == "alloc") {
            const auto result = heap(number(field(line, "heap"))).allocate(number(field(line, "requested")));
            std::cout << "{\"op\":\"" << op << "\",\"status\":\""
                      << status_name(result.status) << "\",\"address\":"
                      << result.address.value() << "}\n";
            return;
        }
        if (op == "free") {
            const auto status = heap(number(field(line, "heap"))).release(Address(number(field(line, "address"))));
            emit_status(op, status);
            return;
        }
        if (op == "pool_init") {
            const auto id = number(field(line, "pool"));
            const auto heap_id = number(field(line, "heap"));
            auto& target = pools[id];
            if (!target) target = std::make_unique<ObjectPool>(heap(heap_id));
            const auto status = target->initialize(
                number(field(line, "size")), number(field(line, "align")),
                field(line, "dedicated") == "1", field(line, "number_limit") == "1",
                field(line, "heap_limit") == "1");
            emit_pool_status(op, status, target.get());
            if (status == Status::ok) pool_heaps[id] = heap_id;
            return;
        }
        if (op == "pool_add") {
            const auto pool_id = number(field(line, "pool"));
            auto& target = pool(pool_id);
            const auto heap_id = number(field(line, "heap"));
            auto& target_heap = heap(heap_id);
            if (pool_heaps[pool_id] != heap_id) {
                emit_pool_status(op, Status::unsupported_configuration, &target);
                return;
            }
            const auto before = target_heap.state().allocated;
            const auto status = target.add_free(number(field(line, "count")));
            std::optional<Address> backing;
            if (status == Status::ok) {
                for (const auto& cell : target_heap.state().allocated) {
                    bool was_present = false;
                    for (const auto& old : before)
                        if (old.start == cell.start && old.bytes == cell.bytes) was_present = true;
                    if (!was_present) {
                        backing = Address(cell.start.value() + 32);
                        break;
                    }
                }
            }
            emit_pool_status(op, status, &target, backing);
            return;
        }
        if (op == "pool_alloc") {
            const auto pool_id = number(field(line, "pool"));
            auto& target = pool(pool_id);
            const auto heap_id = number(field(line, "heap"));
            auto& target_heap = heap(heap_id);
            if (pool_heaps[pool_id] != heap_id) {
                std::cout << "{\"op\":\"" << op << "\",\"status\":\"unsupported_configuration\",\"address\":0}\n";
                return;
            }
            const auto before = target_heap.state().allocated;
            const auto result = target.allocate();
            std::optional<Address> backing;
            if (result.status == Status::ok) {
                for (const auto& cell : target_heap.state().allocated) {
                    bool was_present = false;
                    for (const auto& old : before)
                        if (old.start == cell.start && old.bytes == cell.bytes) was_present = true;
                    if (!was_present) {
                        backing = Address(cell.start.value() + 32);
                        break;
                    }
                }
            }
            std::cout << "{\"op\":\"" << op << "\",\"status\":\""
                      << status_name(result.status) << "\",\"address\":"
                      << result.address.value();
            if (backing) std::cout << ",\"backing\":" << backing->value();
            std::cout << ",\"size\":" << target.state().size
                      << ",\"align_mask\":" << target.state().align_mask
                      << ",\"used\":" << target.state().used
                      << ",\"peak\":" << target.state().peak
                      << ",\"free_count\":" << target.state().free.size() << "}\n";
            return;
        }
        if (op == "pool_free") {
            const auto pool_id = number(field(line, "pool"));
            auto& target = pool(pool_id);
            const auto status = target.release(
                Address(number(field(line, "address"))));
            emit_pool_status(op, status, &target);
            return;
        }
        if (op == "handle_init") {
            const auto status = handles.initialize(
                {HandleAddress(handle_number(field(line, "allocator"))),
                 HandleAddress(handle_number(field(line, "mem_entries"))),
                 HandleAddress(handle_number(field(line, "heap_handles"))),
                 HandleAddress(handle_number(field(line, "current_handle_slot")))},
                {HandleAddress(handle_number(field(line, "arena_lo"))),
                 HandleAddress(handle_number(field(line, "arena_hi")))});
            handle_labels.clear();
            payload_labels.clear();
            if (status == HandleStatus::ok)
                handle_labels["current"] = handles.snapshot().current;
            if (status == HandleStatus::ok) {
                const auto current = handles.snapshot().current;
                emit_handle(op, status, {status, current, {}, 0});
            } else {
                emit_handle(op, status);
            }
            return;
        }
        if (op == "aram_init") {
            const auto status = aram.initialize({
                AramAddress(handle_number(field(line, "initial_base"))),
                handle_number(field(line, "capacity")),
                handle_number(field(line, "hardware_size"))});
            if (status == AramStatus::ok) {
                const auto snapshot = aram.snapshot();
                emit_aram(op, status, {status, snapshot.stack_pointer, 0});
            } else {
                emit_aram(op, status);
            }
            return;
        }
        if (op == "aram_alloc") {
            const auto result = aram.allocate(handle_number(field(line, "requested")));
            emit_aram(op, result.status, result);
            return;
        }
        if (op == "aram_free") {
            const auto result = aram.free();
            emit_aram(op, result.status, result);
            return;
        }
        if (op == "aram_size") {
            if (!aram.initialized()) {
                emit_aram(op, AramStatus::missing_context);
            } else {
                emit_aram(op, AramStatus::ok, {AramStatus::ok, {}, aram.hardware_size()});
            }
            return;
        }
        if (op == "handle_init_from_aram") {
            const auto state = aram.snapshot();
            if (!state.initialized) {
                emit_handle(op, HandleStatus::missing_context);
                return;
            }
            const auto status = handles.initialize(
                {HandleAddress(handle_number(field(line, "allocator"))),
                 HandleAddress(handle_number(field(line, "mem_entries"))),
                 HandleAddress(handle_number(field(line, "heap_handles"))),
                 HandleAddress(handle_number(field(line, "current_handle_slot")))},
                {HandleAddress(state.stack_pointer.value()), HandleAddress(state.hardware_size)});
            handle_labels.clear();
            payload_labels.clear();
            if (status == HandleStatus::ok) {
                const auto current = handles.snapshot().current;
                handle_labels["current"] = current;
                emit_handle(op, status, {status, current, {}, 0});
            } else {
                emit_handle(op, status);
            }
            return;
        }
        if (op == "handle_new" || op == "handle_current_new") {
            const auto result = op == "handle_new"
                ? handles.new_handle(HandleAddress(handle_number(field(line, "lo"))),
                                     HandleAddress(handle_number(field(line, "hi"))))
                : handles.new_current(HandleAddress(handle_number(field(line, "lo"))),
                                      HandleAddress(handle_number(field(line, "hi"))));
            if (result.status == HandleStatus::ok) {
                handle_labels[field(line, "label")] = result.handle;
                if (op == "handle_current_new") handle_labels["current"] = result.handle;
            }
            emit_handle(op, result.status, result);
            return;
        }
        if (op == "handle_alloc") {
            const auto result = handles.allocate(
                handle(field(line, "owner")), handle_number(field(line, "requested")));
            if (result.status == HandleStatus::ok) {
                handle_labels[field(line, "label")] = result.handle;
                payload_labels[field(line, "payload")] = result.payload;
            }
            emit_handle(op, result.status, result);
            return;
        }
        if (op == "handle_free") {
            const auto status = handles.free_payload(handle(field(line, "owner")),
                                                     payload(field(line, "payload")));
            emit_handle(op, status);
            return;
        }
        if (op == "handle_destroy") {
            emit_handle(op, handles.destroy(handle(field(line, "owner"))));
            return;
        }
        if (op == "handle_destroy_current") {
            emit_handle(op, handles.destroy_current());
            handle_labels.erase("current");
            return;
        }
        if (op == "handle_compact") {
            emit_handle(op, handles.compact(handle(field(line, "owner"))));
            return;
        }
        if (op == "handle_compact_current") {
            emit_handle(op, handles.compact_current());
            return;
        }
        throw std::runtime_error("unknown model operation " + op);
    }
};

} // namespace

int main()
{
    Model model;
    std::string line;
    try {
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            model.run(line);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    return 0;
}
